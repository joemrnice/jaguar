#!/usr/bin/env bash
# Removes the jag binary installed by install.sh. Defaults to /usr/local, override with PREFIX=...
set -e

PREFIX="${PREFIX:-/usr/local}"
BIN_PATH="$PREFIX/bin/jag"

if [ -f "$BIN_PATH" ]; then
    rm -f "$BIN_PATH"
    echo "Removed $BIN_PATH"
else
    echo "Nothing to remove at $BIN_PATH"
fi
