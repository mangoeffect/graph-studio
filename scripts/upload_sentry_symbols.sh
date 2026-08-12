#!/usr/bin/env bash
# Thin shim: forwards to the cross-platform upload_sentry_symbols.py (deprecated, will be removed next release).
PY="$(command -v python3 || command -v python)"
exec "$PY" "$(dirname "$0")/upload_sentry_symbols.py" "$@"
