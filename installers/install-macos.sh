#!/usr/bin/env bash
# macOS installer. Builds against the kqueue reactor backend and installs
# the jag binary. Defaults to /usr/local, override with PREFIX=...
#   PREFIX=$HOME/.local ./installers/install-macos.sh
#
# IMPORTANT: the kqueue backend (src/runtime/reactor_kqueue.c) was
# written to the documented kqueue(2) API and mirrors the working Linux
# (epoll) backend, but this project was developed and tested entirely on
# Linux - there was no macOS machine available to verify it against. It
# should work; if `make test` below turns up problems, please report
# them (or check DESIGN_DECISIONS.md for the reasoning to debug it
# yourself). The core language and everything not-networking-specific is
# identical to the well-tested Linux build.
set -e
cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
    echo "installers/install-macos.sh: this doesn't look like macOS (uname -s says '$(uname -s)')." >&2
    echo "Use installers/install-linux.sh on Linux, or see docs/WINDOWS.md on Windows." >&2
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    echo "No C compiler toolchain found. Install the Xcode Command Line Tools first:" >&2
    echo "  xcode-select --install" >&2
    exit 1
fi

PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="$PREFIX/bin"

echo "Building jag (macOS, kqueue backend - see the note at the top of this script)..."
make clean >/dev/null 2>&1 || true
make

mkdir -p "$BIN_DIR" 2>/dev/null
if ! cp ./jag "$BIN_DIR/jag" 2>/tmp/jag_install_err; then
    cat /tmp/jag_install_err >&2
    echo "" >&2
    echo "Could not write to $BIN_DIR. If you piped this through curl with" >&2
    echo "sudo (\`sudo curl ... | bash\`), that only elevates curl, not the" >&2
    echo "install itself - use one of these instead:" >&2
    echo "" >&2
    echo "  PREFIX=\$HOME/.local ./installers/install-macos.sh   # no sudo needed" >&2
    echo "  sudo ./installers/install-macos.sh                    # sudo the install directly" >&2
    exit 1
fi
chmod +x "$BIN_DIR/jag"

echo "Installed jag to $BIN_DIR/jag"
case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) echo "Note: $BIN_DIR is not on your PATH. Add it, e.g.:"
       echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.zshrc && source ~/.zshrc"
       echo "  (or ~/.bash_profile if you're using bash)" ;;
esac
