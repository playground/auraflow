#!/usr/bin/env python3
"""
Send a single PROVISION:{...} line to the ESP32 over UART0.

Usage:
    python3 scripts/send-provision.py <file-containing-the-line>

The file should be one line starting with `PROVISION:`. The script opens
the configured serial port at 115200, writes the line + newline, and
prints whatever the device sends back for ~5 seconds (long enough to
catch the OK / ERR response and the start of the post-restart boot log).

IMPORTANT: stop `idf.py monitor` (Ctrl+]) before running this — only
one process can hold the serial port at a time.

After this finishes, run `npm run monitor:firmware` again to watch the
device come back up in normal (provisioned) mode.
"""

import argparse
import sys
import time

try:
    import serial  # pyserial; bundled with ESP-IDF's Python venv
except ImportError:
    print("ERROR: pyserial not installed. Run from a shell with ESP-IDF env active "
          "(e.g. via `npm run provision` or after `. $IDF_PATH/export.sh`).",
          file=sys.stderr)
    sys.exit(2)


PORT_DEFAULT = "/dev/cu.usbserial-0001"
BAUD_DEFAULT = 115200


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("file", help="File containing one PROVISION:{...} line")
    p.add_argument("--port", default=PORT_DEFAULT,
                   help=f"Serial port (default {PORT_DEFAULT})")
    p.add_argument("--baud", type=int, default=BAUD_DEFAULT,
                   help=f"Baud rate (default {BAUD_DEFAULT})")
    p.add_argument("--wait", type=float, default=5.0,
                   help="Seconds to read responses after sending (default 5)")
    args = p.parse_args()

    with open(args.file, "r") as f:
        line = f.read().strip()

    if not line.startswith("PROVISION:"):
        print(f"ERROR: file content doesn't start with 'PROVISION:' "
              f"(got: {line[:40]!r})", file=sys.stderr)
        return 1

    print(f"→ {args.port} @ {args.baud} ({len(line)} chars)")
    print(f"  preview: {line[:60]}{'...' if len(line) > 60 else ''}")

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"ERROR: couldn't open {args.port}: {e}\n"
              f"  Is `idf.py monitor` still running? Quit it (Ctrl+]) and try again.",
              file=sys.stderr)
        return 3

    with port:
        port.write(line.encode("utf-8") + b"\n")
        port.flush()

        deadline = time.time() + args.wait
        print("\n--- device output ---")
        while time.time() < deadline:
            chunk = port.read(256)
            if chunk:
                sys.stdout.write(chunk.decode(errors="replace"))
                sys.stdout.flush()
        print("\n--- (timeout) ---")

    print("\nDone. Run `npm run monitor:firmware` to keep watching.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
