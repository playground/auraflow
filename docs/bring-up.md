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

## 1. Install ESP-IDF v6.0 (one-time)

Follow Espressif's setup for your OS:
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html>

Typical macOS install:

```bash
mkdir -p ~/sandbox/esp32 && cd ~/sandbox/esp32
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git
./esp-idf/install.sh esp32
```

Then export `IDF_PATH` in your shell rc so the npm wrapper scripts can
find it:

```bash
echo 'export IDF_PATH=$HOME/sandbox/esp32/esp-idf' >> ~/.zshrc
source ~/.zshrc
```

Verify:

```bash
ls "$IDF_PATH/export.sh"   # should exist
```

`scripts/with-idf.sh` sources `$IDF_PATH/export.sh` on demand, so the npm
build / flash / monitor commands work from a fresh shell without manual
sourcing.

## 2. Build the firmware

```bash
cd ~/sandbox/auraflow
npm install                      # only on first checkout
npm run test:c                   # sanity check — pure C modules; should pass
npm run build:firmware           # idf.py build via scripts/with-idf.sh
```

The first build of a fresh checkout takes ~3 min while ESP-IDF compiles
its components. Subsequent incremental builds are <30 s.

If the build complains about a missing component or the SDK version, re-run
`./install.sh esp32` from `$IDF_PATH` to refresh the toolchain.

## 3. Flash + monitor

With the ESP32 plugged in:

```bash
npm run flash:firmware           # builds (if needed), flashes, then monitors
```

The script targets `/dev/cu.usbserial-0001` at 115200 baud — adjust the
port in `package.json` if yours differs (`ls /dev/cu.usbserial-*`).

Monitor controls: **Ctrl+]** to quit, **Ctrl+T R** to reboot the chip,
**Ctrl+T H** for full help.

A successful first boot prints something like:

```
I (xxxx) auraflow: AuraFlow firmware 0.1.0-c starting (boot=power)
W (xxxx) auraflow: NVS not provisioned — listening on UART0 for PROVISION:{...}
```

That's the cue to provision (next section).

## 4. Provision the device

Two paths — pick whichever fits.

### 4a. Web flasher form (recommended)

Open the web flasher (`web/index.html` served at `localhost:8080` or your
GitHub Pages URL) and click **Connect & Provision**. Pick the same serial
port you used to flash. The page waits for the firmware's
`READY:auraflow-provision-v1` heartbeat (within ~8 s of reset), then
reveals the form. Fill it in and click **Send to device**. On success
you'll see `✓ Provisioned. Device will restart…`.

### 4b. CLI script

```bash
# 1. Edit docs/.env (gitignored) with one line:
#    PROVISION:{"sensorId":"auraflow-mainline-01",
#               "wifiSsid":"YourSSID",
#               "wifiPassword":"YourPassword",
#               "homehubUrl":"http://192.168.1.10:3000",
#               "internalApiKey":"paste-INTERNAL_API_KEY-here",
#               "wordOrder":"low-word-first"}
#
# Optional static IP — all three or none:
#               "staticIp":"192.168.1.42",
#               "staticGateway":"192.168.1.1",
#               "staticNetmask":"255.255.255.0"
#
# 2. With the ESP32 plugged in and listening (NVS not provisioned):
npm run provision
```

The script sends the line over UART0 at 115200 baud. The firmware
validates, writes to NVS, and reboots automatically.

### 4c. Already-provisioned device (changing config later)

Once a device is online you don't need either path above — point a
browser at `http://<device-ip>/edit`, change fields, click **Save &
reboot**. Same NVS keys, same validator.

### Verifying provisioning succeeded

After a reboot you should see logs like:

```
I (xxxx) auraflow: provisioned: sensorId=… homehub=… wordOrder=…
I (xxxx) wifi_mgr: got IP
I (xxxx) auraflow: Wi-Fi up
I (xxxx) auraflow: poll task started; cadence will adapt to server config
```

If it stays at `NVS not provisioned`, the values didn't persist — re-run
the provisioning step.

## 5. Register the sensor in HomeHub

Easiest path is the dashboard:

1. Visit `http://<homehub-host>:4201/sensors`
2. Click **+ Add sensor**
3. **Sensor ID** must match the value you wrote to NVS in step 4 (e.g.
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

## 6. Verify ingestion

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
- The internal key doesn't match — compare the value in NVS (visible at
  `http://<device-ip>/edit`) to HomeHub's `INTERNAL_API_KEY` env var
- The sensor ID doesn't match — case-sensitive

If the sensor reports but **rate is nonsense** (NaN, infinity, way off):

- The TUF-2000M is configured for ABCD instead of CDAB. Flip the word
  order via `http://<device-ip>/edit` — change "Modbus word order" to
  ABCD (high-word-first), Save & reboot.

## 7. Calibration check (recommended once)

1. Note the totalizer in the sensor detail view (or in the meter's display).
2. Fully fill a calibrated container (5 gal / 19 L) from a faucet downstream
   of the meter.
3. Note the new totalizer.
4. Difference should match within ±5%.

If off by >5%, recheck pipe outer diameter and wall thickness in the
TUF-2000M's M11/M12 menus, or reposition the transducers (signal quality
should be ≥60 — visible in the sensor detail page).

## 8. Updating the firmware

Once the device is online, all future updates go OTA:

```bash
# Make changes, rebuild, push to the device:
npm run build:firmware
npm run ota:firmware -- <device-ip>
```

The script serves the new `auraflow.bin` from your laptop and POSTs the
URL to `http://<device-ip>/ota`. The device fetches, swaps to the
inactive OTA slot, and reboots — typically 10-60 s end to end.

No USB cable needed for updates after the first flash.

## 9. Next steps

- Once the Tuya valve arrives, follow [`valve-setup.md`](./valve-setup.md).
- After the valve is wired up, use the dashboard's **Test fire valve**
  button on the sensor detail page to verify the actuation path works end
  to end before betting on it during a real L3 event.
