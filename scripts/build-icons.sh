#!/usr/bin/env bash
# Derive PNG icon variants from web/favicon.svg.
#
#   npm run build:icons
#
# Generates:
#   web/favicon-32.png       — small favicon (legacy browsers)
#   web/favicon-180.png      — apple-touch-icon (iOS home screen)
#   web/icon-192.png         — PWA / web manifest "any" 192
#   web/icon-512.png         — PWA / web manifest "any" 512
#
# Renderer preference, picks whichever is available:
#   1. rsvg-convert  (recommended; brew install librsvg)
#   2. inkscape      (brew install inkscape)
#   3. qlmanage + sips  (macOS-only fallback, no install required)
#
# Output is committed alongside the SVG so the flasher works for visitors
# who hit it before this script has been run on their machine.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SVG="$ROOT/web/favicon.svg"
OUT="$ROOT/web"

if [ ! -f "$SVG" ]; then
    echo "build-icons: $SVG missing" >&2
    exit 1
fi

if command -v rsvg-convert >/dev/null 2>&1; then
    render() { rsvg-convert -w "$1" -h "$1" "$SVG" -o "$2"; }
elif command -v inkscape >/dev/null 2>&1; then
    render() { inkscape "$SVG" --export-type=png --export-filename="$2" -w "$1" -h "$1" >/dev/null; }
elif command -v qlmanage >/dev/null 2>&1 && command -v sips >/dev/null 2>&1; then
    # qlmanage emits a single thumbnail at the size you ask for, named
    # "<input>.png" in the output dir. sips then resizes that to the
    # exact requested size with high-quality interpolation.
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    qlmanage -t -s 512 -o "$TMP" "$SVG" >/dev/null 2>&1
    BASE_PNG="$TMP/$(basename "$SVG").png"
    if [ ! -f "$BASE_PNG" ]; then
        echo "build-icons: qlmanage failed to produce $BASE_PNG" >&2
        exit 1
    fi
    render() { sips -z "$1" "$1" "$BASE_PNG" --out "$2" >/dev/null; }
else
    echo "build-icons: no SVG renderer available." >&2
    echo "  brew install librsvg     # recommended" >&2
    echo "  # or: brew install inkscape" >&2
    echo "  # macOS users: qlmanage + sips are built-in but neither was found" >&2
    exit 1
fi

declare -a SIZES=(32 180 192 512)
declare -a NAMES=("favicon-32.png" "favicon-180.png" "icon-192.png" "icon-512.png")

for i in "${!SIZES[@]}"; do
    OUT_PATH="$OUT/${NAMES[$i]}"
    render "${SIZES[$i]}" "$OUT_PATH"
    SIZE_BYTES=$(wc -c < "$OUT_PATH" | tr -d ' ')
    echo "  rendered  ${NAMES[$i]}  (${SIZES[$i]}×${SIZES[$i]}, ${SIZE_BYTES} bytes)"
done

echo
echo "Done. Commit the new web/*.png files alongside favicon.svg."
