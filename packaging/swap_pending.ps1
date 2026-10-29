# M13 swap: applied by Trinity.bat BEFORE the API boots (Windows cannot
# replace a running exe, so a prepared update is consumed at launch).
# Reads the updater's swap marker under TRINITY_STORAGE_ROOT, moves
# native/api/web from the staged tree into the install root, and clears
# the marker. TRINITY_STORAGE_ROOT itself lives outside the swap set,
# so user data is untouched. Any failure means: pending update stays
# pending and gets retried on the next boot - never a broken install.
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$store = $env:TRINITY_STORAGE_ROOT
if (-not $store) { $store = Join-Path $env:LOCALAPPDATA "TrinityAI\data" }
$pending = Join-Path $store "update-pending"
$marker = Join-Path $pending "swap.json"
if (-not (Test-Path $marker)) { exit 0 }

try {
    $plan = Get-Content $marker -Raw | ConvertFrom-Json
    $source = $plan.source
    if (-not (Test-Path $source)) { throw "staged tree missing: $source" }
    $ver = if ($plan.version) { $plan.version } else { "unknown" }
    foreach ($sub in @("native", "api", "web")) {
        $src = Join-Path $source $sub
        $dst = Join-Path $here $sub
        $prev = Join-Path $here "$sub.prev"
        if (-not (Test-Path $src)) { continue }
        if (Test-Path $prev) { Remove-Item $prev -Recurse -Force -ErrorAction SilentlyContinue }
        if (Test-Path $dst) { Rename-Item $dst "$sub.prev" -Force -ErrorAction Stop }
        Copy-Item $src $dst -Recurse -Force
        Write-Host "swapped $sub -> version $ver"
    }
    Remove-Item $marker -Force -ErrorAction SilentlyContinue
    Remove-Item $pending -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    Write-Host "pending update not applied (will retry next boot): $_"
    exit 0
}
exit 0
