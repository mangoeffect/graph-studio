<#
.SYNOPSIS
    build_msi.ps1 - Build + package GraphStudio into an unsigned MSI installer
    for non-Store distribution (GitHub Releases, direct download).

.DESCRIPTION
    Builds the task_graph stack (or reuses the existing build with -SkipBuild),
    stages the app layout via the shared Stage-GsAppLayout helper (identical
    file set to the MSIX package: exe + task_graph.dll + PlugIns\ + models\
    + windeployqt output), adds the VC++ runtime DLLs app-local, renders
    app\graph_studio\packaging\msi-product.wxs.in, harvests the staged file
    tree into a WiX fragment and packs everything into one .msi with the
    WiX Toolset v4+ CLI (`wix build`; the `wix` dotnet tool is auto-installed
    when missing, pinned by -WixVersion).

    The MSI is intentionally NOT Authenticode-signed: unsigned MSI packages
    install fine outside the Store — users click through the SmartScreen
    "Windows protected your PC" prompt (More info -> Run anyway) and the
    "Unknown publisher" UAC prompt, and no publisher-certificate (.cer) trust
    step is needed (unlike the self-signed .msix). Add signing later by
    running signtool on the produced .msi if reputation ever demands it.

    Result layout ($OutDir):
      graph_studio-<version>_x64.msi        — unsigned installer (per-machine,
                                              Program Files, start-menu
                                              shortcut, major-upgrade aware)
      graph_studio-<version>_x64.msi.sha256 — checksum for the Release page

    （<version> 原样保留渠道字样；MSI ProductVersion 用归一化数字四段，见
    ConvertTo-NumericVersion。UpgradeCode 固定于 msi-product.wxs.in，run 号
    编进 revision —— 版本更高的 MSI 安装时自动卸载旧版，跨渠道单调递增。）

.PARAMETER Version
    App version (default 0.1.0). Plain x.y.z expands to four parts with a
    trailing 0; a channel version like 0.1.0-alpha.42 becomes the numeric quad
    0.1.0.42 for the MSI ProductVersion (the file name keeps the channel
    string).

.PARAMETER Config / Jobs / Clean / SkipBuild / SkipSentry / SkipModels / SentryDsn / SentryRelease
    与 build_msix.ps1 同义。-SkipBuild 直接复用现有构建产物（CI 里 msix 步骤
    刚构建过同一棵树时用）。

.PARAMETER DisplayName / Manufacturer / Description
    安装界面与"应用和功能"里显示的名称/发布者/描述。

.PARAMETER WixVersion
    WiX Toolset dotnet tool 版本钉（仅自动安装时使用），默认 6.0.2。

.PARAMETER Qt / OpenCvDir / DisableOpenCv / Cmake / OutDir / Help
    同 build_msix.ps1。

.EXAMPLE
    scripts\build_msi.ps1 -SkipBuild              # package the current build tree
    scripts\build_msi.ps1 -Version 0.2.0-beta.3
    scripts\build_msi.ps1 -SkipModels             # smaller test installer
.note
    Requires .NET (SDK/runtime, for the wix dotnet tool; auto-installed),
    VS2022 (VC\Redist for app-local CRT DLLs — warn-only when absent),
    Qt + OpenCV + the built stack as in build_msix.ps1.
#>
[CmdletBinding()]
param(
    [string]$Version = "0.1.0",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo",
    [string]$Jobs = "",
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$SkipSentry,
    [switch]$SkipModels,
    [string]$SentryDsn = "",
    [string]$SentryRelease = "",
    [string]$DisplayName = "Graph Studio",
    [string]$Manufacturer = "GraphStudio Publisher",
    [string]$Description = "Visual DAG editor and task execution framework built on task_graph.",
    [string]$WixVersion = "6.0.2",
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
$GsDir = Join-Path $RootDir "app\graph_studio"
$PackagingDir = Join-Path $GsDir "packaging"
if (-not $OutDir) { $OutDir = Join-Path $RootDir "dist\msi" }
$OutDir = (New-Item -ItemType Directory -Force -Path $OutDir).FullName

$Arch = "x64"
$PackageBaseName = "graph_studio-$Version`_$Arch"

# MSI ProductVersion 只接受数字四段，归一化规则与 AppxManifest 一致
# （run 号进 revision；文件名保留渠道字样）。
$VersionQuad = ConvertTo-NumericVersion $Version
if ($VersionQuad -ne $Version) {
    Write-Step "Version '$Version' -> MSI ProductVersion '$VersionQuad'（清单须数字四段，文件名保留原样）"
}

# ---- resolve toolchain ----
$Env = Resolve-GsEnv -Qt $Qt -OpenCvDir $OpenCvDir -DisableOpenCv:$DisableOpenCv -Cmake $Cmake
if (-not $Jobs) { $Jobs = Get-DefaultJobs }
$Jobs = [int]$Jobs

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
}
if (-not $SkipBuild -and $SentryRelease) {
    $SentryDefines += "-DGRAPH_STUDIO_SENTRY_VERSION=$SentryRelease"
}
if (-not $SkipBuild) {
    $SentryDefines += "-DGRAPH_STUDIO_ENABLE_JSON_EXPORT=OFF"
}
if (-not $SkipBuild) {
    $Build = Build-GraphStudioStack -Env $Env -Config $Config -Jobs $Jobs -Clean:$Clean -AppDefines $SentryDefines
} else {
    # -SkipBuild 也要把 wgpu_native.dll 进包（CI 里 msix 步骤刚完整构建过，
    # 预构建产物在固定安装路径；缺失时为空 → GpuBootstrap 降级 Vulkan）
    $WgpuDll = Join-Path $RootDir "build\wgpu\install\windows-x86_64\lib\wgpu_native.dll"
    $Build = @{
        RootDir  = $RootDir
        LibBuild = Join-Path $RootDir "build"
        GsDir    = $GsDir
        GsBuild  = Join-Path $GsDir "build"
        WgpuDll  = $(if (Test-Path $WgpuDll) { $WgpuDll } else { $null })
    }
}

# ---- 2) stage package layout（与 build_msix.ps1 共享）----
$Staging = Join-Path $OutDir "staging"
Stage-GsAppLayout -Staging $Staging -Build $Build -Env $Env -Config $Config -SkipModels:$SkipModels

# ---- 3) VC++ 运行库 app-local（MSIX 链路依赖目标机已装 vc_redist；MSI 直接随包 ----
# 拷贝 Microsoft.VC143.CRT 的全部 DLL 到 exe 同级，干净机器开箱即用）。
$VsRoots = @("${env:ProgramFiles}\Microsoft Visual Studio\2022",
             "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022")
$CrtDir = $null
foreach ($vsRoot in $VsRoots) {
    if (-not (Test-Path $vsRoot) -or $CrtDir) { continue }
    foreach ($edition in (Get-ChildItem $vsRoot -Directory -ErrorAction SilentlyContinue)) {
        $redist = Join-Path $edition.FullName "VC\Redist\MSVC"
        if (-not (Test-Path $redist)) { continue }
        $versioned = Get-ChildItem $redist -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+(\.\d+)*$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($ver in $versioned) {
            $cand = Join-Path $ver.FullName "x64\Microsoft.VC143.CRT"
            if (Test-Path $cand) { $CrtDir = $cand; break }
        }
        if ($CrtDir) { break }
    }
}
if ($CrtDir) {
    $crtDlls = @(Get-ChildItem $CrtDir -Filter "*.dll")
    foreach ($dll in $crtDlls) { Copy-Item $dll.FullName (Join-Path $Staging $dll.Name) -Force }
    Write-Step "VC++ runtime (app-local): $CrtDir -> staging root ($($crtDlls.Count) DLLs)"
} else {
    Write-Fail "未找到 VS2022 VC\Redist\MSVC\<ver>\x64\Microsoft.VC143.CRT；MSI 将不含 VC++ 运行库（目标机需已装 vc_redist），继续打包。"
}

# ---- 4) locate / install WiX (dotnet tool) ----
# wix 工具面向较新的 .NET；允许滚向前到本机已有的 runtime（如 net6 工具跑在
# .NET 7/8 上），避免"只装了新 SDK"的机器无法执行。
$env:DOTNET_ROLL_FORWARD = "LatestMajor"
$WixExe = (Get-Command "wix.exe" -ErrorAction SilentlyContinue).Source
if (-not $WixExe) {
    $cand = Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe"
    if (Test-Path $cand) { $WixExe = $cand }
}
if (-not $WixExe) {
    Write-Step "Installing WiX Toolset v$WixVersion (dotnet tool, one-time)"
    $Code = Invoke-Native "dotnet" @("tool", "install", "--global", "wix", "--version", $WixVersion)
    if ($Code -ne 0) {
        Write-Fail "dotnet tool install wix failed (需要 .NET SDK); 或手动安装后重试."
        exit $Code
    }
    # 刚安装时当前进程 PATH 不含 ~/.dotnet/tools，直接用绝对路径。
    $WixExe = Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe"
}
if (-not (Test-Path $WixExe)) { Write-Fail "wix.exe not found: $WixExe"; exit 1 }

# 收割 staging 文件树为 WiX 布局 fragment：每个文件一个组件（Guid="*" 由 WiX
# 按 keypath 生成稳定 GUID，升级时逐文件精准替换），根文件挂 INSTALLFOLDER、
# graph_studio.exe 上加开始菜单快捷方式，子目录生成对应 Directory 树。
function New-MsiLayoutWxs {
    param([string]$Staging, [string]$ShortcutName, [string]$Path)

    function Esc([string]$s) { [System.Security.SecurityElement]::Escape($s) }

    $dirIds = @{}
    $dirIndex = 0
    foreach ($d in (Get-ChildItem -Directory -Recurse -LiteralPath $Staging | Sort-Object FullName)) {
        $dirIds[$d.FullName] = "dir$dirIndex"; $dirIndex++
    }

    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$sb.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
    [void]$sb.AppendLine('  <Fragment>')
    [void]$sb.AppendLine('    <ComponentGroup Id="AppFiles">')

    $fileIndex = 0
    foreach ($f in (Get-ChildItem -File -Recurse -LiteralPath $Staging | Sort-Object FullName)) {
        $dirAttr = if ($f.DirectoryName -eq $Staging) { "INSTALLFOLDER" } else { $dirIds[$f.DirectoryName] }
        $source = Esc $f.FullName
        [void]$sb.AppendLine("      <Component Directory=`"$dirAttr`" Guid=`"*`">")
        if ($f.Name -ieq "graph_studio.exe") {
            [void]$sb.AppendLine("        <File Id=`"fil$fileIndex`" KeyPath=`"yes`" Source=`"$source`">")
            [void]$sb.AppendLine("          <Shortcut Id=`"StartMenuShortcut`" Directory=`"AppMenuFolder`" Name=`"$(Esc $ShortcutName)`" Advertise=`"yes`" WorkingDirectory=`"INSTALLFOLDER`" />")
            [void]$sb.AppendLine("        </File>")
        } else {
            [void]$sb.AppendLine("        <File Id=`"fil$fileIndex`" KeyPath=`"yes`" Source=`"$source`" />")
        }
        [void]$sb.AppendLine("      </Component>")
        $fileIndex++
    }

    [void]$sb.AppendLine('    </ComponentGroup>')
    [void]$sb.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')

    # 递归展开子目录树（组件按 Directory 属性引用叶目录 Id）
    function Emit-DirTree([System.IO.DirectoryInfo]$dir) {
        [void]$sb.AppendLine("      <Directory Id=`"$($dirIds[$dir.FullName])`" Name=`"$(Esc $dir.Name)`">")
        foreach ($sub in ($dir.GetDirectories() | Sort-Object FullName)) {
            Emit-DirTree $sub
        }
        [void]$sb.AppendLine("      </Directory>")
    }
    foreach ($top in (Get-ChildItem -Directory -LiteralPath $Staging | Sort-Object FullName)) {
        Emit-DirTree $top
    }

    [void]$sb.AppendLine('    </DirectoryRef>')
    [void]$sb.AppendLine('  </Fragment>')
    [void]$sb.AppendLine('</Wix>')
    [IO.File]::WriteAllText($Path, $sb.ToString())
    Write-Step "WiX layout: $Path ($fileIndex files, $($dirIds.Count) dirs)"
}

# ---- 5) render product.wxs + harvest layout.wxs ----
$WixDir = Join-Path $OutDir "wix"
New-Item -ItemType Directory -Force -Path $WixDir | Out-Null

$ProductWxs = Join-Path $WixDir "product.wxs"
$Wxs = Get-Content -Raw (Join-Path $PackagingDir "msi-product.wxs.in")
$Wxs = $Wxs.Replace("@GS_MSI_NAME@", $DisplayName)
$Wxs = $Wxs.Replace("@GS_MSI_MANUFACTURER@", $Manufacturer)
$Wxs = $Wxs.Replace("@GS_MSI_VERSION@", $VersionQuad)
$Wxs = $Wxs.Replace("@GS_MSI_DESCRIPTION@", $Description)
Set-Content -Path $ProductWxs -Value $Wxs -Encoding UTF8

$LayoutWxs = Join-Path $WixDir "layout.wxs"
New-MsiLayoutWxs -Staging $Staging -ShortcutName $DisplayName -Path $LayoutWxs

# ---- 6) pack .msi ----
Write-Step "Packing .msi (wix build)"
$MsiPath = Join-Path $OutDir "$PackageBaseName.msi"
if (Test-Path $MsiPath) { Remove-Item $MsiPath -Force }
$Code = Invoke-Native $WixExe @("build", "-arch", $Arch,
                                "-intermediatefolder", (Join-Path $WixDir "obj"),
                                "-out", $MsiPath, $ProductWxs, $LayoutWxs)
if ($Code -ne 0) { exit $Code }

# ---- 7) sha256（Release 页校验用，coreutils 格式）----
$Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $MsiPath).Hash.ToLowerInvariant()
$ShaPath = "$MsiPath.sha256"
Set-Content -Path $ShaPath -Value "$Hash  $(Split-Path -Leaf $MsiPath)" -Encoding ASCII

# ---- summary ----
Write-Ok "$MsiPath"
Write-Ok "$ShaPath"
Write-Step "未签名 MSI（非商店渠道设计）：首次安装 SmartScreen 提示『Windows 已保护你的电脑』属预期，"
Write-Step "  点『更多信息 -> 仍要运行』；UAC『未知发布者』点『是』。无需信任任何证书。"
Write-Step "校验: sha256sum '$(Split-Path -Leaf $MsiPath)'  # 应为 $Hash"
