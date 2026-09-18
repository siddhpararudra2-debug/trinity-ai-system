# Trinity installer scaffolding

Status: **working packaging pipeline, honest gaps.** Nothing here claims a
feature that does not exist yet.

## What is produced today

CPack (configured at the end of `app/native/CMakeLists.txt`) emits two artifacts
from one build:

| Artifact | Generator | Contents |
| --- | --- | --- |
| `Trinity-<version>-win64.zip` | `ZIP` | portable layout, unzip and run |
| `Trinity-<version>-win64.exe` | `NSIS` | Setup wizard with Start‑menu entry and uninstaller |

Installed layout:

```
<install>/
  bin/trinity-shell.exe              native console front-end over trinity_core
  bin/python/services/engine_host.py IPC bridge to the existing Python engines
```

`bin/python/services/engine_host.py` is where the storage layer looks for the
engine host (`StorageLayout::engine_host_script`), so moving the layout keeps the
Python engines reachable without any hardcoded path.

## Building the artifacts locally

```bash
cmake -S app/native -B build/installer -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_BUILD_TYPE=Release -DTRINITY_WITH_QT=OFF -DTRINITY_BUILD_TESTS=OFF
cmake --build build/installer --config Release --parallel
cd build/installer
cpack -C Release -G ZIP
cpack -C Release -G NSIS      # requires NSIS on PATH (choco install nsis)
```

`scripts/package_windows.ps1` wraps exactly these steps.

CI runs the same commands in the `native-installer` job of
`.github/workflows/native.yml` and uploads both artifacts.

## Gaps (tracked, not hidden)

1. **No bundled Python runtime yet.** The installer ships the engine-host script
   but relies on a system Python. The release build must carry an embedded
   interpreter (python.org "embeddable package" extracted under
   `bin/python/runtime`) plus `requirements-enginehost.txt`.
2. **No Qt desktop shell in the installer.** The `trinity` Qt/QML target is
   optional (`TRINITY_WITH_QT=ON`) and is not packaged. Until it is packaged and
   smoke-tested on a clean VM, the shipped front-end is the console shell.
3. **No code signing.** `Trinity-Setup.exe` will be unsigned until a certificate
   is available, so Windows SmartScreen will warn. Signing is a release step,
   not an architecture step.
4. **No smoke test of the installed product.** Packaging tests (install into a
   temp prefix, run `trinity-shell` with `TRINITY_DATA_DIR` pointed at a temp
   directory, assert exit code 0) are the next increment.
