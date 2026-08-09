<#
.SYNOPSIS
    build_sdk.ps1 - Build the task_graph framework as a distributable SDK on Windows.

.DESCRIPTION
    Windows counterpart of scripts/build_sdk.sh. Installs headers + the shared
    library + the CMake package config into a prefix, so standalone plugins can
    be compiled against it via find_package(task_graph).

    Builds with TASK_GRAPH_BUILD_SUBMODULES=OFF: the main repo compiles none of
    its built-in submodule sources.

    Uses the VS multi-config generator (no -DCMAKE_BUILD_TYPE); --config is
    passed to both the build and the install steps.

.PARAMETER Prefix
    SDK install prefix (default: <root>\build\sdk).

.PARAMETER BuildDir
    CMake build directory (default: <root>\build\sdk-build).

.PARAMETER Config
    Build configuration (default: Release).

.PARAMETER Jobs
    Parallel compile jobs (default: logical CPU count).

.PARAMETER Clean
    Delete the build directory first. Strongly recommended if the build directory
    was previously used for a non-SDK build (e.g. by run_tests.ps1): a cached
    TASK_GRAPH_BUILD_SUBMODULES=ON silently overrides our OFF, which breaks the
    SDK configure step.

.PARAMETER DisableOpenCv
    Configure with -DTASK_GRAPH_ENABLE_OPENCV=OFF (OpenCV defaults ON, matching
    the main project).

.PARAMETER OpenCvDir
    OpenCV install prefix (default: auto-detect C:\opencv\build\x64\vc16 or
    $OPENCV_DIR). Passed to CMake as OpenCV_DIR.

.PARAMETER Cmake
    Path to cmake.exe (default: cmake on PATH, else the Visual Studio copy).

.EXAMPLE
    scripts\build_sdk.ps1
    scripts\build_sdk.ps1 -Prefix D:\sdks\task_graph -Config RelWithDebInfo
    scripts\build_sdk.ps1 -DisableOpenCv

.NOTES
    After this succeeds, standalone plugins are configured with:
        -Dtask_graph_DIR=<Prefix>\lib\cmake\task_graph
    (see scripts\build_plugin_standalone.ps1).

    Exit code 0 = success; non-zero = configure/build/install error.
#>
[CmdletBinding()]
param(
    [string]$Prefix = "",
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Jobs = "",
    [switch]$Clean,
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

# ---- locate repo root (script lives in <root>/scripts/) ----
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir

if (-not $Prefix)   { $Prefix   = Join-Path $RootDir "build\sdk" }
if (-not $BuildDir) { $BuildDir = Join-Path $RootDir "build\sdk-build" }
# Resolve relatives to absolute (cwd-independent), matching run_tests.ps1.
$Prefix   = if ([IO.Path]::IsPathRooted($Prefix))   { $Prefix }   else { Join-Path (Get-Location) $Prefix }
$BuildDir = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path (Get-Location) $BuildDir }

# ---- colors (only when attached to a console) ----
$UseColor = $false
try { $UseColor = [Console]::IsOutputRedirected -eq $false -and $env:NO_COLOR -ne "1" } catch { }
$Esc = [char]27
$C_Red   = if ($UseColor) { "$Esc[31m" } else { "" }
$C_Green = if ($UseColor) { "$Esc[32m" } else { "" }
$C_Bold  = if ($UseColor) { "$Esc[1m"  } else { "" }
$C_Reset = if ($UseColor) { "$Esc[0m"  } else { "" }

function Write-Step([string]$msg) { Write-Host "${C_Bold}==> $msg${C_Reset}" }
function Write-Fail([string]$msg) { Write-Host "${C_Red}==> $msg${C_Reset}" }
function Write-Ok([string]$msg)   { Write-Host "${C_Green}${C_Bold}==> $msg${C_Reset}" }

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

# ---- locate cmake ----
function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Fall back to the cmake shipped inside Visual Studio 2022.
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

Write-Step "cmake: $CmakeExe"

# ---- default jobs = logical CPU count ----
if (-not $Jobs) {
    try {
        $Jobs = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    } catch {
        $Jobs = $env:NUMBER_OF_PROCESSORS
    }
    if (-not $Jobs) { $Jobs = 4 }
}
$Jobs = [int]$Jobs

# ---- locate OpenCV (only when enabled) ----
$OpenCvCmakeArg = @()
$OpenCvFlag = if ($DisableOpenCv) { "OFF" } else { "ON" }
if (-not $DisableOpenCv) {
    if (-not $OpenCvDir) {
        foreach ($cand in @("C:\opencv\build\x64\vc16", "${env:OPENCV_DIR}")) {
            if ($cand -and (Test-Path $cand)) { $OpenCvDir = $cand; break }
        }
    }
    if ($OpenCvDir) {
        $OpenCvCmakeArg = @("-DOpenCV_DIR=$(Join-Path $OpenCvDir 'lib')")
    }
}

# ---- clean / stale-cache guard ----
# SDK 构建必须 TASK_GRAPH_BUILD_SUBMODULES=OFF。若复用了一个之前跑过完整构建
# （如 run_tests.ps1）的目录，CMakeCache.txt 里缓存的 ...SUBMODULES=ON 会静默
# 覆盖命令行的 OFF（CMake 布尔缓存：只有 ON 或同名新变量才会覆盖），导致子模块
# 被加载、其 find_package(task_graph) 找到构建目录里残缺的 Config 文件而报错。
# 检测到冲突就要求 -Clean。
# 注：不用 Select-String —— 它在 Windows PowerShell 5.1 下对空路径会抛参数绑定
# 异常（即便配了 -ErrorAction SilentlyContinue）。改用 Get-Content + 字符串匹配。
$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if ((-not $Clean) -and (Test-Path $CacheFile)) {
    $stale = $false
    foreach ($line in (Get-Content $CacheFile -ErrorAction SilentlyContinue)) {
        if ($line -eq "TASK_GRAPH_BUILD_SUBMODULES:BOOL=ON") { $stale = $true; break }
    }
    if ($stale) {
        Write-Fail "Build directory already has a non-SDK cache (TASK_GRAPH_BUILD_SUBMODULES=ON):"
        Write-Host   "  $CacheFile" -ForegroundColor Yellow
        Write-Host   "  This silently overrides our OFF and breaks the SDK configure step." -ForegroundColor Yellow
        Write-Host   "  Re-run with -Clean, or use a fresh build dir (-BuildDir <path>)." -ForegroundColor Yellow
        exit 1
    }
}
if ($Clean) {
    Write-Step "Cleaning build directory $BuildDir"
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
}

# ---- configure ----
Write-Step "Configuring (standalone build, no built-in submodules)"
$CmakeArgs = @(
    "-S", $RootDir,
    "-B", $BuildDir,
    "-DTASK_GRAPH_BUILD_SUBMODULES=OFF",
    "-DTASK_GRAPH_ENABLE_OPENCV=$OpenCvFlag",
    "-DCMAKE_INSTALL_PREFIX=$Prefix"
) + $OpenCvCmakeArg

$Code = Invoke-Native $CmakeExe $CmakeArgs
if ($Code -ne 0) { Write-Fail "Configure failed (exit $Code)"; exit $Code }

# ---- build ----
Write-Step "Building libtask_graph (-j $Jobs, --config $Config)"
$Code = Invoke-Native $CmakeExe @("--build", $BuildDir, "--target", "task_graph", "--config", $Config, "-j", "$Jobs")
if ($Code -ne 0) { Write-Fail "Build failed (exit $Code)"; exit $Code }

# ---- install ----
Write-Step "Installing SDK to $Prefix (--config $Config)"
$Code = Invoke-Native $CmakeExe @("--install", $BuildDir, "--config", $Config)
if ($Code -ne 0) { Write-Fail "Install failed (exit $Code)"; exit $Code }

Write-Host ""
Write-Ok "SDK ready: $Prefix"
$CfgDir = Join-Path $Prefix "lib\cmake\task_graph"
Write-Host "  Standalone plugin configure flag:"
Write-Host "    -Dtask_graph_DIR=$CfgDir"
Write-Host "  (see scripts\build_plugin_standalone.ps1)"
