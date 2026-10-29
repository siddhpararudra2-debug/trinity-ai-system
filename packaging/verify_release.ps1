# Verify a published GitHub Release (M10 gate): download from the public
# Release page, check SHA256SUMS, unzip the portable bundle, and run the
# bundle smoke probe against it.
# Usage: powershell -ExecutionPolicy Bypass -File packaging/verify_release.ps1 [-Version 0.9.0]
param(
    [string]$Version = "0.9.0",
    [string]$Owner = "siddhpararudra2-debug",
    [string]$Repo = "trinity-ai-system",
    [switch]$KeepTemp
)
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$base = "https://github.com/$Owner/$Repo/releases/download/v$Version"
$work = Join-Path $env:TEMP "opencode\m10-verify\$Version"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work -Force | Out-Null

$zip = "TrinityAI-$Version-win64.zip"
$files = @("SHA256SUMS.txt", $zip, "TrinityAI-$Version-win64-setup.exe")
foreach ($f in $files) {
    Write-Host ("==> download {0}" -f $f)
    Invoke-WebRequest -Uri "$base/$f" -OutFile (Join-Path $work $f)
}

# --- SHA-256 manifest check -------------------------------------------------
Write-Host "==> sha256"
$ok = $true
foreach ($line in (Get-Content (Join-Path $work "SHA256SUMS.txt"))) {
    if (-not $line.Trim()) { continue }
    $hash, $name = $line.Trim() -split "\s+", 2
    $name = $name.Trim().TrimStart("*")
    $local = Join-Path $work $name
    if (-not (Test-Path $local)) { Write-Host ("MISSING {0}" -f $name); $ok = $false; continue }
    $actual = (Get-FileHash $local -Algorithm SHA256).Hash.ToLower()
    if ($actual -eq $hash.ToLower()) {
        Write-Host ("  ok    {0}" -f $name)
    } else {
        Write-Host ("  FAIL  {0}: expected {1}, got {2}" -f $name, $hash, $actual)
        $ok = $false
    }
}
if (-not $ok) { throw "SHA256SUMS verification FAILED" }

# --- unzip + smoke the bundle as published ----------------------------------
Write-Host "==> unzip"
Expand-Archive -Path (Join-Path $work $zip) -DestinationPath $work -Force
$bundle = Get-ChildItem $work -Directory | Select-Object -First 1
Write-Host ("==> smoke against {0}" -f $bundle.FullName)
python packaging/smoke_bundle.py $bundle.FullName
if ($LASTEXITCODE -ne 0) { throw "bundle smoke FAILED" }

Write-Host ("M10 GATE: PASS (v{0} verified as published)" -f $Version)
if (-not $KeepTemp) { Remove-Item $work -Recurse -Force }
