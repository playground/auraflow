# Subscription Model (Deferred)

**Status: design-only. Do not build until ~10 real users exist.**

This is the strategy for if/when AuraFlow gets a paid tier. Documented now to
keep the implementation honest about what stays free and where billing logic
must live.

## Decision: keep billing OUT of HomeHub core

HomeHub is a single-tenant, self-hosted, local-first system. Tacking
subscriptions into it would compromise the property that makes it
trustworthy: working without internet.

Instead: a **separate "HomeHub Cloud" companion service** (different repo,
different deployment) holds Stripe, customers, subscriptions, and entitlement
APIs. HomeHub talks to it through a single `/entitlements` endpoint and
caches the response.

```
┌──────────────────────┐         ┌──────────────────────┐
│  HomeHub (local)     │ ←HTTPS→ │  HomeHub Cloud       │
│  - leak engine       │         │  - Stripe customers  │
│  - device control    │         │  - subscriptions     │
│  - push (free)       │         │  - entitlements API  │
│  - BYOK SMS/email    │         │  - SMS/email relay   │
│  - 30-day cache      │         │  - OTA firmware host │
└──────────────────────┘         └──────────────────────┘
```

## Three principles, non-negotiable

1. **Free tier always includes a working notification path.** Push is free,
   BYOK SMS/email is free. The system can always reach the user.
2. **Auto-shutoff is never gated.** If a Tuya/Shelly valve is configured, paid
   or not, it fires on L3.
3. **Grace period > expiration.** Card fails → 30 days of full functionality
   + polite emails → quiet drop to free. Billing errors must not cause floods.

## Tier proposal

| | Free | Plus (~$5/mo) | Pro (~$15/mo) |
|---|---|---|---|
| Leak detection | ✓ | ✓ | ✓ |
| Auto-shutoff | ✓ | ✓ | ✓ |
| Push notifications | ✓ | ✓ | ✓ |
| Local UI + device control | ✓ | ✓ | ✓ |
| BYOK SMS/email | ✓ | ✓ | ✓ |
| Managed SMS | – | 50/mo | 500/mo |
| Managed email | – | ✓ | ✓ |
| Remote access (no port-forward) | – | ✓ | ✓ |
| OTA firmware | – | ✓ | ✓ |
| Off-site SQLite backup | – | ✓ | ✓ |
| Voice escalation | – | – | ✓ |
| Multi-property | – | – | ✓ |
| Insurance-grade event reports | – | – | ✓ |
| History retention | 1 yr local | 1 yr local | unlimited cloud |

## Entitlement protocol

HomeHub stores a `license_key` in `settings`. Daily, it calls:

```
GET https://cloud.homehub.example/entitlements
Authorization: Bearer <license_key>

→ {
    "tier":       "plus",
    "expiresAt":  "2026-12-31T00:00:00Z",
    "features":   ["sms","email_managed","remote_access","ota","backup"],
    "smsQuotaRemaining": 47,
    "gracePeriodEndsAt": null
  }
```

Cached in the `subscription` table for **30 days**. After 30 days of no
contact, falls back to free tier features only — but the safety-critical path
(detection, push, auto-shutoff, BYOK channels) keeps working forever.

## Entitlement check chokepoint

The notification router is the single place that consults entitlements:

```ts
async function send(channel, user, payload) {
  if (channel.requiresEntitlement && !entitled(channel.name)) {
    if (user.byok[channel.name]) return channel.sendViaUserCreds(user, payload);
    return; // skip silently
  }
  await channel.send(user, payload);
}
```

OTA, remote access, and backup each have their own single-point gates.
**The leak engine itself never consults entitlements.**

## Pricing model recommendations

Three workable models, not mutually exclusive:

| Model | Description | Why |
|---|---|---|
| **Hardware-bundled** | Sell AuraFlow kit; software is free; cloud is optional add-on | Most consumer-IoT margin lives in hardware |
| **BYOK** | No subscription; user pastes own Twilio/Resend keys | Zero billing infra; great for power users |
| **Open core** | OSS HomeHub + closed paid HomeHub Cloud as separate product | Cleanest separation; easiest to scale |

**Recommended hybrid: all three.** Free OSS HomeHub with BYOK escape hatch,
optional paid Cloud for users who don't want to run their own Twilio.

## What NOT to monetize

- The local Angular dashboard
- Device control (Kasa/Shelly/Tuya)
- Local push notifications
- The leak engine
- BYOK SMS/email/voice
- Reading + event history within reasonable retention (≥1 yr)

These are the things that would feel rented. Don't.

## Build order

1. **Don't build any of this yet.** Get the leak engine + BYOK notifications
   shipped first.
2. Add **BYOK SMS/email**. Same notification router code, configured
   per-user. Validates which channels people actually use.
3. After ~10 real users, decide if managed SMS is worth running. Often the
   answer is "no — sell hardware bundles instead."
4. If yes: stand up HomeHub Cloud with Stripe + a **single tier** ($X/mo,
   all-in). Don't build three tiers from day one — you'll guess wrong.

## Tables added to HomeHub (small)

```sql
-- Already in homehub-backend.md:
CREATE TABLE subscription (
  license_key   TEXT PRIMARY KEY,
  tier          TEXT NOT NULL DEFAULT 'free',
  features_json TEXT NOT NULL DEFAULT '[]',
  cached_at     TEXT NOT NULL,
  expires_at    TEXT,
  grace_until   TEXT
);
```

That's the entire footprint inside HomeHub. Everything else lives in the
separate Cloud service.
