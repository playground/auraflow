# AuraFlow

Local-first water-flow leak detection. ESP32 + TUF-2000M ultrasonic flow
meter, talks to a [HomeHub](../homehub) backend over LAN.

This repo contains the **ESP32 firmware (Moddable JS)** and hardware/build
docs. All server-side code (ingestion, leak engine, alerts, dashboard) lives
in `homehub`.

```
┌──────────────┐   RS485    ┌──────────────┐  HTTP    ┌──────────────┐
│  TUF-2000M   │ ◄────────► │   ESP32      │ ───────► │   HomeHub    │
│  ultrasonic  │  Modbus    │ (Moddable JS)│  POST    │   backend    │
└──────────────┘            └──────────────┘          └──────────────┘
```

See [`docs/`](./docs) for the full design (architecture, schema,
notifications, hardware, subscription model, roadmap).

## Repo layout

```
auraflow/
├── docs/                   ← design + plan documents
├── src/firmware/           ← ESP32 firmware modules (TypeScript)
│   ├── modbus.ts           ← Modbus RTU framing + CRC16  (pure)
│   ├── modbus.test.ts
│   ├── tuf2000m.ts         ← TUF-2000M parsers + register defs (pure)
│   ├── tuf2000m.test.ts
│   ├── ring-buffer.ts      ← FIFO bounded buffer (pure)
│   ├── ring-buffer.test.ts
│   ├── uplink.ts           ← HTTP uplink to HomeHub + offline buffering
│   ├── nvs.ts              ← Moddable Preference wrapper for config
│   ├── wifi.ts             ← Wi-Fi connect + auto-reconnect + anti-flap
│   ├── main.ts             ← orchestrator (boot, poll loop)
│   └── moddable.d.ts       ← ambient types for Moddable APIs
├── manifest.json           ← Moddable build manifest
├── tsconfig.json           ← TypeScript config (pure modules Node-testable)
└── package.json
```

## Tests (pure logic — no hardware needed)

```bash
npm install
npm test
```

Covers Modbus framing/CRC, TUF-2000M float decoding for both ABCD and CDAB
word orders, and the offline ring buffer. The Moddable I/O modules
(`nvs`, `wifi`, `main`, `uplink` HTTP path) require Moddable + ESP32 to
exercise.

## Building the firmware

Prereqs:

1. Install the [Moddable SDK](https://github.com/Moddable-OpenSource/moddable)
   per their macOS / Linux instructions.
2. `export MODDABLE=/path/to/moddable` and source their setup.

Build + flash to a connected ESP32:

```bash
npm run build:firmware            # debug build
npm run build:firmware:release    # release build
```

## Browser-based flashing (web flasher)

Once binaries have been built once with `mcconfig`, anyone can re-flash
an ESP32 from Chrome/Edge without installing the Moddable SDK. See
[`web/README.md`](./web/README.md) for hosting and binary distribution.

```bash
cd web && python3 -m http.server 8080
# open http://localhost:8080 in Chrome
```

## Provisioning a new sensor

Until the Phase 5 web flasher ships, provision via the Moddable serial REPL
after flashing:

```js
import Preference from 'preference';
const D = 'auraflow';
Preference.set(D, 'wifiSsid',       'YourSSID');
Preference.set(D, 'wifiPassword',   'YourPassword');
Preference.set(D, 'homehubUrl',     'http://192.168.1.10:3000');
Preference.set(D, 'internalApiKey', 'YOUR_INTERNAL_API_KEY');
Preference.set(D, 'sensorId',       'auraflow-mainline-01');
Preference.set(D, 'wordOrder',      'low-word-first');   // CDAB; flip if values look wrong
```

Then create the sensor in HomeHub:

```bash
curl -X POST http://192.168.1.10:3000/api/sensors \
  -H "Authorization: Bearer <admin-jwt>" \
  -H "Content-Type: application/json" \
  -d '{"sensorId":"auraflow-mainline-01","alias":"Main line","type":"flow"}'
```

Power-cycle the ESP32. It'll connect to Wi-Fi, open serial, and begin
polling the TUF-2000M.

## Verifying the float word order

Read a known flow rate (open a faucet at a measured rate or fill a
calibrated bucket). If the value comes back NaN, denormal, or wildly off,
flip `wordOrder` in NVS:

```js
Preference.set('auraflow', 'wordOrder', 'high-word-first');   // ABCD
```

Reset and re-test.

## License

ISC
