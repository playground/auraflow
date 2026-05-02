# ESP32 Firmware Spec

Lives in `auraflow/src/firmware/c/`. Built with **ESP-IDF v6.0** (native C).
The original Moddable TypeScript code remains in `src/firmware/ts/` as an
executable spec only — not built or flashed.

## Module layout

```
auraflow/src/firmware/c/
├── main/
│   ├── main.c              ← orchestrator: boot, NVS load, Wi-Fi up, poll loop
│   ├── modbus.c            ← Modbus RTU framing + CRC16            (pure)
│   ├── tuf2000m.c          ← TUF-2000M parsers + register defs     (pure)
│   ├── ring_buffer.c       ← FIFO bounded buffer                   (pure)
│   ├── nvs_config.c        ← config struct, helpers                (pure)
│   ├── nvs_config_esp.c    ← NVS I/O (load / save / reset, poll-cadence cache)
│   ├── provisioning.c      ← PROVISION:{json} parser + validator   (pure)
│   ├── provisioning_esp.c  ← UART0 listener, cJSON extraction
│   ├── wifi_mgr.c          ← anti-flap + backoff math              (pure)
│   ├── wifi_mgr_esp.c      ← Wi-Fi state machine, static IP application
│   ├── uplink.c            ← payload serializer, poll-config defaults (pure)
│   ├── uplink_esp.c        ← HTTP push + ring-buffer flush, response parse
│   ├── http_config.h       ← /, /edit, /diag, /config, /ota
│   ├── http_config_esp.c
│   ├── ota.h               ← background OTA via esp_https_ota
│   ├── ota_esp.c
│   └── cJSON.{h,c}         ← vendored — v6.0 dropped the bundled `json` aggregate
├── partitions.csv          ← OTA-capable: nvs, otadata, ota_0, ota_1
├── sdkconfig.defaults      ← partition file, FreeRTOS tick, OTA-allow-HTTP, …
└── test/                   ← host-side unit tests for the pure modules
```

The `_esp.c` suffix marks ESP-IDF I/O modules (only built on `ESP_PLATFORM`);
unsuffixed `.c` files are pure logic and run in the host test harness via
`npm run test:c`.

## Modbus / TUF-2000M

The TUF-2000M reports Float32 in **CDAB byte order** by default (low register
first, then high register, big-endian within each register). The parser is
configurable for both word orders so a unit set to ABCD via M88 still works.

```c
// tuf2000m.c — byte-order-aware Float32 decode
modbus_err_t tuf2000m_parse_float_response(
    const uint8_t       *resp,
    size_t               len,
    tuf2000m_word_order_t wo,   // LOW_WORD_FIRST (CDAB) or HIGH_WORD_FIRST (ABCD)
    float               *out);
```

**Verification step before shipping:** open a known faucet, compare the value
to a measured flow (bucket + stopwatch). If nonsense, flip the `wordOrder`
field via `/edit` (or re-provision). Document which byte order the hardware
ships with.

### Registers to read

| Register (decimal) | Bytes | Meaning |
|---|---|---|
| 1–2  | 4 | Instantaneous flow rate (m³/h), Float32 CDAB |
| 9–10 | 4 | Cumulative totalizer (m³), Float32 CDAB |
| 92   | 2 | Signal quality (0–99) |
| 72–73| 4 | Velocity (m/s), Float32 CDAB — useful for diagnostics |

(Register addresses vary slightly across TUF-2000M firmware revisions —
verify against the datasheet that ships with the unit and pin in code with a
named constant in `tuf2000m.h`.)

### TUF-2000M setup (configured once via the meter's keypad)

| Parameter | Value | Notes |
|---|---|---|
| Slave ID | 1 | Modbus address |
| Baud rate | 9600 8N1 | ESP32 UART2 |
| Pipe outer diameter | per install | M11 |
| Pipe wall thickness | per install | M12 |
| Fluid type | Water | M14 |
| Transducer mounting | V or Z method | M23 — V for typical residential |
| Data byte order | CDAB | M88 |

Document the chosen values in `hardware.md` per install.

## Wi-Fi + uplink

- Connect using credentials from NVS. On disconnect, exponential backoff
  capped at 60 s. Anti-flap throttle: more than 5 connects/hour drops to
  one attempt per 5 min.
- POST readings to `${HOMEHUB_URL}/internal/sensors/flow/readings` with
  `X-Internal-Key` header.
- On any failure (network down, 5xx, timeout) the reading goes into a RAM
  ring buffer (~5 min capacity at the default 5 s cadence). On reconnect,
  flush oldest first with `tOffsetMs = nowMs - bufferedAtMs` so HomeHub can
  reconstruct timestamps.
- Apply the response body's `pollIntervalMs` / `flowingPollIntervalMs` /
  `idlePollIntervalMs` immediately for the next loop. Cadence is also
  mirrored to NVS so a reboot resumes at the last-known cadence instead of
  firmware defaults.

## Adaptive polling

Two cadences, both server-pushed in the response body:

- `flowingPollIntervalMs` (default 5 s): used while last reading > threshold.
- `idlePollIntervalMs` (default 30 s): used otherwise.

The dashboard edits these on the server; the ESP32 picks up the change on the
next successful POST. **HomeHub is authoritative** — there's no on-device
control surface that can override what HomeHub says (deliberate, single
source of truth).

## On-device HTTP server

`http_config_esp.c` runs an `esp_http_server` on port 80 once Wi-Fi is up:

| Method | Path     | Purpose |
|--------|----------|---------|
| GET    | /        | HTML status page (auto-refresh every 5s) |
| GET    | /edit    | Config form preloaded from NVS |
| GET    | /diag    | JSON status (scriptable equivalent of /) |
| POST   | /config  | Partial JSON merge → validate → save → reboot |
| POST   | /ota     | `{"url":"..."}` — fetch + apply firmware via esp_https_ota |

The httpd task stack is bumped to 8 KiB (default 4 KiB overflows once the
/edit handler's render buffer + cJSON + nvs_config_load frames stack up).

## Provisioning (first boot)

Three paths — see [`provisioning-scenarios.md`](./provisioning-scenarios.md):

1. **Web flasher form** (recommended) — same browser page that flashed the
   device sends `PROVISION:{json}` over UART0 after the firmware emits
   `READY:auraflow-provision-v1`. Values go straight to NVS.
2. **CLI** — `npm run provision` reads `docs/.env` and sends the same line
   over serial.
3. **Captive portal** (planned, not yet built) — SoftAP + DNS hijack +
   HTML form. Targets shipped consumer hardware.

After first successful POST, HomeHub pins the sensor's MAC in
`sensors.first_seen_mac`. Subsequent ingests with a different MAC for the
same `sensorId` are rejected.

## OTA firmware updates

Today: maker-driven. `POST /ota {"url": "..."}` hands a URL to a background
task that runs `esp_https_ota` (HTTP allowed via
`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` in `sdkconfig.defaults`), writes to the
inactive OTA slot, and `esp_restart()`s on success. One inflight at a time.

`scripts/ota.py` (`npm run ota:firmware -- <ip>`) wraps the trigger end-to-end:
serves the local `auraflow.bin` from a free port, detects the host's LAN IP,
and POSTs the URL to the device.

**Not yet wired (deferred to ship time):**
- Rollback on post-boot failure (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` +
  `esp_ota_mark_app_valid_cancel_rollback()`)
- SHA256 / signed-image verification

**Eventual end-user shape**: HomeHub-driven. HomeHub stores `.bin`s in
`data/firmware/`, dashboard shows "v0.1.0 → v0.2.0 available · Update", and
the click POSTs `http://<homehub>/firmware/auraflow-X.Y.Z.bin` to the
device's `/ota`. See [`subscription-model.md`](./subscription-model.md) for
the optional per-tenant tier.

## Watchdog + boot reason

ESP-IDF's task watchdog is enabled by default for the IDLE tasks. The orchestrator
emits `bootReason` (`power` | `software` | `watchdog` | `unknown`, derived
from `esp_reset_reason()`) on the first POST after each reboot so the
dashboard can surface stability problems.

## Anti-flap

Wi-Fi reconnect history lives in `wifi_mgr.c` (pure helpers, host-tested). If
more than 5 connects happen in the last hour, the manager throttles to one
attempt per 5 min and logs `anti-flap: …`. The same throttle drives the
`/diag` JSON's reconnect-rate field for in-field troubleshooting.

## Web flasher

Static site in `auraflow/web/`. Built on **ESP Web Tools** + WebSerial.
Serves four bins matching the OTA-capable partition layout
(`bootloader.bin` @ 0x1000, `partitions.bin` @ 0x8000,
`ota_data_initial.bin` @ 0xf000, `firmware.bin` @ 0x20000). Required to be
served over HTTPS — GitHub Pages provides it (`localhost` works for dev).

`npm run publish:flasher` copies + renames build outputs into `web/bin/`
and syncs `manifest.json`'s `version` field with `FIRMWARE_VERSION` in
`main.c`. The CI workflow at `.github/workflows/pages.yml` deploys on
every push that touches `web/`.

**Important:** Don't bake `INTERNAL_API_KEY` into the published `.bin`. The
binary is generic; provisioning writes secrets to NVS via WebSerial after
flashing.

## Build

```bash
npm run build:firmware     # idf.py build, via scripts/with-idf.sh
npm run flash:firmware     # build + flash + monitor over USB
npm run monitor:firmware   # serial monitor only (Ctrl-] to quit)
npm run ota:firmware -- <device-ip>
```

Requires `IDF_PATH` exported in your shell rc (`export IDF_PATH=$HOME/sandbox/esp32/esp-idf`
or wherever the SDK lives).

## Open verification items

1. Confirm CDAB vs ABCD against the unit's actual M88 setting.
2. Confirm register addresses against the unit's datasheet revision.
3. Measure flow against a known-volume container before trusting the engine.
4. Verify watchdog reset doesn't disrupt an in-flight Modbus read mid-frame.
5. End-to-end OTA test from a development laptop on the same LAN.
