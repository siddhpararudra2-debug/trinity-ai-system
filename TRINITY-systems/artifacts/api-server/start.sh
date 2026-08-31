#!/bin/bash
# Trinity AI Backend - Python FastAPI startup script
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=== Trinity AI Backend ==="
echo "Starting FastAPI server on port ${PORT:-8080}..."
exec python3 -m uvicorn app.main:app \
  --host 0.0.0.0 \
  --port "${PORT:-8080}" \
  --reload \
  --log-level info
