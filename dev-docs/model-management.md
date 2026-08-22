# 统一模型管理（ModelFinder）

> 状态：已实现（2026-08）。对应核心 API `include/plugin_api.hpp`（ModelFinder 段）、
> 实现 `src/model_finder.cpp`、GraphStudio 接入 `app/graph_studio/src/ModelBootstrap.cpp`。

## 背景与目标

MediaPipe 任务（`mp_*` 10 个 task type）原先必须在 graph.json 的 `model_path` 参数里
写显式文件路径（相对 `_source_dir` 或绝对路径）。目标：图里只填**模型名**
（如 `"face_landmarker.task"`），宿主在 SDK 初始化时注入一个
**ModelFinder 回调**（模型名 → 绝对路径），SDK 内部解析后读取；未命中时回退
原路径语义，保证旧图零改动兼容。

## 核心 API（进程级全局槽，仿 set_log_sink / set_gpu_backend）

```cpp
#include <plugin_api.hpp>

task_graph::ModelFinder finder = [](const std::string& name) -> std::string {
    // name → 绝对路径；返回空串表示未命中（任务回退 _source_dir 相对路径）
};
task_graph::set_model_finder(finder);      // 覆盖式安装；空函数等同清除
task_graph::clear_model_finder();
std::string path = task_graph::find_model("face_landmarker.task");  // 空串=未装/未命中
```

契约（与 LogSink 一致）：

- 可能在**任意线程**被并发调用（ThreadPool worker / on_init 阶段），宿主实现需线程安全；
- **不得抛异常**（WASM/mobile `-fno-exceptions`，异常不能跨 SO 传播）；
- 实现内部互斥锁**外**调用（锁内只拷贝 std::function），回调内可安全再调 `tg_log`；
- 应快速返回；宿主可自行缓存（回调在每任务 on_init 时逐个查询）。

`TG_SDK_VERSION` 随本特性 1 → 2（新增导出符号）。

## 任务侧解析顺序（mediapipe `MediaPipeVisionTaskBase::resolve_model_path`）

1. `model_path` 参数为空 → FAILED（"model_path not set"）；
2. **`find_model(原始值)` 命中 → 直接使用**（finder 优先）；
3. 未命中 → 回退 `resolve_path(_source_dir, 原始值)`（老 graph 的相对/绝对路径语义）。

核心测试 `tests/test_model_finder.cpp` 把该顺序钉住（含 on_init 场景与并发压力）。
其他读模型/资产的任务（js_task 的 `script_path` 等）未来接入时沿用同一模式。

## 宿主接入点

| 宿主 | 接入 |
|---|---|
| GraphStudio 桌面 | `InitModelFinder()`（`entry.cpp`，InitGpuBackend 之后）→ `app/graph_studio/src/ModelBootstrap.cpp` |
| GraphStudio dev 运行 | `scripts/run_graph_studio.py` 注入 `GRAPH_STUDIO_MODELS_DIR=<repo>/submodules/.../tests/models` |
| Linux AppImage | AppRun `export GRAPH_STUDIO_MODELS_DIR=$appdir/usr/share/graph_studio/models` |
| macOS .app | `Contents/Resources/models`（ModelBootstrap 的第 3 候选目录） |
| Windows MSIX | `<exe 目录>\models`（第 2 候选目录） |
| mediapipe 测试 driver | `tests/mp_model_finder.hpp`（`<base_dir>/../models/<name>`） |

测试图资产布局与源码树一致：graph 在 `tests/graphs/`、模型/图片在 `tests/models/`，
图内以 `../models/xxx.jpg` 引用 —— 源码树里的 graph 可直接在 GraphStudio 打开运行
（模型名走 finder，dev 启动脚本注入 `GRAPH_STUDIO_MODELS_DIR`）；构建树把 models
拷到 `<bin>/models`（`<bin>/graphs` 的同级），driver 的软跳过检查与 finder 同样指向
`../models`。

ModelBootstrap 的查找目录候选（按序）：

1. 环境变量 `GRAPH_STUDIO_MODELS_DIR`
2. `<exe 目录>/models`（Windows MSIX 布局 / dev 构建目录）
3. `<exe 目录>/../Resources/models`（macOS bundle）

文件名依次尝试 `原名 / 原名.task / 原名.tflite`；全部未命中返回空串（任务回退）。
fail-open：无任何 models 目录时行为与未装 finder 完全一致。

## 打包随附模型

三平台打包脚本默认把 10 个模型（`.task`/`.tflite`，不含测试图片）打进包内 models
目录，缺失时自动运行 `scripts/download_mediapipe_models.py`（幂等）下载；
下载失败打包报错，`--skip-models` / `-SkipModels` 可跳过：

- `scripts/package_macos.py` → `Contents/Resources/models/`
- `scripts/package_linux.py` → AppDir `usr/share/graph_studio/models/`（AppRun 注入 env）
- `scripts/build_msix.ps1` → staging `<layout>\models\`（makeappx 随整个目录收入）

注意：10 个模型合计约百 MB（holistic_landmarker 最大），对包体敏感的场景用跳过开关。

## WASM 展望

WASM 不装 finder：`_source_dir` 为空（MEMFS 上传），名称解析回退后同样落空。
后续可做：宿主在浏览器里按名 fetch 模型到 MEMFS 后注册一个 MEMFS 版 finder
（回调里同步查 MEMFS 即可，无需改核心）。

## 测试

- 核心：`tests/test_model_finder.cpp`（9 用例：槽语义/并发/重入/解析顺序/DAG on_init）
- E2E：`submodules/mediapipe/mediapipe_vision/tests/`（10 图全部名称式 + driver 装 finder；
  模型缺失软跳过不变；graphs 为 configure 期拷贝，改图需重跑 cmake configure）
