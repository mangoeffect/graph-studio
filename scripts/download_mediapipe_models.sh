#!/usr/bin/env bash
# Thin shim: forwards to the cross-platform download_mediapipe_models.py (deprecated, will be removed next release).
PY="$(command -v python3 || command -v python)"
exec "$PY" "$(dirname "$0")/download_mediapipe_models.py" "$@"
