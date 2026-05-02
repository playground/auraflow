#!/usr/bin/env bash
# Stage the latest firmware binaries into web/bin/ so the GitHub-Pages-
# hosted flasher serves them.
#
#   npm run publish:flasher
#
# Workflow:
#   1. `npm run build:firmware` (must already have run; bins must exist)
#   2. This script copies the four ESP-IDF outputs into web/bin/ with
#      the names manifest.json expects, then rewrites the manifest's
#      `version` field to match FIRMWARE_VERSION in main.c.
#   3. The script does NOT git-commit — eyeball the diff, then
#      `git add web/bin web/manifest.json && git commit -m "..." && git push`.
#      Pages picks it up on the next deploy (~30 s).
#
# The web/bin/*.bin files ARE tracked (they're the published artifact);
# .gitignore was updated to allow them.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/src/firmware/c/build"
DEST="$ROOT/web/bin"
MAIN_C="$ROOT/src/firmware/c/main/main.c"
MANIFEST="$ROOT/web/manifest.json"

# (build artifact name → flasher name) per manifest.json offsets.
declare -a SRCS=(
    "bootloader/bootloader.bin"
    "partition_table/partition-table.bin"
    "ota_data_initial.bin"
    "auraflow.bin"
)
declare -a DESTS=(
    "bootloader.bin"
    "partitions.bin"
    "ota_data_initial.bin"
    "firmware.bin"
)

if [ ! -d "$BUILD" ]; then
    echo "publish-flasher: build directory missing — run 'npm run build:firmware' first." >&2
    exit 1
fi

mkdir -p "$DEST"

for i in "${!SRCS[@]}"; do
    SRC="$BUILD/${SRCS[$i]}"
    OUT="$DEST/${DESTS[$i]}"
    if [ ! -f "$SRC" ]; then
        echo "publish-flasher: missing $SRC" >&2
        echo "  did 'npm run build:firmware' complete cleanly?" >&2
        exit 1
    fi
    cp "$SRC" "$OUT"
    SIZE=$(wc -c < "$OUT" | tr -d ' ')
    echo "  copied  ${DESTS[$i]}  (${SIZE} bytes)"
done

# Sync manifest.version with FIRMWARE_VERSION in main.c so end-users on
# stale tabs see a freshly-published version number.
VERSION=$(awk -F'"' '/#define FIRMWARE_VERSION/ {print $2; exit}' "$MAIN_C")
if [ -z "$VERSION" ]; then
    echo "publish-flasher: could not parse FIRMWARE_VERSION from $MAIN_C" >&2
    exit 1
fi

# In-place version replacement using a tiny python rewrite (avoids
# sed -i portability issues across BSD/GNU).
python3 - "$MANIFEST" "$VERSION" <<'PY'
import json, sys
path, version = sys.argv[1], sys.argv[2]
with open(path) as f:
    m = json.load(f)
m["version"] = version
with open(path, "w") as f:
    json.dump(m, f, indent=2)
    f.write("\n")
PY

echo "  manifest version → $VERSION"
echo
echo "Done. Review the diff, then:"
echo "  git add web/bin web/manifest.json"
echo "  git commit -m 'release: flasher v$VERSION'"
echo "  git push"
