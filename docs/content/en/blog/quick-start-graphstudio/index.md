---
title: "Quick Start with GraphStudio: Your First Compute Graph"
date: 2026-08-01T10:00:00+08:00
tags: ["Beginner", "GraphStudio"]
categories: ["Tutorials"]
summary: "From install to running your first image-processing graph: drag a few nodes, draw a few edges, and learn how graphs interop with C++ and JSON."
showToc: true
---

[GraphStudio](https://github.com/mangoeffect/graph-studio) is the Qt6 desktop editor for the task_graph framework: it turns "assembling a DAG in code" into "dragging and connecting on a canvas". This post gets your first graph running from scratch.

## Getting GraphStudio

**Option 1: download an installer (recommended)**

Pick your platform (macOS `.dmg` / Windows `.msix` / Linux `.AppImage`) on the [download page]({{< relref "download" >}}) — all official plugin tasks are bundled.

**Option 2: build from source**

```bash
# Prerequisites: CMake >= 3.18, a C++20 compiler, Qt6 (macOS: brew install qt)
python scripts/run_graph_studio.py            # build + launch
python scripts/run_graph_studio.py --qt /path/to/qtbase   # when Qt6 isn't auto-detected
```

> Note: the four plugin submodules (OpenCV / GPU / scripting / MediaPipe) are currently private repositories distributed with the official installers. Outside contributors can build the **core framework** and examples normally; for the full editor experience, use the installers.

## A tour of the UI

GraphStudio is organized in three areas:

- **Left, the node palette** — task types grouped by subnode (image I/O, filtering, GPU ops, JS scripting, MediaPipe vision, …);
- **Center, the canvas** — the graph itself: nodes are tasks, edges are data flow, port names are labeled on the wires;
- **Right, the property panel** — parameters of the selected node (e.g. `file_path`, `kernel_size`) plus its port contracts.

## Building your first graph

Goal: read an image → Gaussian blur → Sobel edge detection.

1. Drag **opencv_image_read** onto the canvas and point its `file_path` parameter at a local image;
2. Drag in **opencv_gaussian_blur_filter** and **opencv_sobel_filter**;
3. Connect the ports: `image_read.out → blur.in`, then `blur.out → sobel.in` (drag between the port handles; names autocomplete);
4. Hit **Run**.

When the run finishes, every node shows its status and duration; failures are annotated right on the node, which makes it easy to tell parameter problems from upstream data problems.

## Graphs as data: C++ / JSON interop

The `.json` GraphStudio saves is the framework's graph format (`version: 2.0`):

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

The same JSON runs outside the editor in three lines of C++:

```cpp
using namespace task_graph;

auto dag = DAGSerializer::from_string(json_string);
DAGExecutor executor;
executor.execute(*dag).wait();
```

In other words: **the prototype you tuned in the editor moves into production code unchanged** — no translation step.

## Where to go next

- Curious what happens under the wires? Read [task_graph architecture overview]({{< relref "blog/task-graph-architecture" >}});
- Want to ship your own installers? Read [building the three-platform installers]({{< relref "blog/build-installers-from-source" >}}).
