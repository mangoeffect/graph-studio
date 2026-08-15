---
title: "Building the GraphStudio Installers from Source"
date: 2026-08-15T10:00:00+08:00
tags: ["Build", "Release"]
categories: ["Tutorials"]
summary: "The three packaging commands (package_macos.py / package_linux.py / build_msix.ps1), their dependencies and outputs, and the four-channel versioning of the GitHub Actions release pipeline."
showToc: true
---

Official installers are available from the [download page]({{< relref "download" >}}), but sometimes you need to roll your own: you changed the code, you want to embed your own plugins, or you simply want to verify a pull request. Here is the full packaging story for all three platforms.

## Prerequisites

| Dependency | Notes |
|---|---|
| CMake ≥ 3.18 | All platforms |
| C++20 compiler | Xcode / MSVC 2022 / gcc 11+ |
| Qt6 | `brew install qt` (macOS); `aqt` or the online installer on Windows |
| OpenCV | Optional (`TASK_GRAPH_ENABLE_OPENCV`, REQUIRED when ON — the default) |
| Packaging tools | macOS: `brew install dylibbundler create-dmg`; Linux: `linuxdeployqt` + `appimagetool` (auto-downloaded); Windows: Windows SDK (`makeappx` etc.) |

## macOS: `.dmg`

```bash
python scripts/package_macos.py --version 0.1.0 --out-dir dist/dmg
# Output: dist/dmg/graph_studio-0.1.0-macos.dmg
```

Pipeline: CMake build → `macdeployqt` collects Qt dependencies → `dylibbundler` folds OpenCV and other third-party libs into `Contents/Frameworks/` → `create-dmg` produces the image with an Applications symlink.

## Linux: `.AppImage`

```bash
python scripts/package_linux.py --version 0.1.0 --out-dir dist/appimage
# Output: dist/appimage/graph_studio-0.1.0-x86_64.AppImage
```

Pipeline: AppDir + `.desktop` + icon → `linuxdeployqt` → `appimagetool` (which falls back to extraction mode automatically on FUSE-less CI).

## Windows: `.msix`

```powershell
scripts\build_msix.ps1 -Version 0.1.0 -Config RelWithDebInfo -SkipSign
# Output: dist\msix\graph_studio-0.1.0_x64.msix
```

Pipeline: `windeployqt` → `makepri` resource index → `makeappx`. `-SkipSign` produces a Store-style unsigned package; Partner Center re-signs it when you publish to the Microsoft Store.

## The CI release pipeline

`.github/workflows/release.yml` orchestrates all of the above into a manually-triggered pipeline (`workflow_dispatch`):

1. **Version**: the base version is parsed from `project(task_graph VERSION x.y.z)` in the root `CMakeLists.txt`;
2. **Channel**: dispatch chooses alpha / beta / hotfix / stable, which becomes the tag suffix plus the run number — e.g. `v0.1.0-stable.42`; only **stable** releases are not flagged as pre-releases;
3. The three platforms **package in parallel**, then converge into a single GitHub Release with all three installers attached and release notes auto-generated;
4. The release event rebuilds the website automatically — the [download page]({{< relref "download" >}}) and the [changelog]({{< relref "changelog" >}}) pick up the new version right after.

## Signing and distribution notes

- **macOS**: local artifacts are unsigned and un-notarized; users need right-click → *Open* on first launch (see the [download page]({{< relref "download" >}})). For distribution, set up Apple Developer ID signing + notarization;
- **Windows**: unsigned `.msix` requires Developer mode; for distribution use a code-signing certificate or the Microsoft Store;
- The installers do **not** bundle the Vulkan runtime loader: `libtask_graph.dll` delay-loads `vulkan-1.dll` with a failure hook, so on machines without drivers the GPU backend degrades gracefully and the app still starts.
