# AuraFlow — Documentation Index

AuraFlow is a local-first, non-invasive water-flow monitoring and leak-detection
system. An ESP32 reads ultrasonic transit-time flow data from a TUF-2000M over
RS485/Modbus and forwards readings to the **HomeHub** backend, which runs
duration-based leak detection, drives alerts, and (optionally) closes a smart
shutoff valve.

## Repo split

| Repo | Responsibility |
|---|---|
| `auraflow/` (this repo) | ESP32 firmware (Moddable JS), web flasher, hardware docs |
| `homehub/` | All backend logic: ingestion, persistence, leak engine, alerts, UI |

All server-side code lives in HomeHub. AuraFlow is hardware + firmware + docs.

## Documents

| File | Topic |
|---|---|
| [`bring-up.md`](./bring-up.md) | **Step-by-step ESP32 bring-up runbook** — read this first when you have hardware in hand |
| [`valve-setup.md`](./valve-setup.md) | **Tuya water valve onboarding** — pairing, localKey extraction, DPS, registration |
| [`../web/README.md`](../web/README.md) | **Web flasher** — browser-based flashing via ESP Web Tools, for sharing devices without a toolchain |
| [`architecture.md`](./architecture.md) | System overview, data flow, repo split rationale |
| [`homehub-backend.md`](./homehub-backend.md) | Schema, routes, leak engine, settings |
| [`firmware.md`](./firmware.md) | ESP32 firmware spec, Modbus parser, OTA, web flasher |
| [`notifications.md`](./notifications.md) | Tiered alert strategy (push/email/SMS/voice) |
| [`hardware.md`](./hardware.md) | BOM, wiring, sensor placement, Tuya valve notes |
| [`subscription-model.md`](./subscription-model.md) | Deferred billing strategy and architecture |
| [`roadmap.md`](./roadmap.md) | Phases, sequencing, ship order, MVP scope |
| [`notes.txt`](./notes.txt) | Original design notes (source material — do not edit) |
| [`waterflow.md`](./waterflow.md) | Original implementation summary (source material) |

## Status

- Planning complete.
- No implementation started.
- First slice to ship: HomeHub backend ingestion + leak engine (see
  [`roadmap.md`](./roadmap.md) Phase 1).
