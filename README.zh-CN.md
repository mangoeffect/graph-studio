# task_graph

**一个轻量级、跨平台的 C++20 DAG 任务执行框架。**

task_graph 让您把一个流水线描述为由带类型的、端口相连的任务组成的图，自动进行拓扑并行执行，并可序列化为 JSON / 从 JSON 加载。框架自带插件系统、可选 GPU 后端（Metal / Vulkan / CUDA）、OpenCV、JavaScript（QuickJS）和 MediaPipe 子模块，以及 Qt6 桌面编辑器（GraphStudio）。

[English](./README.md)

---

## 特性

- **带类型的端口任务** —— 任务通过命名端口以 `std::any` 交换数据；同一任务对之间支持多端口边。
- **图执行器** —— 线程池 + 自动拓扑排序；按任务返回状态与耗时的执行结果。
- **JSON 图格式** —— 用数据描述图：`DAGSerializer::from_string` 将 JSON 加载为可运行的 `DAG`。
- **插件模型** —— 编译期链接的子模块（`subnode.json` + `cmake/Subnode.cmake`），另有运行时 `PluginLoader`。
- **GPU 后端** —— Metal（仅 Apple）、Vulkan、CUDA，均为 opt-in，默认关闭。
- **可扩展的子模块** —— OpenCV 图像读写与滤波、GPU 图像算子、QuickJS 脚本、MediaPipe 视觉（10 个任务）。
- **GraphStudio** —— 用于可视化构建和运行计算图的 Qt6 桌面编辑器。

## 架构

核心 `task_graph` 库位于 `src/`，公共头文件在 `include/task_graph/`。桌面端构建为 SHARED 库，iOS / Android / WASM 构建为 STATIC 库。

声明一个插件时，子类化 `INode` 并重写：

- `type()` —— 稳定的任务类型名
- `execute(TaskContext&)` —— 任务主体
- `input_specs()` / `output_specs()` / `param_specs()` —— 端口与参数的契约

用 `ctx.input<T>("port")` 读取上游输出；通过 `TaskResult{.status=..., .value=...}`（默认输出端口 `"out"`）或 `outputs` 返回结果。

跨端口使用的自定义类型必须用 `TG_REGISTER_TYPE(Type, "stable::name")` 注册，以保证跨 SO/dlopen 的稳定类型名。

## 构建与测试

要求：CMake >= 3.16，支持 C++20 的编译器。

```bash
# 配置 + 构建 + 运行全部测试
python scripts/run_tests.py

# 清空构建目录后全新构建
python scripts/run_tests.py -c

# 只运行名字匹配 regex 的测试
python scripts/run_tests.py -R ports

# 只列出所有测试，不运行
python scripts/run_tests.py -l
```

常用 CMake 选项（`cmake -S . -B build ...`）：

| 选项 | 默认 | 说明 |
|---|---|---|
| `TASK_GRAPH_ENABLE_OPENCV` | ON | 图像转换依赖 OpenCV；开启后为 REQUIRED，除非能在预构建目录中找到 |
| `TASK_GRAPH_ENABLE_METAL` | OFF | Metal GPU 后端（仅 Apple） |
| `TASK_GRAPH_ENABLE_VULKAN` | OFF | Vulkan GPU 后端 |
| `TASK_GRAPH_ENABLE_CUDA` | OFF | CUDA GPU 后端 |

构建目录：桌面端用 `build/`，WASM 用 `build_wasm/`，iOS/Android 的 OpenCV 预构建在 `build_ios/` / `build_android/` —— 均已被 gitignore。

构建单个核心测试：

```bash
cmake --build build --target test_dag -j 8
cd build && ctest -R test_dag --output-on-failure
```

## 快速示例

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

更完整的示例见 `examples/basic.cpp`、`examples/parallel.cpp`、`examples/multi_output.cpp`（目标 `example_basic`、`example_parallel`、`example_multi_output`）。

## JSON 图格式

图是带版本号的（`"version": "2.0"`），任务携带参数，支持端口限定边：

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

用 `DAGSerializer::from_string(json)` 加载，再用 `DAGExecutor` 执行。

## 插件 / 子模块

编译期链接的子模块放在 `submodules/`（每个都是独立的嵌入式 git 仓库，被主仓库 gitignore，见下方说明），在 `subnode.json` 中注册，由 `cmake/Subnode.cmake` 接入。

脚手架生成新子模块：

```bash
python3 scripts/generate_submodule.py \
    -d submodules/my_plugins -n math_ops \
    -t AddTask MultiplyTask:math_mul --desc "Math operations"
```

生成器会产出 `CMakeLists.txt`、任务头文件和带双重插件注册的实现（`__attribute__((constructor))` 自动加载 + `extern "C" register_plugin`）。内置任务类型一览：

| 子模块 | 任务类型 |
|---|---|
| `image_reader` | `opencv_image_read` |
| `image_filtering` | `opencv_blur_filter`、`opencv_gaussian_blur_filter`、`opencv_median_blur_filter`、`opencv_bilateral_filter`、`opencv_box_filter`、`opencv_sobel_filter`、`opencv_scharr_filter`、`opencv_laplacian_filter` |
| `gpu_image_processing` | `gpu_box_blur`、`gpu_gaussian_blur`、`gpu_grayscale`、`gpu_brightness_contrast`、`gpu_resize` |
| `js_task` | `js_script` |
| `mediapipe_vision` | `mp_face_landmarker`、`mp_hand_landmarker`、`mp_pose_landmarker`、`mp_object_detector`（另有 `mp_face_detector`、`mp_gesture_recognizer`、`mp_holistic_landmarker`、`mp_image_classifier`、`mp_image_embedder`、`mp_image_segmenter`） |

MediaPipe 视觉需要先用 `scripts/download_mediapipe_models.py` 下载预构建模型和图片到 `submodules/mediapipe/mediapipe_vision/tests/models/`；缺失这些资源时相关测试会 soft-skip。

## 独立编译 & 动态插件（桌面）

桌面端插件主框架可以**互不依赖对方源码**编译：主框架独立构建，插件基于**已安装的 SDK**（公共头文件 + `libtask_graph`）独立构建，并在**运行时**由 `PluginLoader`（dlopen + `register_plugin`）动态加载。

```bash
# 1. 构建 SDK 前缀（头文件 + libtask_graph + CMake 包）
python scripts/build_sdk.py                     # -> build/sdk/

# 2. 基于 SDK 独立编译插件（不引用主仓库源码）
python scripts/build_plugin_standalone.py examples/plugins/demo
python scripts/build_plugin_standalone.py submodules/opencv/image_processing/image_filtering --enable-opencv

# 3. 生成 demo 插件后运行 dlopen 测试（文件缺失时 soft-skip）
ctest -R test_plugin_abi
```

- 原有 in-tree 开发流程（`scripts/run_tests.py`、GraphStudio 扫描 `build/submodules/`）不变；加 `-DTASK_GRAPH_BUILD_SUBMODULES=OFF` 可让核心库严格独立编译。
- 各子模块通过 `use_task_graph_sdk()`（`cmake/SdkUtil.cmake`）链接框架：优先 in-tree 目标，其次 `find_package(task_graph)`（SDK 导入目标），最后旧式 `TASK_GRAPH_ROOT`。
- 插件导出 `register_plugin` / `unregister_plugin` / `get_plugin_info`，并可导出 `TG_DEFINE_PLUGIN_SDK_VERSION`；`PluginLoader` 会拒绝 SDK 版本不匹配的动态库。
- `python scripts/run_tests.py --sdk` 一键完成第 1–3 步。
- WASM / 移动端仍为静态链接（不支持 dlopen）。

## GPU 与跨平台注意事项

- GPU 后端均为 opt-in，默认关闭；Metal 需要 Mac，Vulkan 需要 Vulkan SDK，CUDA 需要 CUDA 工具链。
- WASM / 移动端使用 `-fno-exceptions` 构建：不要跨插件边界抛异常 —— 改返回 `TaskResult{FAILED}`。
- 子模块 CMakeLists 通过 `TASK_GRAPH_ROOT` 变量，既能 `add_subdirectory` 集成，也能独立构建。

## GraphStudio（Qt6 桌面编辑器）

```bash
python scripts/run_graph_studio.py            # 构建并启动
python scripts/run_graph_studio.py --qt /path/to/qtbase   # CMake 找不到 Qt6 时指定前缀
python scripts/run_graph_studio.py -t         # 运行编辑器自带的 ctest 测试
```

无头 UI 测试：`python scripts/run_ui_tests.py`。

崩溃上报（可选）使用 sentry-native + Crashpad；构建需先运行 `python scripts/fetch_sentry.py`，运行时读取 `SENTRY_DSN` 环境变量。没有 DSN 时是干净的 no-op，可安全在本地运行。

## 测试

- 核心测试：`test_dag`、`test_ports`、`test_params`、`test_serializer`、`test_data_types`、`test_task_params`、`test_profiler`、`test_type_registry`、`test_logger`、`test_plugin`，以及（启用 OpenCV 时）`test_opencv_convert`。
- 子模块测试由 JSON 图驱动：每个测试二进制加载一个 `<name>_graph.json`（由 `DAGSerializer` 加载、`DAGExecutor` 执行），描述 `opencv_image_read` → 被测任务。模板以 `tests/graphs/*.json.in` 提交，在配置阶段由 `configure_file` 物化。

## 仓库结构

```
CMakeLists.txt                核心构建
include/task_graph/          公共头文件
src/                         核心库源码
cmake/                       CMake 辅助（Subnode.cmake 等）
submodules/                  嵌入式插件仓库（已 gitignore），如 opencv/、gpu/、scripting/、mediapipe/
subnode.json                 子模块注册表
examples/                    basic / parallel / multi_output
tests/                       核心测试二进制
scripts/                     构建/运行/工具脚本（详见 scripts/README.md）
app/graph_studio/            Qt6 桌面编辑器
```

> `submodules/`、gitignore 的 agent 配置以及各构建目录**不是**仓库树的一部分；`scripts/` 与 `app/` 下的 README 另有深入文档。