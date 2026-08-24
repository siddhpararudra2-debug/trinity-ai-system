#!/usr/bin/env bash
set -euo pipefail

SOURCE="${1:?usage: restore_sqlite.sh BACKUP.db [TARGET.db] --confirm}"
TARGET="${2:-data/trinity.db}"
CONFIRM="${3:-}"
if [[ "$CONFIRM" != "--confirm" ]]; then
  echo "Refusing to overwrite $TARGET without --confirm" >&2
  exit 2
fi
if [[ ! -f "$SOURCE" ]]; then
  echo "Backup not found: $SOURCE" >&2
  exit 1
fi
mkdir -p "$(dirname "$TARGET")"
TMP="${TARGET}.restore.$$"
python3 - "$SOURCE" "$TMP" <<'PY'
import sqlite3
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
with sqlite3.connect(source) as src, sqlite3.connect(destination) as dst:
    src.backup(dst)
PY
mv -f "$TMP" "$TARGET"
echo "Restored $TARGET"
