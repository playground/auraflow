#!/usr/bin/env python3
"""
Trigger an OTA on a device on the LAN.

  scripts/ota.py <device-ip> [firmware.bin]

Defaults the binary to src/firmware/c/build/auraflow.bin (matches what
`npm run build:firmware` produces).

How it works:
  1. Starts an HTTP server in this process on a free port, rooted at the
     directory containing the .bin.
  2. Detects this host's LAN IP (the one that routes to the device).
  3. POSTs {"url": "http://<host-lan-ip>:<port>/<bin>"} to the device's
     /ota endpoint.
  4. Keeps the HTTP server up until the device finishes downloading and
     reboots — you'll see the request hit in the script's log, then the
     device drops off and comes back.

Requires the device to be running firmware that exposes POST /ota
(commit 8ad346c onward).
"""
from __future__ import annotations

import http.server
import json
import os
import socket
import socketserver
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_BIN = Path(__file__).resolve().parent.parent / "src/firmware/c/build/auraflow.bin"


def lan_ip_for(target: str) -> str:
    """Best-effort: which interface IP would route to <target>."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((target, 1))
        return s.getsockname()[0]
    finally:
        s.close()


def serve_directory(directory: Path) -> tuple[socketserver.TCPServer, int]:
    handler_cls = http.server.SimpleHTTPRequestHandler
    # Bind to all interfaces, port 0 = OS-assigned free port.
    os.chdir(directory)
    httpd = socketserver.TCPServer(("0.0.0.0", 0), handler_cls)
    port = httpd.server_address[1]
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, port


def trigger_ota(device_ip: str, url: str) -> None:
    body = json.dumps({"url": url}).encode("utf-8")
    req = urllib.request.Request(
        f"http://{device_ip}/ota",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            print(f"device replied {resp.status}: {resp.read().decode()}")
    except urllib.error.HTTPError as e:
        print(f"device replied {e.code}: {e.read().decode()}", file=sys.stderr)
        raise


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 1

    device_ip = sys.argv[1]
    bin_path = Path(sys.argv[2]).resolve() if len(sys.argv) >= 3 else DEFAULT_BIN
    if not bin_path.is_file():
        print(f"firmware binary not found: {bin_path}", file=sys.stderr)
        print("  hint: run `npm run build:firmware` first", file=sys.stderr)
        return 2

    host_ip = lan_ip_for(device_ip)
    httpd, port = serve_directory(bin_path.parent)
    url = f"http://{host_ip}:{port}/{bin_path.name}"

    print(f"serving  {bin_path}")
    print(f"hosting  {url}")
    print(f"device   {device_ip}")
    print("posting /ota …")
    trigger_ota(device_ip, url)

    print("OTA in flight. Keep this script running until the download finishes.")
    print("Watch the device's serial monitor or its / page for progress.")
    print("Press Ctrl-C to stop the local HTTP server.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nshutting down")
        httpd.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
