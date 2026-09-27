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

# ---- version helpers --------------------------------------------------------

# Normalize an app version into a strictly numeric four-part version. The MSIX
# manifest and the MSI ProductVersion both accept digits only: channel
# versions (release.yml's x.y.z-<channel>.<run>) drop the suffix and fold the
# GitHub run number into the revision quad (0.1.0-alpha.42 -> 0.1.0.42). Run
# numbers are globally unique and monotonically increasing, so version
# ordering holds across channels. File names keep the original channel string.
function ConvertTo-NumericVersion([string]$v) {
    $base = $v
    $rev = "0"
    if ($v -match '^(?<base>\d+(?:\.\d+){1,2})-(?<channel>[A-Za-z]+)\.(?<run>\d+)$') {
        $base = $Matches['base']
        $rev = $Matches['run']
    }
    if ($base -notmatch '^\d+(\.\d+)*$') {
        throw "无法把版本 '$v' 转为数字四段版本（支持 x.y.z 或渠道形式 x.y.z-<channel>.<run>）"
    }
    $parts = @($base.Split('.'))
    if ($parts.Count -lt 3) { $parts = $parts + @("0") * (3 - $parts.Count) }
    if ($parts.Count -gt 3) { $parts = $parts[0..2] }
    ($parts + $rev) -join "."
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
        [switch]$SkipApp,
        # Additional -D defines for the app (graph_studio) configure step only,
        # e.g. Sentry: -DGRAPH_STUDIO_SENTRY_DSN=... / -DGRAPH_STUDIO_SENTRY_VERSION=...
        [string[]]$AppDefines = @()
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

    # wgpu-native prebuild (default GPU backend; idempotent, pinned release).
    # Fetch failure only warns: the CMake probe then skips the wgpu backend and
    # GpuBootstrap falls back to Vulkan.
    Write-Step "Fetching wgpu-native (fetch_wgpu.py, idempotent)"
    $Code = Invoke-Native "python" @((Join-Path $RootDir "scripts\fetch_wgpu.py"))
    if ($Code -ne 0) {
        Write-Fail "wgpu-native fetch failed; wgpu backend disabled (fallback Vulkan)"
    }
    $WgpuInc = Join-Path $RootDir "build\wgpu\install\windows-x86_64\include"
    # 导入库 wgpu_native.dll.lib（wgpu_native.lib 是 Rust 静态库，链它需补
    # std 的系统导入库，且与 wgpu_native.dll 部署模型相悖——不用）。
    $WgpuLib = Join-Path $RootDir "build\wgpu\install\windows-x86_64\lib\wgpu_native.dll.lib"
    if (-not (Test-Path $WgpuLib)) {
        $WgpuLib = Join-Path $RootDir "build\wgpu\install\windows-x86_64\lib\wgpu_native.lib"
    }
    $WgpuDll = Join-Path $RootDir "build\wgpu\install\windows-x86_64\lib\wgpu_native.dll"
    $HasWgpu = (Test-Path (Join-Path $WgpuInc "webgpu\webgpu.h")) -and (Test-Path $WgpuLib)
    if ($HasWgpu) { $TgArgs += "-DTASK_GRAPH_ENABLE_WGPU=ON" }
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
        # The app is its own top-level project: pass the wgpu probe results
        # explicitly (GpuBootstrap defaults to the wgpu backend).
        if ($HasWgpu) {
            $GsArgs += "-DTASK_GRAPH_ENABLE_WGPU=ON"
            $GsArgs += "-DWGPU_INCLUDE_DIR=$WgpuInc"
            $GsArgs += "-DWGPU_LIBRARY=$WgpuLib"
        }
        if ($AppDefines) { $GsArgs += $AppDefines }
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
        # wgpu-native runtime (import-lib counterpart); packaging copies it next
        # to the exe. Empty when the prebuild fetch failed (wgpu disabled).
        WgpuDll   = $(if ($HasWgpu -and (Test-Path $WgpuDll)) { $WgpuDll } else { $null })
    }
}

# ---- packaging layout -------------------------------------------------------

# Stage the GraphStudio app file layout shared by the Windows packagers
# (build_msix.ps1 -> MSIX, build_msi.ps1 -> MSI): copy exe + task_graph.dll
# (+ wgpu / crashpad), OpenCV runtime DLLs, submodule plugins into PlugIns\,
# mediapipe vision.dll into the package root, bundle models, then run
# windeployqt on the staged exe. Creates $Staging fresh; exits when a
# required build artifact is missing.
function Stage-GsAppLayout {
    param(
        [string]$Staging,
        [hashtable]$Build,
        [hashtable]$Env,
        [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
        [string]$Config = "RelWithDebInfo",
        [switch]$SkipModels
    )
    $RootDir = $script:GsRoot
    $ScriptDir = Join-Path $RootDir "scripts"
    $IsDebug = ($Config -eq "Debug")

    $exeSource = Join-Path $Build.GsBuild "$Config\graph_studio.exe"
    $libDllSource = Join-Path $Build.LibBuild "$Config\task_graph.dll"
    foreach ($f in @($exeSource, $libDllSource)) {
        if (-not (Test-Path $f)) { Write-Fail "Missing build artifact: $f (run without -SkipBuild)."; exit 1 }
    }

    if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
    New-Item -ItemType Directory -Force -Path $Staging | Out-Null
    Write-Step "Staging package layout: $Staging"

    Copy-Item $exeSource    (Join-Path $Staging "graph_studio.exe")
    Copy-Item $libDllSource (Join-Path $Staging "task_graph.dll")

    # libwgpu_native.dll next to the exe (default GPU backend runtime; absent when
    # the prebuild fetch failed — the app then degrades to the Vulkan backend)
    if ($Build.WgpuDll) { Copy-Item $Build.WgpuDll (Join-Path $Staging "wgpu_native.dll") }

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
                # vision.dll 是 mediapipe_vision.dll 的依赖而非插件：其依赖解析走
                # loader 标准搜索（exe 目录优先），拷到包根（见下方）；进 PlugIns
                # 是 10MB 死重且永远加载不到。
                if ($dll.Name -eq "vision.dll") { continue }
                Copy-Item $dll.FullName (Join-Path $Staging "PlugIns\$($dll.Name)") -Force
            }
        }
    } else {
        Write-Fail "No submodule plugin DLLs found under $($Build.LibBuild)\submodules\<name>\$Config\."
        exit 1
    }

    # MediaPipe vision.dll -> 包根（exe 同级）。PlugIns\ 里的 mediapipe_vision.dll
    # 依赖它，而 loader 对依赖的搜索顺序是 exe 目录 → system → PATH，不搜索 PlugIns
    # 自身。install 缺失（stub 构建，如最小化 CI）时跳过，不阻断打包。
    $mpVision = Join-Path $Build.LibBuild "mediapipe\install\bin\vision.dll"
    if (Test-Path $mpVision) {
        Copy-Item $mpVision (Join-Path $Staging "vision.dll") -Force
        Write-Step "Bundled MediaPipe vision.dll -> $Staging\vision.dll"
    }

    # 模型文件 -> models\（exe 同级）。任务参数里只填模型名，ModelBootstrap
    #（<exe 目录>/models 布局）从这里查找。三集合：mediapipe（.task/.tflite，
    # mp 后端）+ face/matting（.mnn，mnn 后端）。缺模型先跑下载脚本（幂等、
    # 已存在即跳过）；下载失败则打包失败，-SkipModels 可跳过随包。
    if ($SkipModels) {
        Write-Step "Skipping bundled models (-SkipModels)"
    } else {
        $ModelSets = @(
            @{ Script = "download_mediapipe_models.py"; Dir = "tests\models\mediapipe";
               Exts = @(".task", ".tflite") },
            @{ Script = "download_face_models.py";     Dir = "tests\models\face";
               Exts = @(".mnn") },
            @{ Script = "download_matting_models.py";  Dir = "tests\models\matting";
               Exts = @(".mnn") }
        )
        $ModelsDst = Join-Path $Staging "models"
        New-Item -ItemType Directory -Force -Path $ModelsDst | Out-Null
        $Copied = 0
        foreach ($Set in $ModelSets) {
            $Code = Invoke-Native "python" @((Join-Path $ScriptDir $Set.Script))
            if ($Code -ne 0) {
                Write-Fail "Model download failed ($($Set.Script), network?). Retry, or pass -SkipModels."
                exit $Code
            }
            $ModelsSrc = Join-Path $RootDir $Set.Dir
            $ModelFiles = @(Get-ChildItem -File $ModelsSrc -ErrorAction SilentlyContinue |
                            Where-Object { $_.Extension -in $Set.Exts })
            if ($ModelFiles.Count -eq 0) {
                Write-Fail "No model files found under: $ModelsSrc"
                exit 1
            }
            $ModelFiles | ForEach-Object {
                Copy-Item $_.FullName (Join-Path $ModelsDst $_.Name) -Force
                $Copied++
            }
        }
        Write-Step "Bundled model files -> $ModelsDst ($Copied files)"
    }

    # order matters for windeployqt: run on the exe in the staging tree
    Write-Step "Running windeployqt (Qt $Config kit)"
    $Windeployqt = Join-Path $Env.Qt "bin\windeployqt.exe"
    $WdeployArgs = @(if ($IsDebug) { "--debug" } else { "--release" },
                     "--no-translations", "--compiler-runtime",
                     (Join-Path $Staging "graph_studio.exe"))
    $Code = Invoke-Native $Windeployqt $WdeployArgs
    if ($Code -ne 0) { exit $Code }
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