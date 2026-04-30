# Notifications

Tiered alerts across push, email, SMS, and (optional) voice. Lives entirely in
HomeHub backend at `src/notifications/`.

## Routing matrix

| Level | Trigger | Push | Email | SMS | Voice |
|---|---|---|---|---|---|
| 0 | Normal flow event closed | – | – | – | – |
| 1 | 5+ min flow, or burst rate spike | ✓ | – | – | – |
| 2 | 30+ min flow (`warnAfterMin`) | ✓ | ✓ | – | – |
| 3 | 60+ min flow, vacation flow, quiet-hours flow | ✓ | ✓ | ✓ | optional |
| Special | Auto-shutoff fired but flow didn't drop | ✓ | ✓ | ✓ | ✓ |
| Digest | Weekly consumption summary | – | ✓ | – | – |

L3 fires all channels in **parallel** (`Promise.allSettled`). Don't wait for
one to fail — they all go out at once. Belt and suspenders.

**Quiet hours never apply to L3.** That's the point of L3.

## Channel notes

| Channel | Cost | Wakes user at 3am? | Use case |
|---|---|---|---|
| Push (existing `web-push`) | Free | Maybe — depends on DND/OS | L1 informational, live UI |
| Email | Free under quotas | Rarely | L2 warnings, digests, audit trail |
| SMS | ~$0.008/msg via Twilio (US) | Yes (with iPhone Emergency Bypass) | L3 critical |
| Voice (TTS) | ~$0.014/min via Twilio | Yes — most aggressive | Valve-failed-to-close escalation |

## Provider picks

| Channel | Default provider | Alternatives |
|---|---|---|
| Push | `web-push` (already in HomeHub) | – |
| Email | Resend | AWS SES, SMTP via Nodemailer |
| SMS | Twilio | AWS SNS |
| Voice | Twilio Voice (`<Say>` TwiML) | – |

Provider creds live in HomeHub `settings` table, encrypted at rest (or env
vars for the MVP). UI in the user dashboard to enter creds (BYOK) or to opt
into managed delivery once a subscription tier exists.

## Notifier interface

```ts
// src/notifications/Notifier.ts
export interface NotificationPayload {
  title:    string;
  body:     string;
  level:    0 | 1 | 2 | 3;
  eventId:  number;
  ackUrl:   string;     // /api/events/:id/ack/:token — one-tap, no JWT
  sensorId: string;
}

export interface Notifier {
  readonly channel: 'push' | 'email' | 'sms' | 'voice';
  send(user: User, payload: NotificationPayload): Promise<void>;
}
```

```ts
// src/notifications/router.ts
export async function notify(level, payload, eventId) {
  const users   = await loadEnabledUsers(level);
  const dispatches = users.flatMap(u => channelsFor(u, level).map(ch => ({ u, ch })));

  await Promise.allSettled(dispatches.map(async ({ u, ch }) => {
    if (ch.requiresEntitlement && !entitled(ch.channel)) {
      if (u.byok[ch.channel]) return ch.sendViaUserCreds(u, payload);
      return; // skip silently — push and email already fired
    }
    await ch.send(u, payload);
    recordDispatch(eventId, u.id, ch.channel, level, payload.ackToken);
  }));

  scheduleEscalation(eventId, level);
}
```

## Per-user preferences

`notification_preferences` table (see [`homehub-backend.md`](./homehub-backend.md)).

UI:
- Per-channel toggle per user
- Per-channel `min_level` threshold (e.g. SMS only on L3)
- Quiet hours (HH:MM range) — never applies to L3
- Destination override (alternate email or phone)

## Escalation + acknowledgement

Every notification carries a one-tap ack URL containing a per-dispatch token:

```
https://homehub.local/api/events/123/ack/9f3a...
```

- Hitting it marks `notification_dispatches.acked_at`, sets the event to
  acknowledged, and **cancels pending escalations**.
- No JWT required (the token IS the auth) — but tokens are single-use,
  rate-limited, and tied to a single dispatch row.
- If no ack within `water.escalationDelayMin` (default 5 min) on L3, the
  next user in the household gets paged. If no ack after another 5 min, voice
  call (if enabled).

## Snooze ("lawn mode")

Without snooze, the third false alert kills user trust and they mute the
system entirely. Build it with the first notification:

```
POST /api/sensors/:id/snooze?hours=4&reason=lawn
```

Sets a `snoozedUntil` field; engine still records events but suppresses
notifications below L3 until it expires. A banner stays in the UI for the
duration. **L3 still fires** — burst/vacation alerts cannot be snoozed.

## BYOK before managed

Ship order:

1. **Push** wired to engine (already have `web-push` in HomeHub).
2. **BYOK email** — user pastes Resend/SMTP creds in settings.
3. **BYOK SMS** — user pastes Twilio creds.
4. **Managed delivery** — only once subscription model exists. See
   [`subscription-model.md`](./subscription-model.md).
5. **Voice** — only if SMS proves insufficient.

BYOK costs you nothing to ship and validates the routing matrix with real
users before you commit to running infrastructure.

## Tests

- Routing matrix per level (snapshot test of `channelsFor(user, level)`).
- Quiet-hours override on L3 (must still fire).
- BYOK fallback when entitlements absent.
- Snooze suppresses L1/L2 but not L3.
- Ack token is single-use.
- Escalation timer cancellable on ack.
