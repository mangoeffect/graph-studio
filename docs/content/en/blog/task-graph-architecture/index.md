---
title: "task_graph Architecture Overview: Tasks, Ports, and the Executor"
date: 2026-08-08T10:00:00+08:00
tags: ["Architecture", "Framework"]
categories: ["Tutorials"]
summary: "Typed ports, std::any data flow, thread-pool topological execution, the two-layer plugin model, JSON v2.0 serialization, and opt-in GPU backends — the whole design in five minutes."
showToc: true
---

The core of task_graph is a handful of concepts that compose into most pipeline shapes, from image processing to inference graphs. This post walks through them in data-flow order.

## Task and DAG

Everything starts with a `Task`: it has an ID, a body (a lambda or an `INode` subclass), and most importantly a **port contract**:

```cpp
auto process = std::make_shared<Task>("process", [](TaskContext& ctx) {
    auto data = ctx.input<std::string>("in");   // read from an upstream port
    return TaskResult{.status = TaskStatus::COMPLETED,
                      .value = std::string(*data + "_processed")};
});
```

A `DAG` holds tasks: `add_task` registers them, `connect("fetch", "process")` adds an edge (defaulting to the `out → in` ports; port-qualified multi-port edges are supported too).

## Ports: typed data flow

- Tasks exchange data over **named ports**, carried as `std::any`;
- Read upstream output with `ctx.input<T>("port")`;
- Return via `TaskResult.value` (default output port `"out"`), or write several ports at once through `outputs`;
- Custom types crossing dynamic-library boundaries must be registered: `TG_REGISTER_TYPE(MyType, "my::Type")` — the stable string name keeps types consistent across SO boundaries.

## The executor: topological parallelism

`DAGExecutor` runs a thread pool and schedules by dependency: independent branches run in parallel, dependent nodes wait. After execution every task reports status and duration:

```cpp
DAGExecutor executor;
executor.execute(dag).wait();
for (auto& [id, r] : executor.get_results())
    std::cout << id << ": " << (r.is_success() ? "SUCCESS" : "FAILED") << "\n";
```

A failing task doesn't crash the graph: downstream nodes are skipped and the failure reason travels back in the `TaskResult` (WASM/mobile builds use `-fno-exceptions` — plugin boundaries never throw, they return status).

## Plugin model: compile-time + runtime

Two extension styles share the same `INode` interface (`type()` / `execute()` / `input_specs()` / `output_specs()` / `param_specs()`):

| Style | Mechanism | Fit |
|---|---|---|
| **Subnodes** | Compile-time linked via `subnode.json` + `cmake/Subnode.cmake` | Official plugins (OpenCV, GPU, JS, MediaPipe) |
| **Dynamic plugins** | Runtime `PluginLoader` via dlopen; exports `register_plugin`; SDK-version mismatches are refused | Third-party distribution, load-on-demand |

New plugins don't need hand-written scaffolding: `python scripts/generate_submodule.py` generates the CMakeLists, task classes, and dual registration code.

## JSON serialization (v2.0)

Graphs are versioned pure data (see the sample in the [quick start]({{< relref "blog/quick-start-graphstudio" >}})): a task array plus an edge array, with optional `from_port` / `to_port` qualifiers. `DAGSerializer::from_string` loads one in a single call — and it's the exact format GraphStudio reads and writes.

## GPU backends: all opt-in

The Metal (Apple) / Vulkan / CUDA backends are behind CMake switches (`TASK_GRAPH_ENABLE_METAL/VULKAN/CUDA`, all OFF by default): GPU tasks degrade gracefully where no backend is available, and their tests soft-skip in GPU-less CI. The GPU subnode provides image ops like `gpu_box_blur`, `gpu_gaussian_blur`, and `gpu_resize`.

## Cross-platform shape

- **Desktop**: `libtask_graph` is a SHARED library and supports dlopen'ed plugins;
- **iOS / Android / WASM**: STATIC library + `-fno-exceptions`, with the same graph definitions and task code.

## Recap

"Typed ports + topological parallelism + JSON graphs + a two-layer plugin model" is the whole skeleton of task_graph. Want to run one? Start with the [GraphStudio quick start]({{< relref "blog/quick-start-graphstudio" >}}); want to ship installers? See [building them from source]({{< relref "blog/build-installers-from-source" >}}).
