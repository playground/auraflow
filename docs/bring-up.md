# ESP32 Bring-Up Runbook

End-to-end commands for getting an AuraFlow ESP32 from "out of the box" to
"reporting flow data to HomeHub." Follow top to bottom; total time ≈30 min
once the toolchain is installed.

Prerequisites:

- ESP32 dev board (ESP32-WROOM-32 or -S3) and a data USB cable
- TUF-2000M ultrasonic flow meter, transducers, RS485 ↔ TTL adapter, 24V
  supply for the meter (see [`hardware.md`](./hardware.md))
- HomeHub backend running and reachable on your LAN (note its IP and port)
- HomeHub admin JWT *or* admin login at `/login`

## 1. Install the Moddable SDK (one-time)

Follow the official setup for your OS:
<https://github.com/Moddable-OpenSource/moddable/blob/public/documentation/Moddable%20SDK%20-%20Getting%20Started.md>

After install, verify:

```bash
echo $MODDABLE
# should print the SDK path
mcconfig --help
# should print the build-tool help
```

If `MODDABLE` is empty, source the SDK's `setupmac.sh` (macOS) or run the
shell init their docs specify.

## 2. Build the firmware

```bash
cd ~/sandbox/auraflow
npm install                              # only on first checkout
npm test                                 # sanity check — should be 32/32 green
npm run build:firmware                   # debug build + flash to connected ESP32
# or for a release build later:
npm run build:firmware:release
```

`mcconfig -d -m -p esp32` is what `build:firmware` runs. The first build
compiles the entire SDK and takes ~10 min. Subsequent builds are <30s.

If `mcconfig` fails with "no such file `manifest.json`", run from the repo
root, not from inside `src/`.

## 3. Provision via the Moddable serial REPL

After flash, open the REPL:

```bash
serial2xsbug /dev/cu.usbserial-XXXXXXXX 460800 8N1
# or use the Moddable terminal — whichever your install provides
```

Once you see `auraflow: NVS not provisioned. Run provisioning script over
serial.`, paste this (replace placeholders):

```js
import Preference from 'preference';
const D = 'auraflow';
Preference.set(D, 'wifiSsid',       'YourSSID');
Preference.set(D, 'wifiPassword',   'YourPassword');
Preference.set(D, 'homehubUrl',     'http://192.168.1.10:3000');     // your homehub
Preference.set(D, 'internalApiKey', 'paste-INTERNAL_API_KEY-here');  // from homehub .env
Preference.set(D, 'sensorId',       'auraflow-mainline-01');
Preference.set(D, 'wordOrder',      'low-word-first');               // CDAB; flip later if needed
```

Reset the ESP32 (button on the board, or unplug/replug).

You should now see logs like:

```
auraflow: wifi up — opening serial + starting poll loop
```

If it stays at `NVS not provisioned`, the Preferences didn't take — re-run
the provisioning lines and confirm with `Preference.keys('auraflow')`.

## 4. Register the sensor in HomeHub

Easiest path is the dashboard:

1. Visit `http://<homehub-host>:4201/sensors`
2. Click **+ Add sensor**
3. **Sensor ID** must match the value you wrote to NVS in step 3 (e.g.
   `auraflow-mainline-01`)
4. **Display name** is for the dashboard (e.g. `Main line`)
5. Click **Create**

Or via curl:

```bash
JWT=$(curl -s http://<homehub-host>:3000/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"name":"admin","password":"YOUR_ADMIN_PASSWORD"}' | jq -r .accessToken)

curl -X POST http://<homehub-host>:3000/api/sensors \
  -H "Authorization: Bearer $JWT" \
  -H 'Content-Type: application/json' \
  -d '{"sensorId":"auraflow-mainline-01","alias":"Main line","type":"flow"}'
```

## 5. Verify ingestion

Within a minute of provisioning + registering, the dashboard's `/sensors`
list should show:

- **last seen**: a recent timestamp (the ESP32 is posting)
- **flowing** badge if water is currently moving through the meter
- No **stale** badge

Click the sensor to open detail view. Open a faucet downstream — you should
see the live rate chart respond within ~5s.

If the sensor shows "never reported":

- ESP32 is not reaching HomeHub — check the URL, port, and that they're on
  the same LAN
- The internal key doesn't match — compare NVS value to HomeHub's
  `INTERNAL_API_KEY` env var
- The sensor ID doesn't match — case-sensitive

If the sensor reports but **rate is nonsense** (NaN, infinity, way off):

- The TUF-2000M is configured for ABCD instead of CDAB. Flip the word order:
  ```js
  Preference.set('auraflow', 'wordOrder', 'high-word-first');
  ```
- Reset the ESP32; rate should look normal.

## 6. Calibration check (recommended once)

1. Note the totalizer in the sensor detail view (or in the meter's display).
2. Fully fill a calibrated container (5 gal / 19 L) from a faucet downstream
   of the meter.
3. Note the new totalizer.
4. Difference should match within ±5%.

If off by >5%, recheck pipe outer diameter and wall thickness in the
TUF-2000M's M11/M12 menus, or reposition the transducers (signal quality
should be ≥60 — visible in the sensor detail page).

## 7. Next steps

- Once the Tuya valve arrives, follow [`valve-setup.md`](./valve-setup.md).
- After the valve is wired up, use the dashboard's **Test fire valve**
  button on the sensor detail page to verify the actuation path works end
  to end before betting on it during a real L3 event.
