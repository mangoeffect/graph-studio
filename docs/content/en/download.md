---
title: "Download GraphStudio"
description: "GraphStudio installers: macOS .dmg, Windows .msix, and Linux .AppImage, published via GitHub Releases."
layout: downloads
buildPostTitle: "Build from source"
---

## Installation notes

### macOS (`.dmg`)

1. Download the `.dmg`, open it, and drag **GraphStudio** into `Applications`;
2. The current builds are unsigned and not notarized, so Gatekeeper may block the first launch: right-click the app in Finder and choose *Open*, or allow it under *System Settings → Privacy & Security*.

### Windows (`.msix`)

1. Download the `.msix` and double-click to install (Windows 10 1809+ required);
2. The package is currently unsigned: enable **Developer mode** under *Settings → System → For developers* first, or trust-sign the package yourself;
3. Once published to the Microsoft Store, packages will be signed and distributed by the Store.

### Linux (`.AppImage`)

```bash
chmod +x graph_studio-*-x86_64.AppImage
./graph_studio-*-x86_64.AppImage
```

> AppImages need FUSE (shipped by most distributions); on FUSE-less systems run `--appimage-extract` and launch the extracted AppRun.

## Release channels

The release pipeline offers four channels: **alpha** / **beta** / **hotfix** / **stable**. This page highlights the latest **stable** release; the other channels are distinguishable by tag on [GitHub Releases](https://github.com/mangoeffect/graph-studio/releases) (e.g. `v0.1.0-beta.42`).

## Build from source

Don't want to wait for a release? Producing your own installer is a one-liner — see [Building the three-platform installers from source]({{< relref "blog/build-installers-from-source" >}}) on the blog:

```bash
python scripts/package_macos.py --version 0.1.0   # macOS -> dist/dmg/*.dmg
python scripts/package_linux.py --version 0.1.0   # Linux -> dist/appimage/*.AppImage
```
