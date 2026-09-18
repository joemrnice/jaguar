#!/usr/bin/env bash
# Linux installer. Builds against the epoll reactor backend and installs
# the jag binary. Defaults to /usr/local, override with PREFIX=...
#   PREFIX=$HOME/.local ./installers/install-linux.sh
set -e
cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Linux" ]; then
    echo "installers/install-linux.sh: this doesn't look like Linux (uname -s says '$(uname -s)')." >&2
    echo "Use installers/install-macos.sh on macOS, or see docs/WINDOWS.md on Windows." >&2
    exit 1
fi

if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
    echo "No C compiler found. Install one first, e.g.:" >&2
    echo "  Debian/Ubuntu: sudo apt install build-essential" >&2
    echo "  Fedora:        sudo dnf groupinstall 'Development Tools'" >&2
    echo "  Arch:          sudo pacman -S base-devel" >&2
    exit 1
fi

PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="$PREFIX/bin"

echo "Building jag (Linux, epoll backend)..."
make clean >/dev/null 2>&1 || true
make

mkdir -p "$BIN_DIR"
cp ./jag "$BIN_DIR/jag"
chmod +x "$BIN_DIR/jag"

echo "Installed jag to $BIN_DIR/jag"
case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) echo "Note: $BIN_DIR is not on your PATH. Add it, e.g.:"
       echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc && source ~/.bashrc" ;;
esac
