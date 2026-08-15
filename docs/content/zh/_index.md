---
title: "task_graph · GraphStudio"
heroBadge: "C++20 · 跨平台 · 插件化"
heroTitle: "轻量级、跨平台的 C++20 DAG 任务执行框架"
heroSubtitle: "把流水线描述成带类型、端口相连的任务图，自动进行拓扑并行执行，并可序列化为 JSON 随处加载 —— 附带 Qt6 可视化编辑器 GraphStudio。"
platforms: ["macOS", "Windows", "Linux", "iOS", "Android", "WASM"]
graphStudio:
  title: "GraphStudio —— 可视化计算图编辑器"
  desc: "不需要写代码也能搭建并运行计算图：在画布上拖拽任务节点、连接端口、配置参数，然后一键运行并观察每个任务的执行状态与耗时。"
  bullets:
    - "拖拽节点、连线端口，可视化构建计算图"
    - "一键运行，实时查看每个任务的状态与耗时"
    - "图保存为 JSON，与 C++ 侧 `DAGSerializer` 完全互通"
    - "内置 OpenCV / GPU / JS 脚本 / MediaPipe 插件任务"
  tutorialLabel: "阅读上手教程"
features:
  - icon: "🔗"
    title: "带类型的端口任务"
    desc: "任务通过命名端口以 `std::any` 交换数据，同一任务对之间支持多端口边。"
  - icon: "⚡"
    title: "自动拓扑并行"
    desc: "线程池执行器按依赖关系自动调度，按任务返回状态与耗时。"
  - icon: "📝"
    title: "JSON 图格式"
    desc: "图即数据：`DAGSerializer::from_string` 一行加载为可运行的 DAG（版本化格式 v2.0）。"
  - icon: "🧩"
    title: "插件系统"
    desc: "编译期链接的子模块 + 运行时 `PluginLoader`（dlopen + SDK 版本校验），两种扩展方式任选。"
  - icon: "🚀"
    title: "GPU 后端"
    desc: "Metal / Vulkan / CUDA 全部 opt-in、默认关闭，不拖累无 GPU 环境的构建与运行。"
  - icon: "🖼️"
    title: "内置子模块"
    desc: "OpenCV 图像读写与滤波、GPU 图像算子、QuickJS 脚本、MediaPipe 视觉（10 个任务）。"
  - icon: "🖥️"
    title: "GraphStudio 编辑器"
    desc: "Qt6 桌面端可视化编辑器，构建、运行、调试计算图一站式完成。"
  - icon: "🌍"
    title: "全平台"
    desc: "桌面构建 SHARED 库，iOS / Android / WASM 构建 STATIC 库，同一套图定义跨端复用。"
---

## 十行代码跑起来 {#quick-start}

```cpp
#include <task_graph/task_graph.hpp>

using namespace task_graph;

int main() {
    DAG dag;

    auto fetch = std::make_shared<Task>("fetch", [](TaskContext& ctx) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = std::string("user_123")};
    });
    auto process = std::make_shared<Task>("process", [](TaskContext& ctx) {
        auto data = ctx.input<std::string>("in");
        return TaskResult{.status = TaskStatus::COMPLETED,
                          .value = std::string(*data + "_processed")};
    });

    dag.add_task(fetch);
    dag.add_task(process);
    dag.connect("fetch", "process");               // out -> in

    DAGExecutor executor;
    executor.execute(dag).wait();
}
```

## 图即数据 {#json-graph}

同一张图也可以用 JSON 描述，交给 `DAGSerializer::from_string` 加载、`DAGExecutor` 执行 —— GraphStudio 保存和加载的就是这种格式：

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

## 深入了解

- 📖 完整文档见 [GitHub 仓库 README](https://github.com/mangoeffect/graph-studio)（含构建选项、插件开发、GPU 后端说明）
- 🚀 从零开始搭建第一张图，见博客[《快速上手 GraphStudio》]({{< relref "blog/quick-start-graphstudio" >}})
- 📦 想自己出安装包，见[《从源码构建三端安装包》]({{< relref "blog/build-installers-from-source" >}})
