# Assembles the portable Trinity distribution (native + api + web) and
# smoke-tests it end to end, then zips it as the release artifact.
# Every step either really happens or the script fails: no half shelves.
#
# Usage: powershell -File packaging/package.ps1 [-NativeExe <path>] [-Version <v>]
param(
    [string]$NativeExe = "app/native/build/release-vcpkg/Release/Trinity.exe",
    [string]$Version = "",
    [string]$WinDeployQt = "",
    # M12: signing. PFX from a secret (base64) or plain path; empty = an
    # honest unsigned build. Signing happens BEFORE the smoke and the zip
    # so hashes and the manifest describe the signed bytes.
    [string]$SignPfx = "",
    [string]$SignPfxPassword = ""
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $Version) {
    $toml = Get-Content (Join-Path $root "pyproject.toml") -Raw
    if ($toml -match '(?m)^version\s*=\s*"([^"]+)"') { $Version = $Matches[1] }
    if (-not $Version) { $Version = "1.0.0" }
}
Write-Host ("==> packaging version {0}" -f $Version)

$stage = [System.IO.Path]::GetFullPath((Join-Path $root ("packaging/dist/TrinityAI-" + $Version + "-win64")))
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# 1. native engine bundle (also runs the hermetic selftest).
Write-Host "==> native"
$bnArgs = @{ TrinityExe = $NativeExe; OutDir = (Join-Path $stage "native") }
if (-not $WinDeployQt) { $WinDeployQt = $env:TRINITY_WINDEPLOYQT }
if ($WinDeployQt) { $bnArgs.WinDeployQt = $WinDeployQt }
powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "bundle_native.ps1") @bnArgs |
    ForEach-Object { Write-Host ("    {0}" -f $_) }
if ($LASTEXITCODE -ne 0) { throw "native bundling failed" }

# 1b. sign the shipped engine exe BEFORE any hashing/compression (M12).
# TRINITY_SIGN_PFX_TARGET=pfx marks the secret as a base64-encoded PFX;
# any other non-empty value is used as a plain certificate file path.
if (-not $SignPfx) { $SignPfx = $env:TRINITY_SIGN_PFX }
if (-not $SignPfxPassword) { $SignPfxPassword = $env:TRINITY_SIGN_PFX_PASSWORD }
$signArgs = @{ Files = (Join-Path $stage "native\Trinity.exe") }
if ($SignPfx -and $env:TRINITY_SIGN_PFX_TARGET -eq "base64") {
    $sArg = Join-Path $env:TEMP "trinity-sign.pfx"
    [IO.File]::WriteAllBytes($sArg, [Convert]::FromBase64String($SignPfx))
    $signArgs.PfxPath = $sArg
    $signArgs.PfxPassword = $SignPfxPassword
} elseif ($SignPfx) {
    $signArgs.PfxPath = $SignPfx
    $signArgs.PfxPassword = $SignPfxPassword
}
if ($signArgs.PfxPath) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sign_files.ps1") @signArgs |
        ForEach-Object { Write-Host ("    {0}" -f $_) }
    if ($LASTEXITCODE -ne 0) { throw "signing failed" }
} else {
    Write-Host "    unsigned build: no TRINITY_SIGN_PFX configured (waiting on the M12 certificate)"
}

# 2. backend: frozen uvicorn process.
Write-Host "==> api"
Write-Host "    pyinstaller"
# PS 5.1 turns native stderr records into terminating errors under
# Stop + 2>&1; success is verified by the artifact check, not the log.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & python -m PyInstaller (Join-Path $PSScriptRoot "trinity-api.spec") --noconfirm `
        --distpath (Join-Path $PSScriptRoot "dist") --workpath (Join-Path $PSScriptRoot "build") 2>&1 |
        Out-Null
} finally {
    $ErrorActionPreference = $prevEap
}
if (-not (Test-Path (Join-Path $PSScriptRoot "dist/trinity-api/trinity-api.exe"))) {
    throw "PyInstaller produced no trinity-api.exe"
}
# M11 proof point: the kernel must have been collected, and separately
# its TK DLL payload must have followed it. Distinguish these two so a
# runner failure says exactly which half is broken.
$internal = Join-Path $PSScriptRoot "dist/trinity-api/_internal"
$ocpPyds = @(Get-ChildItem $internal -Recurse -Filter "*.pyd" |
    Where-Object { $_.Directory.Name -eq "OCP" -or $_.Name -like "OCP*" })
$tkDlls = @(Get-ChildItem $internal -Recurse -Filter "TK*.dll")
Write-Host ("OCP collected: {0} pyd, {1} TK dll" -f $ocpPyds.Count, $tkDlls.Count)
if ($ocpPyds.Count -eq 0) {
    Write-Output ("::error ::OCP was NOT collected: the build env lacks OpenCascade (pip install '.[cad]') or analysis failed")
    exit 1
}
if ($tkDlls.Count -eq 0) {
    Write-Output "::error ::OCP pyds collected but no TK*.dll - binary dependency scan failed"
    exit 1
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage "api") | Out-Null
Copy-Item (Join-Path $PSScriptRoot "dist/trinity-api/*") (Join-Path $stage "api") -Recurse -Force

# 3. UI: the Next.js static export (build it when missing/asked fresh).
Write-Host "==> web"
$exported = Join-Path $root "frontend/out/index.html"
if (-not (Test-Path $exported)) {
    Write-Host "    no out/ export: building (TRINITY_STATIC_EXPORT=1)"
    Push-Location (Join-Path $root "frontend")
    try {
        $env:TRINITY_STATIC_EXPORT = "1"
        $env:NEXT_PUBLIC_TRINITY_API_URL = "/api"
        & npm.cmd run build 2>&1 | Select-Object -Last 4 | ForEach-Object { Write-Host ("    {0}" -f $_) }
        if ($LASTEXITCODE -ne 0) { throw "npm run build failed" }
    } finally {
        $env:TRINITY_STATIC_EXPORT = $null
        $env:NEXT_PUBLIC_TRINITY_API_URL = $null
        Pop-Location
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage "web") | Out-Null
Copy-Item (Join-Path $root "frontend/out/*") (Join-Path $stage "web") -Recurse -Force

# 4. Launcher: set the bundle environment, then start the single API
# process that serves the UI and the native bridge.
Write-Host "==> launcher"
$bat = @'
@echo off
setlocal
set HERE=%~dp0
if "%TRINITY_API_PORT%"=="" set TRINITY_API_PORT=8000
set TRINITY_UI_DIR=%HERE%web
set TRINITY_NATIVE_EXEC=%HERE%native\Trinity.exe
set TRINITY_QT_BIN=%HERE%native
if "%TRINITY_STORAGE_ROOT%"=="" set TRINITY_STORAGE_ROOT=%LOCALAPPDATA%\TrinityAI\data
echo Starting Trinity AI (api port %TRINITY_API_PORT%)...
start "Trinity AI" /D "%HERE%api" trinity-api.exe
timeout /t 4 /nobreak >nul
start "" http://127.0.0.1:%TRINITY_API_PORT%/
endlocal
'@
Set-Content -Path (Join-Path $stage "Trinity.bat") -Value $bat -Encoding Ascii

# 5. End-to-end smoke of the assembled layout.
Write-Host "==> smoke"
& python (Join-Path $PSScriptRoot "smoke_bundle.py") $stage
if ($LASTEXITCODE -ne 0) { throw "distribution smoke failed" }

# 6. The portable zip IS the release artifact; keep installer + zip.
Write-Host "==> zip"
$zip = "$stage.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host ("==> done: {0}" -f $zip)
