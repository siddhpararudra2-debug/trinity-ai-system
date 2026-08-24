#!/usr/bin/env bash
set -euo pipefail

DB_PATH="${1:-data/trinity.db}"
DEST="${2:-backups/trinity-$(date -u +%Y%m%dT%H%M%SZ).db}"
mkdir -p "$(dirname "$DEST")"

python3 - "$DB_PATH" "$DEST" <<'PY'
import sqlite3
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
if not source.is_file():
    raise SystemExit(f"database not found: {source}")
destination.parent.mkdir(parents=True, exist_ok=True)
with sqlite3.connect(source) as src, sqlite3.connect(destination) as dst:
    src.backup(dst)
print(destination)
PY
