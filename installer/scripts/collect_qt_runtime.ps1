# Trinity — collect Qt runtime after build (windeployqt)
# Called from package_windows.ps1 when -WithQt is used, or manually:
#   pwsh installer/scripts/collect_qt_runtime.ps1 -BuildDir build/installer -QtDir C:/Qt/6.8.0/msvc2022_64
[CmdletBinding()]
param(
    [string]$BuildDir = "build/installer",
    [string]$QtDir = $env:Qt6_DIR
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$binDir = Join-Path $root "$BuildDir/bin/Release"
if (-not (Test-Path $binDir)) { $binDir = Join-Path $root "$BuildDir/Release" }
if (-not (Test-Path (Join-Path $binDir "trinity.exe"))) { Write-Host "trinity.exe not found under $binDir — nothing to deploy"; exit 0 }
if (-not $QtDir) {
    # try qmake on PATH
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) { $QtDir = Split-Path (Split-Path $qmake.Source -Parent) -Parent }
}
if (-not $QtDir -or -not (Test-Path $QtDir)) { Write-Warning "QtDir not found — skipping windeployqt. Set -QtDir or Qt6_DIR"; exit 0 }
$windeployqt = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) { $windeployqt = Join-Path $QtDir "windeployqt.exe" }
if (-not (Test-Path $windeployqt)) { Write-Warning "windeployqt not found at $windeployqt"; exit 0 }
Write-Host "Running windeployqt on $binDir/trinity.exe..."
& $windeployqt --release --qmldir (Join-Path $root "app/native/qml") (Join-Path $binDir "trinity.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
Write-Host "Qt runtime collected."
