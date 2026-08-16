<#
.SYNOPSIS
    build_msix.ps1 - One-click build + package of GraphStudio into an MSIX for
    the Microsoft Store.

.DESCRIPTION
    Builds the task_graph stack (root library + subnode plugins + graph_studio),
    stages a Windows Store package layout, runs windeployqt to gather the Qt
    runtime, copies OpenCV and the dlopen'd plugins into PlugIns\, renders
    AppxManifest.xml from a template, builds resources.pri (makepri), packs an
    .msix (makeappx), optionally self-signs it (signtool), and assembles an
    .msixupload (with .appxsym debug symbols) for Partner Center.

    Result layout ($OutDir):
      graph_studio-<version>_x64.msix        — signed (default) or unsigned (-SkipSign)
      graph_studio-<version>_x64.msixupload  — upload this to the Microsoft Store

    Store submission notes:
      * -IdentityName must equal the name you reserved in Partner Center.
      * -Publisher must equal "CN=<Publisher ID>" from Partner Center
        (View app identity details). For local self-signed testing any CN works
        as long as the certificate subject matches.
      * Version must increase per submission; the last quad stays 0.
      * Declare the runFullTrust restricted capability in the submission.

.PARAMETER Version
    App version (default 0.1.0). Expanded to four parts with a trailing 0.

.PARAMETER Config
    Build configuration (default RelWithDebInfo so .pdb symbols are available
    for the .appxsym). Debug works too (deploys the Qt debug kit).

.PARAMETER Jobs
    Parallel compile jobs (default: logical CPU count).

.PARAMETER Clean
    Delete the GraphStudio app build directory before rebuilding.

.PARAMETER SkipBuild
    Do not build; package whatever is already in the configured build dirs.

.PARAMETER SkipSign
    Leave the .msix unsigned (recommended for Store upload — the Store re-signs
    the package for you). Off by default, producing a self-signed package that
    can be installed locally for testing.

.PARAMETER SkipSentry
    Build without the Sentry/Crashpad crash-reporting module (default: fetch
    sentry-native via scripts\fetch_sentry.py and build it in; the app is a
    clean no-op at runtime unless a DSN is configured).

.PARAMETER SentryDsn
    Embed the Sentry DSN at compile time so release packages report crashes
    without any environment variable (a public client key — safe to ship).
    Runtime SENTRY_DSN still takes precedence when set.

.PARAMETER SentryRelease
    Full channel version for the Sentry release string (e.g. 0.1.0-beta.42)
    so crashes group per published GitHub tag; defaults to the root project
    VERSION parsed by the app CMakeLists.

.PARAMETER CertThumbprint
    Reuse an existing code-signing certificate (CurrentUser\My) by thumbprint.

.PARAMETER IdentityName
    Package identity Name (default "GraphStudio"). Must match the Store-reserved
    app name for Store submissions.

.PARAMETER Publisher
    Certificate/publisher subject (default "CN=GraphStudioDev"). Must match the
    signing certificate subject for local installs; for the Store it must equal
    your Partner Center Publisher ID (CN=...).

.PARAMETER DisplayName
    Store display name.

.PARAMETER PublisherDisplayName
    Publisher display name shown in the Store.

.PARAMETER Description
    Package/Store description.

.PARAMETER Qt / OpenCvDir / DisableOpenCv / Cmake
    Toolchain overrides, same as run_graph_studio.ps1.

.PARAMETER OutDir
    Output directory (default <repo>\dist\msix).

.PARAMETER Help
    Show this help.

.EXAMPLE
    scripts\build_msix.ps1                            # build + self-signed test msix + upload
    scripts\build_msix.ps1 -SkipSign                  # unsigned msixupload for the Store
    scripts\build_msix.ps1 -Version 0.2.0 -SkipSign
    scripts\build_msix.ps1 -SkipSentry                # build without crash reporting
    scripts\build_msix.ps1 -SentryDsn <dsn> -SentryRelease 0.1.0-beta.42 -SkipSign
.note
    Requires the Windows SDK tools (makeappx, makepri, signtool) — bundled with
    Visual Studio / Windows SDK installs. windeployqt comes with Qt.
#>
[CmdletBinding()]
param(
    [string]$Version = "0.1.0",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo",
    [string]$Jobs = "",
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$SkipSign,
    [switch]$SkipSentry,
    [string]$SentryDsn = "",
    [string]$SentryRelease = "",
    [string]$CertThumbprint = "",
    [string]$IdentityName = "GraphStudio",
    [string]$Publisher = "CN=GraphStudioDev",
    [string]$DisplayName = "Graph Studio",
    [string]$PublisherDisplayName = "GraphStudio Publisher",
    [string]$Description = "Visual DAG editor and task execution framework built on task_graph.",
    [string]$Qt = "",
    [switch]$DisableOpenCv,
    [string]$OpenCvDir = "",
    [string]$Cmake = "",
    [string]$OutDir = "",
    [switch]$Help
)

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Path -Detailed
    exit 0
}

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "lib\gs-common.ps1")

$RootDir = Split-Path -Parent $ScriptDir
if (-not $OutDir) { $OutDir = Join-Path $RootDir "dist\msix" }
$OutDir = (New-Item -ItemType Directory -Force -Path $OutDir).FullName

# Compress-Archive only accepts .zip destinations; zip to a temp .zip then
# rename so .appxsym / .msixupload archives can be produced.
function Compress-ToFile {
    param([string[]]$Items, [string]$Target)
    $tmpZip = $Target + ".zip"
    if (Test-Path $tmpZip) { Remove-Item $tmpZip -Force }
    Compress-Archive -LiteralPath $Items -DestinationPath $tmpZip -Force
    Move-Item $tmpZip $Target -Force
}

$GsDir = Join-Path $RootDir "app\graph_studio"
$PackagingDir = Join-Path $GsDir "packaging"

$Arch = "x64"
$IsDebug = ($Config -eq "Debug")

# ---- version -> 4-part appx (last quad must be 0 for the Store) ----
function ConvertTo-AppxVersion([string]$v) {
    $parts = @($v.Split('.'))
    if ($parts.Count -lt 4) { $parts = $parts + @("0") * (4 - $parts.Count) }
    if ($parts.Count -gt 4) { $parts = $parts[0..3] }
    $parts[3] = "0"
    ($parts -join ".")
}
$VersionQuad = ConvertTo-AppxVersion $Version
if ($VersionQuad -ne $Version) {
    Write-Step "Version '$Version' -> appx '$VersionQuad' (trailing quad forced to 0)"
}
$PackageBaseName = "graph_studio-$VersionQuad`_$Arch"

if (-not $Publisher.StartsWith("CN=")) { $Publisher = "CN=$Publisher" }

Write-Step "Publishing identity: $IdentityName  publisher: $Publisher  version: $VersionQuad"

# ---- resolve toolchain ----
$Env = Resolve-GsEnv -Qt $Qt -OpenCvDir $OpenCvDir -DisableOpenCv:$DisableOpenCv -Cmake $Cmake
if (-not $Jobs) { $Jobs = Get-DefaultJobs }
$Jobs = [int]$Jobs

$Windeployqt = Join-Path $Env.Qt "bin\windeployqt.exe"
$Makeappx = Find-SdkTool "makeappx"
$Makepri  = Find-SdkTool "makepri"
$Signtool = Find-SdkTool "signtool"
foreach ($t in @(@("windeployqt", $Windeployqt), @("makeappx", $Makeappx),
                  @("makepri", $Makepri), @("signtool", $Signtool))) {
    if (-not $t[1] -or -not (Test-Path $t[1])) {
        Write-Fail "Missing tool: $($t[0]) (Windows SDK / Qt required)."
        exit 1
    }
}

# ---- 1) build ----
$SentryDefines = @()
if (-not $SkipBuild -and -not $SkipSentry) {
    $SentryRoot = Join-Path $GsDir "third_party\sentry-native"
    if (-not (Test-Path (Join-Path $SentryRoot "CMakeLists.txt"))) {
        Write-Step "Fetching sentry-native (first run is slow)"
        $Code = Invoke-Native "python" @((Join-Path $ScriptDir "fetch_sentry.py"))
        if ($Code -ne 0) { Write-Fail "fetch_sentry.py failed"; exit $Code }
    }
    if ($SentryDsn) {
        Write-Step "Embedding Sentry DSN"
        $SentryDefines += "-DGRAPH_STUDIO_SENTRY_DSN=$SentryDsn"
    }
    if ($SentryRelease) {
        $SentryDefines += "-DGRAPH_STUDIO_SENTRY_VERSION=$SentryRelease"
    }
}
if (-not $SkipBuild) {
    $Build = Build-GraphStudioStack -Env $Env -Config $Config -Jobs $Jobs -Clean:$Clean -AppDefines $SentryDefines
} else {
    $Build = @{
        RootDir  = $RootDir
        LibBuild = Join-Path $RootDir "build"
        GsDir    = $GsDir
        GsBuild  = Join-Path $GsDir "build"
    }
}

$exeSource = Join-Path $Build.GsBuild "$Config\graph_studio.exe"
$libDllSource = Join-Path $Build.LibBuild "$Config\task_graph.dll"
foreach ($f in @($exeSource, $libDllSource)) {
    if (-not (Test-Path $f)) { Write-Fail "Missing build artifact: $f (run without -SkipBuild)."; exit 1 }
}

# ---- 2) stage package layout ----
$Staging = Join-Path $OutDir "staging"
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Force -Path $Staging | Out-Null
Write-Step "Staging package layout: $Staging"

Copy-Item $exeSource    (Join-Path $Staging "graph_studio.exe")
Copy-Item $libDllSource (Join-Path $Staging "task_graph.dll")

# crashpad_handler.exe next to the exe (Sentry release builds)
$crashpad = Join-Path $Build.GsBuild "$Config\crashpad_handler.exe"
if (Test-Path $crashpad) { Copy-Item $crashpad (Join-Path $Staging "crashpad_handler.exe") }

# OpenCV runtime DLLs (world build; skip the debug '*d.dll' in release kits)
if (-not $Env.DisableOpenCv -and $Env.OpenCvDir) {
    $opencvBin = Join-Path $Env.OpenCvDir "bin"
    if (-not (Test-Path $opencvBin)) { Write-Fail "OpenCV bin dir missing: $opencvBin"; exit 1 }
    Get-ChildItem $opencvBin -Filter "opencv_*.dll" |
        Where-Object { $IsDebug -or $_.Name -notmatch 'd\.dll$' } |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $Staging $_.Name) }
}

# subnode plugins -> PlugIns\ (collected by PluginBootstrap from <exe dir>/PlugIns)
$pluginDirs = Get-GsPluginDirs -LibBuild $Build.LibBuild -Config $Config
if ($pluginDirs) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Staging "PlugIns") | Out-Null
    foreach ($dir in $pluginDirs) {
        foreach ($dll in (Get-ChildItem $dir -Filter "*.dll")) {
            Copy-Item $dll.FullName (Join-Path $Staging "PlugIns\$($dll.Name)") -Force
        }
    }
} else {
    Write-Fail "No submodule plugin DLLs found under $($Build.LibBuild)\submodules\<name>\$Config\."
    exit 1
}

# order matters for windeployqt: run on the exe in the staging tree
Write-Step "Running windeployqt (Qt $Config kit)"
$WdeployArgs = @(if ($IsDebug) { "--debug" } else { "--release" },
                 "--no-translations", "--compiler-runtime",
                 (Join-Path $Staging "graph_studio.exe"))
$Code = Invoke-Native $Windeployqt $WdeployArgs
if ($Code -ne 0) { exit $Code }

# ---- 3) manifest + assets ----
Write-Step "Writing AppxManifest.xml"
$AssetsSrc = Join-Path $PackagingDir "Assets"
if (-not (Test-Path $AssetsSrc)) {
    Write-Fail "Missing package assets: run scripts\generate_assets.ps1 first."
    exit 1
}
Copy-Item -Recurse $AssetsSrc (Join-Path $Staging "Assets")

$Manifest = Get-Content -Raw (Join-Path $PackagingDir "AppxManifest.xml.in")
$Manifest = $Manifest.Replace("@GS_APPX_IDENTITY@", $IdentityName)
$Manifest = $Manifest.Replace("@GS_APPX_PUBLISHER@", $Publisher)
$Manifest = $Manifest.Replace("@GS_APPX_VERSION@", $VersionQuad)
$Manifest = $Manifest.Replace("@GS_APPX_DISPLAY_NAME@", $DisplayName)
$Manifest = $Manifest.Replace("@GS_APPX_PUBLISHER_DISPLAY_NAME@", $PublisherDisplayName)
$Manifest = $Manifest.Replace("@GS_APPX_DESCRIPTION@", $Description)
Set-Content -Path (Join-Path $Staging "AppxManifest.xml") -Value $Manifest -Encoding UTF8

# ---- 4) resources.pri (required by the Store) ----
Write-Step "Building resources.pri (makepri)"
$PriConfig = Join-Path $OutDir "priconfig.xml"
$Code = Invoke-Native $Makepri @("createconfig", "/cf", $PriConfig, "/dq", "en-US", "/overwrite")
if ($Code -ne 0) { exit $Code }
$Code = Invoke-Native $Makepri @("new", "/pr", $Staging, "/cf", $PriConfig, "/o",
                                 "/of", (Join-Path $Staging "resources.pri"))
if ($Code -ne 0) { exit $Code }

# ---- 5) pack .msix ----
Write-Step "Packing .msix (makeappx)"
$PkgPath = Join-Path $OutDir "$PackageBaseName.msix"
if (Test-Path $PkgPath) { Remove-Item $PkgPath -Force }
$Code = Invoke-Native $Makeappx @("pack", "/d", $Staging, "/p", $PkgPath, "/o")
if ($Code -ne 0) { exit $Code }

# ---- 6) sign (unless -SkipSign) ----
if (-not $SkipSign) {
    Write-Step "Signing .msix (self-signed dev certificate, signtool)"
    $cert = $null
    if ($CertThumbprint) {
        $cert = Get-Item "Cert:\CurrentUser\My\$CertThumbprint" -ErrorAction SilentlyContinue
    }
    if (-not $cert) {
        $cert = Get-ChildItem "Cert:\CurrentUser\My" -CodeSigningCert -ErrorAction SilentlyContinue |
            Where-Object { $_.Subject -eq $Publisher } | Select-Object -First 1
    }
    if (-not $cert) {
        Write-Step "Creating self-signed code-signing certificate: $Publisher"
        $cert = New-SelfSignedCertificate -Type CodeSigning -Subject $Publisher `
            -CertStoreLocation "Cert:\CurrentUser\My" -KeyExportPolicy Exportable `
            -NotAfter (Get-Date).AddYears(1)
    }
    $SignArgs = @("sign", "/sha1", $cert.Thumbprint, "/fd", "SHA256",
                  "/td", "SHA256", "/tr", "http://timestamp.digicert.com", $PkgPath)
    $Code = Invoke-Native $Signtool $SignArgs
    if ($Code -ne 0) {
        Write-Step "Timestamp server unreachable; retrying without RFC3161 timestamp"
        $Code = Invoke-Native $Signtool @("sign", "/sha1", $cert.Thumbprint,
                                          "/fd", "SHA256", $PkgPath)
    }
    if ($Code -ne 0) { exit $Code }
} else {
    Write-Step "Skipped signing (-SkipSign); the Microsoft Store will re-sign on submission."
}

# ---- 7) .appxsym + .msixupload ----
$SymPath = ""
$Syms = @(
    (Join-Path $Build.GsBuild "$Config\graph_studio.pdb"),
    (Join-Path $Build.LibBuild "$Config\task_graph.pdb")
) | Where-Object { Test-Path $_ }
if ($Syms) {
    $SymPath = Join-Path $OutDir "$PackageBaseName.appxsym"
    if (Test-Path $SymPath) { Remove-Item $SymPath -Force }
    Compress-ToFile -Items $Syms -Target $SymPath
    Write-Step "Debug symbols: $SymPath"
}

$UploadPath = Join-Path $OutDir "$PackageBaseName.msixupload"
if (Test-Path $UploadPath) { Remove-Item $UploadPath -Force }
$UploadItems = @($PkgPath); if ($SymPath) { $UploadItems += $SymPath }
Compress-ToFile -Items $UploadItems -Target $UploadPath

# ---- summary ----
Write-Ok "$PkgPath"
Write-Ok "$UploadPath"
Write-Step "Local test: trust the cert, then  Add-AppxPackage -Path '$PkgPath'"
Write-Step "Store upload: Partner Center > upload '$UploadPath' (identity '$IdentityName', publisher '$Publisher'),"
Write-Step "  and declare the runFullTrust restricted capability. Bump -Version for every submission."