#!/usr/bin/env bash
# Thin shim: forwards to the cross-platform package_macos.py.
PY="$(command -v python3 || command -v python)"
exec "$PY" "$(dirname "$0")/package_macos.py" "$@"
