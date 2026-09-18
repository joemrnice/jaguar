#!/usr/bin/env bash
# Cross-platform entry point: detects the OS and delegates to the
# platform-specific installer in installers/. Run this directly, or
# run the platform script yourself if you prefer:
#   installers/install-linux.sh   (Linux, epoll backend)
#   installers/install-macos.sh   (macOS, kqueue backend)
#   docs/WINDOWS.md               (Windows - requires WSL; see why in that file)
set -e
cd "$(dirname "$0")"

case "$(uname -s)" in
    Linux)
        exec ./installers/install-linux.sh "$@"
        ;;
    Darwin)
        exec ./installers/install-macos.sh "$@"
        ;;
    *)
        echo "install.sh: unrecognized platform '$(uname -s)'." >&2
        echo "If this is native Windows (not WSL), see docs/WINDOWS.md - it isn't supported" >&2
        echo "natively, but works fully under WSL (which reports as Linux, so if you're" >&2
        echo "seeing this from inside WSL, something else is wrong - please check that" >&2
        echo "'uname -s' really does print something other than Linux for you)." >&2
        exit 1
        ;;
esac
