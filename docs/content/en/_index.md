---
title: "task_graph · GraphStudio"
heroBadge: "C++20 · Cross-platform · Plugin-based"
heroTitle: "A lightweight, cross-platform DAG task-execution framework in C++20"
heroSubtitle: "Describe your pipeline as a graph of typed, port-connected tasks, execute it with automatic topological parallelization, and serialize it to/from JSON — with the Qt6 visual editor GraphStudio included."
platforms: ["macOS", "Windows", "Linux", "iOS", "Android", "WASM"]
graphStudio:
  title: "GraphStudio — the visual graph editor"
  desc: "Build and run compute graphs without writing code: drag task nodes onto the canvas, connect ports, tweak parameters, then run and watch each task's status and timing in real time."
  bullets:
    - "Drag-and-drop nodes and port connections to compose graphs visually"
    - "One-click run with per-task status and duration"
    - "Graphs save as JSON, fully interoperable with the C++ `DAGSerializer`"
    - "Ships with OpenCV / GPU / JavaScript / MediaPipe plugin tasks"
  tutorialLabel: "Read the tutorial"
features:
  - icon: "🔗"
    title: "Typed port-based tasks"
    desc: "Tasks exchange data over named ports as `std::any`; multi-port edges between a task pair are supported."
  - icon: "⚡"
    title: "Topological parallelism"
    desc: "A thread-pool executor schedules by dependency automatically, returning status and duration per task."
  - icon: "📝"
    title: "JSON graph format"
    desc: "Graphs are data: `DAGSerializer::from_string` loads JSON into a runnable `DAG` (versioned format v2.0)."
  - icon: "🧩"
    title: "Plugin system"
    desc: "Compile-time-linked subnodes plus a runtime `PluginLoader` (dlopen + SDK version checks)."
  - icon: "🚀"
    title: "GPU backends"
    desc: "Metal / Vulkan / CUDA — all opt-in and OFF by default, so GPU-less builds stay lean."
  - icon: "🖼️"
    title: "Batteries included"
    desc: "OpenCV image I/O + filtering, GPU image ops, QuickJS scripting, and MediaPipe vision (10 tasks)."
  - icon: "🖥️"
    title: "GraphStudio editor"
    desc: "A Qt6 desktop editor to build, run, and debug compute graphs in one place."
  - icon: "🌍"
    title: "Everywhere"
    desc: "SHARED library on desktop, STATIC on iOS / Android / WASM — the same graph definition travels across targets."
---

## Up and running in ten lines {#quick-start}

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

## Graphs as data {#json-graph}

The same graph can be expressed as JSON, loaded by `DAGSerializer::from_string` and executed by `DAGExecutor` — exactly the format GraphStudio saves and loads:

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

## Learn more

- 📖 Full documentation lives in the [GitHub repository README](https://github.com/mangoeffect/graph-studio) (build options, plugin development, GPU backends)
- 🚀 To compose your first graph from scratch, read [Quick Start with GraphStudio]({{< relref "blog/quick-start-graphstudio" >}}) on the blog
- 📦 To produce installers yourself, see [Building the three-platform installers from source]({{< relref "blog/build-installers-from-source" >}})
