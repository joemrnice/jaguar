#!/usr/bin/env bash
# Jaguar remote installer - downloads the source from GitHub, builds it,
# and installs the `jag` binary. Meant to be run as a one-liner:
#
#   curl -fsSL https://raw.githubusercontent.com/<ORG>/<REPO>/main/get-jaguar.sh | bash
#
# Safe to re-run (rebuilds and reinstalls). Override where it installs to
# and what it fetches with environment variables:
#
#   PREFIX=$HOME/.local curl -fsSL .../get-jaguar.sh | bash   # no sudo needed
#   JAGUAR_REF=v0.2.0    curl -fsSL .../get-jaguar.sh | bash   # pin a release tag
#
# This script only ever builds from source on your machine - it does not
# download or execute a prebuilt binary. Read it before piping it into a
# shell, the way you should for any install-by-curl script.
set -euo pipefail

JAGUAR_REPO="${JAGUAR_REPO:-YOUR_GITHUB_USERNAME/jaguar}"
JAGUAR_REF="${JAGUAR_REF:-main}"
PREFIX="${PREFIX:-/usr/local}"

info()  { printf '\033[1;36m==>\033[0m %s\n' "$1"; }
fail()  { printf '\033[1;31merror:\033[0m %s\n' "$1" >&2; exit 1; }

# --- 1. platform check -------------------------------------------------
OS="$(uname -s)"
case "$OS" in
    Linux)  INSTALLER="installers/install-linux.sh" ;;
    Darwin) INSTALLER="installers/install-macos.sh" ;;
    *)
        fail "unsupported platform '$OS'. Native Windows isn't supported - install inside WSL instead (which reports as Linux to this script). See docs/WINDOWS.md in the repository for why and how."
        ;;
esac

if [ "$JAGUAR_REPO" = "joemrnice/jaguar" ]; then
    fail "get-jaguar.sh still has its placeholder repo path. If you're the maintainer, edit JAGUAR_REPO at the top of this file before publishing it; if you're a user seeing this, the maintainer hasn't finished hosting setup yet - see docs/PUBLISHING.md."
fi

if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    fail "no C compiler found. Install one first (build-essential / Xcode Command Line Tools / equivalent) and re-run."
fi

if ! command -v make >/dev/null 2>&1; then
    fail "'make' not found. Install it first and re-run."
fi

# --- 2. fetch source -----------------------------------------------------
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

if command -v git >/dev/null 2>&1; then
    info "Cloning $JAGUAR_REPO@$JAGUAR_REF..."
    git clone --depth 1 --branch "$JAGUAR_REF" \
        "https://github.com/$JAGUAR_REPO.git" "$WORKDIR/src" \
        2>/dev/null || fail "could not clone https://github.com/$JAGUAR_REPO (check JAGUAR_REPO/JAGUAR_REF are correct and the repo/ref is public)"
elif command -v curl >/dev/null 2>&1; then
    info "git not found - downloading a source archive of $JAGUAR_REPO@$JAGUAR_REF instead..."
    command -v tar >/dev/null 2>&1 || fail "'tar' is required to unpack the downloaded archive"
    curl -fsSL "https://github.com/$JAGUAR_REPO/archive/refs/heads/$JAGUAR_REF.tar.gz" \
        -o "$WORKDIR/src.tar.gz" \
        || fail "could not download https://github.com/$JAGUAR_REPO/archive/refs/heads/$JAGUAR_REF.tar.gz (if $JAGUAR_REF is a tag, not a branch, set JAGUAR_REF accordingly - tags use the same URL shape)"
    mkdir -p "$WORKDIR/src"
    tar -xzf "$WORKDIR/src.tar.gz" -C "$WORKDIR/src" --strip-components=1
else
    fail "neither 'git' nor 'curl' is available - install one and re-run."
fi

# --- 3. build + install --------------------------------------------------
cd "$WORKDIR/src"
[ -f "$INSTALLER" ] || fail "expected $INSTALLER in the downloaded source but didn't find it - is JAGUAR_REPO pointed at the right repository?"

info "Building and installing (PREFIX=$PREFIX)..."
chmod +x "$INSTALLER"
PREFIX="$PREFIX" "./$INSTALLER"

info "Done. Verifying..."
if command -v jag >/dev/null 2>&1; then
    jag --version
else
    "$PREFIX/bin/jag" --version 2>/dev/null || true
    echo ""
    info "jag was installed to $PREFIX/bin/jag, but that directory isn't on your PATH yet."
    echo "  Add it, e.g.:  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.bashrc && source ~/.bashrc"
fi
