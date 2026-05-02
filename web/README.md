# AuraFlow Web Flasher

Browser-based ESP32 flasher built on [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
+ WebSerial. End users can flash an AuraFlow ESP32 from Chrome/Edge with no
toolchain install — they just need a USB cable and the URL to this page.

## What this does (and doesn't)

| Step | Status |
|---|---|
| Flash bootloader + partitions + ota_data + firmware to a connected ESP32 | ✓ |
| Show flashing progress and erase-then-flash UX | ✓ (handled by ESP Web Tools) |
| Provision Wi-Fi credentials, HomeHub URL, sensorId, static IP | ✓ — WebSerial form on the page |
| Build the `.bin` files | ✗ — `npm run build:firmware` produces them |

The provisioning form sends a single line over UART0 of the form
`PROVISION:{json}\n`. The firmware's provisioning listener consumes it
when NVS is empty, validates, writes, and reboots. Heartbeat
`READY:auraflow-provision-v1` lets the page detect provisioning mode.

## Layout

The flasher is four files: `index.html`, `manifest.json`, this README,
and `bin/` containing the four ESP-IDF outputs the manifest references.
`manifest.json` pins offsets to the OTA-capable partition table:

| File | Offset |
|---|---|
| `bin/bootloader.bin`        | 0x1000  |
| `bin/partitions.bin`        | 0x8000  |
| `bin/ota_data_initial.bin`  | 0xf000  |
| `bin/firmware.bin`          | 0x20000 |

## Publishing a new build

```bash
npm run build:firmware       # produces src/firmware/c/build/*.bin
npm run publish:flasher      # copies + renames into web/bin/, syncs manifest version
git add web/bin web/manifest.json
git commit -m "release: flasher v<X.Y.Z>"
git push                     # GitHub Pages picks it up in ~30 s
```

`publish:flasher` reads `FIRMWARE_VERSION` from `main.c` and writes it
into `manifest.json` so users on stale tabs see the new version number.

## Running locally

WebSerial works over `localhost` even on plain HTTP, so no TLS setup
needed:

```bash
cd web
python3 -m http.server 8080
```

Open <http://localhost:8080> in Chrome or Edge. Click the install button.

## Hosting on GitHub Pages

Pages requires HTTPS for non-localhost WebSerial — Pages provides it.

One-time setup (UI):

1. Repo → **Settings → Pages**
2. **Source**: `Deploy from a branch`
3. **Branch**: `main`, **Folder**: `/web`
4. Save. Site goes live at `https://<owner>.github.io/<repo>/` after
   ~30 s.

Private repo + GitHub Pro (or higher) is supported — sources stay
private, the published site is public.

## Sharing

Once Pages is live and the `.bin` files are committed under `web/bin/`:

1. End user opens the URL in Chrome/Edge
2. Plugs in an ESP32 over USB
3. Click "Connect & Flash" → pick the serial port → ~30 s later, done
4. Use the in-page form to set Wi-Fi creds + HomeHub URL → device
   reboots and joins

## Web flasher vs OTA

| Tool | When |
|---|---|
| Web flasher | First-time flash on a fresh / bricked / re-purposed ESP32 |
| OTA (`POST /ota`, `npm run ota:firmware`) | Subsequent updates to a running, provisioned device |

Once a device is provisioned and on Wi-Fi, OTA is the better story —
no USB cable required.
