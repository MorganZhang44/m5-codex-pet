#!/bin/sh
# Remove only this package's owned runtime, CLI, identity and LaunchAgent.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$SCRIPT_DIR/macos/install.py" uninstall "$@"
