# Provisioning + configuration — three scenarios

How an AuraFlow ESP32 gets its initial config (Wi-Fi creds, HomeHub URL,
sensor ID, static IP) and how those values change over the device's
lifetime. Three distinct lifecycles, served by the same firmware.

## TL;DR

| # | Scenario | Audience | Mechanism | Status |
|---|---|---|---|---|
| 1 | Pre-provisioning via web flasher | Makers / dev | Browser → ESP Web Tools flash → WebSerial form → `PROVISION:{...}` over UART0 | ✓ shipped (commit `8a78097` + `b33ca14`); needs static-IP fields added |
| 2 | Runtime tweaks via HomeHub dashboard | Anyone with dashboard access | Form on `/sensors/:id` → `PUT /api/sensors/:id/config` → next upload response delivers values back to firmware | ✓ shipped (commit `33dc97d` for polling cadence); pattern repeatable for more fields |
| 3 | Captive portal for shipped hardware | Non-technical end users | Power on → ESP32 in SoftAP mode → user joins `AuraFlow-Setup-XXXX` from phone → DNS hijack pops a portal → fill form → save + restart | not yet built — planned |

All three write the same NVS keys via the same validation code. The
firmware doesn't care which surface populated them.

---

## Scenario 1 — pre-provisioning via web flasher

Maker / developer workflow. Plug in USB, flash via the existing web
flasher (`auraflow/web/index.html`), fill in a form, click Save.

```
Mac/laptop browser
  │ ESP Web Tools (WebSerial) flashes firmware.bin
  │
  │ User fills "Connect & Provision" form:
  │   sensorId, wifiSsid, wifiPassword,
  │   homehubUrl, internalApiKey, wordOrder
  │   [+ static IP fields, see Commit 1 below]
  │
  ▼ WebSerial sends "PROVISION:{json}\n" at 115200 baud
ESP32 (running unprovisioned firmware)
  │ provisioning_esp.c parses the line, validates, saves NVS
  │ trace "OK\n", esp_restart()
  ▼
ESP32 (provisioned) — wifi_mgr_start, uplink loop running
```

**Already shipped.** The form lives in `web/index.html`. Firmware listener
in `src/firmware/c/main/provisioning_esp.c`. Match against the PROVISION
JSON validation in `src/firmware/c/main/provisioning.c`.

**Pending: static IP fields.** Form needs `staticIp` / `staticGateway`
/ `staticNetmask` inputs (under an "Advanced" expander), protocol gets
the same fields, `wifi_mgr` applies them before joining Wi-Fi.

---

## Scenario 2 — runtime tweaks via HomeHub dashboard

For values that can change while the device is online without breaking
the connection. Polling cadence, word order, alert thresholds, snooze,
auto-shutoff target, etc.

```
HomeHub dashboard at /sensors/:id
  │ user edits a field, clicks Save
  │
  ▼ PUT /api/sensors/:id/config { fieldA: ..., fieldB: ... }
HomeHub backend
  │ merges into sensors.config JSON in SQLite, version++
  ▼
ESP32 firmware
  │ on next POST /internal/sensors/flow/readings:
  │ response includes flowingPollIntervalMs, idlePollIntervalMs,
  │ configVersion, ... pulled from sensors.config
  │
  ▼ uplink_push() returns uplink_poll_config_t
main.c poll task uses new values on the very next iteration
```

**Already shipped:** the polling-cadence editor (commit `33dc97d`). The
mechanism is a complete pipeline:

- `sensor.config` JSON storage (SQLite)
- `PUT /api/sensors/:id/config` route (admin-gated)
- The existing internal ingestion route returns config in its response
- The C firmware's `uplink` module reads the response and the
  orchestrator switches behavior on the next poll cycle

**Adding more fields is the same recipe**, ~30 lines of frontend per
field plus 0–5 firmware lines if the device itself uses the value:

| Candidate field | Firmware-side use? | Notes |
|---|---|---|
| Modbus word order toggle | Yes — switch CDAB ↔ ABCD live | Useful for "I provisioned wrong, swap from dashboard" |
| Per-event debounce counts | No (engine is server-side) | Pure homehub change |
| `flowingThresholdM3h` | No (engine + cadence-selector both server-side and on-device for cadence) | Mixed; defaults are fine |
| Quiet-hours window | No (engine-side) | Pure homehub |
| Vacation mode | No (engine-side) | Pure homehub |

**Hard constraint:** scenario 2 cannot change anything that breaks the
push channel itself — Wi-Fi credentials, HomeHub URL, static IP.

---

## Scenario 3 — captive portal for shipped hardware

The productization story. Manufacturer pre-flashes firmware (and
optionally pre-binds a per-unit sensor ID at the factory). Customer
opens the box, plugs in, joins a Wi-Fi network from their phone, fills
in a portal form, and the sensor starts reporting to their HomeHub.

```
Power on → NVS unprovisioned → SoftAP mode

ESP32 broadcasts "AuraFlow-Setup-XXXX" (XXXX = last 4 of MAC)
  │
  │ User joins from phone
  │ DNS hijack (UDP listener answering all queries with AP IP)
  │ Phone OS triggers captive portal probe:
  │   iOS:     /hotspot-detect.html
  │   Android: /generate_204
  │   → catch-all handler returns the portal HTML
  │
  ▼ Phone shows portal:
HTML form
  │ Wi-Fi network dropdown (from /scan endpoint)
  │ Password
  │ HomeHub URL
  │ Static IP fields (optional)
  │ Sensor ID (or pre-bound, read-only)
  │
  ▼ POST /config { same JSON shape as PROVISION }
ESP32
  │ saves NVS, drops AP, esp_restart()
  ▼
ESP32 (provisioned) — joins home Wi-Fi, posts to HomeHub
```

**Not yet built.** Implementation plan is below.

After provisioning, the same `esp_http_server` instance can stay alive
on the device's STA-mode IP and serve a small read-only `/diag` plus
read-write `/config` for post-provisioning changes (commit 3 below).

---

## How the three scenarios coexist in one firmware

The firmware boot decides which provisioning surfaces to start, based
on NVS state:

```
app_main()
  │
  ▼ nvs_config_load(&cfg)
  │
  ├─ !is_provisioned(cfg)
  │    │
  │    ├─ start UART listener (existing — for maker workflow)
  │    └─ start SoftAP + captive portal (new — for consumer workflow)
  │       (both write the same NVS keys via the same validator;
  │        whichever lands first wins → save + esp_restart)
  │
  └─ is_provisioned(cfg)
       │
       ├─ wifi_mgr_start (STA mode, optionally with static IP)
       ├─ on_wifi_up → poll_task (Modbus + uplink loop)
       └─ start HTTP config server on the STA IP (commit 3 — optional)
```

This means scenarios 1 and 3 are **active simultaneously** when
unprovisioned. A maker USB-flashing a fresh board can use the WebSerial
form; a non-technical user with the same firmware can use the captive
portal. Same NVS keys either way.

Scenario 2 is independent of the boot lifecycle — it operates while
the device is online via the upload-response config push.

---

## Implementation plan — three commits

### Commit 1 — static IP plumbing (~2 hours)

Foundation for everything else. Useful with or without the captive portal.

**Touched files:**
- `src/firmware/c/main/nvs_config.{h,c,_esp.c}` — three new string fields:
  `static_ip`, `static_gateway`, `static_netmask`
- `src/firmware/c/main/provisioning.{h,c,_esp.c}` — cJSON extraction +
  validation. If any of the three is set, all three must be set.
- `src/firmware/c/main/wifi_mgr.{h,_esp.c}` — `wifi_mgr_config_t` gets
  the three fields; on `_esp.c` side, `esp_netif_dhcpc_stop` +
  `esp_netif_set_ip_info` before `esp_wifi_start` if static is configured
- `src/firmware/c/test/test_nvs_config.c`,
  `src/firmware/c/test/test_provisioning.c` — new validation tests
- `web/index.html` — three new optional inputs in an Advanced disclosure
- `web/index.html` JS — include in the PROVISION JSON

After this commit: anyone (maker) can flash + provision with a known
static IP via the web flasher.

### Commit 2 — captive portal (~1 day)

The consumer workflow. Adds AP mode + DNS hijack + HTTP server with
HTML form for first-time setup.

**New components:**
- `wifi_mgr` AP-mode entry — when unprovisioned, start `WIFI_MODE_APSTA`
  with SSID `AuraFlow-Setup-XXXX` (XXXX from MAC). DHCP server enabled
  via `esp_netif`.
- `captive_dns_esp.c` — small UDP listener on port 53 answering all
  queries with the AP's IP. ~100 lines, vendored from the ESP-IDF
  `wifi_provisioning_softap` example.
- `http_config_esp.c` — `esp_http_server` URI handlers:
  - `GET /` — captive portal HTML
  - `GET /scan` — JSON list of nearby SSIDs (esp_wifi_scan_start)
  - `POST /config` — accept PROVISION-shape JSON, validate (re-uses
    `provisioning_validate_struct`), save NVS, esp_restart
  - Catch-all path returning the portal (handles iOS/Android probes)
- `web/portal.html` (built into firmware as a PROGMEM string) — mobile-
  friendly form, scan rendering, password masking, optional static-IP
  toggle. Standalone HTML+CSS+inline JS, no build step.

**REQUIRES additions:** `esp_http_server`.

After this commit: a sealed unit can be powered on by a non-technical
user who configures Wi-Fi from their phone in under two minutes.

### Commit 3 — STA-mode config + diagnostics endpoints (~half day, optional)

Re-uses commit 2's HTTP handlers, mounted on the post-provisioning STA
IP. Adds:
- `GET /diag` — uptime, RSSI, last reading rate, free heap, last error,
  pending uplink buffer count
- `POST /config` (already exists from commit 2) — lets the user change
  HomeHub URL, static IP, even Wi-Fi creds, without going back to the
  captive portal or web flasher

After this commit: the device is self-explanatory. Browse to
`http://<device-ip>/` from any browser on the LAN to see what it's
doing or change its settings.

---

## Caveats and open questions

### Pre-binding sensor IDs at the factory

For shipping units, each unit needs a unique `sensorId`. Options:

- **Manufacturer programs NVS at flash time** — separate "factory"
  build that writes `sensorId` (and possibly `internalApiKey`) before
  the firmware ever boots. Captive portal then asks for everything *but*
  sensor ID.
- **Captive portal asks for it** — user types it from a sticker on the
  device. Works but pushes the responsibility to the user, easy to
  fat-finger.

Open. Defer until we have an actual unit-shipping motion.

### HomeHub URL discovery

Two business models:

- **User runs their own HomeHub** (current). Captive portal asks for
  the URL. User must know their HomeHub's IP/hostname.
- **Hosted "AuraFlow Cloud"** (`subscription-model.md`). URL becomes a
  build-time constant. Captive portal omits the field.

Open. Tied to the subscription decision.

### AP password

- **Open AP**: easiest. Anyone in radio range during setup can reach
  the portal — but the only damage is mis-provisioning a device they
  don't own.
- **Fixed-string password** (e.g., `auraflow-setup`): typed on the
  setup card. Modest deterrent.
- **Per-unit password** (last 4 of MAC, printed on a sticker): better
  but requires factory programming.

Recommend fixed string for first ship — easy to document, no factory
tooling needed.

### Captive portal browser quirks

iOS and Android each have probe URLs they hit to detect captive
portals:
- iOS:     `/hotspot-detect.html`, `/library/test/success.html`
- Android: `/generate_204`, `/gen_204`

A catch-all URI handler returning the portal HTML covers both. Some
older Windows machines also use `connecttest.txt` — the same catch-all
handles it.

### Security model for the STA-mode HTTP server (commit 3)

When the device exposes `/config` on the home LAN, anyone on that LAN
can theoretically hit it. Standard IoT trust model is "if you're on my
LAN, you're trusted" — same as other HomeHub-controlled devices. If
this changes, we add a Bearer token gated against `internalApiKey`.

---

## Cross-references

- [`firmware.md`](./firmware.md) — original Moddable JS firmware spec (now reference)
- [`bring-up.md`](./bring-up.md) — current C-firmware build/flash/provision steps
- [`homehub-backend.md`](./homehub-backend.md) — schema + API surfaces consumed
- [`subscription-model.md`](./subscription-model.md) — productization decisions
- [`roadmap.md`](./roadmap.md) — phase ordering
