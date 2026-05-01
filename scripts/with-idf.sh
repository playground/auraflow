#!/usr/bin/env bash
# Run a command with the ESP-IDF environment active.
# Sources $IDF_PATH/export.sh (silently), then execs the rest of the args.
#
# Used by package.json so `npm run build:firmware`, `flash:firmware`, etc.
# work in a fresh shell that hasn't sourced export.sh.
#
# Requires IDF_PATH to be exported in your shell rc. Typical .zshrc:
#   export IDF_PATH=$HOME/sandbox/esp32/esp-idf

set -e

if [ -z "$IDF_PATH" ]; then
    echo "with-idf.sh: IDF_PATH is not set." >&2
    echo "  Add 'export IDF_PATH=/path/to/esp-idf' to your ~/.zshrc and reload." >&2
    exit 1
fi

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "with-idf.sh: $IDF_PATH/export.sh not found." >&2
    exit 1
fi

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" > /dev/null

exec "$@"
