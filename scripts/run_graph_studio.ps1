<#
.SYNOPSIS
    run_graph_studio.ps1 - Build and launch GraphStudio (Qt6 GUI) on Windows.

.DESCRIPTION
    Windows counterpart of scripts/run_graph_studio.sh.
      1) Builds the root task_graph library into <root>\build (Debug, OpenCV
         + subnode plugins ON). GraphStudio hardcodes <root>/build for both
         link_directories and plugin discovery, so this directory is required.
      2) Copies build\Debug\task_graph.lib to build\ so the app's
         link_directories(../build) can find it under the multi-config VS
         generator.
      3) Configure + builds app/graph_studio into app\graph_studio\build.
      4) Sets up the runtime PATH (Qt bin, OpenCV bin, build\Debug) and
         TASK_GRAPH_PLUGINS_PATH (each plugin's given Config output dir), then
         launches graph_studio.exe.

    Shares tool detection / stack build / console helpers with
    scripts\lib\gs-common.ps1.

.PARAMETER Config
    Build configuration (default: Debug).

.PARAMETER Jobs
    Parallel compile jobs (default: logical CPU count).

.PARAMETER Clean
    Delete the GraphStudio build directory first.

.PARAMETER NoBuild
    Skip building; just launch the existing binary.

.PARAMETER BuildOnly
    Build only, do not launch.

.PARAMETER Test
    Run GraphStudio's ctest suite (headless, QT_QPA_PLATFORM=offscreen) and exit.

.PARAMETER Qt
    Qt6 prefix containing bin + lib\cmake\Qt6 (default: auto-detect
    C:\Qt\<version>\msvc2022_64, or env QT_PREFIX_PATH).

.PARAMETER DisableOpenCv
    Configure the root library without OpenCV.

.PARAMETER OpenCvDir
    OpenCV install prefix (default: auto-detect C:\opencv\build\x64\vc16).

.PARAMETER Cmake
    Path to cmake.exe.

.EXAMPLE
    scripts\run_graph_studio.ps1
    scripts\run_graph_studio.ps1 -Clean -BuildOnly
    scripts\run_graph_studio.ps1 -Test

.NOTES
    Exit code 0 = success; non-zero = build error or missing binary.
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",
    [string]$Jobs = "",
    [switch]$Clean,
    [switch]$NoBuild,
    [switch]$BuildOnly,
    [switch]$Test,
    [string]$Qt = "",
    [switch]$DisableOpenCv,
    [string]$OpenCvDir = "",
    [string]$Cmake = "",
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
$GsDir = Join-Path $RootDir "app\graph_studio"
$GsBuild = Join-Path $GsDir "build"
$LibBuild = Join-Path $RootDir "build"

# ---- resolve toolchain (cmake / ctest / Qt / OpenCV) ----
$Env = Resolve-GsEnv -Qt $Qt -OpenCvDir $OpenCvDir -DisableOpenCv:$DisableOpenCv -Cmake $Cmake
if (-not $Jobs) { $Jobs = Get-DefaultJobs }
$Jobs = [int]$Jobs
Write-Step "Qt6 prefix: $($Env.Qt)"

# ---- build (unless --no-build) ----
if (-not $NoBuild) {
    $Build = Build-GraphStudioStack -Env $Env -Config $Config -Jobs $Jobs -Clean:$Clean
} else {
    if (-not (Test-Path "$GsBuild\$Config\graph_studio.exe") -and -not (Test-Path $GsBuild)) {
        Write-Fail "Build directory $GsBuild does not exist (and --no-build was given)."
        exit 1
    }
    $Build = @{ RootDir = $RootDir; LibBuild = $LibBuild; GsDir = $GsDir; GsBuild = $GsBuild }
}

# ---- runtime environment for both tests and launch ----
Add-GsRuntimePath -Env $Env

$libBin = Join-Path $LibBuild $Config
if (Test-Path $libBin) { $env:PATH = $libBin + [IO.Path]::PathSeparator + $env:PATH }

$pluginDirs = Get-GsPluginDirs -LibBuild $LibBuild -Config $Config
if ($pluginDirs) {
    $env:TASK_GRAPH_PLUGINS_PATH = ($pluginDirs -join [IO.Path]::PathSeparator)
    Write-Step "TASK_GRAPH_PLUGINS_PATH: $env:TASK_GRAPH_PLUGINS_PATH"
}

# ---- run unit tests ----
if ($Test) {
    Write-Step "Running GraphStudio unit tests"
    $env:QT_QPA_PLATFORM = "offscreen"
    if (-not (Test-Path $Env.Ctest)) {
        $Env.Ctest = if ($Env.Cmake) { $Env.Cmake -replace 'cmake\.exe$', 'ctest.exe' } else { "ctest" }
    }
    Push-Location $GsBuild
    try { $Code = Invoke-Native $Env.Ctest @("-C", $Config, "--output-on-failure"); exit $Code }
    finally { Pop-Location }
}

# ---- build only ----
if ($BuildOnly) {
    Write-Ok "Build complete (--build-only)"
    exit 0
}

# ---- locate + launch ----
$Bin = Join-Path $GsBuild "$Config\graph_studio.exe"
if (-not (Test-Path $Bin)) {
    Write-Fail "GraphStudio binary not found: $Bin"
    exit 1
}

Write-Step "Launching GraphStudio"
& $Bin
exit $LASTEXITCODE