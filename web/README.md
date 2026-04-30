# AuraFlow Web Flasher

Browser-based ESP32 flasher built on [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
+ WebSerial. End users can flash an AuraFlow ESP32 from Chrome/Edge with no
toolchain install — they just need a USB cable and the URL to this page.

## What this does (and doesn't)

| Step | Status |
|---|---|
| Flash bootloader + partitions + firmware to a connected ESP32 | ✓ |
| Show flashing progress and erase-then-flash UX | ✓ (handled by ESP Web Tools) |
| Provision Wi-Fi credentials, HomeHub URL, sensorId | ✗ — still needs Moddable serial REPL (see [`docs/bring-up.md` § 3](../docs/bring-up.md)) |
| Build the `.bin` files | ✗ — you must build with `mcconfig` first |

Provisioning-over-WebSerial is a follow-up — would require firmware-side
support for accepting JSON over UART0 outside the Moddable debugger.

## Getting the binaries

The web flasher needs three `.bin` files in `web/bin/`:

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`

They're not committed (gitignored — they're per-machine build output).
After running a Moddable build you have to copy them in:

```bash
# From repo root, after a successful `npm run build:firmware:release`
# Adjust the source path to match your Moddable SDK layout — typical:
SRC=$MODDABLE/build/bin/esp32/release/auraflow

cp "$SRC/bootloader.bin" web/bin/bootloader.bin
cp "$SRC/partitions.bin" web/bin/partitions.bin
cp "$SRC/xs_esp32.bin"   web/bin/firmware.bin    # name varies; see Moddable build output
```

If the source filenames don't match exactly (Moddable's release naming
shifts between SDK versions), look in the build output directory and pick
the three corresponding files. Verify the offsets in `manifest.json`
match your bootloader's table — defaults (4096 / 32768 / 65536) are
standard ESP32 offsets.

## Running locally for development

WebSerial works over `localhost` even on plain HTTP, so no TLS setup
needed for local testing:

```bash
cd web
python3 -m http.server 8080
# or: npx http-server -p 8080
```

Open <http://localhost:8080> in Chrome or Edge. Click "Connect & Flash".

## Hosting publicly (GitHub Pages)

WebSerial requires HTTPS for non-localhost contexts. GitHub Pages gives
you HTTPS for free.

1. Repo → Settings → Pages → Source: `Deploy from a branch`,
   Branch: `main`, Folder: `/web`.
2. Wait ~30 s for the action; site goes live at
   `https://<user>.github.io/<repo>/`.
3. **Commit the `.bin` files for this purpose.** The `.gitignore`
   excludes them by default to keep dev clean — for a publish branch,
   either add an exception or use a separate branch:

   ```bash
   git checkout -b gh-pages
   # remove the gitignore line for web/bin/*.bin on this branch
   git add web/bin/*.bin
   git commit -m "Publish firmware binaries"
   git push -u origin gh-pages
   # then change the Pages source to the gh-pages branch / root
   ```

Alternative: use GitHub Releases to host the binaries and update the
`manifest.json` paths to absolute URLs pointing at the release assets.
That keeps the binaries out of git history entirely.

## Sharing with someone else

Once the binaries are published:

1. They open your URL in Chrome/Edge
2. Plug in an ESP32
3. Click "Connect & Flash" → pick serial port → done in ~30 s
4. Reach for `docs/bring-up.md` § 3 for provisioning

This is the workflow that makes the web flasher worth the setup — anyone
with a USB cable can flash without installing anything.

## Updating the firmware

When you publish a new build:

1. Run `npm run build:firmware:release`
2. Copy the new `.bin` files into `web/bin/`
3. Bump `version` in `manifest.json` (so users on stale tabs see the new
   number)
4. Push to your hosting branch
5. Existing users hit the page again, flash, get the new firmware

Note: Phase 7 (OTA) is the better story for fielded devices — once OTA
ships, sensors that are already provisioned and online will update
themselves without anyone touching them. The web flasher is the
"first-time flash" tool, OTA is the "ongoing update" tool.
