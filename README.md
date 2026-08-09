# task_graph

**A lightweight, cross-platform DAG task-execution framework in C++20.**

task_graph lets you describe a pipeline as a graph of typed, port-connected tasks, execute it with automatic topological parallelization, and serialize it to/from JSON. It ships with a plugin system, opt-in GPU backends (Metal / Vulkan / CUDA), OpenCV, JavaScript (QuickJS) and MediaPipe subnodes, and a Qt6 desktop editor (GraphStudio).

[简体中文](./README.zh-CN.md)

---

## Features

- **Typed port-based tasks** — tasks exchange data over named ports as `std::any`; multi-port edges between a task pair are supported.
- **Graph executor** — automatic topological ordering with a thread pool; results are returned per task with status and duration.
- **JSON graph format** — build graphs as data: `DAGSerializer::from_string` loads JSON into a runnable `DAG`.
- **Plugin model** — compile-time-linked subnodes (`subnode.json` + `cmake/Subnode.cmake`), plus a runtime `PluginLoader`.
- **GPU backends** — Metal (Apple), Vulkan, CUDA: all opt-in, all off by default.
- **Extensible via subnodes** — OpenCV image I/O + filtering, GPU image ops, QuickJS scripting, and MediaPipe vision (10 tasks).
- **GraphStudio** — a Qt6 desktop editor for building and running graphs visually.

## Architecture

The core `task_graph` library lives in `src/` with headers in `include/task_graph/`. It builds as a SHARED library on desktop and STATIC on iOS / Android / WASM.

To declare a plugin, subclass `INode` and override:

- `type()` — stable task type name
- `execute(TaskContext&)` — task body
- `input_specs()` / `output_specs()` / `param_specs()` — port & parameter contract

Read upstream output with `ctx.input<T>("port")`; return results via `TaskResult{.status=..., .value=...}` (default output port `"out"`) or via `outputs`.

Custom types flowing across ports must be registered with `TG_REGISTER_TYPE(Type, "stable::name")` for stable cross-SO names.

## Build & test

Requirements: CMake >= 3.16, a C++20 compiler.

```bash
# configure + build + run all tests
scripts/run_tests.sh

# clean rebuild
scripts/run_tests.sh -c

# run a subset (regex)
scripts/run_tests.sh -R ports

# list tests without running
scripts/run_tests.sh -l
```

Useful CMake options (`cmake -S . -B build ...`):

| Option | Default | Note |
|---|---|---|
| `TASK_GRAPH_ENABLE_OPENCV` | ON | OpenCV for image conversion; REQUIRED when ON unless a prebuilt tree is found |
| `TASK_GRAPH_ENABLE_METAL` | OFF | Metal GPU backend (Apple only) |
| `TASK_GRAPH_ENABLE_VULKAN` | OFF | Vulkan GPU backend |
| `TASK_GRAPH_ENABLE_CUDA` | OFF | CUDA GPU backend |

Build dirs: desktop uses `build/`, WASM `build_wasm/`, iOS/Android OpenCV prebuilds live in `build_ios/` / `build_android/` — all gitignored.

Build a focused core test:

```bash
cmake --build build --target test_dag -j 8
cd build && ctest -R test_dag --output-on-failure
```

## Quick example

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
    for (auto& [id, r] : executor.get_results())
        std::cout << id << ": " << (r.is_success() ? "SUCCESS" : "FAILED") << "\n";
}
```

More complete samples: `examples/basic.cpp`, `examples/parallel.cpp`, `examples/multi_output.cpp` (build targets `example_basic`, `example_parallel`, `example_multi_output`).

## JSON graph format

Graphs are versioned (`"version": "2.0"`), tasks carry params, edges may be port-qualified:

```json
{
  "version": "2.0",
  "tasks": [
    { "id": "src", "type": "opencv_image_read", "params": { "file_path": "tests/data/test.png" } },
    { "id": "gaussian", "type": "opencv_gaussian_blur_filter" },
    { "id": "sobel", "type": "opencv_sobel_filter" }
  ],
  "edges": [
    { "from": "src", "from_port": "out", "to": "gaussian", "to_port": "in" },
    { "from": "gaussian", "from_port": "out", "to": "sobel", "to_port": "in" }
  ]
}
```

Load it with `DAGSerializer::from_string(json)` and run it with `DAGExecutor`.

## Plugins / subnodes

Compile-time-linked subnodes live in `submodules/` (each its own embedded git repo, gitignored by the main repo — see note below), registered in `subnode.json` and wired by `cmake/Subnode.cmake`.

Scaffold a new subnode:

```bash
python3 scripts/generate_submodule.py \
    -d submodules/my_plugins -n math_ops \
    -t AddTask MultiplyTask:math_mul --desc "Math operations"
```

The generator produces `CMakeLists.txt`, a task header, and a source with dual plugin registration
(`__attribute__((constructor))` auto-load + `extern "C" register_plugin`). Recommended task classes

| Type | Expected task types |
|---|---|
| `image_reader` | `opencv_image_read` |
| `image_filtering` | `opencv_blur_filter`, `opencv_gaussian_blur_filter`, `opencv_median_blur_filter`, `opencv_bilateral_filter`, `opencv_box_filter`, `opencv_sobel_filter`, `opencv_scharr_filter`, `opencv_laplacian_filter` |
| `gpu_image_processing` | `gpu_box_blur`, `gpu_gaussian_blur`, `gpu_grayscale`, `gpu_brightness_contrast`, `gpu_resize` |
| `js_task` | `js_script` |
| `mediapipe_vision` | `mp_face_landmarker`, `mp_hand_landmarker`, `mp_pose_landmarker`, `mp_object_detector` (+ `mp_face_detector`, `mp_gesture_recognizer`, `mp_holistic_landmarker`, `mp_image_classifier`, `mp_image_embedder`, `mp_image_segmenter`) |

MediaPipe vision requires prebuilt models + demo images downloaded via `scripts/download_mediapipe_models.sh` into `submodules/mediapipe/mediapipe_vision/tests/models/`; tests soft-skip when those assets are absent.

## GPU & cross-platform notes

- GPU backends are opt-in and OFF by default; Metal builds need a Mac, Vulkan needs the Vulkan SDK, CUDA needs a CUDA toolkit.
- WASM / mobile builds use `-fno-exceptions`: don't throw across plugin boundaries — return `TaskResult{FAILED}` instead.
- Submodule CMakeLists use `TASK_GRAPH_ROOT` so they work via `add_subdirectory` or standalone.

## GraphStudio (Qt6 desktop editor)

```bash
scripts/run_graph_studio.sh            # build + launch
scripts/run_graph_studio.sh --qt /path/to/qtbase   # if Qt6 isn't auto-detected
scripts/run_graph_studio.sh -t         # run the editor's own ctest suite
```

Headless UI tests: `scripts/run_ui_tests.sh`.

Crash reporting (optional) uses sentry-native + Crashpad; build requires `scripts/fetch_sentry.sh`, and a `SENTRY_DSN`
environment variable at runtime. No DSN ⇒ clean no-op when running locally.

## Tests

- Core tests: `test_dag`, `test_ports`, `test_params`, `test_serializer`, `test_data_types`, `test_task_params`, `test_profiler`, `test_type_registry`, `test_logger`, `test_plugin`, and (with OpenCV) `test_opencv_convert`.
- Submodule tests are JSON-graph-driven: each test binary loads a `<name>_graph.json` (loaded by `DAGSerializer`, executed by `DAGExecutor`) describing `opencv_image_read` → the tasks under test. Templates are committed as `tests/graphs/*.json.in` and materialized by `configure_file` at configure time.

## Repository layout

```
CMakeLists.txt                 Core build
include/task_graph/            Public headers
src/                           Core library sources
cmake/                         CMake helpers (Subnode.cmake, ...)
submodules/                    Embedded plugin repos (gitignored) e.g. opencv/, gpu/, scripting/, mediapipe/
subnode.json                   Subnode registry
examples/                      basic / parallel / multi_output
tests/                         Core test binaries
scripts/                       Build/run/tooling scripts (see scripts/README.md)
app/graph_studio/              Qt6 desktop editor
```

> `submodules/`, the gitignored agent config, and build dirs are intentionally not part of the tracked tree; READMEs under `scripts/` and `app/` carry their own in-depth docs.