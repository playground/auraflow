# Hardware & Install

## Bill of materials (per sensor)

| Item | Qty | Notes |
|---|---|---|
| ESP32 dev board (ESP32-WROOM-32 or -S3) | 1 | USB-powered |
| TUF-2000M ultrasonic flow meter (main unit) | 1 | RS485 output |
| Ultrasonic transducers | 2 | Size matched to pipe OD: TS-2 (small), TS-M1 (medium) |
| RS485 ↔ TTL converter (MAX3485 or auto-direction module) | 1 | 3.3V logic |
| 24V DC power supply for TUF-2000M | 1 | Per spec |
| USB power supply for ESP32 | 1 | 5V 1A |
| Acoustic couplant grease | 1 tube | For transducer/pipe interface |
| Cable ties / mounting brackets | — | Per install |

Optional:

| Item | Purpose |
|---|---|
| Tuya / Shelly motorized ball valve | Auto-shutoff target |
| 3D-printed enclosure | Protect ESP32 + RS485 module |

## Wiring

```
        ESP32 (UART2)              RS485 module           TUF-2000M
        ──────────────             ──────────────         ──────────
GPIO 16 (RX)  ◄────────── RO ─────┤                ┌──── A (D+)
GPIO 17 (TX)  ──────────► DI ─────┤                │
                          DE/RE ─◄┤ tied or auto   │
3.3V          ──────────► VCC                      │
GND           ──────────► GND ────┤                └──── B (D-)
                                                   GND ─ GND
                                  ┌──── 24V DC ──── V+
                                  └──── GND ─────── V-
```

Notes:

- If the RS485 module has separate DE/RE, tie them together and either drive
  with a GPIO before each transmit, or use an auto-direction module to
  simplify firmware.
- Keep RS485 cable runs ≤ 50 m at 9600 baud — well within typical home use.
- 120Ω termination resistors at both ends only matter on long runs.
- If your TUF-2000M ships powered (mains in the unit), skip the 24V supply.

## Transducer placement

V-method (typical residential, pipes ≤ DN50/2"):

```
       ┌── Transducer A
       ▼
═══════════════════════ pipe ═══════════════════════
       ▲
       └── Transducer B  (offset by calculated distance D)
```

Z-method (large or noisy pipes):

```
   Transducer A ──┐
                  ▼
═════════════════════════ pipe ═════════════════════════
                                          ▲
                                  Transducer B ──┘
```

- Use the spacing distance D from the TUF-2000M's calculator (input pipe OD,
  wall thickness, fluid).
- Ensure straight-pipe length: ≥ 10× pipe OD upstream, ≥ 5× downstream from
  the transducer pair. Bends and valves nearby cause turbulent readings.
- Couplant grease **must** be applied between transducer face and pipe — air
  gaps kill the ultrasonic signal.
- Aim for signal quality (register 92) ≥ 60 after install. Below that,
  reposition.

## TUF-2000M one-time setup (via meter keypad)

| Menu | Field | Value |
|---|---|---|
| M11 | Pipe outer diameter | per install (mm) |
| M12 | Pipe wall thickness | per install (mm) |
| M14 | Fluid type | Water |
| M23 | Transducer mounting | V-method (or Z) |
| M88 | Data byte order | **CDAB** (firmware assumes this) |
| M*  | Slave ID | 1 |
| M*  | Baud | 9600 8N1 |

Document the chosen pipe OD/wall values per install — they're the single
biggest source of accuracy error.

## Calibration check

After install, run a known-volume calibration:

1. Note the totalizer reading (register 9–10).
2. Fully fill a calibrated container (5 gal / 19 L) from a faucet downstream
   of the sensor.
3. Note the new totalizer reading. Difference should match within ±5%.
4. If off by >5%, recheck pipe OD/wall thickness in M11/M12, or repeat the
   transducer placement.

A "Calibration helper" UI in HomeHub lets the user log expected vs measured
volume per check, building an accuracy history for the sensor.

## Auto-shutoff valve compatibility

HomeHub already supports Kasa, Shelly, and Tuya. For AuraFlow's auto-shutoff
target, options:

### Tuya Wi-Fi water valves (e.g. lever-switch retrofit valves)

**Functionally compatible** via HomeHub's existing Tuya driver — these
present as standard on/off devices on DPS 1.

Caveats (verify per unit):

- **localKey extraction** required: pair in the Smart Life app, then extract
  the device's local key via the Tuya IoT developer portal or `tinytuya`.
  Without it, the device can only be controlled via Tuya cloud — defeats
  local-first.
- **Closure speed**: lever-switch retrofits use a servo to physically rotate
  an existing ball valve; closure takes 5–15 s. Fine for burst protection,
  but not instant.
- **Firmware version**: `tuyapi` (the lib HomeHub uses) supports v3.1–3.4
  reliably; v3.5 is hit-or-miss. Confirm after pairing.
- **DPS variation**: most use DPS 1 for on/off, but some use 12 or expose a
  countdown on DPS 9. Run `device.get()` once after pairing and document.
- **Battery models**: avoid — they sleep, taking 10+ s to wake. Mains-powered
  units only.
- **Listing variability**: third-party marketplace listings rotate hardware;
  two units from the same listing may behave differently. Order one, prove
  it, then standardize.

**Verification logic in the engine** — required:

After firing `setPower(valveIp, false)` on L3, monitor the next reading. If
flow has not dropped ≥80% within 30 s, raise an "auto-shutoff failed"
critical alert and do **not** retry the valve close (servo wear). Surface
this prominently in the UI.

### Shelly Plus + motorized ball valve

More reliable than Tuya retrofits. First-class HomeHub support. Recommended
where retrofitability isn't a constraint.

## Network requirements

- ESP32 needs Wi-Fi reachability to the HomeHub host.
- HomeHub host should have a stable LAN IP or a `.local` mDNS hostname.
- TLS termination at a reverse proxy (e.g. Caddy) is recommended. WebSerial
  for the flasher requires HTTPS (or `localhost`).
- Nothing in the system requires inbound internet. All cloud-dependent
  features (managed SMS, OTA, remote access) are opt-in additions described
  in [`subscription-model.md`](./subscription-model.md).

## Per-install record

Keep a YAML or JSON record of every install (in HomeHub's `sensors.config`
or alongside it):

```yaml
sensorId: auraflow-mainline-01
location: "Main line, behind water heater"
installedAt: 2026-05-04
pipe:
  outerDiameterMm: 22
  wallThicknessMm: 1.5
  material: copper
transducers: TS-2
mountingMethod: V
spacingMm: 47.3
signalQualityAtInstall: 87
shutoffValve:
  type: tuya
  deviceId: <id>
  closureTimeSec: 12
  verifiedAt: 2026-05-04
calibrationCheck:
  expectedM3: 0.0189   # 5 gal
  measuredM3: 0.0193
  errorPct: 2.1
```
