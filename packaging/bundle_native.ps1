# Builds the packaged native engine bundle: Trinity.exe plus every
# runtime DLL it needs (Qt via windeployqt, OpenCV from the vcpkg leg).
# The result must answer an engine call with NO Qt on PATH -- a bundle
# that only works next to a developer checkout is not a bundle.
#
# Usage: powershell -File packaging/bundle_native.ps1 [-TrinityExe <path>] [-OutDir <path>]
param(
    [string]$TrinityExe = "app/native/build/release-vcpkg/Release/Trinity.exe",
    [string]$OutDir = "packaging/dist/native",
    [string]$WinDeployQt = "C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$src = Join-Path $root $TrinityExe
if (-not (Test-Path $src)) {
    throw ("Trinity.exe not found at {0}; build the windows-release-vcpkg preset first" -f $src)
}
if (-not (Test-Path $WinDeployQt)) {
    throw ("windeployqt not found at {0}; pass -WinDeployQt for your Qt install" -f $WinDeployQt)
}

# Resolve OutDir against the repo root ONCE, absolutely -- windeployqt
# gets pushed-location CWDs that would otherwise eat relative paths.
$OutDir = [System.IO.Path]::GetFullPath((Join-Path $root $OutDir))

Write-Host "==> bundling native engine"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "    copying Trinity.exe"
Copy-Item $src (Join-Path $OutDir "Trinity.exe") -Force

# OpenCV DLLs: the vcpkg manifest installs next to the build tree.
# src = .../build/release-vcpkg/Release/Trinity.exe
$legDir = Split-Path (Split-Path $src)  # .../build/release-vcpkg
$vcpkgBin = Join-Path $legDir "vcpkg_installed/x64-windows/bin"
if (Test-Path $vcpkgBin) {
    $dlls = Get-ChildItem $vcpkgBin -Filter "*.dll"
    Write-Host ("    copying {0} vcpkg/OpenCV DLLs" -f $dlls.Count)
    Copy-Item (Join-Path $vcpkgBin "*.dll") $OutDir -Force
} else {
    # Binary dir layouts vary; fall back to scanning around the exe.
    $found = Get-ChildItem (Split-Path (Split-Path $legDir)) -Recurse -Filter "opencv_core*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        Write-Host ("    copying OpenCV DLLs from {0}" -f $found.DirectoryName)
        Copy-Item (Get-ChildItem $found.DirectoryName -Filter "*.dll") $OutDir -Force
    } else {
        throw "No OpenCV DLLs found beside the build; the packaged vision engine would be a lie"
    }
}

Write-Host "    windeployqt"
Push-Location $OutDir
try {
    # Native tool stderr (warnings like missing dxcompiler) must not
    # abort under Stop + 2>&1 redirect; only hard absence of Qt6Core.dll
    # verifies success below.
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $WinDeployQt --release --no-compiler-runtime (Join-Path $OutDir "Trinity.exe") 2>&1 |
            Select-String -Pattern "error:" | ForEach-Object { Write-Host "    $_" }
    } finally {
        $ErrorActionPreference = $prevEap
    }
} finally {
    Pop-Location
}
if (-not (Test-Path (Join-Path $OutDir "Qt6Core.dll"))) {
    throw "windeployqt produced no Qt6Core.dll"
}

# Hermetic verification: same selftest the CI runs, with a PATH scrubbed
# of any system Qt so the bundled DLLs are the only option.
Write-Host "==> verifying the bundle hermetically"
$sterile = [System.IO.Path]::GetTempPath() + "trinity_bundle_path"
New-Item -ItemType Directory -Force -Path $sterile | Out-Null
$env:PATH = $sterile
Push-Location $OutDir
try {
    $out = & .\Trinity.exe --selftest 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ("bundled Trinity.exe --selftest failed with code {0}" -f $LASTEXITCODE)
    }
    Write-Host "    bundle --selftest passed"
} finally {
    Pop-Location
}

Write-Host ("==> native bundle ready: {0}" -f $OutDir)
