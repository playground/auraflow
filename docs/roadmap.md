# Roadmap

Phases roughly in order of dependency. Each phase delivers something you can
exercise end-to-end before moving on.

## Phase 1 — HomeHub backend MVP (smallest end-to-end slice)

**Ship target: testable with `curl`, no hardware needed.**

In `homehub/packages/backend/`:
- Schema migrations (`sensors`, `flow_readings`, `flow_events`,
  `flow_event_acks`, `notification_preferences`,
  `notification_dispatches`, `subscription`).
- `src/sensors/flow-engine.ts` — pure state machine + tests.
- `src/sensors/consumption.ts` — totalizer-bucketed rollups.
- `src/routes/internal.ts` — extend with `POST /internal/sensors/flow/readings`.
- `src/routes/sensors.ts` — `GET /api/sensors`, detail, readings, events,
  consumption, snooze, ack.
- A–F hardening: persist debounce; signal-quality gate; tOffsetMs;
  shutoff-verify; per-sensorId rate limit; MAC pinning.
- 7-day reading retention cron.

**Acceptance:** synthetic POST stream produces correct event timeline with
L1→L2→L3 transitions and emits Socket.io events. No hardware.

## Phase 2 — Notifications (push first, BYOK after)

In `homehub/packages/backend/src/notifications/`:
- `Notifier` interface + router with routing matrix.
- `providers/push.ts` — wire existing `web-push` into the engine.
- `providers/email.ts` — Resend (or BYOK SMTP).
- `providers/sms.ts` — Twilio (BYOK first, managed deferred to subscription work).
- One-tap ack endpoint with single-use tokens.
- Snooze ("lawn mode").
- Escalation timer.
- Tests for routing matrix, quiet-hours override, ack idempotency.

**Acceptance:** L2 fires push+email; L3 fires push+email+SMS; ack URL stops
escalation; snooze suppresses L1/L2 but not L3.

## Phase 3 — ESP32 firmware

In `auraflow/src/firmware/`:
- Modbus RTU + CRC16.
- TUF-2000M reader (CDAB float, signal quality).
- Wi-Fi connect + auto-reconnect.
- Uplink with ring buffer + `tOffsetMs`.
- Server-pushed adaptive polling.
- Provisioning (NVS + first-boot AP captive portal).
- Hardware watchdog + boot-reason reporting.
- `/diag` read-only HTTP server.

**Acceptance:** ESP32 wired to a TUF-2000M produces identical engine behavior
to the synthetic stream from Phase 1, against a real homehub instance.

## Phase 4 — HomeHub frontend (Angular)

In `homehub/packages/frontend/`:
- Add `chart.js` + `ng2-charts` (no chart lib present yet — verified).
- Sensors feature module: list view, detail page.
- Live rate chart (last 1h, Socket.io stream).
- Daily/weekly/monthly consumption bar chart (totalizer-derived).
- Event timeline (Gantt-style with alert-level color coding).
- Settings panel for per-sensor `config` JSON edits.
- Snooze button + active-snooze banner.
- Per-user notification preferences UI.
- BYOK provider creds form (Twilio/Resend).

**Acceptance:** dashboard reflects live readings, consumption charts match
totalizer math, snoozing one sensor doesn't affect another.

## Phase 5 — Web flasher

In `auraflow/web/`:
- ESP Web Tools install button.
- `manifest.json` for ESP32 partitions.
- Post-flash provisioning form over WebSerial.
- HTTPS hosting (GitHub Pages or equivalent).

**Acceptance:** non-technical user can flash + provision a new sensor end-to-end
from a browser.

## Phase 6 — Auto-shutoff

- Wire `setPower(valve, false)` into the engine on L3 transitions.
- 30-second post-fire verification job.
- "Auto-shutoff failed" critical alert path.
- UI: link a sensor to a shutoff device; per-event manual close button.

**Acceptance:** an L3 alert closes the configured valve; a deliberately
non-responsive valve produces the failure alert.

## Phase 7 — Hardware operability

- OTA firmware channel (`GET /firmware/<sensorId>/manifest.json`).
- A/B partition + boot-failure rollback.
- Sensor health view (RSSI, uptime, boot reasons, signal quality history).
- "Stale sensor" alert (no readings for >5× expected interval).

## Phase 8 — Subscription (deferred)

See [`subscription-model.md`](./subscription-model.md). Don't start until
~10 real users exist.

## Sequencing

```
Phase 1 ─► Phase 2 ─┬─► Phase 4 ─► Phase 5
                    │
Phase 3 ────────────┤
                    │
                    └─► Phase 6 ─► Phase 7 ─► Phase 8
```

Phase 1 and Phase 2 are blocking for everything. Phase 3 (firmware) and
Phase 4 (frontend) can run in parallel once Phase 1 is in.

## Open questions before implementation

These should be answered before or during Phase 1, not after:

1. **Modbus byte order on this specific TUF-2000M unit** — confirm CDAB vs
   ABCD (M88 setting). Plan assumes CDAB.
2. **Auto-shutoff valve choice** — Walmart Tuya retrofit (cheap, works,
   slow) vs Shelly Plus + motorized ball valve (cleaner). Picks change
   Phase 6 priorities.
3. **Single sensor or multiple at install?** Plan supports many; confirm.
4. **Network reachability** — ESP32 and HomeHub on the same LAN? If yes,
   plain HTTP is fine for the MVP. If HomeHub will ever be reached
   externally, set up TLS termination via a reverse proxy now.
5. **Push reliability for the user's primary phone** — confirm web-push
   actually wakes the phone before relying on it as the L1/L2 channel. If
   not, BYOK SMS comes earlier in Phase 2.

## MVP definition (what "shipped" means)

The smallest thing that's actually useful:

- One ESP32 monitoring the main line.
- HomeHub running locally, ingesting readings, running the leak engine.
- Push notification when 30 min of continuous flow is detected.
- A web page showing live flow rate and the event timeline.
- A one-tap snooze button for "I'm watering the lawn for 4 hours."

That's Phase 1 + the push slice of Phase 2 + the minimum slice of Phase 4 +
Phase 3 firmware. Everything else is enhancement.
