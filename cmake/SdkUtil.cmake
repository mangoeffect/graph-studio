# ============================================================
# SdkUtil.cmake — 子模块插件接入 task_graph SDK 的统一入口。
#
# 在子模块的 CMakeLists 里调用：
#     include(SdkUtil)                # 独立构建时已由 task_graphConfig 引入
#     use_task_graph_sdk(<target>)
#
# 三种接入模式（按优先级）：
#   1) 构建期内链接（add_subdirectory）：主仓库已定义 task_graph target
#   2) 独立构建：find_package(task_graph) 提供的 task_graph::task_graph 导入目标
#   3) 旧式：-DTASK_GRAPH_ROOT=<repo>，链接仓库根目录下预构建的 libtask_graph
#
# 这样子模块仓库可以完全脱离主仓库源码独立编译，产物是一个供运行时 dlopen 的
# SHARED 动态库（桌面平台）。
# ============================================================

function(use_task_graph_sdk target)
    if(TARGET task_graph)
        # 模式 1：作为主仓库的 add_subdirectory 子目录
        target_link_libraries(${target} PRIVATE task_graph)
    elseif(TARGET task_graph::task_graph)
        # 模式 2：独立构建 + find_package(task_graph)
        target_link_libraries(${target} PRIVATE task_graph::task_graph)
    elseif(DEFINED TASK_GRAPH_ROOT)
        # 模式 3（legacy，供 js_task 等旧式独立构建）：仓库路径 + 预构建库
        if(EXISTS "${TASK_GRAPH_ROOT}/include/plugin_api.hpp")
            target_include_directories(${target} PRIVATE "${TASK_GRAPH_ROOT}/include")
        endif()
        if(EXISTS "${TASK_GRAPH_ROOT}/build")
            target_link_directories(${target} PRIVATE "${TASK_GRAPH_ROOT}/build")
        endif()
        target_link_libraries(${target} PRIVATE task_graph)
    else()
        message(FATAL_ERROR
            "use_task_graph_sdk(${target}): 找不到 task_graph。\n"
            "  独立编译插件时请指定：\n"
            "    -Dtask_graph_DIR=<sdk>/lib/cmake/task_graph   (SDK 前缀，见 scripts/build_sdk.sh)\n"
            "  或旧式：-DTASK_GRAPH_ROOT=/path/to/task_graph   （仓库根 + 预构建 libtask_graph）")
    endif()
endfunction()