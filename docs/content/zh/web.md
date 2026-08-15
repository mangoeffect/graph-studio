---
title: 在线体验
description: 在浏览器中直接运行 GraphStudio（WebAssembly 版），无需安装任何东西。
layout: web
---

GraphStudio 被完整编译为 WebAssembly（多线程），可以在浏览器里直接运行。

**首次加载约 16 MB**（wasm 主模块 + Qt 运行时），请耐心等待；之后浏览器缓存会显著加快。

- 打开后页面**自动刷新一次**属正常现象：Web 版通过 service worker 启用跨域隔离
  （SharedArrayBuffer，多线程 wasm 的必要条件），刷新后即进入隔离上下文。
- 无痕/隐私窗口等禁用 service worker 的环境下 Web 版可能无法启动，请改用桌面版。
- 图像处理子节点（OpenCV）与 JS 脚本节点均已内置，可以直接搭建计算图运行；
  结果图像面板在 Web 版为静态展示（桌面版是 GPU 加速视图）。
- 重度使用建议[下载桌面安装包]({{< relref "download" >}})，性能与稳定性更佳。
