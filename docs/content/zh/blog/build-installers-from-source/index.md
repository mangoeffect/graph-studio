---
title: "从源码构建 GraphStudio 三端安装包"
date: 2026-08-15T10:00:00+08:00
tags: ["构建", "发布"]
categories: ["教程"]
summary: "package_macos.py / package_linux.py / build_msix.ps1 三条打包命令、各自的依赖与产物路径，以及 GitHub Actions 发布流水线的四渠道版本规则。"
showToc: true
---

官方安装包可以从[下载页]({{< relref "download" >}})直接拿，但有时候你需要自己打包：改了代码、要嵌入自己的插件、或者单纯想验证一条 pull request。本文记录三端打包的完整姿势。

## 前置依赖

| 依赖 | 说明 |
|---|---|
| CMake ≥ 3.18 | 三端通用 |
| C++20 编译器 | Xcode / MSVC 2022 / gcc 11+ |
| Qt6 | `brew install qt`（macOS）；Windows 用 `aqt` 或官网安装器 |
| OpenCV | 可选（`TASK_GRAPH_ENABLE_OPENCV`，默认 ON 时 REQUIRED） |
| 平台打包工具 | macOS：`brew install dylibbundler create-dmg`；Linux：`linuxdeployqt` + `appimagetool`（脚本自动下载）；Windows：Windows SDK（`makeappx` 等） |

## macOS：`.dmg`

```bash
python scripts/package_macos.py --version 0.1.0 --out-dir dist/dmg
# 产物：dist/dmg/graph_studio-0.1.0-macos.dmg
```

流水线：CMake 构建 → `macdeployqt` 收集 Qt 依赖 → `dylibbundler` 把 OpenCV 等第三方库塞进 `Contents/Frameworks/` → `create-dmg` 打出带 Applications 快捷方式的镜像。

## Linux：`.AppImage`

```bash
python scripts/package_linux.py --version 0.1.0 --out-dir dist/appimage
# 产物：dist/appimage/graph_studio-0.1.0-x86_64.AppImage
```

流水线：AppDir + `.desktop` + 图标 → `linuxdeployqt` → `appimagetool`（CI 的无 FUSE 环境下自动走解压模式）。

## Windows：`.msix`

```powershell
scripts\build_msix.ps1 -Version 0.1.0 -Config RelWithDebInfo -SkipSign
# 产物：dist\msix\graph_studio-0.1.0_x64.msix
```

流水线：`windeployqt` → `makepri` 资源索引 → `makeappx` 打包。`-SkipSign` 产出 Store 风格未签名包；正式上架 Microsoft Store 时由 Partner Center 重签名。

## CI 发布流水线

`.github/workflows/release.yml` 把上面三步编排成手动触发的发布流水线（`workflow_dispatch`）：

1. **版本号**：基准版本取自根 `CMakeLists.txt` 的 `project(task_graph VERSION x.y.z)`；
2. **渠道**：发布时选择 测试版 / 预览版 / 修复版 / 正式版，对应 tag 后缀 `alpha` / `beta` / `hotfix` / `stable`，加上 run 号成 `v0.1.0-stable.42` 这样的完整标签；只有**正式版**不会被标记为 prerelease；
3. **三端并行打包**后汇总为一个 GitHub Release，附三件安装包，并自动生成 release notes；
4. Release 发布事件会触发官网自动重建 —— [下载页]({{< relref "download" >}})与[更新日志]({{< relref "changelog" >}})随后就能看到新版本。

## 签名与分发提示

- **macOS**：本地产物未签名未公证，用户首次打开需右键「打开」（详见[下载页安装说明]({{< relref "download" >}})）；对外分发建议配置 Apple Developer ID 签名 + 公证；
- **Windows**：未签名 `.msix` 需要开发者模式；对外分发建议代码签名证书，或走 Microsoft Store；
- 安装包内**不捆绑** Vulkan 运行时加载器：`libtask_graph.dll` 对 `vulkan-1.dll` 做了延迟加载并带失败兜底，无驱动机器上 GPU 后端自动降级、应用正常启动。
