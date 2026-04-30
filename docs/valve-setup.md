# Tuya Water Valve Setup

Step-by-step to get a Tuya Wi-Fi water valve (typical lever-switch retrofit
type — "AC Wi-Fi Smart Water Valve / Electric Gas Valve Controller") wired
up as the AuraFlow auto-shutoff target.

These valves are functionally fine but full of footguns. Plan ~30 min for
first setup; subsequent units are 5 min.

## What you'll need

- The valve, mounted on a ball-valve handle and powered (mains, not battery)
- Smart Life or Tuya app on a phone
- Either a Tuya IoT developer account *or* `tinytuya` installed on a laptop
- HomeHub admin access

## 1. Pair the valve in Smart Life

This step is unavoidable — the valve won't accept LAN control until it's
been onboarded to the Tuya cloud once.

1. Power up the valve. Hold the pair button until the indicator blinks
   rapidly (typically ~5 s; check the manual that came with it).
2. In Smart Life: **+ Add device → Auto Scan** (or manually pick "Water
   Valve Controller" → "Wi-Fi"). Pair to your 2.4 GHz network.
3. Confirm you can toggle the valve from the app — it should physically
   close and re-open. Time how long the close takes; note this as the
   **closure time** for HomeHub's verification window.

**If pairing fails**: usually 2.4 GHz only. Confirm your phone's Wi-Fi is
on the 2.4 GHz network (not 5 GHz) before retrying.

## 2. Extract the local key

You need the device's `localKey` to control it locally. Without it,
HomeHub can't talk to it without going through Tuya cloud — which defeats
the local-first design.

### Option A: tinytuya (faster if you don't already have a Tuya dev account)

```bash
pip install tinytuya
python -m tinytuya wizard
```

The wizard walks you through creating a Tuya cloud project, linking your
Smart Life account, and pulling all device IDs + local keys into a local
JSON file. Takes ~10 min.

After completion, `devices.json` (or similar) in the working directory has
entries like:

```json
[
  {
    "name": "Water Valve",
    "id": "bf123abc456def...",
    "key": "x9y8z7w6v5u4t3s2",
    "ip": "192.168.1.42",
    "ver": "3.4"
  }
]
```

Note the `id`, `key`, `ip`, and `ver`.

### Option B: Tuya IoT developer portal directly

1. Sign up at <https://iot.tuya.com> (free).
2. Create a Cloud project: **Cloud → Development → Create Cloud Project**.
   - Industry: any
   - Development Method: "Smart Home"
   - Data Center: pick the closest one
3. Once created, on the project's **Devices** tab → **Link Tuya App
   Account** → scan the QR with Smart Life. Your devices appear.
4. Click on the valve → copy `Device ID` and `Local Key`. The app will
   also show the Wi-Fi-detected `IP` and `Active Time` / `Online`.
5. The protocol version is shown alongside ("3.3", "3.4", or "3.5").

Note: HomeHub's UI also has a Tuya Cloud integration in `/settings →
Integrations → Tuya Cloud` that does this for you in-app — paste your
Access ID + Secret + Region from the IoT portal and the dashboard pulls
device IDs + local keys directly.

## 3. Identify the DPS

DPS = "data point" = the index for a specific function on a Tuya device.
Most valves use DPS 1 for on/off, but some lever-switch retrofits use 12,
and some expose a countdown on 9.

Find which DPS toggles your valve:

```bash
# install tuyapi globally for one-off testing
npm install -g tuyapi

# then, with deviceId/key/ip/version from step 2:
node -e "
const T = require('tuyapi');
(async () => {
  const d = new T({ id: 'YOUR_DEVICE_ID', key: 'YOUR_LOCAL_KEY', ip: 'YOUR_IP', version: '3.4', issueGetOnConnect: false });
  await d.connect();
  console.log(await d.get({ schema: true }));   // dumps all DPS values
  await d.disconnect();
})();
"
```

Output will look like:

```js
{
  dps: { '1': true, '9': 0, '12': false, '101': 'on' }
}
```

Open and close the valve from the Smart Life app and re-run the snippet —
the DPS that flips between `true`/`false` is the one HomeHub needs.

If it's `1`, you're done — that's the default. If it's something else
(e.g. `12`), make a note; you'll set it in HomeHub config.

## 4. Verify the protocol version works

The `tuyapi` library used by HomeHub supports v3.1, v3.3, and v3.4
reliably. v3.5 is hit-or-miss.

If your device shows `ver: "3.5"`:

- Try connecting with `version: '3.5'` in the snippet above first
- If `device.connect()` hangs or returns garbage, you're stuck unless you
  use Tuya cloud control (which loses local-first)
- In that case, return the unit and try a different SKU

If it's 3.3 or 3.4, you're golden.

## 5. Time the close

Critical for setting realistic expectations:

```bash
node -e "
const T = require('tuyapi');
(async () => {
  const d = new T({ id: 'YOUR_DEVICE_ID', key: 'YOUR_LOCAL_KEY', ip: 'YOUR_IP', version: '3.4', issueGetOnConnect: false });
  await d.connect();
  console.time('close');
  await d.set({ dps: '1', set: false });   // adjust DPS number if not 1
  // poll until acknowledgement; or just measure with a stopwatch from the audible motor
  console.timeEnd('close');
  await d.disconnect();
})();
"
```

Lever-switch retrofits typically take 5–15 s for the servo to fully rotate
the ball valve. Note the time. HomeHub's verification window is 60 s —
plenty of headroom — but if your valve takes longer than 30 s you should
audit whether it's mechanically faulty (the spring + handle binding can
sometimes slow them).

## 6. Register the valve in HomeHub

Two paths.

### Easy: paste into the dashboard

1. Go to `/settings → Integrations → Tuya Cloud` and enter your Access ID,
   Access Secret, and Region (one-time setup).
2. Click **Auto-fill from Tuya Cloud** during device discovery — HomeHub
   will populate the deviceId + key for you.

### Manual: SQL or `tuyapi` registration

Add to `devices` directly:

```sql
INSERT INTO devices (alias, ip, model, has_emeter, driver_type, driver_config)
VALUES (
  'Main shutoff valve',
  '192.168.1.42',
  'Tuya valve v3.4',
  0,
  'tuya',
  '{"deviceId":"bf123abc456def","key":"x9y8z7w6v5u4t3s2","version":"3.4"}'
);
```

If your valve uses a non-default DPS (e.g. 12), HomeHub's Tuya driver
defaults to DPS 1 — you'll need to extend the driver_config to specify
the dpsKey, or open an issue with the DPS value so the driver can be
made aware of it.

## 7. Wire it as the AuraFlow shutoff target

In the dashboard, open the sensor detail page (`/sensors/:id`) and:

1. In the **Auto-shutoff** card, pick the valve from the dropdown
2. Click **Save**
3. Click **Test fire valve** — you'll get a confirmation dialog. Confirm.
4. Watch the live chart. If the valve closed and water was running, the
   rate should drop to ~0 within your closure time + ~10 s buffer.
5. Open the valve again from Smart Life or the homehub dashboard.

If **Test fire valve** returns "valve actuation failed":

- The deviceId/key combo is wrong (re-check step 2)
- The version is wrong (re-check step 4)
- The valve is offline / unreachable on the LAN

If the actuation succeeded but **flow didn't drop**:

- The valve closed something different (you have it on a wrong line)
- The mechanical linkage between the servo and the ball valve handle has
  slipped (very common with retrofit clamps — re-tighten)
- The DPS sent the command but the servo didn't respond — sleep mode?
  battery-powered? confirm mains power.

## Troubleshooting cheat sheet

| Symptom | Likely cause |
|---|---|
| `Connect timeout` from tuyapi | wrong key, wrong IP, or device on different VLAN |
| Connects but `get({schema:true})` is empty | wrong protocol version — try the other |
| Toggles on Smart Life but not from tuyapi | local key was rotated; re-pull from cloud |
| Slow response after long idle | battery model — return it; mains is required |
| Closes but water still flows | mechanical: linkage slipped, or wrong line |
| `version: '3.5'` everywhere | swap for a different SKU or accept cloud-only |

## After bring-up

The valve is now part of the AuraFlow auto-shutoff loop. On any L3 alert
the engine will fire it within milliseconds, and verify within 60 s. If
verification fails (rate didn't drop), you'll get a critical multi-channel
notification — including SMS if you've configured Twilio in
`/settings → Integrations → SMS alerts (Twilio)`.

Set a calendar reminder to re-test the valve every ~3 months (Test fire
valve button) — servo retrofits sometimes seize from disuse.
