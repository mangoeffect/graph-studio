# task_graph 脚本

本目录的核心构建/测试脚本已统一为**跨平台 Python**（Python 3.9+，纯标准库），同一份脚本在 macOS / Linux / Windows 通用，无需各平台单独实现：

| 用途 | Python 脚本（推荐，三平台通用） |
|---|---|
| **一键跑全部测试（框架 + 子模块 + UI）** | **`scripts/run_all_test.py`** |
| 构建并运行全部单元测试 | `scripts/run_tests.py` |
| 构建并启动 GraphStudio (Qt6 GUI) | `scripts/run_graph_studio.py` |
| 把 task_graph 装成可分发 SDK 前缀 | `scripts/build_sdk.py` |
| 用 SDK 前缀独立编译一个插件 | `scripts/build_plugin_standalone.py` |

```bash
# 任意平台
python scripts/run_tests.py
python scripts/run_graph_studio.py -t
python scripts/build_sdk.py
python scripts/build_plugin_standalone.py examples/plugins/demo
```

> 历史的 `*.sh` / `*.ps1` 同名文件已退化为 **thin shim**：只做 `exec python3 <name>.py "$@"` / `& python <name>.py @args` 转发，下个版本删除。在没装 Python 的极少数环境下，仍可直接调用 cmake（macOS/Linux：`cmake -S . -B build && cmake --build build`；Windows VS 生成器：`cmake -S . -B build` 然后 `cmake --build build --config Debug`）。

此外还有跨平台的 Python 脚本：

- `generate_submodule.py` — 插件子模块脚手架生成器（纯 Python，无平台依赖）
- `wasm_dev_server.py` — WASM 多线程开发服务器（标准库 `http.server`，跨平台）

其余 `*.sh`（`run_ui_tests.sh`、`release_graph_studio.sh`、`build_android/ios/wasm/opencv_*/mediapipe_macos.sh`、`fetch_sentry.sh`、`upload_sentry_symbols.sh`、`download_mediapipe_models.sh`）目前只有 macOS/Linux 版本，详见各脚本头部注释。

## 共享工具包 `gs/`

四个核心 Python 脚本共享一套纯标准库的工具包 `scripts/gs/`：

- `console.py` — 着色输出（step/ok/fail/warn，遵循 `NO_COLOR`，输出前 flush 保证顺序）
- `runner.py` — `subprocess` 包装（等价 PowerShell 的 `Invoke-Native`，透传输出并返回退出码）
- `platform.py` — OS 判断 / `os.cpu_count()` / 动态库后缀 / 运行时搜索路径注入（Windows `PATH`、macOS `DYLD_LIBRARY_PATH`、Linux `LD_LIBRARY_PATH`）/ 桌面 GPU 后端特性宏（macOS Metal / Windows Vulkan）
- `toolchain.py` — cmake/ctest 发现（`shutil.which` → VS2022 自带 cmake → `CMakeCache.txt` 兜底）+ Windows SDK 工具发现
- `deps.py` — Qt6 / OpenCV 跨平台探测（`C:\Qt\<ver>\msvc2022_64`、Homebrew `opt/qt`、`C:\opencv\build\x64\vc16`、`$OPENCV_DIR`）
- `cmake.py` — `CMake` 类：单配置生成器（Unix，`-DCMAKE_BUILD_TYPE`）与多配置生成器（Windows VS，`--config` / `-C`）的统一抽象
- `sdk.py` — `build_sdk` / `build_plugin_standalone` 的共享实现（含 stale-cache 守卫、`task_graph.lib` 镜像、`TASK_GRAPH_PLUGINS_PATH` 收集）

---

## run_all_test.py — 一键全量测试（框架 + 子模块 + UI）

`run_all_test.py` 在三个独立脚本（`run_tests.py` / `run_all_submodules_test.py` / `run_ui_tests.py`）之上做一次「单根构建 + 多阶段 ctest」编排，**一次根构建共享给框架与子模块阶段**，避免重复配置/构建根库，并在末尾汇总各阶段通过情况。三平台通用。

阶段划分（`TASK_GRAPH_BUILD_SUBMODULES` 默认 ON，故框架与子模块测试二进制都在同一棵根 `build/` 里，仅是 ctest 过滤视角不同）：

| 阶段 | 构建树 | ctest 过滤 |
|---|---|---|
| 框架测试 `framework` | 根 `build/` | `ctest -E <子模块正则>`（排除子模块） |
| 子模块测试 `submodules` | 根 `build/` | `ctest -R <子模块正则>`（子模块子集，沿用 `run_all_submodules_test.py` 的正则表） |
| UI 测试 `ui` | `app/graph_studio/build/` | `ctest`（4 个 Qt 测试，`QT_QPA_PLATFORM=offscreen` 无头） |

```bash
# 构建 + 跑全部三阶段
python scripts/run_all_test.py

# 清空两棵构建树后全新构建
python scripts/run_all_test.py -c

# 复用现有产物，只跑测试（不构建）
python scripts/run_all_test.py --no-build

# 跳过 UI 阶段（无需 Qt，只跑框架 + 子模块）
python scripts/run_all_test.py --skip-ui

# 显式指定阶段（逗号分隔）
python scripts/run_all_test.py --phases framework,submodules

# 首失败即停（默认跑完全部，末尾汇总所有失败）
python scripts/run_all_test.py --fail-fast

# 额外构建 SDK + demo 插件，激活 test_plugin_abi
python scripts/run_all_test.py --sdk

# 先下载 MediaPipe 模型再跑（mediapipe 测试需要）
python scripts/run_all_test.py --download-models
```

| 参数 | 缩写 | 说明 |
|---|---|---|
| `--jobs` | `-j` | 并行编译线程数（默认 CPU 核数） |
| `--clean` | `-c` | 清空根 `build/` 与 `app/graph_studio/build/` 后全新构建 |
| `--no-build` | | 跳过构建，复用现有产物只跑测试 |
| `--config` | | 构建配置（默认 `Debug`） |
| `--verbose` | `-v` | ctest 详细输出 |
| `--cmake` | | 手动指定 cmake 可执行文件 |
| `--qt` | | 手动指定 Qt6 前缀（UI 阶段） |
| `--opencv-dir` | | OpenCV 前缀（默认自动探测） |
| `--skip-framework` / `--skip-submodules` / `--skip-ui` | | 跳过对应阶段 |
| `--phases` | | 显式指定要运行的阶段（逗号分隔：`framework,submodules,ui`） |
| `--fail-fast` | | 某阶段失败立即停止（默认跑完全部） |
| `--sdk` | | 额外构建 SDK + demo 插件，激活 `test_plugin_abi` |
| `--download-models` | | 运行前先下载 MediaPipe 模型 |
| `--help` | `-h` | 显示帮助 |

> - 退出码：`0` 表示全部通过（或全部被跳过/门控）；非 `0` 表示任一非跳过阶段失败或构建出错。
> - Windows 上 UI 阶段会自动镜像 `build/<Config>/task_graph.lib` 到 `build/`，以匹配 `app/graph_studio` 的 `link_directories(../build)`（沿用 `run_graph_studio.py` 的做法）。
> - 子模块阶段的「子模块名 → 测试名正则」表直接复用自 `run_all_submodules_test.py`，新增子模块只需在那里登记。

---

## run_graph_studio.py — GraphStudio 运行器

一键构建根库 `task_graph`（GUI 运行时依赖 `build/libtask_graph`），再构建 `app/graph_studio` 并启动。macOS 启动 `.app` bundle，Windows/Linux 启动可执行文件。三平台通用。

```bash
# 构建并启动
python scripts/run_graph_studio.py

# 清空构建目录后全新构建再启动
python scripts/run_graph_studio.py -c

# 只构建不启动
python scripts/run_graph_studio.py --build-only

# 跳过构建，直接启动现有产物
python scripts/run_graph_studio.py --no-build

# 运行 GraphStudio 单元测试
python scripts/run_graph_studio.py -t

# 手动指定 Qt6 前缀（含 lib/cmake/Qt6）
python scripts/run_graph_studio.py --qt /opt/homebrew/Cellar/qtbase/6.11.1
```

| 参数 | 缩写 | 说明 |
|---|---|---|
| `--jobs` | `-j` | 并行编译线程数（默认 CPU 核数） |
| `--clean` | `-c` | 先清空构建目录再全新构建 |
| `--no-build` | | 跳过构建，直接启动 |
| `--build-only` | | 只构建，不启动 |
| `--test` | `-t` | 运行 GraphStudio 单元测试（ctest） |
| `--qt` | | 手动指定 Qt6 前缀路径 |
| `--help` | `-h` | 显示帮助 |

> 需要本机已安装 Qt6（macOS 可用 Homebrew：`brew install qt`）。若 CMake 找不到 Qt6，用 `--qt` 指定前缀或设置环境变量 `QT_PREFIX_PATH`。

---

## run_tests.py — 单元测试运行器

一键在 `build/` 目录用 CMake 配置 + 构建，再用 `ctest` 运行全部单元测试。退出码 `0` 表示全部通过。三平台通用（Windows 自动用 VS 多配置生成器 + `--config`）。

```bash
# 构建并运行全部测试
python scripts/run_tests.py

# 先清空 build 目录再全新构建
python scripts/run_tests.py -c

# 只跑名字匹配 regex 的测试（如 port / serializer）
python scripts/run_tests.py -R ports

# 列出所有测试后退出，不运行
python scripts/run_tests.py -l

# 跳过构建，直接跑现有二进制
python scripts/run_tests.py --no-build

# 关闭 OpenCV 相关测试（默认开启，与主项目一致）
python scripts/run_tests.py --disable-opencv
```

| 参数 | 缩写 | 说明 |
|---|---|---|
| `--build-dir` | `-b` | 构建目录（默认 `build`） |
| `--jobs` | `-j` | 并行编译线程数（默认 CPU 核数） |
| `--filter` | `-R` | 只运行名字匹配 regex 的测试 |
| `--clean` | `-c` | 先清空构建目录再全新构建 |
| `--list` | `-l` | 列出所有测试后退出 |
| `--no-build` | | 跳过配置/构建，直接运行 |
| `--config` | | 构建配置（默认 `Debug`） |
| `--enable-opencv` / `--disable-opencv` | | 打开/关闭 `TASK_GRAPH_ENABLE_OPENCV`（默认开） |
| `--sdk` | | 先构建 SDK + 独立 demo 插件，再跑含 `test_plugin_abi` 的全部测试 |
| `--verbose` | `-v` | ctest 详细输出 |
| `--help` | `-h` | 显示帮助 |

> 失败时自动打印失败用例的完整输出（`ctest --output-on-failure`）。

---

## Windows 脚本（`*.ps1`，已退化为 thin shim）

> **注意**：自跨平台 Python 脚本上线后，`*.ps1` 与 `*.sh` 都已退化为 thin shim，仅转发到同名 `.py`（下个版本删除）。Windows 上**推荐直接用 Python**：`python scripts\run_tests.py`、`python scripts\run_graph_studio.py` 等。下方历史用法仍可工作（shim 会自动转发），但参数风格为旧 PowerShell 形式。

Windows 上的 PowerShell 脚本与 `*.sh` 一一对应，用 Visual Studio 多配置生成器构建（构建和安装步骤都带 `--config`）。脚本会自动探测本机的 cmake（PATH 上找不到时回退到 VS 2022 自带的 cmake）、OpenCV（`C:\opencv\build\x64\vc16` 或环境变量 `OPENCV_DIR`）和 Qt（`run_graph_studio.ps1` 探测 `C:\Qt\<版本>\msvc2022_64`）。所有 `.ps1` 都支持 `-Cmake <path>` 手动指定 cmake.exe，也都支持 `-Help` 打印用法（等价于 `*.sh` 的 `-h|--help`，底层走 PowerShell comment-based help）。

```powershell
# 构建 + 运行全部测试（默认 Debug）
scripts\run_tests.ps1
scripts\run_tests.ps1 -Filter port -Verbose
scripts\run_tests.ps1 -Clean -DisableOpenCv
scripts\run_tests.ps1 -Sdk                       # 先 build_sdk.ps1 + 独立 demo 插件，再跑含 test_plugin_abi 的全部测试

# 构建 + 启动 GraphStudio（自动设好 PATH / TASK_GRAPH_PLUGINS_PATH）
scripts\run_graph_studio.ps1
scripts\run_graph_studio.ps1 -Clean -BuildOnly
scripts\run_graph_studio.ps1 -Test            # headless ctest（QT_QPA_PLATFORM=offscreen）

# 构建 SDK 前缀 + 独立编译 demo 插件（见下节）
scripts\build_sdk.ps1
scripts\build_plugin_standalone.ps1 examples\plugins\demo
```

> 与 `*.sh` 的差异：Windows 上不能传 `-DCMAKE_BUILD_TYPE`，改用 `--config`；VS 多配置生成器把产物放在 `<build>\<Config>\` 子目录（如 `build\Debug\`），`run_graph_studio.ps1` 会自动把 `build\Debug\task_graph.lib` 拷到 `build\` 以匹配 app 的 `link_directories(../build)`。

---

## SDK 构建与独立插件编译

`build_sdk.*` 把 task_graph 框架（头文件 + 动态库 + CMake 包配置）安装到一个可分发的前缀，`build_plugin_standalone.*` 仅依赖该前缀独立编译一个插件为运行时动态库，完全不引用主仓库源码。典型流程是先装 SDK 再编插件：

```bash
# macOS / Linux
scripts/build_sdk.sh                              # 默认前缀 build/sdk，Release
scripts/build_plugin_standalone.sh examples/plugins/demo
scripts/build_plugin_standalone.sh submodules/opencv/image_processing/image_filtering --opencv
```

```powershell
# Windows
scripts\build_sdk.ps1                             # 默认前缀 build\sdk，Release
scripts\build_plugin_standalone.ps1 examples\plugins\demo
scripts\build_plugin_standalone.ps1 submodules\opencv\image_processing\image_filtering -EnableOpenCv
```

### build_sdk.ps1 / build_sdk.sh

| 参数 (.ps1) | 参数 (.sh) | 说明 |
|---|---|---|
| `-Prefix` | `--prefix` | SDK 安装前缀（默认 `<root>/build/sdk`） |
| `-BuildDir` | `--build-dir` | CMake 构建目录（默认 `<root>/build/sdk-build`） |
| `-Config` | `--build-type` | 构建类型（默认 `Release`） |
| `-Jobs` | `-j` / `--jobs` | 并行编译线程数（默认 CPU 核数） |
| `-Clean` | — | 先清空构建目录（仅 .ps1，见下方"SDK 构建目录被污染"排查） |
| `-DisableOpenCv` | `--no-opencv` | 关闭 OpenCV（默认跟随主项目 ON） |
| `-OpenCvDir` | — | OpenCV 前缀（仅 .ps1，默认自动探测；.sh 走系统 find_package） |
| `-Cmake` | — | cmake.exe 路径（仅 .ps1） |

以 `TASK_GRAPH_BUILD_SUBMODULES=OFF` 独立构建，主仓库不编译任何内置子模块源码。产物：

```
<prefix>/include/{plugin_api.hpp, task_graph/**}
<prefix>/lib/{libtask_graph.dylib | libtask_graph.so | task_graph.lib + task_graph.dll}
<prefix>/lib/cmake/task_graph/{task_graphConfig.cmake, task_graphTargets.cmake, SdkUtil.cmake}
```

### build_plugin_standalone.ps1 / build_plugin_standalone.sh

| 参数 (.ps1) | 参数 (.sh) | 说明 |
|---|---|---|
| `SourceDir`（位置参数） | 第一个位置参数 | 插件源目录（须含 CMakeLists.txt） |
| `-SdkDir` | `--sdk` | SDK 前缀（默认 `<root>/build/sdk`） |
| `-OutRoot` | `--out-root` | 输出根目录（默认 `<root>/build/standalone/plugins`） |
| `-Config` | — | 构建类型（默认 `Release`；.sh 把 Release 写死、不解析构建类型，仅 .ps1 可调） |
| `-Jobs` | `-j` / `--jobs` | 并行编译线程数（默认 CPU 核数） |
| `-EnableOpenCv` | `--opencv` | 打开 OpenCV 依赖（默认 OFF） |
| `-Clean` | — | 先清空插件的构建目录（仅 .ps1） |
| `-Cmake` | — | cmake.exe 路径（仅 .ps1） |

产物是运行时可 dlopen / LoadLibrary 的动态库：`<OutRoot>/<name>/<name>.{dylib,so,dll}`。Windows 上 VS 多配置生成器会把它放在 `<OutRoot>/<name>/<Config>/<name>.dll`，脚本结尾会打印实际路径。

### 排查：SDK 配置报 "include could not find requested file: SdkUtil.cmake"

这是 SDK 构建目录被**之前的完整构建**（如 `run_tests.ps1`）污染导致的。完整构建把 `TASK_GRAPH_BUILD_SUBMODULES=ON` 写进了 `CMakeCache.txt`；CMake 的布尔缓存语义下，后续命令行的 `-DTASK_GRAPH_BUILD_SUBMODULES=OFF` **会被静默忽略**（只有 `ON` 或同名新变量才会覆盖）。结果子模块仍被加载，其 `find_package(task_graph)` 找到构建目录里残缺的 `task_graphConfig.cmake`（它引用的 `SdkUtil.cmake` / `task_graphTargets.cmake` 只在 `cmake --install` 后才生成）→ 报错。

两种修法（任选其一）：

```powershell
# 方法 1：用 -Clean 清掉污染的缓存（build_sdk.ps1 会自动检测并提示）
scripts\build_sdk.ps1 -Clean

# 方法 2：用独立的构建目录（默认就是 build\sdk-build，别复用 build\）
scripts\build_sdk.ps1 -BuildDir build\sdk-build
```

`build_sdk.ps1` 会检测到 `CMakeCache.txt` 里的 `TASK_GRAPH_BUILD_SUBMODULES=ON` 并直接报错提示用 `-Clean`，避免踩这个坑。

### 让 test_plugin_abi 用上独立编译的 demo 插件

`tests/test_plugin_abi.cpp` 默认查找 `<build>/standalone/plugins/demo/demo_plugin.{dylib,so,dll}`，找不到就 soft-skip（不算失败）。

一键做法（推荐）：`run_tests.ps1 -Sdk`（对应 `run_tests.sh --sdk`）会自动跑 `build_sdk.ps1` + `build_plugin_standalone.ps1`，并把 `TASK_GRAPH_DEMO_PLUGIN` 指向 `build\standalone\plugins\demo\Release\demo_plugin.dll`：

```powershell
scripts\run_tests.ps1 -Sdk
```

手动做法（或只想单独跑 `test_plugin_abi` 时）：Windows 上由于产物在 `<Config>` 子目录，需要显式指向它：

```powershell
$env:TASK_GRAPH_DEMO_PLUGIN = "<root>\build\standalone\plugins\demo\Release\demo_plugin.dll"
scripts\run_tests.ps1 -Filter plugin_abi
```

macOS / Linux 上若用了非默认 `--out-root`，同理设 `TASK_GRAPH_DEMO_PLUGIN` 环境变量即可。

---

## generate_submodule.py — 子模块脚手架生成器

`generate_submodule.py` 用于快速生成符合 task_graph 框架规范的插件子模块骨架，免去手动搭建目录结构、编写 CMakeLists、声明任务类、实现插件注册的重复工作。

生成模板基于 [image_filtering](../submodules/opencv/image_processing/image_filtering) 子模块的实现模式，包含：

- `CMakeLists.txt` — 编译配置（可选 OpenCV 依赖）
- `include/{name}/{name}.hpp` — 任务类声明
- `src/{name}.cpp` — 任务实现 + 插件注册（`__attribute__((constructor/destructor))` + `extern "C"`）

## 快速开始

```bash
# 交互模式（推荐首次使用）
python3 scripts/generate_submodule.py

# 命令行模式
python3 scripts/generate_submodule.py \
    --dir submodules/my_plugins \
    --name math_ops \
    --tasks AddTask MultiplyTask:math_mul
```

## 命令行参数

| 参数 | 缩写 | 说明 |
|---|---|---|
| `--dir` | `-d` | 子模块输出目录（如 `submodules/opencv/image_processing`） |
| `--name` | `-n` | 子模块名称，须小写字母开头，仅含小写字母/数字/下划线 |
| `--tasks` | `-t` | task 列表（空格分隔），格式见下方 |
| `--opencv` | — | 启用 OpenCV 依赖（生成 `get_input_mat` 辅助函数） |
| `--desc` | — | 模块描述（用于 CMakeLists 的 `project(... DESCRIPTION)`） |
| `--force` | — | 覆盖已存在的文件（默认跳过） |

### Task 格式

| 输入格式 | type_name 推导规则 | 示例 |
|---|---|---|
| `ClassName` | 自动推导为 `{module}_{Class去Task后缀转snake_case}` | `BlurTask` + 模块 `image_filtering` → `image_filtering_blur` |
| `ClassName:type_name` | 显式指定 | `BlurTask:my_blur` → `my_blur` |

## 使用示例

### 示例 1：通用计算模块（多 task）

```bash
python3 scripts/generate_submodule.py \
    -d submodules/my_plugins \
    -n math_ops \
    -t AddTask MultiplyTask:math_mul \
    --desc "Math operations plugin"
```

生成结构：
```
submodules/my_plugins/math_ops/
├── CMakeLists.txt
├── include/math_ops/math_ops.hpp      # AddTask, MultiplyTask 类声明
└── src/math_ops.cpp                   # 实现 + 注册（type: math_ops_add, math_mul）
```

### 示例 2：OpenCV 图像处理模块

```bash
python3 scripts/generate_submodule.py \
    -d submodules/opencv/image_processing \
    -n my_filter \
    -t BlurTask:my_blur SharpenTask:my_sharpen \
    --opencv
```

OpenCV 模式额外生成：
- `get_input_mat()` 辅助函数（优先取 `cv::Mat`，回退 `Image::to_mat()`）
- CMakeLists 中的 `find_package(OpenCV REQUIRED)` 与 `TASK_GRAPH_ENABLE_OPENCV` 守卫
- execute() 中预置 `cv::Mat` 处理骨架

### 示例 3：交互模式

```bash
python3 scripts/generate_submodule.py
```

按提示依次输入目录、模块名、task 列表（每行一个，空行结束）、是否启用 OpenCV。

## 命名规范

脚本会对输入做严格校验，不符合规范时报错退出：

| 类型 | 规则 | 合法示例 | 非法示例 |
|---|---|---|---|
| 模块名 (`--name`) | 小写字母开头，仅含 `a-z0-9_` | `image_filtering`, `math_ops` | `Math-Ops`, `imageFiltering` |
| 类名 (task) | 大驼峰，字母数字 | `BlurTask`, `AddTask` | `addTask`, `blur_task` |
| type 名 | 小写字母开头，仅含 `a-z0-9_` | `opencv_blur`, `math_mul` | `Math-Add`, `Blur` |

## 生成后的接入步骤

1. **在 [subnode.json](../subnode.json) 中注册子模块**：

```json
{
  "name": "math_ops",
  "url": "./submodules/my_plugins/math_ops",
  "ref": "main",
  "type": "local",
  "tasks": ["math_ops_add", "math_mul"]
}
```

2. **实现 `src/{name}.cpp` 中的 `TODO` 逻辑**：
   - `execute()` — 任务核心逻辑
   - `check_input()` — 输入数据类型校验

3. **重新 cmake 配置并编译**：

```bash
cmake -S . -B build
cmake --build build -j
```

4. **在 DAG 中使用**：

```cpp
dag.add_plugin_task("math_ops_add");
dag.add_plugin_task("math_mul");
```

## 生成的代码特征

与 image_filtering 子模块保持一致的最佳实践：

- **type 名称集中定义**：`const char* const kXxxType`（规避全局 `std::string` 静态初始化顺序问题）
- **`type()` 返回引用稳定**：`static const std::string type(kXxxType)`
- **双重插件注册**：同时提供 `__attribute__((constructor/destructor))`（自动加载）和 `extern "C" register_plugin/unregister_plugin`（手动加载）
- **CMake 主项目路径兼容**：通过 `TASK_GRAPH_ROOT` 变量，既支持 `add_subdirectory` 集成构建，也支持 `-DTASK_GRAPH_ROOT=...` 独立构建

## 常见问题

### Q: 生成时报错 "已存在，使用 --force 覆盖"

目标目录已有同名文件。确认要覆盖后加 `--force`。

### Q: OpenCV 模块编译时提示 `opencv2/opencv.hpp not found`

需在主项目 cmake 时启用 OpenCV：`cmake -DTASK_GRAPH_ENABLE_OPENCV=ON ...`。未启用时 CMakeLists 会自动跳过编译（`return()`）。

### Q: 生成的模块独立 cmake 编译报错 `library 'task_graph' not found`

子模块设计为主项目的子目录，`task_graph` 库 target 只在主项目构建树中存在。独立构建需指定主项目根：`cmake -DTASK_GRAPH_ROOT=/path/to/task_graph ...`，但仍需主项目先编译出 `libtask_graph`。推荐直接用 `add_subdirectory` 集成方式。
