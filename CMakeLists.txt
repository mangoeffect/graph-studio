cmake_minimum_required(VERSION 3.16)

project(task_graph
    VERSION 0.1.0
    DESCRIPTION "A lightweight DAG task execution framework in C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

option(TASK_GRAPH_ENABLE_CUDA "Enable CUDA support for GPU image operations" OFF)

if(TASK_GRAPH_ENABLE_CUDA)
    find_package(CUDA REQUIRED)
    include_directories(${CUDA_INCLUDE_DIRS})
    link_libraries(${CUDA_LIBRARIES})
    add_definitions(-DTASK_GRAPH_ENABLE_CUDA)
endif()

# Metal support (Apple only)
if(APPLE)
    option(TASK_GRAPH_ENABLE_METAL "Enable Metal support for GPU image operations" OFF)
    if(TASK_GRAPH_ENABLE_METAL)
        add_definitions(-DTASK_GRAPH_ENABLE_METAL)
    endif()
endif()

# Vulkan support
option(TASK_GRAPH_ENABLE_VULKAN "Enable Vulkan support for GPU image operations" OFF)
if(TASK_GRAPH_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    include_directories(${Vulkan_INCLUDE_DIRS})
    add_definitions(-DTASK_GRAPH_ENABLE_VULKAN)
endif()

option(TASK_GRAPH_ENABLE_OPENCV "Enable OpenCV support for image conversion" OFF)

if(TASK_GRAPH_ENABLE_OPENCV)
    # WASM 下优先用 emcmake 构建的 OpenCV 静态库
    if(EMSCRIPTEN AND EXISTS "${CMAKE_SOURCE_DIR}/build_wasm/opencv/install/lib/cmake/opencv4")
        set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_wasm/opencv/install/lib/cmake/opencv4")
    endif()
    find_package(OpenCV REQUIRED)
    include_directories(${OpenCV_INCLUDE_DIRS})
    add_definitions(-DTASK_GRAPH_ENABLE_OPENCV)
endif()

if(EMSCRIPTEN)
    set(TASK_GRAPH_LIB_TYPE STATIC)
else()
    set(TASK_GRAPH_LIB_TYPE SHARED)
endif()

add_library(task_graph ${TASK_GRAPH_LIB_TYPE}
    src/task.cpp
    src/dag.cpp
    src/compiler.cpp
    src/thread_pool.cpp
    src/executor.cpp
    src/context.cpp
    src/plugin.cpp
    src/plugin_registry.cpp
    src/dag_serializer.cpp
    src/logger.cpp
    src/data_types.cpp
    src/gpu_image_ops.cpp
    src/task_manager.cpp
    src/task_params.cpp
    src/profiler.cpp
)

# Add GPU backend sources
if(TASK_GRAPH_ENABLE_METAL AND APPLE)
    target_sources(task_graph PRIVATE
        src/gpu_backends/metal_backend.mm
    )
    set_source_files_properties(src/gpu_backends/metal_backend.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    find_library(METAL_FRAMEWORK Metal)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    target_link_libraries(task_graph PUBLIC
        ${METAL_FRAMEWORK}
        ${CORE_FOUNDATION_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
endif()

if(TASK_GRAPH_ENABLE_VULKAN)
    target_sources(task_graph PRIVATE
        src/gpu_backends/vulkan_backend.cpp
    )
endif()

target_include_directories(task_graph
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(task_graph
    PUBLIC
        $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:Threads::Threads>
)

if(TASK_GRAPH_ENABLE_OPENCV)
    target_link_libraries(task_graph PRIVATE ${OpenCV_LIBS})
endif()

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(task_graph
        PUBLIC
            dl
    )
endif()

target_compile_definitions(task_graph
    PRIVATE
        TASK_GRAPH_BUILD
)

target_compile_options(task_graph
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -O2
)

# ============================================================
# 库测试目标（合并精简后）
# ============================================================
# 一个 helper：tests/test_util.hpp（纯头文件，无需目标）

# 便捷宏：定义一个链接 task_graph 的测试可执行文件
function(add_tg_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE task_graph)
endfunction()

add_tg_test(test_dag)
add_tg_test(test_ports)
add_tg_test(test_params)
add_tg_test(test_serializer)
add_tg_test(test_data_types)
add_tg_test(test_gpu_image)
add_tg_test(test_task_params)
add_tg_test(test_profiler)
add_tg_test(test_type_registry)
add_tg_test(test_logger)

# test_plugin 合并了 subnode 内置插件测试，需链接子模块 + filesystem
add_tg_test(test_plugin)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(test_plugin PRIVATE stdc++fs)
endif()

if(TASK_GRAPH_ENABLE_OPENCV)
    add_executable(test_opencv_convert tests/test_opencv_convert.cpp)
    target_link_libraries(test_opencv_convert PRIVATE task_graph ${OpenCV_LIBS})

    add_executable(test_opencv_image_filtering tests/test_opencv_image_filtering.cpp)
    target_link_libraries(test_opencv_image_filtering PRIVATE task_graph ${OpenCV_LIBS} image_filtering)
endif()

# ============================================================
# 示例
# ============================================================
add_executable(example_basic examples/basic.cpp)
target_link_libraries(example_basic PRIVATE task_graph)

add_executable(example_parallel examples/parallel.cpp)
target_link_libraries(example_parallel PRIVATE task_graph)

add_executable(example_plugin examples/plugin.cpp)
target_link_libraries(example_plugin PRIVATE task_graph)

add_executable(example_multi_output examples/multi_output.cpp)
target_link_libraries(example_multi_output PRIVATE task_graph)

# ============================================================
# 示例插件动态库
# ============================================================
add_library(task_plugin_example MODULE plugins/example_plugin.cpp)
target_include_directories(task_plugin_example PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(task_plugin_example PRIVATE task_graph)
set_target_properties(task_plugin_example PROPERTIES PREFIX "" SUFFIX ".so")
if(APPLE)
    set_target_properties(task_plugin_example PROPERTIES SUFFIX ".dylib")
endif()
if(WIN32)
    set_target_properties(task_plugin_example PROPERTIES SUFFIX ".dll")
endif()

# ============================================================
# 子模块（subnode）：编译期链接的内置插件
# ============================================================
include(Subnode)
parse_subnode_json("${PROJECT_SOURCE_DIR}/subnode.json")
foreach(submodule ${SUBMODULES})
    add_subnode_module(${submodule})
endforeach()

# test_plugin 依赖 subnode 内置插件（task1/task2/task_processor）
foreach(submodule ${SUBMODULES})
    if(TARGET ${submodule})
        target_link_libraries(test_plugin PRIVATE ${submodule})
    endif()
endforeach()

# ============================================================
# ctest 注册（全部核心测试）
# ============================================================
enable_testing()
add_test(NAME test_dag COMMAND test_dag)
add_test(NAME test_ports COMMAND test_ports)
add_test(NAME test_params COMMAND test_params)
add_test(NAME test_serializer COMMAND test_serializer)
add_test(NAME test_data_types COMMAND test_data_types)
add_test(NAME test_gpu_image COMMAND test_gpu_image)
add_test(NAME test_task_params COMMAND test_task_params)
add_test(NAME test_profiler COMMAND test_profiler)
add_test(NAME test_type_registry COMMAND test_type_registry)
add_test(NAME test_logger COMMAND test_logger)
add_test(NAME test_plugin COMMAND test_plugin)
if(TASK_GRAPH_ENABLE_OPENCV)
    add_test(NAME test_opencv_convert COMMAND test_opencv_convert)
    add_test(NAME test_opencv_image_filtering COMMAND test_opencv_image_filtering)
endif()
