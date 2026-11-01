#!/usr/bin/env bash
# M16 Linux launcher for the portable bundle.
#
# Layout decisions recorded (the deferred-item "real layout decision"):
#   - User data lives in XDG_DATA_HOME (default ~/.local/share/TrinityAI)
#     so the bundle directory itself stays read-only friendly. Override
#     with TRINITY_STORAGE_ROOT.
#   - There is no swap_pending step here: auto-update stays
#     Windows-only by M13's manifest scope (latest.yml references the
#     win64 asset); this bundle updates by re-downloading release
#     assets. Nothing pretends otherwise.
#   - TRINITY_NATIVE_EXEC stays unset: native-backed engines must keep
#     refusing honestly (the smoke proves that refusal in the bundle).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export TRINITY_API_PORT="${TRINITY_API_PORT:-8000}"
export TRINITY_UI_DIR="$HERE/web"
export TRINITY_APP_VERSION="${TRINITY_APP_VERSION:-1.0.0}"
if [ -z "${TRINITY_STORAGE_ROOT:-}" ]; then
    export TRINITY_STORAGE_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/TrinityAI/data"
fi
mkdir -p "$TRINITY_STORAGE_ROOT"

echo "Starting Trinity AI (api port $TRINITY_API_PORT, data: $TRINITY_STORAGE_ROOT)..."
(cd "$HERE/api" && exec ./trinity-api) &
sleep 4
echo "Trinity AI is up: http://127.0.0.1:$TRINITY_API_PORT/"
wait
