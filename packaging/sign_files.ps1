# Code-signing harness (M12). Signs the shipped binaries with a PFX
# certificate, or with a self-signed code-signing cert for local
# distribution. No target -> an honest no-op print. Policy: signing is
# wired by the caller BEFORE hashing/compression so manifests describe
# signed bytes.
#
# Usage:
#   powershell -File sign_files.ps1 -Files native\Trinity.exe -PfxPath cert.pfx -PfxPassword <pw>
#   powershell -File sign_files.ps1 -Files ...                  # self-signed fallback
#   powershell -File sign_files.ps1 -Mode new -PfxOut dev.pfx   # create a PFX
param(
    [ValidateSet("sign", "new")]
    [string]$Mode = "sign",

    # sign mode
    [string[]]$Files = @(),
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    # When PfxPath is empty, a self-signed code-signing cert with this
    # subject is looked up in the user cert store (created when missing)
    # and used: zero-config local distribution. It does NOT clear
    # SmartScreen for other machines - that needs a CA-issued cert.
    [string]$SelfSignedSubject = "Trinity AI (self-signed)",

    # new mode: writes a self-signed code-signing PFX here
    [string]$PfxOut = ""
)

$ErrorActionPreference = "Stop"

function Find-Signtool {
    if ($env:TRINITY_SIGNTOOL -and (Test-Path $env:TRINITY_SIGNTOOL)) {
        return $env:TRINITY_SIGNTOOL
    }
    $kits = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\10.*" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [Version]$_.Name } -Descending
    foreach ($kd in $kits) {
        foreach ($arch in @("x64", "x86")) {
            $c = Join-Path $kd.FullName "$arch\signtool.exe"
            if (Test-Path $c) { return $c }
        }
    }
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "signtool.exe not found; install Windows SDK or set TRINITY_SIGNTOOL"
}

function Get-DevCert {
    param([string]$Subject)
    $cn = if ($Subject -match '^CN=') { $Subject } else { "CN=$Subject" }
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $cn -and $_.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3' } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject $cn `
            -KeyUsage DigitalSignature `
            -KeyExportPolicy Exportable `
            -KeySpec Signature `
            -HashAlgorithm SHA256 `
            -CertStoreLocation Cert:\CurrentUser\My
        Write-Host ("created self-signed code-signing cert {0}" -f $cert.Thumbprint)
    }
    return $cert
}

if ($Mode -eq "new") {
    if (-not $PfxOut) { throw "new mode needs -PfxOut <path.pfx>" }
    $cert = Get-DevCert -Subject $SelfSignedSubject
    $exportPw = ConvertTo-SecureString -String "trinity-dev" -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $PfxOut -Password $exportPw | Out-Null
    Write-Host ("exported PFX: {0} (export password: trinity-dev)" -f $PfxOut)
    exit 0
}

# ---------------------------------------------------------------- sign mode
if (-not $Files -or @($Files).Count -eq 0) {
    Write-Host "sign: no target files; nothing to do"
    exit 0
}

$signtool = Find-Signtool
$tsArgs = @("/fd", "SHA256", "/td", "SHA256", "/tr", "http://timestamp.digicert.com")

if ($PfxPath) {
    if (-not (Test-Path $PfxPath)) { throw ("PFX not found: {0}" -f $PfxPath) }
    $pwArgs = @("/f", $PfxPath)
    if ($PfxPassword) { $pwArgs += @("/p", $PfxPassword) }
    $certDesc = $PfxPath
    # Renewal policy (docs/PROCESS.md): a cert inside its final 60 days
    # must not be allowed into a release - renew first, then ship.
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(
        $PfxPath, $PfxPassword)
    $days = ($cert.NotAfter - (Get-Date)).TotalDays
    Write-Host ("cert subject '{0}' expires {1:yyyy-MM-dd} ({2:N0} days)" -f $cert.Subject, $cert.NotAfter, $days)
    if ($days -lt 60) {
        throw ("signing cert expires in {0:N0} days (<60). Renew first, then sign." -f $days)
    }
} else {
    $cert = Get-DevCert -Subject $SelfSignedSubject
    $pwArgs = @("/sha1", $cert.Thumbprint)
    $certDesc = "self-signed {0}" -f $cert.Thumbprint
    $days = ($cert.NotAfter - (Get-Date)).TotalDays
    Write-Host ("self-signed expires {0:yyyy-MM-dd} ({1:N0} days)" -f $cert.NotAfter, $days)
    if ($days -lt 60) {
        throw ("signing cert expires in {0:N0} days (<60). Renew first, then sign." -f $days)
    }
}

foreach ($f in $Files) {
    if (-not (Test-Path $f)) { throw ("sign target not found: {0}" -f $f) }
    & $signtool sign @pwArgs @tsArgs $f 2>&1 |
        Select-Object -Last 2 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) { throw ("signing failed: {0}" -f $f) }
}
Write-Host ("signed {0} file(s) via {1}" -f @($Files).Count, $certDesc)
