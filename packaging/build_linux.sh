#!/usr/bin/env bash
# M16 — Linux portable bundle builder.
#
# Product decision recorded here: the Linux bundle is api/ + web/ +
# trinity.sh. The native Trinity.exe daemon is Windows-only (WinHTTP,
# Qt6, MSVC toolchain), so this bundle deliberately ships WITHOUT it —
# and the registry honestly keeps those six engines on their scaffold
# refusals. The Python engines (math, cad with the OpenCascade kernel
# per M11, research router, agents) are fully live.
#
# Reuses the same spec as the Windows bundle (M11's kernel-inclusive
# spec, now cross-platform) and the same smoke contract
# (`smoke_bundle.py --platform linux`). The kernel-collection gate
# below is the Linux cousin of release.yml's OCP assert: if OCP does
# not reach _internal/, the build fails loudly instead of shipping a
# bundle whose catalog silently loses the STEP kernel.
#
# Run on the packaging machine / CI runner:
#   packaging/build_linux.sh [version]     # version defaults to 1.0.0
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
VER="${1:-1.0.0}"
OUT="$ROOT/packaging/dist/TrinityAI-$VER-linux64"

echo "== [1/5] build env (mirrors release.yml: requirements + [cad] + pinned PyInstaller) =="
python -m pip install --upgrade pip
python -m pip install -r "$ROOT/requirements.txt"
python -m pip install ".[cad]"
python -m pip install "pyinstaller==6.22.3"

# M11 lesson, Linux side: the kernel must be importable in the build env
# BEFORE freezing, and the collected bundle must actually contain its
# shared objects — gates both directions, no silent kernel loss.
python - <<'PY'
import OCP.STEPControl  # identity check: build env can import the kernel

print("OCP build-env identity check ok")
PY

echo "== [2/5] api: PyInstaller onedir (shared spec) =="
cd "$ROOT/packaging"
rm -rf build/trinity-api dist/trinity-api
python -m PyInstaller --clean -y trinity-api.spec
KERNEL_OBJECTS=$(find dist/trinity-api/_internal -name 'TK[^.]*.so' -o -name 'OCP*.so' | wc -l)
if [ "$KERNEL_OBJECTS" -lt 5 ]; then
    echo "FAIL: kernel collection gate: $KERNEL_OBJECTS kernel .so objects in _internal (expected > 5)" >&2
    exit 1
fi
echo "kernel collection gate ok ($KERNEL_OBJECTS kernel .so objects)"

echo "== [3/5] web: static export pointed at the same-origin /api seam =="
cd "$ROOT/frontend"
npm ci
NEXT_PUBLIC_TRINITY_API_URL=/api npm run build
if [ ! -f out/index.html ]; then
    echo "FAIL: frontend/out/index.html missing - static export failed" >&2
    exit 1
fi

echo "== [4/5] assemble $OUT =="
cd "$ROOT"
rm -rf "$OUT"
mkdir -p "$OUT"
mv packaging/dist/trinity-api "$OUT/api"
cp -r frontend/out "$OUT/web"
cp packaging/trinity.sh "$OUT/trinity.sh"
chmod +x "$OUT/api/trinity-api" "$OUT/trinity.sh"
printf 'bundler: api/ (Python engines + OpenCascade kernel) + web/ (static UI)\n' \
    > "$OUT/LAYOUT.txt"
printf 'native engines: unavailable by design on this platform bundle -- the\n' \
    'registry keeps them on honest scaffold refusals (capability_unavailable).\n' \
    'auto-update: Windows-only by the M13 manifest scope; update this\n' \
    'bundle by re-downloading. Nothing pretends otherwise.\n' \
    >> "$OUT/LAYOUT.txt"

echo "== [5/5] zip =="
cd "$(dirname "$OUT")"
rm -f "TrinityAI-$VER-linux64.zip"
# `zip` is not preinstalled on the runner; the stdlib zipfile module is.
python -m zipfile -c "TrinityAI-$VER-linux64.zip" "$(basename "$OUT")"
echo "BUILD OK: $OUT"
