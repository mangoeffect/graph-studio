---
title: "task_graph 架构概览：任务、端口与执行器"
date: 2026-08-08T10:00:00+08:00
tags: ["架构", "框架"]
categories: ["教程"]
summary: "typed port、std::any 数据流、线程池拓扑执行、双层插件模型、JSON v2.0 序列化与 opt-in GPU 后端 —— 五分钟看懂 task_graph 的设计。"
showToc: true
---

task_graph 的核心只有几个概念，但组合起来足以覆盖从图像流水线到推理管路的大部分场景。本文按数据流动的顺序过一遍。

## Task 与 DAG

一切从 `Task` 开始：它有一个 ID、一个执行体（lambda 或子类化的 `INode`），以及最重要的 —— **端口契约**：

```cpp
auto process = std::make_shared<Task>("process", [](TaskContext& ctx) {
    auto data = ctx.input<std::string>("in");   // 从上游端口读数据
    return TaskResult{.status = TaskStatus::COMPLETED,
                      .value = std::string(*data + "_processed")};
});
```

`DAG` 是任务的容器：`add_task` 注册，`connect("fetch", "process")` 建边（默认接 `out → in` 端口，也支持端口限定的多端口边）。

## 端口：类型化的数据流

- 任务间通过**命名端口**交换数据，载荷是 `std::any`；
- 读上游输出用 `ctx.input<T>("port")`；
- 返回结果放 `TaskResult.value`（默认输出端口 `"out"`），或通过 `outputs` 一次写多个端口；
- 跨动态库使用的自定义类型必须注册：`TG_REGISTER_TYPE(MyType, "my::Type")` —— 稳定的字符串名保证跨 SO 边界的类型一致性。

## 执行器：拓扑并行

`DAGExecutor` 内置线程池，按依赖关系自动调度：无依赖关系的分支并行跑，有依赖的串行等待。执行完成后每个任务都能查到状态与耗时：

```cpp
DAGExecutor executor;
executor.execute(dag).wait();
for (auto& [id, r] : executor.get_results())
    std::cout << id << ": " << (r.is_success() ? "SUCCESS" : "FAILED") << "\n";
```

任务失败不会让整图崩溃：下游被跳过，失败原因随 `TaskResult` 带回（WASM/移动端以 `-fno-exceptions` 构建，插件边界一律不抛异常、只返回状态）。

## 插件模型：编译期 + 运行时

两种扩展方式，共享同一套 `INode` 接口（`type()` / `execute()` / `input_specs()` / `output_specs()` / `param_specs()`）：

| 方式 | 机制 | 适合 |
|---|---|---|
| **子模块（subnode）** | 编译期链接，`subnode.json` + `cmake/Subnode.cmake` 接入 | 官方插件（OpenCV、GPU、JS、MediaPipe） |
| **动态插件** | 运行时 `PluginLoader` dlopen，导出 `register_plugin`，SDK 版本不匹配会拒绝加载 | 第三方分发、按需装载 |

新插件不用手写脚手架：`python scripts/generate_submodule.py` 一键生成 CMakeLists、任务类与双重注册代码。

## JSON 序列化（v2.0）

图是带版本号的纯数据（见[快速上手]({{< relref "blog/quick-start-graphstudio" >}})中的示例）：任务数组 + 边数组，边可带 `from_port` / `to_port`。`DAGSerializer::from_string` 一行加载，GraphStudio 编辑器读写的也是同一格式。

## GPU 后端：全部 opt-in

Metal（Apple）/ Vulkan / CUDA 三个后端由 CMake 开关控制（`TASK_GRAPH_ENABLE_METAL/VULKAN/CUDA`，默认全关）：需要 GPU 的任务在无后端环境优雅降级，测试在 CI 上自动 soft-skip。GPU 子模块提供 `gpu_box_blur`、`gpu_gaussian_blur`、`gpu_resize` 等图像算子。

## 跨平台形态

- **桌面**：`libtask_graph` 为 SHARED 库，支持 dlopen 动态插件；
- **iOS / Android / WASM**：STATIC 库 + `-fno-exceptions`，同一套图定义与任务代码复用。

## 小结

「typed port + 拓扑并行 + JSON 图 + 双层插件」是 task_graph 的全部骨架。想动手跑一遍？从[快速上手 GraphStudio]({{< relref "blog/quick-start-graphstudio" >}})开始；想出包分发？看[从源码构建三端安装包]({{< relref "blog/build-installers-from-source" >}})。
