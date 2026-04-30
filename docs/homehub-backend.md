# HomeHub Backend Spec

All backend code lives in `homehub/packages/backend/`. AuraFlow contributes a
new sensor subsystem to HomeHub.

## New files

| Path | Purpose |
|---|---|
| `src/routes/sensors.ts` | JWT-protected user API: list sensors, fetch readings/events/consumption |
| `src/routes/internal.ts` | Extended with `POST /internal/sensors/flow/readings` |
| `src/sensors/flow-engine.ts` | Pure state machine: open/extend/close events, alert level transitions |
| `src/sensors/flow-engine.test.ts` | Unit tests for the engine |
| `src/sensors/consumption.ts` | Bucketed totalizer-based consumption rollups |
| `src/notifications/router.ts` | Tiered notification dispatch (see `notifications.md`) |
| `src/notifications/providers/*.ts` | One file per channel (push, email, sms, voice) |

## Schema additions (additive migration in `src/db/index.ts`)

```sql
CREATE TABLE IF NOT EXISTS sensors (
  id                INTEGER PRIMARY KEY AUTOINCREMENT,
  sensor_id         TEXT    NOT NULL UNIQUE,        -- 'auraflow-mainline-01'
  type              TEXT    NOT NULL,               -- 'flow' (extensible)
  alias             TEXT    NOT NULL,               -- 'Main line'
  config            TEXT,                           -- JSON, see below
  first_seen_mac    TEXT,                           -- pinned on first ingestion
  last_seen_at      TEXT,
  last_rssi         INTEGER,
  last_uptime_sec   INTEGER,
  last_signal_quality INTEGER,
  firmware_version  TEXT,
  pending_state     TEXT,                           -- 'idle' | 'flowing' (debounce persistence)
  pending_count     INTEGER NOT NULL DEFAULT 0,
  created_at        TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS flow_readings (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  sensor_id       INTEGER NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,
  rate_m3h        REAL    NOT NULL,
  total_m3        REAL,                             -- cumulative totalizer
  signal_quality  INTEGER,
  t_offset_ms     INTEGER NOT NULL DEFAULT 0,       -- for ring-buffered flushes
  recorded_at     TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_flow_readings_sensor_time
  ON flow_readings(sensor_id, recorded_at);

CREATE TABLE IF NOT EXISTS flow_events (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  sensor_id    INTEGER NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,
  start_time   TEXT    NOT NULL DEFAULT (datetime('now')),
  end_time     TEXT,
  peak_rate    REAL,
  avg_rate     REAL,
  total_m3     REAL,
  status       TEXT    NOT NULL DEFAULT 'active'    -- 'active' | 'closed'
                                CHECK(status IN ('active','closed')),
  alert_level  INTEGER NOT NULL DEFAULT 0           -- 0..3, monotonic until close
);
CREATE INDEX idx_flow_events_active ON flow_events(sensor_id, status);

CREATE TABLE IF NOT EXISTS flow_event_acks (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id    INTEGER NOT NULL REFERENCES flow_events(id) ON DELETE CASCADE,
  user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  acked_at    TEXT    NOT NULL DEFAULT (datetime('now')),
  reason      TEXT                                  -- 'pool', 'lawn', 'shower', etc.
);

CREATE TABLE IF NOT EXISTS notification_preferences (
  id                INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id           INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  channel           TEXT    NOT NULL,               -- 'push' | 'email' | 'sms' | 'voice'
  min_level         INTEGER NOT NULL DEFAULT 1,     -- 1..3
  quiet_hours_from  TEXT,                           -- 'HH:MM' or null
  quiet_hours_to    TEXT,
  enabled           INTEGER NOT NULL DEFAULT 1,
  destination       TEXT,                           -- email addr or phone E.164
  UNIQUE(user_id, channel)
);

CREATE TABLE IF NOT EXISTS notification_dispatches (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id      INTEGER NOT NULL REFERENCES flow_events(id) ON DELETE CASCADE,
  user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  channel       TEXT    NOT NULL,
  level         INTEGER NOT NULL,
  sent_at       TEXT    NOT NULL DEFAULT (datetime('now')),
  acked_at      TEXT,
  ack_token     TEXT    UNIQUE
);

-- Subscription cache (see subscription-model.md). Empty/'free' until a license is set.
CREATE TABLE IF NOT EXISTS subscription (
  license_key    TEXT PRIMARY KEY,
  tier           TEXT NOT NULL DEFAULT 'free',
  features_json  TEXT NOT NULL DEFAULT '[]',
  cached_at      TEXT NOT NULL DEFAULT (datetime('now')),
  expires_at     TEXT,
  grace_until    TEXT
);
```

Follow HomeHub's existing additive-migration convention (try/catch on
`ALTER TABLE` for column adds, full rebuild only when constraints change).

## Per-sensor `config` JSON

Stored in `sensors.config`. Defaults applied if fields are missing.

```jsonc
{
  "version": 1,                         // bump on edit; ESP32 reads to detect change
  "pipeOuterDiameterMm": 25,            // informational; configured in TUF-2000M itself
  "fluidType": "water",
  "minSignalQuality": 60,               // reject readings below this
  "flowingThresholdM3h": 0.006,         // ~0.1 L/min — counts as "flowing"
  "warnAfterMin": 30,                   // L1 → L2 transition
  "criticalAfterMin": 60,               // L2 → L3 transition
  "burstRateM3h": 1.5,                  // immediate L3 if exceeded
  "slowDripMaxM3h": 0.06,
  "slowDripAfterHours": 24,
  "pollIntervalMs": 5000,
  "flowingPollIntervalMs": 5000,
  "idlePollIntervalMs": 30000,
  "quietHours": { "from": "01:00", "to": "05:00", "thresholdM3h": 0.003 },
  "vacationMode": false,                // any flow → immediate L3
  "shutoffDeviceId": null,              // FK to devices(id); null = no auto-close
  "ignoredEventIds": []                 // user-acknowledged "I know I'm watering"
}
```

## Global settings (`settings` table rows)

| Key | Default | Purpose |
|---|---|---|
| `water.unitPreference` | `imperial` | `metric` \| `imperial` for UI |
| `water.costPerM3` | `null` | optional $ overlay on consumption charts |
| `water.timezone` | system | for daily-bucket boundaries |
| `water.weeklyDigestEnabled` | `1` | turn weekly summary email on/off |
| `water.weeklyDigestDayOfWeek` | `0` | 0=Sunday |
| `water.escalationDelayMin` | `5` | minutes to wait for ack before next channel |

## Routes

### Internal (X-Internal-Key)

```
POST /internal/sensors/flow/readings
```
Request body:
```jsonc
{
  "sensorId":      "auraflow-mainline-01",
  "rateM3h":       0.42,
  "totalM3":       1234.56,           // optional
  "signalQuality": 91,                // optional
  "tOffsetMs":     0,                 // ≥0; subtracted from receivedAt
  "rssi":          -62,               // optional diagnostics
  "uptimeSec":     84211,
  "firmwareVersion": "0.3.1",
  "mac":           "AA:BB:CC:DD:EE:FF",
  "bootReason":    "power"            // 'power'|'software'|'watchdog'|'unknown'
}
```
Response:
```jsonc
{
  "pollIntervalMs":         5000,     // server-pushed adaptive cadence
  "flowingPollIntervalMs":  5000,
  "idlePollIntervalMs":     30000,
  "configVersion":          1,        // ESP32 caches; refetches full config when bumped
  "ackTokens":              ["..."]   // optional: tokens for any pending acks
}
```

### User API (JWT)

```
GET  /api/sensors                              -- list
GET  /api/sensors/:id                          -- detail + config
PUT  /api/sensors/:id/config                   -- edit thresholds (bumps config.version)
GET  /api/sensors/:id/readings?from=&to=
GET  /api/sensors/:id/events?status=&limit=
GET  /api/sensors/:id/consumption?bucket=hour|day|month&from=&to=
POST /api/events/:id/ack                       -- ack with optional reason
POST /api/sensors/:id/snooze?hours=4           -- "lawn mode" — suppress alerts
POST /api/events/:id/ack/:token                -- one-tap ack from notification (no JWT)
```

## Leak engine state machine

`processFlow(sensorId, rate, totalM3, signalQuality, recordedAt)` is a pure
function over the current sensor row + open event row. It returns
`{ event, levelChanged }`.

**Transitions:**

| From | To | Trigger |
|---|---|---|
| no event | open at level 0 | `rate > flowingThreshold` for 2 consecutive samples |
| level 0 | level 1 | `rate > burstRateM3h` OR duration ≥ 5 min |
| level 1 | level 2 | duration ≥ `warnAfterMin` |
| level 2 | level 3 | duration ≥ `criticalAfterMin` |
| any | level 3 (immediate) | `vacationMode === true` |
| any | level 3 (immediate) | quiet hours active AND `rate > quietHours.thresholdM3h` |
| open | closed | `rate < flowingThreshold` for 3 consecutive samples |

**Invariants:**

- `alert_level` is monotonic within an event — never downgraded until close.
- Debounce counters live in `sensors.pending_state` + `pending_count` so a
  backend restart mid-event does not flicker the state.
- Readings with `signalQuality < minSignalQuality` are stored but **not** fed
  to the engine (`last_signal_quality` is updated for the dashboard).
- On level change, append a row to `notification_dispatches` and call the
  notification router.

**Slow-drip detector** is a separate periodic job (every 15 min) that scans
closed events of the last 48 h for `avg_rate < slowDripMaxM3h && duration_h >
slowDripAfterHours` and raises a level-2 alert independently.

## Reading retention

Cron job in HomeHub prunes `flow_readings` older than 7 days. `flow_events`
are kept indefinitely (low-cardinality summary data).

## Auto-shutoff verification

When the engine fires `getDriver(...).setPower(valveIp, false)` on level 3:

1. Record dispatch in `notification_dispatches` (channel `'shutoff'`).
2. Schedule a 30 s follow-up job that re-reads the next reading.
3. If `rate` has not dropped by ≥80%, escalate: push critical "valve failed
   to close", and on tier `Pro`, place a voice call.
4. Do **not** retry the valve close (servo wear).

## Hardening / fixes baked in

These are the issues raised during plan review — they're requirements, not
nice-to-haves:

- **A.** Persist debounce counters in `sensors.pending_state/count`.
- **B.** Reject readings below `minSignalQuality`; surface in UI.
- **C.** Honor `tOffsetMs` for ring-buffered flushes.
- **D.** Verify auto-shutoff actually stopped flow within 30 s.
- **E.** Per-sensorId rate limit (10 req/s) on the ingestion route.
- **F.** Pin `first_seen_mac` on first ingestion; reject on mismatch.

## Tests required before merge

- `flow-engine.test.ts`: open/extend/close, level transitions, monotonicity,
  debounce-survives-restart, signal-quality rejection.
- Integration test: POST a synthetic 90-minute flow stream (5 s cadence) and
  assert L1 → L2 → L3 transitions fire at correct timestamps.
- Integration test: simulate ring-buffer flush with `tOffsetMs`; assert
  `recorded_at` is reconstructed correctly.
- `notification-router.test.ts`: routing matrix per level, quiet-hours
  override on L3, BYOK fallback when entitlements absent.
