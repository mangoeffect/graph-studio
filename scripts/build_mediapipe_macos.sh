#!/usr/bin/env bash
# Thin shim: forwards to the cross-platform build_mediapipe.py (deprecated, will be removed next release).
# Kept under the old name (build_mediapipe_macos.sh) for backward compatibility.
PY="$(command -v python3 || command -v python)"
exec "$PY" "$(dirname "$0")/build_mediapipe.py" "$@"
