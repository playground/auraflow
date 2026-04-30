# ESP32 Firmware Spec

Lives in `auraflow/src/`. Built with **Moddable SDK** so we can write
JavaScript/TypeScript instead of C/C++.

## Module layout

```
auraflow/src/
├── firmware/
│   ├── main.ts             — orchestrator + adaptive polling loop
│   ├── modbus.ts           — Modbus RTU framing + CRC16
│   ├── tuf2000m.ts         — read flow rate, totalizer, signal quality
│   ├── wifi.ts             — connect, auto-reconnect with backoff
│   ├── uplink.ts           — POST to HomeHub, ring buffer on failure
│   ├── provisioning.ts     — first-boot AP captive portal
│   ├── nvs.ts              — Wi-Fi creds, HomeHub URL, key, sensorId
│   ├── ota.ts              — daily firmware update check
│   ├── watchdog.ts         — hardware watchdog setup
│   └── diag.ts             — minimal /diag HTTP server (read-only)
├── manifest.json           — Moddable build manifest
├── mcconfig                — chipset config
└── web/                    — see web flasher section
    ├── index.html
    └── manifest.json
```

## Modbus / TUF-2000M

The TUF-2000M reports Float32 in **CDAB byte order** (low register first, then
high register, big-endian within each register). Confirmed and codified.

```ts
// firmware/tuf2000m.ts
function parseTUF2000Float(highRegister: number, lowRegister: number): number {
  const buf = new ArrayBuffer(4);
  const view = new DataView(buf);
  view.setUint16(0, lowRegister,  false); // big-endian within register
  view.setUint16(2, highRegister, false);
  return view.getFloat32(0, false);
}

function parseFlowResponse(resp: Uint8Array): number {
  const high = (resp[3] << 8) | resp[4];
  const low  = (resp[5] << 8) | resp[6];
  return parseTUF2000Float(high, low);
}
```

**Verification step before shipping:** open a known faucet, compare the value
to a measured flow (bucket + stopwatch). If nonsense, the unit is configured
for ABCD — flip the two `setUint16` calls. Document which config the hardware
ships with.

### Registers to read

| Register (decimal) | Bytes | Meaning |
|---|---|---|
| 1–2 | 4 | Instantaneous flow rate (m³/h), Float32 CDAB |
| 9–10 | 4 | Cumulative totalizer (m³), Float32 CDAB |
| 92 | 2 | Signal quality (0–99) |
| 72–73 | 4 | Velocity (m/s), Float32 CDAB — useful for diagnostics |

(Register addresses vary slightly across TUF-2000M firmware revisions —
verify against the datasheet that ships with the unit and pin in code with a
named constant.)

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
  capped at 60 s.
- POST readings to `${HOMEHUB_URL}/internal/sensors/flow/readings` with
  `X-Internal-Key` header.
- On any failure (network down, 5xx, timeout) push the reading into a RAM
  ring buffer (~5 min capacity). On reconnect, flush oldest first with
  `tOffsetMs = nowMs - bufferedAtMs`.
- Apply the response body's `pollIntervalMs` immediately for the next loop.
  No on-device config UI needed.

## Adaptive polling

Two cadences, both server-pushed in the response body:

- `flowingPollIntervalMs` (default 5 s): used while last reading > threshold.
- `idlePollIntervalMs` (default 30 s): used otherwise.

The dashboard edits these on the server; the ESP32 picks up the change on the
next successful POST. **No HTTP server is needed on the ESP32 for config.**

A small read-only diagnostics server (`/diag` returning RSSI, uptime, free
heap, last error, last reading) is kept for in-field troubleshooting only —
never used as a control plane.

## Provisioning (first boot)

1. ESP32 has no NVS values → boots into AP mode (`AuraFlow-Setup-XXXX`).
2. User connects to the AP, browser opens captive portal.
3. Form captures: Wi-Fi SSID + password, HomeHub URL, `INTERNAL_API_KEY`,
   `sensorId` (e.g. `auraflow-mainline-01`).
4. Values persisted to NVS; ESP32 reboots into normal mode.
5. After first successful POST, HomeHub pins the sensor's MAC in
   `sensors.first_seen_mac`. Subsequent ingests with a different MAC for the
   same `sensorId` are rejected.

The web flasher (below) can also do this provisioning step over WebSerial
immediately after flashing, so the user never has to join an AP.

## OTA firmware updates

Daily check (random offset 0–60 min to avoid thundering herd):

```
GET ${HOMEHUB_URL}/firmware/${sensorId}/manifest.json
→ { "version": "0.4.0",
    "url": "https://.../auraflow-0.4.0.bin",
    "sha256": "..." }
```

If `version != currentVersion`, download, verify SHA256, flash to the inactive
partition, set boot pointer, reboot. On boot failure (no successful POST
within 5 minutes), revert to previous partition. Standard A/B partition
pattern.

OTA is a tier-`plus` feature when subscriptions land — the manifest endpoint
returns 404 for free-tier instances. See [`subscription-model.md`](./subscription-model.md).

## Watchdog + boot reason

Configure ESP32 hardware watchdog with 60 s timeout. Reset it from the main
loop. Any hang → reboot. Send `bootReason` (`power` | `software` |
`watchdog` | `unknown`) in the first POST after each reboot so the dashboard
can surface stability problems.

## Anti-flap

If Wi-Fi reconnects more than 5× in an hour, throttle to one attempt per 5
min and log the issue (visible in `/diag`). Don't busy-loop on a misconfigured
network.

## Web flasher

Static site in `auraflow/web/`. Built on **ESP Web Tools** + WebSerial.
Required to be served over HTTPS (`localhost` works for dev).

```html
<!-- web/index.html -->
<!DOCTYPE html>
<html>
<head>
  <title>AuraFlow Flasher</title>
  <script type="module"
          src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module">
  </script>
</head>
<body>
  <h1>Flash your AuraFlow sensor</h1>
  <p>Use Chrome or Edge over HTTPS.</p>
  <esp-web-install-button manifest="./manifest.json">
    <button slot="activate">Connect &amp; Flash</button>
  </esp-web-install-button>
  <hr>
  <h2>Provision</h2>
  <!-- After flash, open serial port and POST provisioning JSON to ESP32 -->
  <form id="prov">
    <input name="ssid" placeholder="Wi-Fi SSID">
    <input name="psk"  placeholder="Wi-Fi password" type="password">
    <input name="url"  placeholder="HomeHub URL (https://...)">
    <input name="key"  placeholder="INTERNAL_API_KEY">
    <input name="sid"  placeholder="sensorId (e.g. auraflow-mainline-01)">
    <button type="submit">Send provisioning</button>
  </form>
</body>
</html>
```

```jsonc
// web/manifest.json
{
  "name": "AuraFlow Water Flow Monitor",
  "version": "0.1.0",
  "builds": [{
    "chipFamily": "ESP32",
    "parts": [
      { "path": "bootloader.bin", "offset": 4096 },
      { "path": "partitions.bin", "offset": 32768 },
      { "path": "firmware.bin",   "offset": 65536 }
    ]
  }]
}
```

**Important:** Don't bake `INTERNAL_API_KEY` into the published `.bin`. The
binary is generic; provisioning writes secrets to NVS via WebSerial after
flashing.

## Build

```bash
# Manual
mcconfig -m -p esp32

# Web flashing tool
# - host web/index.html, web/manifest.json, and the .bin files over HTTPS
# - WebSerial requires HTTPS (localhost OK for dev)
```

## Open verification items

1. Confirm CDAB vs ABCD against the unit's actual M88 setting.
2. Confirm register addresses against the unit's datasheet revision.
3. Measure flow against a known-volume container before trusting the engine.
4. Verify watchdog reset doesn't disrupt an in-flight Modbus read mid-frame.
