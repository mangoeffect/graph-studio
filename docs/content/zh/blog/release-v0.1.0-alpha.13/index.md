---
title: "v0.1.0-alpha.13 发布：Web 版全面可用，视觉任务与工程包"
date: 2026-09-17T20:00:00+08:00
tags: ["发布", "GraphStudio", "WASM"]
categories: ["发布说明"]
summary: "本次测试版让浏览器里的 GraphStudio 第一次真正可用：拖拽、右键菜单、文件夹整树拖入全部打通，人脸检测与人像抠像任务携默认模型开箱即用，并引入 .tgp 单文件工程包与 wgpu 默认 GPU 后端。"
showToc: true
---

距离上个测试版 [v0.1.0-alpha.8](https://github.com/mangoeffect/graph-studio/releases/tag/v0.1.0-alpha.8) 一个月。这一版的主线是把「浏览器里的 GraphStudio」从能打开变成能用，同时补齐视觉类任务和工程分享能力。

## 获取

- **在线体验（无需安装）**：[在线版 GraphStudio](https://studio.mangoeffect.net/web/)，支持直接拖入 `graph.json`、整个 `graphs/` 文件夹或 `.tgp` 工程包；
- **桌面安装包**：[下载页]({{< relref "download" >}})（macOS `.dmg` / Windows `.msix` / Linux `.AppImage`）。

## Web 版（WASM）全面可用

上个版本的 Web 端能打开但不好用——右键菜单弹不出来、任务库拖拽失效、拖入 `graph.json` 后图片路径读不出。这些在本版全部修复：

- **右键菜单**：`QMenu::exec()` 在 Qt 6.6 WASM 上不可用，改为非阻塞 `popup()`，桌面与 Web 同一代码路径（顺带新增画布节点右键删除）；
- **任务库拖拽**：重写为应用级事件过滤器方案，绕开 WASM 平台 `grabMouse` 无效的限制；
- **文件/文件夹拖入**：松散 `graph.json` 会按图内引用自动配对资产；拖入整个文件夹（如子模块的 `tests/graphs/`）会按原始相对结构落位，配套 `data/` 资产直接命中；缺失资产会在应用内日志面板给出告警，而不是默默失败；
- **URL 打开**：`?open=<graph.json 的 URL>&run=1` 冷启动直接跑图，方便分享一键可运行的图；
- **模型随包**：face/matting 的默认模型打进 Web 包，视觉任务在浏览器里开箱即用。

## 新任务：人脸检测与人像抠像

- **`face_detect`**——默认输出人脸框，`output_landmarks=true` 时叠加 478 点关键点；mediapipe / mnn 双后端，`auto` 模式自动降级；
- **`matting`**——人像抠像，一路输入三路输出：`out`（alpha 掩码）、`mask`（灰度图）、`cutout`（背景透明的抠出图）；同样双后端。

两者在节点面板里归入新的 **Vision** 分类，模型参数（`model_path` 等）按后端自动显隐，默认模型随桌面安装包与 Web 包附带，零配置可用。

## .tgp 工程包：一个文件分享完整工程

浏览器拿不到你磁盘上的兄弟目录资产，单独发一个 `graph.json` 给别人总是缺文件。新的 **工程包（`.tgp`）** 把图 JSON 与全部相对路径依赖打包成单个 ZIP 文件：工具栏 Open、拖拽、URL 打开全通道支持，桌面同样受益（分享/归档）。工程态下 Save/Save As 会转为导出新包，保证不丢原始资产。

## wgpu 成为默认 GPU 后端

GPU 计算链路全面迁移到 **wgpu**：WGSL 单源（一套 shader 走 Metal / Vulkan / D3D12），compute 侧 17 个算子全部完成移植并默认走 wgpu；Metal / Vulkan 手写后端保留作回退与基准（`TG_GPU_BACKEND` 可强制切换）。渲染侧同样补齐 WGSL 单源，并新增一批滤镜原语任务：`render_lut`（HALD/stripe 布局自动识别）、`render_lut_cube`（`.cube` 文件转换）、可分离高斯/盒滤波、Sobel/Scharr/Laplacian、形态学膨胀/腐蚀、USM 锐化等，复合滤镜用 `render_pipeline` 的 `pass{i}_*` 参数即可编排。

## 引擎与脚本能力编入核心库

MNN 推理引擎、MediaPipe 十项视觉任务（`mp_*`）、QuickJS 脚本引擎（`js_script` 任务）不再以独立插件仓库分发，直接编入核心库——安装包结构更简单，也不再需要为这些能力单独准备插件。渲染效果同时支持 JS 生命周期脚本（`onInit` / `onParamChange` / `onBeforeRender` / `onAfterRender` / `onRelease`）。

## 其他改进

- **图像查看器重写**：纯 QPainter 实现（无原生窗口/GL 依赖），桌面与 Web 同一代码路径；GPU 驻留结果改为选中时按需同步回 CPU，避免大图全量回读；
- **资产路径解析**：读入型任务的资产引用按「图所在目录 + 两级祖先」探测，源码树里的测试图现在可以直接拖进 GraphStudio 运行；
- **稳定性**：修复 Windows MSVC Debug 构建链接错误（LNK2019）与 Linux 上 GPU 图测试进程退出期的段错误；
- **开发者**：纯 C API（`tg_sdk_c.h`）、TaskGraphSdk 生命周期 API、DagConfig 只读配置 API、统一 ModelFinder 模型解析，以及覆盖桌面 / WASM / macOS / Android 的五轨道自动化测试。

## 完整变更清单

见 [GitHub Release v0.1.0-alpha.13](https://github.com/mangoeffect/graph-studio/releases/tag/v0.1.0-alpha.13)。

---

在线体验直达：[studio.mangoeffect.net/web](https://studio.mangoeffect.net/web/)。遇到问题欢迎提 [issue](https://github.com/mangoeffect/graph-studio/issues)。
