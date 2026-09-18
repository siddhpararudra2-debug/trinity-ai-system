# Trinity — Windows packaging helper.
#
# Configures, builds and packages the native core exactly as the CI
# native-installer job does. Requires CMake on PATH and, for the NSIS generator,
# NSIS (winget install NSIS.NSIS  or  choco install nsis).
#
# Usage:
#   pwsh installer/scripts/package_windows.ps1                 # portable ZIP
#   pwsh installer/scripts/package_windows.ps1 -Generators "ZIP;NSIS"
#   pwsh installer/scripts/package_windows.ps1 -WithQt         # if Qt 6 is available
[CmdletBinding()]
param(
    [string] $BuildDir = "build/installer",
    [string] $Generators = "ZIP",
    [switch] $WithQt
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$source = Join-Path $root "app/native"
$qt = if ($WithQt) { "ON" } else { "OFF" }

Push-Location $root
try {
    Write-Host "Configuring (TRINITY_WITH_QT=$qt)..."
    cmake -S $source -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_BUILD_TYPE=Release -DTRINITY_WITH_QT=$qt -DTRINITY_BUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

    Write-Host "Building Release..."
    cmake --build $BuildDir --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

    if ($WithQt) {
        Write-Host "Collecting Qt runtime (windeployqt)..."
        $collect = Join-Path $PSScriptRoot "collect_qt_runtime.ps1"
        if (Test-Path $collect) { pwsh -NoProfile -File $collect -BuildDir $BuildDir }
    }

    # Smoke test the installed tree before cpack
    Write-Host "Smoke: trinity-shell --help (if built)..."
    $smokeBin = Join-Path $BuildDir "bin/Release/trinity-shell.exe"
    if (-not (Test-Path $smokeBin)) { $smokeBin = Join-Path $BuildDir "Release/trinity-shell.exe" }
    if (Test-Path $smokeBin) {
        $env:TRINITY_DATA_DIR = Join-Path $env:TEMP ("trinity-smoke-" + [Guid]::NewGuid().ToString("N").Substring(0,8))
        try { & $smokeBin --help 2>&1 | Out-Null; Write-Host "smoke ok (exit $LASTEXITCODE)" } finally { Remove-Item -Recurse -Force $env:TRINITY_DATA_DIR -ErrorAction SilentlyContinue; Remove-Item Env:TRINITY_DATA_DIR -ErrorAction SilentlyContinue }
    }

    foreach ($generator in $Generators.Split(";")) {
        Write-Host "Packaging with $generator..."
        Push-Location $BuildDir
        try {
            cpack -C Release -G $generator
            if ($LASTEXITCODE -ne 0) { throw "cpack $generator failed" }
        }
        finally { Pop-Location }
    }

    Write-Host "Artifacts:"
    Get-ChildItem -Path $BuildDir |
        Where-Object { $_.Extension -in ".zip", ".exe" } |
        ForEach-Object { Write-Host ("  {0} ({1:N0} bytes)" -f $_.Name, $_.Length) }
}
finally {
    Pop-Location
}
