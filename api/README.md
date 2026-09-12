# api/ — 消费者 API(独立存放)

宿主 App 消费框架的两套"纯消费"API(只吃 JSON、不碰内部结构)单独放在本目录,
与框架内部实现(`src/`、`include/task_graph/`)分离。头文件逻辑路径不变
(`<task_graph/dag_config.hpp>` 等),构建与安装自动合并到统一的 include 面。

## 目录

```
api/
├── include/task_graph/
│   ├── dag_config.hpp     # 1) 只读 DAG JSON 配置 API
│   ├── sdk.hpp            # 2) SDK 生命周期 API(C++)
│   └── tg_sdk_c.h         #    SDK 生命周期 API(纯 C,extern "C")
└── src/                   # 三者实现(编入核心库 task_graph)
```

## 1. 只读 DAG JSON 配置(`DagConfigLoader` / `DagConfig`)

只消费 JSON 文件或 JSON 字符串;零副作用加载(不触碰 PluginRegistry、
不实例化任务)→ 不可变快照 + 结构化诊断(`DagConfigIssue`:severity /
stage / JSON pointer / 行列号,一次报全)。需要执行时显式 `to_dag()`。
设计:`dev-docs/dag-config-api.md`。

## 2. SDK 生命周期(`TaskGraphSdk` C++ / `tg_sdk_*` 纯 C)

create → init(全局配置:日志回调/env/globals/线程数)→ load_graph →
bind_input/bind_output(复用已配置的图,只换输入输出)→ execute →
update_graph(diff 增量更新,热状态保留)→ shutdown。
图边界用内置任务类型 `io_input` / `io_output` 标记(`data_type` 参数做
绑定期类型前置校验)。设计:`dev-docs/sdk-lifecycle-api.md`。

## 安装与打包

```sh
cmake -S . -B build && cmake --build build
cmake --install build --prefix /your/prefix        # 或 --prefix 前缀安装
```

安装面(见根 CMakeLists 的 install 规则):

- 库:`lib/libtask_graph.{dylib,so|a}` / `task_graph.lib`
- 头:`include/task_graph/`(主 include 与本目录 api/include 合并)
- CMake 包:`lib/cmake/task_graph/`(task_graphConfig.cmake +
  task_graphTargets.cmake + SdkUtil.cmake),消费方:

```cmake
find_package(task_graph REQUIRED)
target_link_libraries(my_app PRIVATE task_graph::task_graph)
```

C 消费者只需 `#include <task_graph/tg_sdk_c.h>` 并链接同一目标。
