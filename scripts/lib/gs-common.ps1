<#
.SYNOPSIS
    gs-common.ps1 - Shared helpers for the GraphStudio Windows PowerShell
    scripts (run_graph_studio.ps1, build_msix.ps1).

.DESCRIPTION
    Dot-source this file to reuse Qt/OpenCV/CMake detection, batch build of
    the task_graph stack, and console helpers:

        . "$PSScriptRoot\lib\gs-common.ps1"

    Importing it does NOT build anything; the top-level scripts own their
    orchestration. Requires Windows PowerShell 5.1+.
#>

$script:GsRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ---- console helpers -------------------------------------------------------

function Initialize-GsColors {
    $script:C_Bold   = ""
    $script:C_Red    = ""
    $script:C_Green  = ""
    $script:C_Reset  = ""
    try {
        if ([Console]::IsOutputRedirected -eq $false -and $env:NO_COLOR -ne "1") {
            $script:C_Bold   = "$([char]27)[1m"
            $script:C_Red    = "$([char]27)[31m"
            $script:C_Green  = "$([char]27)[32m"
            $script:C_Reset  = "$([char]27)[0m"
        }
    } catch { }
}
Initialize-GsColors

function Write-Step([string]$msg) { Write-Host "${C_Bold}==> $msg${C_Reset}" }
function Write-Fail([string]$msg) { Write-Host "${C_Red}==> $msg${C_Reset}" }
function Write-Ok([string]$msg)   { Write-Host "${C_Green}${C_Bold}==> $msg${C_Reset}" }

# Run a native exe without letting stderr trip $ErrorActionPreference=Stop;
# streams all output to the console and returns the exit code.
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

# ---- environment detection -------------------------------------------------

function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Inside VS-installed CMake's bin we usually find ctest/ctest too.
    $vsRoots = @("${env:ProgramFiles}\Microsoft Visual Studio\2022",
                 "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022")
    foreach ($root in $vsRoots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($edition in (Get-ChildItem $root -Directory)) {
            $cand = Join-Path $edition.FullName "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$name.exe"
            if (Test-Path $cand) { return $cand }
        }
    }
    return $null
}

# Find a tool hosted in the Windows SDK bin\<version>\x64 (makeappx, makepri,
# signtool, ...). Picks the newest installed SDK version.
function Find-SdkTool([string]$name) {
    $kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (-not (Test-Path "$kitsRoot")) { return $null }
    $best = Get-ChildItem $kitsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $best) { return $null }
    $cand = Join-Path $best.FullName "x64\$name.exe"
    if (Test-Path $cand) { return $cand }
    return $null
}

function Get-DefaultJobs {
    $jobs = ""
    try { $jobs = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors }
    catch { $jobs = $env:NUMBER_OF_PROCESSORS }
    if (-not $jobs) { $jobs = 4 }
    [int]$jobs
}

# Resolve the shared toolchain. Throws (via exit 1) and prints a message when
# a mandatory piece is missing. Returns a hashtable:
#   @{ Cmake; Ctest; Qt; OpenCvDir; DisableOpenCv }
function Resolve-GsEnv {
    param(
        [string]$Qt = "",
        [string]$OpenCvDir = "",
        [switch]$DisableOpenCv,
        [string]$Cmake = ""
    )
    $cmake = if ($Cmake) { $Cmake } else { Find-Tool "cmake" }
    if (-not $cmake -or -not (Test-Path $cmake)) {
        Write-Fail "cmake not found. Install CMake or pass -Cmake <path>."
        exit 1
    }
    $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"

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

    if (-not $DisableOpenCv -and -not $OpenCvDir) {
        foreach ($cand in @("C:\opencv\build\x64\vc16", "${env:OPENCV_DIR}")) {
            if ($cand -and (Test-Path $cand)) { $OpenCvDir = $cand; break }
        }
    }

    return @{
        Cmake        = $cmake
        Ctest        = $ctest
        Qt           = $Qt
        OpenCvDir    = $OpenCvDir
        DisableOpenCv = [bool]$DisableOpenCv
    }
}

# ---- builds ----------------------------------------------------------------

# Configure + build the root task_graph library with its subnode plugins into
# <root>/build, mirror task_graph.lib up (multi-config VS generator quirk),
# then configure + build graph_studio into app/graph_studio/build.
# Returns a hashtable with resolved directories:
#   @{ RootDir; LibBuild; GsDir; GsBuild }
function Build-GraphStudioStack {
    param(
        [hashtable]$Env,
        [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
        [string]$Config = "Debug",
        [int]$Jobs = 8,
        [switch]$Clean,
        [switch]$SkipApp
    )
    $RootDir  = $script:GsRoot
    $LibBuild = Join-Path $RootDir "build"
    $GsDir    = Join-Path $RootDir "app\graph_studio"
    $GsBuild  = Join-Path $GsDir "build"

    if ($Clean) {
        Write-Step "Cleaning GraphStudio build directory"
        if (Test-Path $GsBuild) { Remove-Item -Recurse -Force $GsBuild }
    }

    Write-Step "Building task_graph library + subnode plugins"
    $TgArgs = @("-S", $RootDir, "-B", $LibBuild)
    if ($Env.DisableOpenCv) { $TgArgs += "-DTASK_GRAPH_ENABLE_OPENCV=OFF" }
    elseif ($Env.OpenCvDir) { $TgArgs += "-DOpenCV_DIR=$(Join-Path $Env.OpenCvDir 'lib')" }
    # GpuBootstrap.cpp needs the Vulkan backend symbols on desktop Win32.
    $TgArgs += "-DTASK_GRAPH_ENABLE_VULKAN=ON"
    $Code = Invoke-Native $Env.Cmake $TgArgs
    if ($Code -ne 0) { exit $Code }
    $Code = Invoke-Native $Env.Cmake @("--build", $LibBuild, "--config", $Config, "-j", "$Jobs")
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

    if (-not $SkipApp) {
        $GsArgs = @("-S", $GsDir, "-B", $GsBuild, "-DCMAKE_PREFIX_PATH=$($Env.Qt)")
        if (-not $Env.DisableOpenCv -and $Env.OpenCvDir) {
            $GsArgs += "-DOpenCV_DIR=$(Join-Path $Env.OpenCvDir 'lib')"
        }
        Write-Step "Configuring graph_studio"
        $Code = Invoke-Native $Env.Cmake $GsArgs
        if ($Code -ne 0) { exit $Code }

        Write-Step "Building graph_studio (-j $Jobs, --config $Config)"
        $Code = Invoke-Native $Env.Cmake @("--build", $GsBuild, "--config", $Config, "-j", "$Jobs")
        if ($Code -ne 0) { exit $Code }
    }

    return @{
        RootDir   = $RootDir
        LibBuild  = $LibBuild
        GsDir     = $GsDir
        GsBuild   = $GsBuild
    }
}

# ---- runtime layout helpers ------------------------------------------------

# Prepend Qt/OpenCV bin dirs to PATH for running tests / the app.
function Add-GsRuntimePath {
    param([hashtable]$Env)
    $qtBin = Join-Path $Env.Qt "bin"
    if (Test-Path $qtBin) { $env:PATH = $qtBin + [IO.Path]::PathSeparator + $env:PATH }
    if (-not $Env.DisableOpenCv -and $Env.OpenCvDir) {
        $opencvBin = Join-Path $Env.OpenCvDir "bin"
        if (Test-Path $opencvBin) { $env:PATH = $opencvBin + [IO.Path]::PathSeparator + $env:PATH }
    }
}

# Multi-config generators drop plugin DLLs into each plugin dir's <Config>
# subfolder; return those directories that actually contain DLLs.
function Get-GsPluginDirs {
    param(
        [string]$LibBuild,
        [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
        [string]$Config = "Debug"
    )
    Get-ChildItem (Join-Path $LibBuild "submodules") -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName $Config } |
        Where-Object { Test-Path (Join-Path $_ "*.dll") }
}