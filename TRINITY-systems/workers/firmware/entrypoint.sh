#!/bin/sh
set -e

echo "[sandbox] Firmware compilation sandbox starting"
echo "[sandbox] Network: DISABLED"
echo "[sandbox] User: $(whoami)"
echo "[sandbox] Working directory: $(pwd)"

if [ $# -eq 0 ]; then
    echo "[sandbox] No command specified. Exiting."
    exit 0
fi

exec "$@"
