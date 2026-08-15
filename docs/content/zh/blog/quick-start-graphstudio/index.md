---
title: "快速上手 GraphStudio：你的第一个计算图"
date: 2026-08-01T10:00:00+08:00
tags: ["入门", "GraphStudio"]
categories: ["教程"]
summary: "从安装到运行第一张图像处理计算图：拖几个节点、连几条线，理解 GraphStudio 的工作方式，以及图如何与 C++ / JSON 互通。"
showToc: true
---

[GraphStudio](https://github.com/mangoeffect/graph-studio) 是 task_graph 框架的 Qt6 桌面编辑器：把「写代码组装 DAG」变成「画布上拖拽连线」。本文带你从零跑起第一张图。

## 获取 GraphStudio

**方式一：直接下载安装包（推荐）**

到官网[下载页]({{< relref "download" >}})选择对应平台（macOS `.dmg` / Windows `.msix` / Linux `.AppImage`），内置全部官方插件任务。

**方式二：源码构建**

```bash
# 依赖：CMake >= 3.18、C++20 编译器、Qt6（macOS: brew install qt）
python scripts/run_graph_studio.py            # 构建并启动
python scripts/run_graph_studio.py --qt /path/to/qtbase   # Qt6 未被自动探测到时
```

> 注意：OpenCV / GPU / 脚本 / MediaPipe 四个插件子模块仓库目前为私有，随官方安装包一同分发；外部克隆者可以正常构建**核心框架**与示例，完整编辑器体验建议使用安装包。

## 认识界面

打开 GraphStudio 后你会看到三块区域：

- **左侧节点面板** —— 按子模块分组的任务类型（图像读写、滤波、GPU 算子、JS 脚本、MediaPipe 视觉……）；
- **中央画布** —— 计算图本体：节点代表任务，连线代表数据流向，线上标注端口名；
- **右侧属性面板** —— 选中节点的参数（如 `file_path`、`kernel_size`）、输入输出端口契约。

## 搭建第一张图

目标：读入一张图片 → 高斯模糊 → Sobel 边缘检测。

1. 从面板拖入 **opencv_image_read**，在属性面板把 `file_path` 指向一张本地图片；
2. 再拖入 **opencv_gaussian_blur_filter** 和 **opencv_sobel_filter**；
3. 依次连线：`image_read.out → blur.in`、`blur.out → sobel.in`（拖动端口圆点即可，端口名会自动提示）；
4. 点击**运行**。

运行结束后每个节点会显示执行状态与耗时；任务失败时错误信息直接标在节点上，方便定位是参数问题还是上游数据问题。

## 图即数据：与 C++ / JSON 互通

GraphStudio 保存的 `.json` 就是框架的图格式（`version: 2.0`）：

```json
{
  "version": "2.0",
  "tasks": [
    { "id": "src", "type": "opencv_image_read", "params": { "file_path": "test.png" } },
    { "id": "gaussian", "type": "opencv_gaussian_blur_filter" },
    { "id": "sobel", "type": "opencv_sobel_filter" }
  ],
  "edges": [
    { "from": "src", "from_port": "out", "to": "gaussian", "to_port": "in" },
    { "from": "gaussian", "from_port": "out", "to": "sobel", "to_port": "in" }
  ]
}
```

同一份 JSON 可以脱离编辑器，在 C++ 里直接加载运行：

```cpp
using namespace task_graph;

auto dag = DAGSerializer::from_string(json_string);
DAGExecutor executor;
executor.execute(*dag).wait();
```

也就是说：**编辑器里调通的原型，可以原样搬进生产代码**，不需要二次翻译。

## 下一步

- 想知道连线背后发生了什么？读[《task_graph 架构概览》]({{< relref "blog/task-graph-architecture" >}})；
- 想自己出安装包分发？读[《从源码构建三端安装包》]({{< relref "blog/build-installers-from-source" >}})。
