<#
.SYNOPSIS
    build_plugin_standalone.ps1 - Build a single task_graph plugin as a runtime DLL on Windows.

.DESCRIPTION
    Windows counterpart of scripts/build_plugin_standalone.sh. Compiles a plugin
    source directory against an already-installed task_graph SDK prefix only
    (no reference to the main repo sources), producing a runtime-loadable DLL.

    Uses the VS multi-config generator: --config is passed to the build step,
    and the product lands in <OutRoot>\<name>\<Config>\<name>.dll.

.PARAMETER SourceDir
    Plugin source directory (must contain a CMakeLists.txt). Positional.

.PARAMETER SdkDir
    task_graph SDK prefix produced by build_sdk.ps1 (default: <root>\build\sdk).

.PARAMETER OutRoot
    Output root for standalone plugin builds (default: <root>\build\standalone\plugins).

.PARAMETER Config
    Build configuration (default: Release).

.PARAMETER Jobs
    Parallel compile jobs (default: logical CPU count).

.PARAMETER EnableOpenCv
    Configure with -DTASK_GRAPH_ENABLE_OPENCV=ON (default OFF). Set this for
    plugins that depend on OpenCV (e.g. submodules\opencv\**).

.PARAMETER Clean
    Delete the plugin's build directory first. Use this if a previous configure
    left a conflicting cache (e.g. a different SDK prefix or OpenCV setting).

.PARAMETER Cmake
    Path to cmake.exe (default: cmake on PATH, else the Visual Studio copy).

.EXAMPLE
    scripts\build_sdk.ps1
    scripts\build_plugin_standalone.ps1 examples\plugins\demo
    scripts\build_plugin_standalone.ps1 submodules\opencv\image_processing\image_filtering -EnableOpenCv

.NOTES
    After this succeeds, the product DLL is loadable via PluginLoader at runtime
    (see tests\test_plugin_abi.cpp). To make test_plugin_abi pick it up, set:
        $env:TASK_GRAPH_DEMO_PLUGIN = "<OutRoot>\<name>\<Config>\demo_plugin.dll"
    Exit code 0 = success; non-zero = configure/build error.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$SourceDir,
    [string]$SdkDir = "",
    [string]$OutRoot = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Jobs = "",
    [switch]$EnableOpenCv,
    [switch]$Clean,
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

if (-not $SdkDir)  { $SdkDir  = Join-Path $RootDir "build\sdk" }
if (-not $OutRoot) { $OutRoot = Join-Path $RootDir "build\standalone\plugins" }

# Resolve SourceDir to absolute (relative to cwd, matching the .sh `cd "$SRC_DIR" && pwd`).
if (-not ([IO.Path]::IsPathRooted($SourceDir))) { $SourceDir = Join-Path (Get-Location) $SourceDir }

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

# ---- validate inputs ----
if (-not (Test-Path (Join-Path $SourceDir "CMakeLists.txt"))) {
    Write-Fail "No CMakeLists.txt found under: $SourceDir"
    exit 1
}
$CfgFile = Join-Path $SdkDir "lib\cmake\task_graph\task_graphConfig.cmake"
if (-not (Test-Path $CfgFile)) {
    Write-Fail "SDK package not found: $CfgFile"
    Write-Host  "  Run scripts\build_sdk.ps1 first." -ForegroundColor Yellow
    exit 1
}

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

$Name   = Split-Path -Leaf $SourceDir
$OutDir = Join-Path $OutRoot $Name
$OpenCvFlag = if ($EnableOpenCv) { "ON" } else { "OFF" }

# ---- clean ----
if ($Clean) {
    Write-Step "Cleaning plugin build directory $OutDir"
    if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
}

# ---- configure ----
Write-Step "Configuring plugin $Name (task_graph_DIR=$CfgFile)"
$CmakeArgs = @(
    "-S", $SourceDir,
    "-B", $OutDir,
    "-Dtask_graph_DIR=$(Join-Path $SdkDir 'lib\cmake\task_graph')",
    "-DTASK_GRAPH_ENABLE_OPENCV=$OpenCvFlag"
)
$Code = Invoke-Native $CmakeExe $CmakeArgs
if ($Code -ne 0) { Write-Fail "Configure failed (exit $Code)"; exit $Code }

# ---- build ----
Write-Step "Building $Name (-j $Jobs, --config $Config)"
$Code = Invoke-Native $CmakeExe @("--build", $OutDir, "--config", $Config, "-j", "$Jobs")
if ($Code -ne 0) { Write-Fail "Build failed (exit $Code)"; exit $Code }

# ---- product location (VS multi-config nests under <Config>) ----
$ProductDir = Join-Path $OutDir $Config
$Product    = Join-Path $ProductDir "$Name.dll"

Write-Host ""
if (Test-Path $Product) {
    Write-Ok "Plugin built: $Product"
} else {
    # Fallback: single-config generators put the DLL directly in OutDir.
    $AltProduct = Join-Path $OutDir "$Name.dll"
    if (Test-Path $AltProduct) {
        $Product = $AltProduct
        Write-Ok "Plugin built: $Product"
    } else {
        Write-Ok "Build output dir: $OutDir"
        Write-Host "  (expected $Name.dll under $ProductDir or $OutDir; check the build log if absent)" -ForegroundColor Yellow
    }
}
Write-Host "  Loadable at runtime via PluginLoader (see tests\test_plugin_abi.cpp)."
if ($Name -eq "demo_plugin") {
    Write-Host "  To let test_plugin_abi pick it up:"
    Write-Host "    `$env:TASK_GRAPH_DEMO_PLUGIN = `"$Product`""
}
