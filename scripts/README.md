# task_graph 子模块脚手架生成器

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
