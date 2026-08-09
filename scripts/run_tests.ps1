<#
.SYNOPSIS
    run_tests.ps1 - Configure, build and run all task_graph unit tests on Windows.

.DESCRIPTION
    Windows counterpart of scripts/run_tests.sh. Uses the VS multi-config
    generator (no -DCMAKE_BUILD_TYPE), builds with `--config <Config>` and
    runs ctest with `-C <Config>`. Prepends the build output dir and the
    OpenCV runtime bin to PATH so test executables find their DLLs.

.PARAMETER BuildDir
    Build directory (default: build). Note: GraphStudio's PluginBootstrap and
    app CMakeLists hardcode the root "build" tree, so use the default here if
    you also run scripts/run_graph_studio.ps1.

.PARAMETER Config
    Build configuration (default: Debug).

.PARAMETER Jobs
    Parallel compile jobs (default: logical CPU count).

.PARAMETER Clean
    Delete the build directory first.

.PARAMETER NoBuild
    Skip configure/build; just run the existing binaries with ctest.

.PARAMETER List
    List available tests and exit without running them.

.PARAMETER Verbose
    Verbose ctest output (--output-on-failure is on by default).

.PARAMETER Filter
    Only run tests whose name matches this regex (ctest -R).

.PARAMETER DisableOpenCv
    Configure with -DTASK_GRAPH_ENABLE_OPENCV=OFF (for machines without OpenCV).

.PARAMETER OpenCvDir
    OpenCV install prefix (default: auto-detect C:\opencv\build\x64\vc16).

.PARAMETER Cmake
    Path to cmake.exe (default: cmake on PATH, else the Visual Studio copy).

.PARAMETER Sdk
    Before running tests, build the SDK prefix (scripts\build_sdk.ps1) and the
    standalone demo plugin (scripts\build_plugin_standalone.ps1), then point
    TASK_GRAPH_DEMO_PLUGIN at the product so test_plugin_abi can load it.
    Counterpart of run_tests.sh --sdk. SDK/plugin build with their default
    Release config regardless of -Config.

.EXAMPLE
    scripts\run_tests.ps1
    scripts\run_tests.ps1 -Filter port -Verbose
    scripts\run_tests.ps1 -Clean -DisableOpenCv
    scripts\run_tests.ps1 -Sdk

.NOTES
    Exit code 0 = all tests passed; non-zero = failure or build error.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",
    [string]$Jobs = "",
    [switch]$Clean,
    [switch]$NoBuild,
    [switch]$List,
    [string]$Filter = "",
    [switch]$DisableOpenCv,
    [string]$OpenCvDir = "",
    [string]$Cmake = "",
    [switch]$Sdk,
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

# Make BuildDir absolute so relative PATH entries resolve regardless of cwd.
$BuildDir = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $RootDir $BuildDir }

# ---- colors (only when attached to a console) ----
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
if (-not $CmakeExe) {
    # Last resort: reuse the one recorded in an existing build cache.
    $cache = Join-Path $RootDir "$BuildDir\CMakeCache.txt"
    if (Test-Path $cache) {
        $m = Select-String -Path $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.*)$'
        if ($m) { $CmakeExe = $m.Matches[0].Groups[1].Value }
    }
}
if (-not $CmakeExe -or -not (Test-Path $CmakeExe)) {
    Write-Fail "cmake not found. Install CMake or pass -Cmake <path>."
    exit 1
}
$CtestExe = if (Split-Path -Parent $CmakeExe) { Join-Path (Split-Path -Parent $CmakeExe) "ctest.exe" } else { "ctest" }

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

# ---- clean ----
if ($Clean) {
    Write-Step "Cleaning build directory $BuildDir"
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
}

# ---- locate OpenCV (used at configure time and for the runtime PATH) ----
if (-not $DisableOpenCv -and -not $OpenCvDir) {
    foreach ($cand in @("C:\opencv\build\x64\vc16", "${env:OPENCV_DIR}")) {
        if ($cand -and (Test-Path $cand)) { $OpenCvDir = $cand; break }
    }
}

# ---- configure + build ----
if (-not $NoBuild) {
    $CmakeArgs = @("-S", $RootDir, "-B", $BuildDir)
    if ($DisableOpenCv) {
        $CmakeArgs += "-DTASK_GRAPH_ENABLE_OPENCV=OFF"
    }
    elseif ($OpenCvDir) {
        $CmakeArgs += "-DOpenCV_DIR=$(Join-Path $OpenCvDir 'lib')"
    }
    Write-Step "Configuring (cmake $($CmakeArgs -join ' '))"
    $Code = Invoke-Native $CmakeExe $CmakeArgs
    if ($Code -ne 0) { exit $Code }

    Write-Step "Building (-j $Jobs, --config $Config)"
    $Code = Invoke-Native $CmakeExe @("--build", $BuildDir, "--config", $Config, "-j", "$Jobs")
    if ($Code -ne 0) { exit $Code }
}
else {
    if (-not (Test-Path $BuildDir)) {
        Write-Fail "Build directory $BuildDir does not exist (and --no-build was given)."
        exit 1
    }
}

# ---- standalone plugins (-Sdk): build SDK prefix + standalone demo plugin so
#      test_plugin_abi can dlopen it at runtime. Counterpart of run_tests.sh --sdk.
#      build_sdk.ps1 / build_plugin_standalone.ps1 default to Release, so the
#      product lands in build\standalone\plugins\demo\Release\demo_plugin.dll
#      regardless of this script's -Config. -Sdk is independent of -NoBuild. ----
if ($Sdk) {
    $BuildSdk = Join-Path $ScriptDir "build_sdk.ps1"
    Write-Step "Building SDK prefix ($BuildSdk)"
    $SdkArgs = @("-Jobs", "$Jobs")
    if ($DisableOpenCv) { $SdkArgs += "-DisableOpenCv" }
    elseif ($OpenCvDir) { $SdkArgs += "-OpenCvDir", $OpenCvDir }
    & $BuildSdk @SdkArgs
    if ($LASTEXITCODE -ne 0) { Write-Fail "build_sdk.ps1 failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

    $BuildPlugin = Join-Path $ScriptDir "build_plugin_standalone.ps1"
    $DemoSrc = Join-Path $RootDir "examples\plugins\demo"
    Write-Step "Building standalone demo plugin ($BuildPlugin)"
    & $BuildPlugin $DemoSrc -Jobs $Jobs
    if ($LASTEXITCODE -ne 0) { Write-Fail "build_plugin_standalone.ps1 failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

    $DemoPlugin = Join-Path $RootDir "build\standalone\plugins\demo\Release\demo_plugin.dll"
    if (Test-Path $DemoPlugin) {
        $env:TASK_GRAPH_DEMO_PLUGIN = $DemoPlugin
        Write-Step "TASK_GRAPH_DEMO_PLUGIN: $DemoPlugin"
    } else {
        Write-Host "Warning: demo plugin not found at $DemoPlugin (test_plugin_abi will soft-skip)." -ForegroundColor Yellow
    }
}

# ---- list tests ----
if ($List) {
    Write-Step "Available tests:"
    Push-Location $BuildDir
    try { $Code = Invoke-Native $CtestExe @("-N"); exit $Code } finally { Pop-Location }
}

# ---- PATH for test executables ----
$BinDir = Join-Path $BuildDir $Config
if (Test-Path $BinDir) {
    $env:PATH = $BinDir + [IO.Path]::PathSeparator + $env:PATH
}
if (-not $DisableOpenCv) {
    $opencvBin = Join-Path $OpenCvDir "bin"
    if (Test-Path $opencvBin) {
        $env:PATH = $opencvBin + [IO.Path]::PathSeparator + $env:PATH
    }
    elseif ($OpenCvDir) {
        Write-Host "Warning: OpenCV bin dir not found at $opencvBin" -ForegroundColor Yellow
    }
}

# ---- run tests ----
$CtestArgs = @("--output-on-failure", "-C", $Config)
if ($Filter) { $CtestArgs += "-R", $Filter }
if ($PSBoundParameters.ContainsKey('Verbose')) { $CtestArgs += "-V" }

Write-Step "Running tests (ctest $($CtestArgs -join ' '))"
Push-Location $BuildDir
try {
    $Status = Invoke-Native $CtestExe $CtestArgs
} finally {
    Pop-Location
}

Write-Host ""
if ($Status -eq 0) { Write-Ok "All tests passed" }
else { Write-Fail "Some tests failed (exit $Status)" }
exit $Status
