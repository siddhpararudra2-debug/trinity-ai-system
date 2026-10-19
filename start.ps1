<#
.SYNOPSIS
    Starts Trinity: the Python API and the Next.js frontend, waits until both
    actually answer, opens a browser, and shuts both down cleanly.

.DESCRIPTION
    Nothing here fakes readiness. The API is polled on /api/health and the
    frontend on /, and the browser only opens once both have returned 200.
    If a process dies during startup its log is printed rather than the
    script waiting out the timeout on a port nothing is listening on.

    The native engine is optional. If Trinity.exe is found the six bridged
    consoles work; if it is not, the app still starts and those consoles say
    the backend is unavailable, which is the honest result. The vcpkg build is
    preferred because the plain build has no OpenCV, so the vision engine
    reports its capability as missing instead of loading images.

.PARAMETER ApiPort
    Port for the Python API. Default 8000.

.PARAMETER WebPort
    Port for the Next.js server. Default 3000.

.PARAMETER Dev
    Run the frontend in dev mode with hot reload instead of a production build.

.PARAMETER NoBuild
    Skip the production build even if one is missing. Faster when you have
    already built and nothing changed.

.PARAMETER NoBrowser
    Do not open a browser.

.EXAMPLE
    .\start.ps1
    .\start.ps1 -Dev
#>
[CmdletBinding()]
param(
    [int]$ApiPort = 8000,
    [int]$WebPort = 3000,
    [switch]$Dev,
    [switch]$NoBuild,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$runDir = Join-Path $root '.run'
$apiUrl = "http://127.0.0.1:$ApiPort"
$webUrl = "http://127.0.0.1:$WebPort"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor White }
function Write-Ok($msg) { Write-Host "    $msg" -ForegroundColor DarkGray }
function Write-Bad($msg) { Write-Host "    $msg" -ForegroundColor Red }
function Write-Warn($msg) { Write-Host "    $msg" -ForegroundColor Yellow }

function Stop-Child($proc, $label) {
    if (-not $proc -or $proc.HasExited) { return }
    # The whole tree: `next start` and uvicorn both spawn children, and
    # killing only the parent leaves the port held.
    & taskkill /PID $proc.Id /T /F 2>&1 | Out-Null
    Write-Ok "stopped $label"
}

function Wait-Http($url, $label, $proc, $timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { return $false }
        try {
            $r = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 3 -ErrorAction Stop
            if ($r.StatusCode -eq 200) { return $true }
        } catch {
            # Not up yet, or still compiling. Keep waiting.
        }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Show-Tail($path, $lines) {
    if (-not (Test-Path $path)) { return }
    Write-Bad "--- last $lines lines of $path ---"
    Get-Content $path -Tail $lines | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
}

function Find-QtBin {
    <#
        Trinity.exe links Qt6Core but ships no Qt DLLs next to itself, so
        without the Qt bin directory on PATH it cannot start at all. An
        explicit override wins; otherwise use whatever is already on PATH,
        then fall back to the usual C:\Qt install layout.
    #>
    if ($env:TRINITY_QT_BIN -and (Test-Path (Join-Path $env:TRINITY_QT_BIN 'Qt6Core.dll'))) {
        return $env:TRINITY_QT_BIN
    }
    foreach ($dir in ($env:Path -split ';')) {
        if ($dir -and (Test-Path (Join-Path $dir 'Qt6Core.dll'))) { return $null }
    }
    if (Test-Path 'C:\Qt') {
        $hit = Get-ChildItem -Path 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue } |
            ForEach-Object { Join-Path $_.FullName 'bin' } |
            Where-Object { Test-Path (Join-Path $_ 'Qt6Core.dll') } |
            Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

# --------------------------------------------------------------------------
Write-Step "Trinity"

New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$apiLog = Join-Path $runDir 'api.log'
$webLog = Join-Path $runDir 'web.log'
Remove-Item $apiLog, $webLog -ErrorAction SilentlyContinue

# --- prerequisites
foreach ($cmd in @('python', 'node')) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        Write-Bad "'$cmd' is not on PATH. Install it and run this again."
        exit 1
    }
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    Write-Bad "'npm' is not on PATH. Install Node.js and run this again."
    exit 1
}

# --- native engine, if one was built
$native = $null
$buildDir = Join-Path $root 'app\native\build'
if (Test-Path $buildDir) {
    $native = Get-ChildItem -Path $buildDir -Filter 'Trinity.exe' -Recurse -ErrorAction SilentlyContinue |
        Sort-Object @{ Expression = { if ($_.FullName -match 'vcpkg') { 0 } else { 1 } } },
                         @{ Expression = { $_.FullName.Length } } |
        Select-Object -First 1
}
if ($native) {
    $env:TRINITY_NATIVE_EXEC = $native.FullName
    $kind = if ($native.FullName -match 'vcpkg') { 'OpenCV build' } else { 'plain build, no OpenCV' }
    Write-Ok "native: $($native.FullName) ($kind)"

    $qtBin = Find-QtBin
    if ($qtBin) {
        $env:Path = "$qtBin;$env:Path"
        Write-Ok "qt:     $qtBin"
    }
    else {
        Write-Warn 'qt:     Qt6Core.dll not found; the native engine will not start.'
        Write-Ok 'set TRINITY_QT_BIN to the directory holding it'
    }
} else {
    Write-Warn 'native: no Trinity.exe under app/native/build'
    Write-Ok 'the six bridged consoles will report the backend unavailable'
}

# NEXT_PUBLIC_TRINITY_API_URL is inlined at build time, so it has to be set
# before `npm run build` runs, not just before the server starts.
$env:NEXT_PUBLIC_TRINITY_API_URL = "$apiUrl/api"
$env:TRINITY_STORAGE_ROOT = Join-Path $root 'data'
Write-Ok "api target: $apiUrl/api"

# --- free the ports first, or uvicorn fails with an opaque "address in use"
foreach ($port in @($ApiPort, $WebPort)) {
    $busy = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
    if ($busy) {
        Write-Warn "port $port is already in use; stopping whatever holds it"
        $busy | Select-Object -ExpandProperty OwningProcess -Unique |
            ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Seconds 2
    }
}

# --- frontend
$frontend = Join-Path $root 'frontend'
if (-not (Test-Path (Join-Path $frontend 'node_modules'))) {
    Write-Step 'installing frontend dependencies (first run only)'
    Push-Location $frontend
    try { & npm ci 2>&1 | Out-Null } finally { Pop-Location }
}

if ($Dev) {
    Write-Step 'starting frontend (dev)'
    $web = Start-Process -FilePath 'npm.cmd' `
        -ArgumentList @('run', 'dev', '--', '-p', "$WebPort") `
        -WorkingDirectory $frontend -PassThru -NoNewWindow `
        -RedirectStandardOutput $webLog -RedirectStandardError (Join-Path $runDir 'web.err.log')
} else {
    $built = Test-Path (Join-Path $frontend '.next\BUILD_ID')
    if ((-not $built) -and (-not $NoBuild)) {
        Write-Step 'building frontend (first run only, takes a minute)'
        Push-Location $frontend
        try {
            & npm run build 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Write-Bad 'frontend build failed; run `npm run build` in frontend/ to see why'
                exit 1
            }
        } finally { Pop-Location }
    }
    Write-Step 'starting frontend'
    $web = Start-Process -FilePath 'npm.cmd' `
        -ArgumentList @('run', 'start', '--', '-p', "$WebPort") `
        -WorkingDirectory $frontend -PassThru -NoNewWindow `
        -RedirectStandardOutput $webLog -RedirectStandardError (Join-Path $runDir 'web.err.log')
}

# --- api
Write-Step 'starting api'
$api = Start-Process -FilePath 'python' `
    -ArgumentList @('-m', 'uvicorn', 'src.main:app', '--host', '127.0.0.1', '--port', "$ApiPort") `
    -WorkingDirectory $root -PassThru -NoNewWindow `
    -RedirectStandardOutput $apiLog -RedirectStandardError (Join-Path $runDir 'api.err.log')

# --- readiness
Write-Step 'waiting for both to answer'
$apiUp = Wait-Http "$apiUrl/api/health" 'api' $api 90
$webUp = Wait-Http "$webUrl/" 'web' $web 120

if (-not $apiUp) {
    Write-Bad "api never became healthy at $apiUrl/api/health"
    Show-Tail (Join-Path $runDir 'api.err.log') 25
    Stop-Child $web 'frontend'
    Stop-Child $api 'api'
    exit 1
}
if (-not $webUp) {
    Write-Bad "frontend never answered at $webUrl/"
    Show-Tail (Join-Path $runDir 'web.err.log') 25
    Stop-Child $web 'frontend'
    Stop-Child $api 'api'
    exit 1
}

Write-Ok "api      $apiUrl  (health ok)"
Write-Ok "frontend $webUrl  (200 ok)"
Write-Ok "logs     $runDir"

# A 200 from /api/health says the process is up, not that any engine works.
# Every engine is native, so if the bridge is unreachable the app is a dead
# shell -- check for real and say so rather than reporting success.
$bridgeOk = $false
if ($native) {
    try {
        $probeBody = @{
            engine = 'research'
            operation = 'search'
            parameters = @{ query = 'startup probe' }
        } | ConvertTo-Json -Depth 5
        $probe = Invoke-RestMethod -Uri "$apiUrl/api/execute" -Method Post `
            -Body $probeBody -ContentType 'application/json' -TimeoutSec 30
        if ($probe.success) {
            Write-Ok 'engine:  native bridge answered a real call'
            $bridgeOk = $true
        }
        else {
            Write-Warn "engine:  native bridge refused the probe -- $($probe.errors[0].message)"
        }
    }
    catch {
        Write-Warn "engine:  native bridge probe failed -- $($_.Exception.Message)"
    }
    if (-not $bridgeOk) {
        Write-Ok 'every engine is native; the consoles will report unavailable'
        Write-Ok "see $runDir\api.log"
    }
}

if (-not $NoBrowser) {
    Start-Process $webUrl | Out-Null
    Write-Ok "opened $webUrl"
}

Write-Step 'running. Ctrl+C to stop both.'
Write-Ok 'this window must stay open'

try {
    while (-not $api.HasExited -and -not $web.HasExited) {
        Start-Sleep -Milliseconds 500
    }
    # Something died on its own. Say which, rather than exiting silently.
    if ($api.HasExited) {
        Write-Bad "api exited with code $($api.ExitCode)"
        Show-Tail (Join-Path $runDir 'api.err.log') 25
    }
    if ($web.HasExited) {
        Write-Bad "frontend exited with code $($web.ExitCode)"
        Show-Tail (Join-Path $runDir 'web.err.log') 25
    }
}
finally {
    Write-Step 'shutting down'
    Stop-Child $web 'frontend'
    Stop-Child $api 'api'
    Write-Ok 'stopped'
}