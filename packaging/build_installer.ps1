# Compiles the Inno Setup installer and verifies the INSTALLED build: a
# silent install into a scratch directory, then the same bundle smoke the
# portable zip passes. The installer ships only when the installed copy
# genuinely answers the API/UI/engine probes.
#
# Usage: powershell -File packaging/build_installer.ps1 [-Version <v>]
param([string]$Version = "")

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $Version) {
    $toml = Get-Content (Join-Path $root "pyproject.toml") -Raw
    if ($toml -match '(?m)^version\s*=\s*"([^"]+)"') { $Version = $Matches[1] }
    if (-not $Version) { $Version = "1.0.0" }
}

$iscc = $env:TRINITY_ISCC
if ($iscc -and -not (Test-Path $iscc)) {
    throw ("TRINITY_ISCC points at {0}, which does not exist" -f $iscc)
}
if (-not $iscc) {
    $iscc = Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"
    if (-not (Test-Path $iscc)) {
        $iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    }
}
if (-not (Test-Path $iscc)) {
    throw ("Inno Setup compiler not found; install Inno Setup 6 or set TRINITY_ISCC ({0})" -f $iscc)
}

$dist = Join-Path $root ("packaging\dist\TrinityAI-" + $Version + "-win64")
if (-not (Test-Path (Join-Path $dist "api\trinity-api.exe"))) {
    throw ("Staged distribution not found at {0}; run package.ps1 first" -f $dist)
}

# Kill leftovers: a stale trinity-api.exe holds file locks on the dlls.
Get-Process trinity-api -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host ("==> compiling installer (version {0})" -f $Version)
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & $iscc ("/DVersion={0}" -f $Version) (Join-Path $PSScriptRoot "installer.iss") 2>&1 | Out-Null
} finally {
    $ErrorActionPreference = $prevEap
}
if (-not (Test-Path (Join-Path $PSScriptRoot ("dist\TrinityAI-" + $Version + "-win64-setup.exe")))) {
    throw "ISCC produced no setup ex"
}
Write-Host "    installer compiled"

# Verify the installed build, not the staging copy: silent-install into
# a scratch dir and run the same end-to-end smoke against it.
Write-Host "==> silent-installing into a scratch directory"
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("trinity-setup-verify-" + [System.IO.Path]::GetRandomFileName())
try {
    $setupArgs = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
        ("/DIR={0}" -f $scratch),
        ("/LOG={0}" -f (New-Item -ItemType File -Force -Path (Join-Path ([System.IO.Path]::GetTempPath()) "trinity-setup.log")).FullName)
    )
    $setupProc = Start-Process -FilePath (Join-Path $PSScriptRoot ("dist\TrinityAI-" + $Version + "-win64-setup.exe")) -ArgumentList $setupArgs -PassThru -Wait -WindowStyle Hidden
    if ($setupProc.ExitCode -ne 0) { throw ("silent setup failed with code {0}" -f $setupProc.ExitCode) }
    if (-not (Test-Path (Join-Path $scratch "Trinity.bat"))) {
        throw "silent installation did not lay down the launcher"
    }
    Write-Host ("    installed to {0}" -f $scratch)
    Write-Host "==> smoke-testing the installed build"
    & python (Join-Path $PSScriptRoot "smoke_bundle.py") $scratch
    if ($LASTEXITCODE -ne 0) { throw "installed-build smoke failed" }
} finally {
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
    Get-Process trinity-api -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Host ("==> installer ready: {0}" -f (Join-Path $PSScriptRoot ("dist\TrinityAI-" + $Version + "-win64-setup.exe")))

# M12: sign the installer AFTER its silent-verify but BEFORE the release
# hash manifest hashes it - manifest bytes = published bytes.
if (-not $env:TRINITY_SIGN_PFX_TARGET) { $env:TRINITY_SIGN_PFX_TARGET = "" }
$signPfx = $env:TRINITY_SIGN_PFX
$signPw = $env:TRINITY_SIGN_PFX_PASSWORD
$signArgs = @{
    Files = (Join-Path $PSScriptRoot ("dist\TrinityAI-" + $Version + "-win64-setup.exe"))
}
if ($signPfx -and $env:TRINITY_SIGN_PFX_TARGET -eq "base64") {
    $sArg = Join-Path ([System.IO.Path]::GetTempPath()) "trinity-sign.pfx"
    [IO.File]::WriteAllBytes($sArg, [Convert]::FromBase64String($signPfx))
    $signArgs.PfxPath = $sArg
    $signArgs.PfxPassword = $signPw
} elseif ($signPfx) {
    $signArgs.PfxPath = $signPfx
    $signArgs.PfxPassword = $signPw
}
if ($signArgs.PfxPath) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sign_files.ps1") @signArgs |
        ForEach-Object { Write-Host ("    {0}" -f $_) }
    if ($LASTEXITCODE -ne 0) { throw "signing the installer failed" }
} else {
    Write-Host "    installer left unsigned: no TRINITY_SIGN_PFX configured"
}
