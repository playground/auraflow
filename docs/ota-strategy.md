# HomeHub OTA Strategy

**Status: decision recorded. Build option A first; revisit at fleet
inflection points.**

This doc covers OTA for the **HomeHub container** running on a Raspberry
Pi (or any Linux box). ESP32 firmware OTA is a separate path — already
shipped, see [`firmware.md`](./firmware.md).

The question is: how do HomeHub deployments in the field get upgraded
from one release to the next?

## Three options considered

### A. Manual "Update now" button in the dashboard

A card in the HomeHub UI shows running version + latest available, with
an **Update** button. Backend endpoint runs `docker compose pull &&
docker compose up -d` and reports back. Same UX pattern as the ESP32
firmware OTA already shipped on `sensor-detail`.

- Zero cloud infra. ~1 day of code.
- Users stay in control — matches the local-first ethos.
- No mass-bricking risk: a bad release only affects users who clicked.
- Add a "behind by N versions" nag banner + an opt-in "auto-update on
  Sunday 3 AM" toggle. The toggle is the gateway drug to option B
  without committing the operational burden.

Downside: forgetful users never update. Acceptable until fleet > ~50
and stragglers start mattering.

### B. Watchtower + pinned tags + version-check endpoint

Watchtower runs alongside HomeHub on the Pi, polling for new images.
HomeHub fetches a small `GET /api/recommended-version?tier=plus`
endpoint hourly and rewrites the compose tag if it differs, then
restarts. Endpoint runs on a $5/mo Lightsail box.

- Better than option A at scale — no clicks needed.
- Worse than option A at early stage — a bad image at 3 AM reboots
  every paying customer's Pi at once.
- DIY rollout staging via the version endpoint (10% → 50% → 100% over
  48 h). DIY rollback via local health check after `up -d`.

Reasonable when the fleet is past ~50 deployed units and the manual
path is leaving stragglers.

### C. Mender (managed OTA-as-a-service)

Purpose-built for IoT fleet OTA. Free up to 50 devices, then ~$0.40–
$1/device/month. Gives staged rollouts, rollback, fleet dashboard, and
audit log out of the box — no ops work.

Worth it once past ~200 devices. Until then, paying for capacity that
isn't used.

### D. Open Horizon (rejected)

Considered and rejected for current scale. Built for 10K+
heterogeneous edge nodes with policy-based deployment. EC2 management
hub burns ~$35/mo even idle. Adds operational complexity (Exchange,
Agbot, CSS, agent) that doesn't pay back at hundreds-of-units scale.

Revisit only if: fleet > 5K AND multiple SKUs need different builds AND
someone's full-time job is fleet ops.

## Decision rule

| Fleet size | Choice |
|---|---|
| 0 — ~50 units | **Option A** (manual + opt-in auto) |
| ~50 — ~500 | **Option B** (Watchtower + version endpoint) |
| ~500+ | **Option C** (Mender) or **B** if happy DIYing |
| 5K+ AND multi-SKU | Reconsider Open Horizon |

Migration between A → B → C is straightforward — the version-check
endpoint is the same shape regardless of who's consuming it.

## Non-negotiables (apply to all options)

1. **Never use `:latest` in production compose files.** Pin to specific
   tags blessed by the version endpoint.
2. **Local DB volumes survive container recreation.** `docker compose
   up -d` must not drop SQLite state.
3. **Health-check + auto-rollback** after any update: curl
   `/api/health` for 60 s; if it never comes up, `docker compose down`
   and pull the previous tag.
4. **Safety-critical paths never gate on OTA state.** Leak detection,
   auto-shutoff, and BYOK notifications must work on whatever version
   is currently installed, even if updates are paused or failing.
5. **"Pause updates" toggle in the UI.** Some users will always want
   to vet releases manually.

## What this means for the subscription model

[`subscription-model.md`](./subscription-model.md) lists "OTA firmware"
as a Plus-tier feature. With option A in place, that's misleading —
manual updates are free. The accurate gating is:

- **Free:** manual update button + nag banner.
- **Plus:** auto-update opt-in (when shipped) + staged rollout slot
  (Plus users get blessed releases first or last, depending on
  positioning).

When option B/C lands, "managed updates" becomes the genuine paid
feature.

## Hardware bundle implication

If we ship a "HomeHub Pi" SKU (see the hardware bundle discussion in
[`subscription-model.md`](./subscription-model.md)), the device should
arrive with option A enabled, the auto-update opt-in *off* by default,
and the dashboard surfaced prominently on first boot. Selling hardware
that updates itself without consent is a recall waiting to happen.
