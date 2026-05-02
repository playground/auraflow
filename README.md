# AuraFlow

Local-first water-flow leak detection. ESP32 + TUF-2000M ultrasonic flow
meter, talks to a [HomeHub](../homehub) backend over LAN.

This repo contains the **ESP32 firmware (ESP-IDF C)** and hardware/build
docs. All server-side code (ingestion, leak engine, alerts, dashboard)
lives in `homehub`.

```
┌──────────────┐   RS485    ┌──────────────┐   HTTP   ┌──────────────┐
│  TUF-2000M   │ ◄────────► │   ESP32      │ ───────► │   HomeHub    │
│  ultrasonic  │  Modbus    │  (ESP-IDF C) │  POST    │   backend    │
└──────────────┘            └──────────────┘          └──────────────┘
```

See [`docs/`](./docs) for the full design (architecture, schema,
notifications, hardware, subscription model, roadmap).

## Firmware language

The active firmware is **native C on ESP-IDF v6.0** under `src/firmware/c/`.
We started in Moddable TypeScript; ESP-IDF v6.0's newlib changes broke
`__FILE` / `_REENT` in xtensa-esp-elf, so we ported. The TS code is kept
in `src/firmware/ts/` as an executable spec — handy for understanding
intent — but is not built or flashed.

## Repo layout

```
auraflow/
├── docs/                       ← design + plan documents
├── src/firmware/c/             ← active firmware (ESP-IDF v6.0)
│   ├── main/
│   │   ├── main.c              ← orchestrator (boot, poll task)
│   │   ├── modbus.c            ← Modbus RTU framing + CRC16  (pure)
│   │   ├── tuf2000m.c          ← TUF-2000M parsers + register defs (pure)
│   │   ├── ring_buffer.c       ← FIFO bounded buffer (pure)
│   │   ├── nvs_config.c        ← config struct, helpers (pure)
│   │   ├── nvs_config_esp.c    ← NVS I/O
│   │   ├── provisioning.c      ← PROVISION:{json} parser + validator (pure)
│   │   ├── provisioning_esp.c  ← UART listener
│   │   ├── wifi_mgr.c          ← anti-flap + backoff math (pure)
│   │   ├── wifi_mgr_esp.c      ← Wi-Fi state machine, static IP
│   │   ├── uplink.c            ← payload serializer + poll-config (pure)
│   │   ├── uplink_esp.c        ← HTTP push, ring-buffer flush
│   │   ├── http_config.h       ← /, /edit, /diag, /config, /ota
│   │   ├── http_config_esp.c
│   │   ├── ota.h               ← background OTA via esp_https_ota
│   │   ├── ota_esp.c
│   │   └── cJSON.{h,c}         ← vendored — v6.0 dropped bundled `json`
│   ├── partitions.csv          ← OTA-capable layout (ota_0/ota_1/otadata)
│   └── sdkconfig.defaults
├── src/firmware/ts/            ← original Moddable TS (executable spec, not built)
├── web/                        ← browser-based flasher (ESP Web Tools + WebSerial)
│   ├── index.html              ← maker / end-user landing page
│   ├── manifest.json           ← chip + parts/offsets for ESP Web Tools
│   ├── bin/                    ← published .bin files (committed)
│   └── .nojekyll
├── scripts/
│   ├── with-idf.sh             ← sources ESP-IDF env so npm scripts work in fresh shells
│   ├── send-provision.py       ← serial sender for PROVISION:{json}
│   ├── ota.py                  ← serves the .bin and POSTs the URL to /ota
│   └── publish-flasher.sh      ← copies build output → web/bin/, syncs manifest version
└── package.json
```

## Building & flashing

Requires the ESP-IDF v6.0 toolchain at `$IDF_PATH` (typical:
`$HOME/sandbox/esp32/esp-idf`). `scripts/with-idf.sh` sources the env on
demand so the npm scripts work in a fresh shell.

```bash
npm run build:firmware                 # idf.py build
npm run flash:firmware                 # build + flash + monitor over USB
npm run monitor:firmware               # serial monitor (Ctrl-] to quit)
npm run ota:firmware -- <device-ip>    # serve .bin locally, trigger OTA via POST /ota
```

First flash on a fresh board needs USB. Once provisioned and on Wi-Fi,
all future updates can go OTA.

## Tests

```bash
npm run test:c     # host-side C tests (Modbus framing, CRC, TUF parsers,
                   # ring buffer, uplink poll-config defaults)
```

Pure modules (`*.c` without the `_esp` suffix) are testable on the host;
ESP-IDF I/O modules need the device to exercise.

## Web flasher

Browser-based flash for end users — no toolchain install. Hosted via
GitHub Pages on this repo (Source: GitHub Actions, deployed by
`.github/workflows/pages.yml`).

```bash
# Local dev
cd web && python3 -m http.server 8080  # open in Chrome/Edge

# Publish a new release
npm run build:firmware
npm run publish:flasher                # copies bins + syncs manifest version
git add web/bin web/manifest.json && git commit -m "release: flasher v..."
git push                               # workflow re-deploys Pages
```

End-user flow: open the published URL in Chrome/Edge → plug ESP32 → click
**Connect & Flash** → fill the on-page form → device reboots into
operation.

## Provisioning

Three paths, in order of preference:

1. **Web flasher form** (recommended): the same page that flashes also has
   a "Connect & Provision" form. Sends `PROVISION:{json}` over UART0
   after the device's `READY:auraflow-provision-v1` heartbeat.
2. **CLI**: `npm run provision` — reads `docs/.env` (gitignored) and
   sends the same line over serial.
3. **On-device after first boot**: hit `http://<device-ip>/edit` to change
   any field. Saves to NVS and reboots.

Required fields: `wifiSsid`, `wifiPassword`, `homehubUrl`,
`internalApiKey`, `sensorId`, `wordOrder` (default `low-word-first` —
CDAB, matches most TUF-2000M units). Optional: `staticIp` /
`staticGateway` / `staticNetmask` (all three or none).

## On-device endpoints

Once provisioned and online, the device runs an HTTP server on port 80:

| Method | Path     | Purpose |
|--------|----------|---------|
| GET    | /        | HTML status page (auto-refresh 5s) |
| GET    | /edit    | Config form preloaded from NVS |
| GET    | /diag    | JSON status (scriptable) |
| POST   | /config  | Partial JSON merge → save → reboot |
| POST   | /ota     | `{"url":"..."}` — fetch + apply firmware |

## Verifying the float word order

If your first reading is NaN, denormal, or wildly off, flip the word
order. Either via `/edit` (Modbus word order dropdown), or by
re-provisioning with `wordOrder: "high-word-first"`. The TUF-2000M's M88
setting tells you which the unit is using.

## Useful monitor keys

```
Ctrl+]      Quit the monitor
Ctrl+T R    Reboot the chip
Ctrl+T A    Toggle log addresses
Ctrl+T H    Full help menu
```

## License

ISC
