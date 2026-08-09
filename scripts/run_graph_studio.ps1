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
         TASK_GRAPH_PLUGINS_PATH (each plugin's Debug output dir), then
         launches graph_studio.exe.

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
$RootDir = Split-Path -Parent $ScriptDir
$GsDir = Join-Path $RootDir "app\graph_studio"
$GsBuild = Join-Path $GsDir "build"
$LibBuild = Join-Path $RootDir "build"

$UseColor = $false
try { $UseColor = [Console]::IsOutputRedirected -eq $false -and $env:NO_COLOR -ne "1" } catch { }
$Esc = [char]27
$C_Red = if ($UseColor) { "$Esc[31m" } else { "" }
$C_Green = if ($UseColor) { "$Esc[32m" } else { "" }
$C_Bold = if ($UseColor) { "$Esc[1m" } else { "" }
$C_Reset = if ($UseColor) { "$Esc[0m" } else { "" }

function Write-Step([string]$msg) { Write-Host "${C_Bold}==> $msg${C_Reset}" }
function Write-Fail([string]$msg) { Write-Host "${C_Red}==> $msg${C_Reset}" }
function Write-Ok([string]$msg) { Write-Host "${C_Green}${C_Bold}==> $msg${C_Reset}" }

# Run a native exe without letting stderr trips EA=Stop; returns exit code.
function Invoke-Native {
    param([string]$FilePath, [string[]]$Arguments)
    $OldPref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $Output = & $FilePath @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $ErrorActionPreference = $OldPref
    $Output | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) { Write-Host $_.ToString() }
        else { Write-Host $_ }
    }
    $ExitCode
}

# ---- locate cmake/ctest ----
function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vsRoots = @("${env:ProgramFiles}\Microsoft Visual Studio\2022", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022")
    foreach ($root in $vsRoots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($edition in (Get-ChildItem $root -Directory)) {
            $cand = Join-Path $edition.FullName "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$name.exe"
            if (Test-Path $cand) { return $cand }
        }
    }
    return $null
}

$CmakeExe = if ($Cmake) { $Cmake } else { Find-Tool "cmake" }
if (-not $CmakeExe -or -not (Test-Path $CmakeExe)) {
    Write-Fail "cmake not found. Install CMake or pass -Cmake <path>."
    exit 1
}
$CtestExe = if (Split-Path -Parent $CmakeExe) { Join-Path (Split-Path -Parent $CmakeExe) "ctest.exe" } else { "ctest" }

# ---- detect Qt6 prefix ----
if (-not $Qt) {
    if ($env:QT_PREFIX_PATH -and (Test-Path $env:QT_PREFIX_PATH)) {
        $Qt = $env:QT_PREFIX_PATH
    }
    elseif (Test-Path "C:\Qt") {
        $best = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        if ($best) {
            $cand = Join-Path $best.FullName "msvc2022_64"
            if (Test-Path $cand) { $Qt = $cand }
        }
    }
}
if (-not $Qt -or -not (Test-Path (Join-Path $Qt "lib\cmake\Qt6"))) {
    Write-Fail "Qt6 not found. Pass -Qt <prefix> (a directory containing lib\cmake\Qt6)."
    exit 1
}
Write-Step "Qt6 prefix: $Qt"

# ---- default jobs ----
if (-not $Jobs) {
    try { $Jobs = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors }
    catch { $Jobs = $env:NUMBER_OF_PROCESSORS }
    if (-not $Jobs) { $Jobs = 4 }
}
$Jobs = [int]$Jobs

# ---- clean ----
if ($Clean) {
    Write-Step "Cleaning GraphStudio build directory"
    if (Test-Path $GsBuild) { Remove-Item -Recurse -Force $GsBuild }
}

# ---- locate OpenCV for the root library configure ----
if (-not $DisableOpenCv -and -not $OpenCvDir) {
    foreach ($cand in @("C:\opencv\build\x64\vc16", "${env:OPENCV_DIR}")) {
        if ($cand -and (Test-Path $cand)) { $OpenCvDir = $cand; break }
    }
}

if (-not $NoBuild) {
    # ---- 1) root library + subnode plugins into <root>/build ----
    Write-Step "Building task_graph library + subnode plugins"
    $TgArgs = @("-S", $RootDir, "-B", $LibBuild)
    if ($DisableOpenCv) { $TgArgs += "-DTASK_GRAPH_ENABLE_OPENCV=OFF" }
    elseif ($OpenCvDir) { $TgArgs += "-DOpenCV_DIR=$(Join-Path $OpenCvDir 'lib')" }
    # GpuBootstrap.cpp 在 Win32/Linux 引用 VulkanGpuBackend（对应 macOS 的 Metal），
    # 需启用 Vulkan 才有 Vulkan backend 的实现符号。
    $TgArgs += "-DTASK_GRAPH_ENABLE_VULKAN=ON"
    $Code = Invoke-Native $CmakeExe $TgArgs
    if ($Code -ne 0) { exit $Code }
    $Code = Invoke-Native $CmakeExe @("--build", $LibBuild, "--config", $Config, "-j", "$Jobs")
    if ($Code -ne 0) { exit $Code }

    # multi-config VS generator puts task_graph.lib in build\<Config>; the app
    # CMakeLists uses link_directories(<root>/build), so mirror the lib up.
    $LibSrc = Join-Path $LibBuild "$Config\task_graph.lib"
    $LibDst = Join-Path $LibBuild "task_graph.lib"
    if (Test-Path $LibSrc) {
        Copy-Item $LibSrc $LibDst -Force
    } else {
        Write-Fail "task_graph.lib not found at $LibSrc (link may fail)."
    }

    # ---- 2) configure + build graph_studio ----
    $GsArgs = @("-S", $GsDir, "-B", $GsBuild, "-DCMAKE_PREFIX_PATH=$Qt")
    if (-not $DisableOpenCv -and $OpenCvDir) { $GsArgs += "-DOpenCV_DIR=$(Join-Path $OpenCvDir 'lib')" }
    Write-Step "Configuring graph_studio"
    $Code = Invoke-Native $CmakeExe $GsArgs
    if ($Code -ne 0) { exit $Code }

    Write-Step "Building graph_studio (-j $Jobs, --config $Config)"
    $Code = Invoke-Native $CmakeExe @("--build", $GsBuild, "--config", $Config, "-j", "$Jobs")
    if ($Code -ne 0) { exit $Code }
}
else {
    if (-not (Test-Path "$GsBuild\$Config\graph_studio.exe") -and -not (Test-Path $GsBuild)) {
        Write-Fail "Build directory $GsBuild does not exist (and --no-build was given)."
        exit 1
    }
}

# ---- runtime environment for both tests and launch ----
$qtBin = Join-Path $Qt "bin"
if (Test-Path $qtBin) { $env:PATH = $qtBin + [IO.Path]::PathSeparator + $env:PATH }

$opencvBin = ""
if (-not $DisableOpenCv) {
    $opencvBin = Join-Path $OpenCvDir "bin"
    if (Test-Path $opencvBin) { $env:PATH = $opencvBin + [IO.Path]::PathSeparator + $env:PATH }
}

$libBin = Join-Path $LibBuild $Config
if (Test-Path $libBin) { $env:PATH = $libBin + [IO.Path]::PathSeparator + $env:PATH }

# Windows multi-config generator drops plugin DLLs into each plugin dir's
# <Config> subfolder. Point the app at them via TASK_GRAPH_PLUGINS_PATH.
$pluginDirs = Get-ChildItem (Join-Path $LibBuild "submodules") -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName $Config } |
    Where-Object { Test-Path (Join-Path $_ "*.dll") }
if ($pluginDirs) {
    $env:TASK_GRAPH_PLUGINS_PATH = ($pluginDirs -join [IO.Path]::PathSeparator)
    Write-Step "TASK_GRAPH_PLUGINS_PATH: $env:TASK_GRAPH_PLUGINS_PATH"
}

# ---- run unit tests ----
if ($Test) {
    Write-Step "Running GraphStudio unit tests"
    $env:QT_QPA_PLATFORM = "offscreen"
    if (-not $CtestExe -or -not (Test-Path $CtestExe)) {
        $CtestExe = if ($Cmake) { $Cmake -replace 'cmake\.exe$', 'ctest.exe' } else { "ctest" }
    }
    Push-Location $GsBuild
    try { $Code = Invoke-Native $CtestExe @("-C", $Config, "--output-on-failure"); exit $Code }
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