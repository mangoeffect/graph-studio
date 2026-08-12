cmake_minimum_required(VERSION 3.16)

project(demo_plugin
    VERSION 1.0.0
    DESCRIPTION "Standalone demo plugin for task_graph SDK"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 独立编译演示：仅依赖已安装的 task_graph SDK（脚本 scripts/build_plugin_standalone.py），
# 不引用主仓库源码。产物 demo_plugin.dylib 供运行时 dlopen。
add_library(demo_plugin SHARED src/demo_plugin.cpp)

find_package(task_graph QUIET)
if(NOT TARGET task_graph
   AND NOT TARGET task_graph::task_graph
   AND NOT DEFINED TASK_GRAPH_ROOT)
    message(FATAL_ERROR
        "demo_plugin: 找不到 task_graph SDK。请先运行 scripts/build_sdk.py 生成 SDK 前缀，然后配置：\n"
        "  cmake -S examples/plugins/demo -B <build> -Dtask_graph_DIR=<sdk>/lib/cmake/task_graph")
endif()
use_task_graph_sdk(demo_plugin)

if(MSVC)
    target_compile_options(demo_plugin PRIVATE /W3)
else()
    target_compile_options(demo_plugin PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

# 桌面 SHARED 插件命名规范：无 lib 前缀 + 平台后缀（与内置子模块一致）
set_target_properties(demo_plugin PROPERTIES PREFIX "" SUFFIX ".so")
if(APPLE)
    set_target_properties(demo_plugin PROPERTIES SUFFIX ".dylib")
endif()
if(WIN32)
    set_target_properties(demo_plugin PROPERTIES SUFFIX ".dll")
endif()