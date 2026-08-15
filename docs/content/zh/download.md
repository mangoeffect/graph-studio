---
title: "下载 GraphStudio"
description: "GraphStudio 安装包下载：macOS .dmg、Windows .msix、Linux .AppImage，来自 GitHub Releases。"
layout: downloads
buildPostTitle: "从源码构建"
---

## 安装说明

### macOS（`.dmg`）

1. 下载 `.dmg` 并打开，把 **GraphStudio** 拖入 `Applications` 文件夹；
2. 当前版本未做开发者签名与公证，首次打开可能被 Gatekeeper 拦截：在 Finder 中右键选择「打开」，或在「系统设置 → 隐私与安全性」中点击「仍要打开」。

### Windows（`.msix`）

1. 下载 `.msix` 双击安装（需要 Windows 10 1809+）；
2. 安装包当前未签名：请先在「设置 → 系统 → 开发者选项」中开启**开发者模式**，或对包做信任签名后再安装；
3. 正式上架 Microsoft Store 后将由 Store 统一签名分发。

### Linux（`.AppImage`）

```bash
chmod +x graph_studio-*-x86_64.AppImage
./graph_studio-*-x86_64.AppImage
```

> AppImage 需要 FUSE（多数发行版自带）；无 FUSE 环境可用 `--appimage-extract` 解压后运行。

## 版本渠道

发布流水线提供四种渠道：**测试版**（alpha）/ **预览版**（beta）/ **修复版**（hotfix）/ **正式版**（stable）。本页默认展示最新**正式版**；其余渠道可在 [GitHub Releases](https://github.com/mangoeffect/graph-studio/releases) 按标签区分（如 `v0.1.0-beta.42`）。

## 从源码构建

等不及发布？自己打包也只需一条命令，详见博客[《从源码构建 GraphStudio 三端安装包》]({{< relref "blog/build-installers-from-source" >}})：

```bash
python scripts/package_macos.py --version 0.1.0   # macOS -> dist/dmg/*.dmg
python scripts/package_linux.py --version 0.1.0   # Linux -> dist/appimage/*.AppImage
```
