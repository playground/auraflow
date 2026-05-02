# Architecture

## System diagram

```
┌─────────────────────┐    RS485     ┌──────────────────┐
│  TUF-2000M          │ ◄──Modbus──► │  ESP32           │
│  ultrasonic flow    │   RTU 9600   │  (ESP-IDF C)     │
│  transducer + meter │              │  - read flow     │
└─────────────────────┘              │  - parse CDAB    │
                                     │  - buffer        │
                                     │  - HTTP POST     │
                                     └────────┬─────────┘
                                              │ HTTPS
                                              │ X-Internal-Key
                                              ▼
            ┌─────────────────────────────────────────────────┐
            │   HomeHub backend (Express + better-sqlite3)    │
            │                                                 │
            │  POST /internal/sensors/flow/readings           │
            │     │                                           │
            │     ▼                                           │
            │  flow-engine (state machine)                    │
            │     │                                           │
            │     ├─► flow_readings (raw, 7-day retention)    │
            │     ├─► flow_events   (durable summaries)       │
            │     │                                           │
            │     ▼                                           │
            │  notification router  ──► push (free)           │
            │                       ──► email (BYOK/managed)  │
            │                       ──► SMS  (BYOK/managed)   │
            │                                                 │
            │  device driver registry                         │
            │     ├─ Kasa, Shelly, Tuya (existing)            │
            │     └─► auto-shutoff valve (Tuya/Shelly)        │
            └────────────────┬────────────────────────────────┘
                             │ Socket.io
                             ▼
                   ┌─────────────────────┐
                   │  HomeHub frontend   │
                   │  (Angular v21)      │
                   │  - sensors view     │
                   │  - live chart       │
                   │  - consumption      │
                   │  - event timeline   │
                   └─────────────────────┘
```

## Why this split

HomeHub is already an operational, multi-driver smart-home backend with auth,
SQLite, Socket.io, and web-push wired in. AuraFlow adds a **sensor** subsystem
(inbound data) alongside HomeHub's existing **device** subsystem (outbound
control).

Putting the leak engine in HomeHub means the same backend that detects a leak
can directly close a Tuya/Shelly valve via the existing `DeviceDriver` registry
— no extra cross-service plumbing.

## Sensor vs device

HomeHub's existing `DeviceDriver` interface is for **outbound control**
(`setPower`, `getPower`, `getEnergy`). Sensors push inbound readings. They don't
fit the same shape, so AuraFlow introduces a parallel `sensors` table and
`/internal/sensors/...` ingestion path rather than forcing sensors through the
device abstraction.

Future sensor types (temperature, motion, leak puck) extend the same `sensors`
table by adding new `type` values and per-type ingestion routes / engines.

## Data flow (single reading)

1. ESP32 issues Modbus query to TUF-2000M every N seconds (adaptive: 5 s
   flowing, 30 s idle).
2. TUF-2000M responds with two 16-bit registers encoding a Float32 in CDAB byte
   order. ESP32 parses via `DataView`.
3. ESP32 POSTs to `https://homehub.local/internal/sensors/flow/readings` with
   `X-Internal-Key`. On failure, reading goes into a RAM ring buffer with a
   relative timestamp offset; flushed on reconnect.
4. HomeHub auth-checks the internal key, looks up sensor by `sensorId`, inserts
   `flow_readings`, calls `flow-engine.processFlow(...)`.
5. Engine updates the open `flow_events` row (or opens a new one), evaluates
   alert level transitions, emits Socket.io events, fires notifications on
   level changes.
6. Response body carries the current desired poll interval back to the ESP32 so
   the dashboard can adjust cadence without a device-side HTTP server.

## Identity and auth

| Identity | Issued by | Where it lives |
|---|---|---|
| ESP32 → HomeHub | `INTERNAL_API_KEY` env var on HomeHub | NVS on ESP32 (set during provisioning) |
| `sensorId` | Human, at provisioning time | NVS on ESP32; pinned to MAC on first ingestion |
| User → HomeHub | Existing JWT auth | HomeHub `users` table |
| HomeHub → Cloud (future) | License key | HomeHub `subscription` table |

The ESP32 is treated as a trusted-LAN device. No per-device PKI in the MVP.
TLS termination at a reverse proxy in front of HomeHub is the recommended
hardening path.

## Local-first invariants

The system MUST continue to function with no internet:

- Leak detection runs locally against local SQLite.
- Push notifications work without external services (web-push uses VAPID keys
  generated locally; phones receive when they're on the same network or via
  their OS's push relay).
- Auto-shutoff fires regardless of cloud state.
- The Tuya driver in HomeHub does local LAN control (port 6668), not cloud
  control, so Tuya valve closure works without internet.

Cloud-dependent features (managed SMS, remote access, OTA, off-site backup) are
strictly opt-in additions — see [`subscription-model.md`](./subscription-model.md).
