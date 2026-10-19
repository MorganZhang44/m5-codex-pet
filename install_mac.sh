#!/bin/sh
# Install only for the current logged-in user. No sudo or firmware changes.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$SCRIPT_DIR/macos/install.py" install "$@"
