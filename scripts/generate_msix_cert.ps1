<#
.SYNOPSIS
    generate_msix_cert.ps1 - One-time creation of the fixed GraphStudio MSIX
    code-signing certificate consumed by CI.

.DESCRIPTION
    .github/workflows/release.yml signs every Windows .msix with the
    certificate stored in the MSIX_CERT_PFX / MSIX_CERT_PASSWORD repo secrets,
    so all releases share one publisher identity and users only ever trust a
    single .cer. Run this script once per certificate lifetime; it creates (or
    reuses) the self-signed certificate in Cert:\CurrentUser\My and exports
    into -OutDir (default <repo>\dist\msix-cert, gitignored):

      GraphStudioDev.cer                public key; uploaded alongside every
                                        Release so users can trust the publisher
      GraphStudioDev.pfx                private key + random password (SECRET)
      GraphStudioDev.pfx.b64            base64 of the PFX, ready for
                                        `gh secret set MSIX_CERT_PFX < file`
      GraphStudioDev.pfx-password.txt   the random password, ready for
                                        `gh secret set MSIX_CERT_PASSWORD < file`

    The password is deliberately not printed to the console. After setting the
    secrets, delete the local password file (and the .pfx if you do not need a
    backup of the private key).

    Because the certificate also lives in Cert:\CurrentUser\My, local runs of
    build_msix.ps1 sign with it automatically (subject match).

.PARAMETER Subject
    Certificate subject; also the MSIX Publisher. Default "CN=GraphStudioDev".

.PARAMETER Years
    Validity in years (default 5). Windows warns on install when the signing
    cert (or a non-timestamped signature) has expired, so do not go too low.

.PARAMETER OutDir
    Output directory (default <repo>\dist\msix-cert, gitignored).

.PARAMETER Force
    Create a fresh certificate even if one with the same subject already
    exists in Cert:\CurrentUser\My.

.EXAMPLE
    scripts\generate_msix_cert.ps1
    scripts\generate_msix_cert.ps1 -Subject "CN=GraphStudioDev" -Years 5
#>
[CmdletBinding()]
param(
    [string]$Subject = "CN=GraphStudioDev",
    [int]$Years = 5,
    [string]$OutDir = "",
    [switch]$Force,
    [switch]$Help
)

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Path -Detailed
    exit 0
}

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "lib\gs-common.ps1")

if (-not $Subject.StartsWith("CN=")) { $Subject = "CN=$Subject" }
if ($OutDir) {
    $OutDir = (New-Item -ItemType Directory -Force -Path $OutDir).FullName
} else {
    $OutDir = Join-Path (Split-Path -Parent $ScriptDir) "dist\msix-cert"
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
$BaseName = ($Subject -replace '^CN=', '') -replace '[^A-Za-z0-9_.-]', '_'

# ---- create or reuse the certificate ----
$cert = Get-ChildItem "Cert:\CurrentUser\My" -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $Subject } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1
if ($cert -and -not $Force) {
    Write-Step "Reusing existing certificate $($cert.Thumbprint) (expires $($cert.NotAfter.ToString('yyyy-MM-dd')))"
} else {
    Write-Step "Creating self-signed code-signing certificate: $Subject ($Years years)"
    $cert = New-SelfSignedCertificate -Type CodeSigning -Subject $Subject `
        -CertStoreLocation "Cert:\CurrentUser\My" -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears($Years)
}

# ---- random password (crypto RNG, confusable characters excluded); ----------
# written to a file only, never echoed, so it does not leak into transcripts.
$alphabet = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789".ToCharArray()
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
$bytes = New-Object byte[] 24
$rng.GetBytes($bytes)
$password = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })
$rng.Dispose()

$CerPath = Join-Path $OutDir "$BaseName.cer"
$PfxPath = Join-Path $OutDir "$BaseName.pfx"
$B64Path = Join-Path $OutDir "$BaseName.pfx.b64"
$PwdPath = Join-Path $OutDir "$BaseName.pfx-password.txt"

$secure = ConvertTo-SecureString -String $password -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $CerPath | Out-Null
# no trailing newline: `gh secret set < file` must ingest the exact value
[System.IO.File]::WriteAllText($B64Path, [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($PfxPath)))
[System.IO.File]::WriteAllText($PwdPath, $password)

# ---- next steps (best-effort repo slug for the gh commands) ----
$repo = ""
try {
    $url = git remote get-url origin 2>$null
    if ($url -match 'github\.com[:/]([^/]+/[^/.]+)(\.git)?$') { $repo = $Matches[1] }
} catch { }

Write-Ok "Publisher certificate : $CerPath"
Write-Ok "Code-signing PFX      : $PfxPath  (SECRET - never commit)"
Write-Ok "PFX base64            : $B64Path"
Write-Ok "PFX password file     : $PwdPath  (SECRET - delete after use)"
Write-Step "Publish the secrets (CI signs every .msix with this certificate):"
if ($repo) {
    Write-Step "  gh secret set MSIX_CERT_PFX -R $repo < $B64Path"
    Write-Step "  gh secret set MSIX_CERT_PASSWORD -R $repo < $PwdPath"
} else {
    Write-Step "  gh secret set MSIX_CERT_PFX < $B64Path"
    Write-Step "  gh secret set MSIX_CERT_PASSWORD < $PwdPath"
}
Write-Step "  ('<' input redirection only works in bash/cmd; in PowerShell use:"
Write-Step "   Get-Content <file> -Raw | gh secret set <name> -R <owner/repo>)"
Write-Step "End users install with:"
Write-Step "  Import-Certificate $CerPath -CertStoreLocation Cert:\LocalMachine\TrustedPeople   (admin)"
Write-Step "  Add-AppxPackage <the downloaded .msix>"
