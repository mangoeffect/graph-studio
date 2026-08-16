# task_graph

**A lightweight, cross-platform DAG task-execution framework in C++20.**

task_graph lets you describe a pipeline as a graph of typed, port-connected tasks, execute it with automatic topological parallelization, and serialize it to/from JSON. It ships with a plugin system, opt-in GPU backends (Metal / Vulkan / CUDA), OpenCV, JavaScript (QuickJS) and MediaPipe subnodes, and a Qt6 desktop editor (GraphStudio).

[简体中文](./README.zh-CN.md) · [Website](https://mangoeffect.github.io/graph-studio/)

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

Requirements: CMake >= 3.18, a C++20 compiler.

```bash
# configure + build + run all tests
python scripts/run_tests.py

# clean rebuild
python scripts/run_tests.py -c

# run a subset (regex)
python scripts/run_tests.py -R ports

# list tests without running
python scripts/run_tests.py -l
```

Tests are registered with CTest **per test case**: every GoogleTest `TEST()` gets its
own ctest entry (`test_dag.Dag.basic_dag_execution`), and submodule graph tests register
one entry per graph (`test_image_filtering_graph.single_filter`). The one-shot runners
therefore show a clear per-case pass/fail list, and single cases can be filtered:

```bash
ctest --test-dir build -C Debug -R test_dag.Dag.basic_dag_execution
ctest --test-dir build -C Debug -R test_gpu_image_graph.gpu_crop
```

Useful CMake options (`cmake -S . -B build ...`):

| Option | Default | Note |
|---|---|---|
| `TASK_GRAPH_ENABLE_OPENCV` | ON | OpenCV for image conversion; REQUIRED when ON unless a prebuilt tree is found |
| `TASK_GRAPH_ENABLE_METAL` | OFF | Metal GPU backend (Apple only) |
| `TASK_GRAPH_ENABLE_VULKAN` | OFF | Vulkan GPU backend |
| `TASK_GRAPH_ENABLE_CUDA` | OFF | CUDA GPU backend |
| `TASK_GRAPH_BUILD_TESTS` | ON | Build unit tests; fetches GoogleTest (v1.15.2) via FetchContent on first configure and caches it under `build/_deps`. Offline builds: point `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` at a local googletest copy, or turn this option OFF |

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

MediaPipe vision requires prebuilt models + demo images downloaded via `scripts/download_mediapipe_models.py` into `submodules/mediapipe/mediapipe_vision/tests/models/`; tests soft-skip when those assets are absent.

## Standalone build & dynamic plugins (desktop)

On desktop the framework and its plugins can be compiled fully independently of each
other: the framework builds standalone, and plugins build against an **installed SDK**
(public headers + `libtask_graph`) and are loaded at **runtime** via `PluginLoader`
(dlopen + `register_plugin`).

```bash
# 1. Build the SDK prefix (headers + libtask_graph + CMake package)
python scripts/build_sdk.py                     # -> build/sdk/

# 2. Build any plugin standalone against the SDK (no main-repo source)
python scripts/build_plugin_standalone.py examples/plugins/demo
python scripts/build_plugin_standalone.py submodules/opencv/image_processing/image_filtering --enable-opencv

# 3. Run the dlopen test once the demo plugin exists (it soft-skips otherwise)
ctest -R test_plugin_abi
```

- The in-tree dev workflow (`scripts/run_tests.py`, GraphStudio scanning `build/submodules/`) is unchanged; pass `-DTASK_GRAPH_BUILD_SUBMODULES=OFF` to build the core strictly standalone.
- Each submodule links the framework through `use_task_graph_sdk()` (`cmake/SdkUtil.cmake`), which prefers the in-tree target, then `find_package(task_graph)` (SDK), then legacy `TASK_GRAPH_ROOT`.
- Plugins export `register_plugin` / `unregister_plugin` / `get_plugin_info` and may export `TG_DEFINE_PLUGIN_SDK_VERSION`; `PluginLoader` refuses libraries whose SDK version doesn't match the host.
- `python scripts/run_tests.py --sdk` runs steps 1–3 automatically.
- WASM / mobile builds remain statically linked (no dlopen).

## GPU & cross-platform notes

- GPU backends are opt-in and OFF by default; Metal builds need a Mac, Vulkan needs the Vulkan SDK, CUDA needs a CUDA toolkit.
- WASM / mobile builds use `-fno-exceptions`: don't throw across plugin boundaries — return `TaskResult{FAILED}` instead.
- Submodule CMakeLists use `TASK_GRAPH_ROOT` so they work via `add_subdirectory` or standalone.

## GraphStudio (Qt6 desktop editor)

```bash
python scripts/run_graph_studio.py            # build + launch
python scripts/run_graph_studio.py --qt /path/to/qtbase   # if Qt6 isn't auto-detected
python scripts/run_graph_studio.py -t         # run the editor's own ctest suite
```

Headless UI tests: `python scripts/run_ui_tests.py`.

Crash reporting (optional, desktop) uses sentry-native + Crashpad; build requires `python scripts/fetch_sentry.py`, and a `SENTRY_DSN`
environment variable at runtime (or one embedded at compile time for release packages). No DSN ⇒ clean no-op when running locally;
a missing sentry-native checkout also builds fine. Crash reports carry recent WARN+ log breadcrumbs and the current graph context.
End-to-end local verification (no real Sentry project needed): `python scripts/verify_crash_reporting.py`. CI releases enable it
conditionally via secrets (`SENTRY_DSN` / `SENTRY_AUTH_TOKEN`, …) and always archive per-platform debug symbols as Release assets;
see [dev-docs/crash-reporting.md](dev-docs/crash-reporting.md).

## Tests

- Core tests are GoogleTest-based (`tests/*.cpp`; shared assertion helpers live in `tests/tg_test_helpers.hpp`). Each `TEST(Suite, case)` registers a separate ctest entry named `<exe>.<Suite>.<case>`. Suites: `test_dag`, `test_ports`, `test_params`, `test_serializer`, `test_data_types`, `test_task_params`, `test_profiler`, `test_type_registry`, `test_logger`, `test_path_utils`, `test_plugin`, `test_stream_executor`, `test_plugin_abi` (soft-skips without the standalone demo plugin), and — with OpenCV — `test_opencv_convert`.
- Submodule tests are JSON-graph-driven: each driver accepts graph names as argv and registers one ctest entry per graph (e.g. `test_gpu_image_graph.gpu_crop`), loaded by `DAGSerializer` and executed by `DAGExecutor`. Graphs are committed under each module's `tests/graphs/` and copied into the build tree at configure time. Exit code 2 means an environment soft-skip (missing plugin / FFmpeg / GPU compute backend) and shows up as `Skipped` in ctest.
- Test discovery uses `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`: test binaries are probed for their case list when ctest starts (not at build time), so the Windows DLL search paths injected by the scripts are already in place.

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