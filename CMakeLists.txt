cmake_minimum_required(VERSION 3.18)

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

# ============================================================
# 平台检测：iOS / Android / 移动端统一标记
# ============================================================
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TASK_GRAPH_IOS ON)
endif()
if(ANDROID)
    set(TASK_GRAPH_ANDROID ON)
endif()
if(TASK_GRAPH_IOS OR TASK_GRAPH_ANDROID)
    set(TASK_GRAPH_MOBILE ON)
endif()

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
    add_definitions(-DTASK_GRAPH_ENABLE_VULKAN)

    # shaderc（Vulkan SDK 自带）：Vulkan compute 的 GLSL -> SPIR-V 运行时编译。
    # 可选依赖--找不到时 Vulkan 后端仅保留 buffer 上传/下载，compute 降级。
    find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp
        HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
    find_library(SHADERC_LIBRARY NAMES shaderc_combined
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
    find_library(SHADERC_LIBRARY_DEBUG NAMES shaderc_combinedd
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
    set(TG_SHADERC_LINK "")
    set(TG_SHADERC_DEFINE "")
    if(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY AND SHADERC_LIBRARY_DEBUG)
        message(STATUS "Vulkan compute enabled: shaderc at ${SHADERC_LIBRARY} / ${SHADERC_LIBRARY_DEBUG}")
        set(TG_SHADERC_LINK optimized ${SHADERC_LIBRARY} debug ${SHADERC_LIBRARY_DEBUG})
        set(TG_SHADERC_DEFINE TASK_GRAPH_VULKAN_COMPUTE)
    elseif(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY)
        if(MSVC)
            # SDK 只带 release 版 shaderc_combined.lib：Debug 配置（MDd）链接它
            # 会 LNK2038 运行时库不匹配，仅在非 Debug 配置启用 shaderc/compute。
            message(STATUS "Vulkan compute enabled in non-Debug configs only: "
                           "release-only shaderc at ${SHADERC_LIBRARY}")
            set(TG_SHADERC_LINK "$<$<NOT:$<CONFIG:Debug>>:${SHADERC_LIBRARY}>")
            set(TG_SHADERC_DEFINE "$<$<NOT:$<CONFIG:Debug>>:TASK_GRAPH_VULKAN_COMPUTE>")
        else()
            message(STATUS "Vulkan compute enabled: shaderc at ${SHADERC_LIBRARY}")
            set(TG_SHADERC_LINK ${SHADERC_LIBRARY})
            set(TG_SHADERC_DEFINE TASK_GRAPH_VULKAN_COMPUTE)
        endif()
    else()
        message(WARNING "shaderc not found in Vulkan SDK; Vulkan compute disabled (buffer ops only)")
    endif()
endif()

option(TASK_GRAPH_ENABLE_OPENCV "Enable OpenCV support for image conversion" ON)

# 桌面平台默认把 submodules/ 内置插件一起构建进同一棵构建树（供 scripts/run_tests.py
# 与 GraphStudio 开发工作流使用）。关闭后主仓库完全独立编译，插件改为运行时 dlopen：
#   cmake -S . -B build -DTASK_GRAPH_BUILD_SUBMODULES=OFF
option(TASK_GRAPH_BUILD_SUBMODULES "Build builtin subnodes from submodules/ into this build tree" ON)

# 单元测试（GoogleTest，FetchContent 首次配置时下载并缓存到 build/_deps）。
# WASM 不构建；离线/最小化构建可 -DTASK_GRAPH_BUILD_TESTS=OFF 跳过（免联网）。
option(TASK_GRAPH_BUILD_TESTS "Build unit tests (fetches GoogleTest at configure time)" ON)

if(TASK_GRAPH_ENABLE_OPENCV)
    # WASM 下优先用 emcmake 构建的 OpenCV 静态库
    if(EMSCRIPTEN AND EXISTS "${CMAKE_SOURCE_DIR}/build_wasm/opencv/install/lib/cmake/opencv4")
        set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_wasm/opencv/install/lib/cmake/opencv4")
    elseif(TASK_GRAPH_IOS AND EXISTS "${CMAKE_SOURCE_DIR}/build_ios/opencv/install/lib/cmake/opencv4")
        set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_ios/opencv/install/lib/cmake/opencv4")
    elseif(TASK_GRAPH_ANDROID AND EXISTS "${CMAKE_SOURCE_DIR}/build_android/opencv/install/lib/cmake/opencv4")
        set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_android/opencv/install/lib/cmake/opencv4")
    endif()
    find_package(OpenCV REQUIRED)
    include_directories(${OpenCV_INCLUDE_DIRS})
    add_definitions(-DTASK_GRAPH_ENABLE_OPENCV)
endif()

if(EMSCRIPTEN OR TASK_GRAPH_MOBILE)
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
    src/gpu_kernel_library.cpp
    src/gpu_image_task.cpp
    src/task_manager.cpp
    src/task_params.cpp
    src/profiler.cpp
)

# 核心类（INode/DAG/DAGExecutor 等）没有显式 __declspec(dllexport)。
# macOS/Linux 默认 -fvisibility 就能导出，MSVC 必须显式导出；这里让
# CMake 自动生成 .def 导出 DLL 的全部符号（含这些 C++ 类的方法）。
# 内部对象（TypeRegistry 等）由此也可被测试程序正常导入。
if(WIN32 AND TASK_GRAPH_LIB_TYPE STREQUAL "SHARED")
    set_target_properties(task_graph PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

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

if(TASK_GRAPH_ENABLE_VULKAN AND NOT EMSCRIPTEN)
    target_link_libraries(task_graph PRIVATE Vulkan::Vulkan)
    if(TG_SHADERC_LINK)
        target_link_libraries(task_graph PRIVATE ${TG_SHADERC_LINK})
        target_compile_definitions(task_graph PRIVATE ${TG_SHADERC_DEFINE})
    endif()
    if(SHADERC_INCLUDE_DIR)
        target_include_directories(task_graph PRIVATE ${SHADERC_INCLUDE_DIR})
    endif()
endif()

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

if(MSVC)
    # MSVC 各配置自带优化档（Debug /Od + /RTC1，Release /O2 /DNDEBUG），无需重复指定
    target_compile_options(task_graph PRIVATE /W3)
else()
    target_compile_options(task_graph PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

# ============================================================
# 库测试目标（合并精简后）— 移动端 / WASM 跳过
# ============================================================
# GoogleTest 经 FetchContent 拉取并缓存在 build/_deps。
# gtest_discover_tests 用 PRE_TEST 模式：用例发现推迟到 ctest 运行时，
# 避免构建期执行测试 exe 找不到 DLL（Windows 下 task_graph / opencv_world
# 的搜索路径由 scripts 在运行 ctest 前注入进程环境）。需要 CMake >= 3.18。

if(NOT TASK_GRAPH_MOBILE)

if(NOT EMSCRIPTEN AND TASK_GRAPH_BUILD_TESTS)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# 便捷宏：定义一个链接 task_graph 的测试可执行文件。
# 每条 TEST(Suite, case) 注册为独立 ctest 条目（名字 "<exe>.<Suite>.<case>"），
# 一键脚本里逐用例可见，并支持 ctest -R 用例级过滤。
function(add_tg_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE task_graph GTest::gtest_main)
    gtest_discover_tests(${name}
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "${name}."
    )
endfunction()

add_tg_test(test_dag)
add_tg_test(test_ports)
add_tg_test(test_params)
add_tg_test(test_serializer)
add_tg_test(test_path_utils)
add_tg_test(test_data_types)
add_tg_test(test_task_params)
add_tg_test(test_profiler)
add_tg_test(test_type_registry)
add_tg_test(test_logger)

add_tg_test(test_plugin)

# 流式执行器（IStreamSource/IStreamSink 逐帧循环）单元测试：纯 mock 源/汇，不依赖 OpenCV。
add_tg_test(test_stream_executor)

# 插件 ABI 动态加载测试（独立编译的 demo 插件由 scripts/build_sdk.py +
# scripts/build_plugin_standalone.py 生成；文件缺失时 soft-SKIP）
add_tg_test(test_plugin_abi)
if(WIN32)
    set(_TASK_GRAPH_DEMO_PLUGIN_EXT dll)
elseif(APPLE)
    set(_TASK_GRAPH_DEMO_PLUGIN_EXT dylib)
else()
    set(_TASK_GRAPH_DEMO_PLUGIN_EXT so)
endif()
target_compile_definitions(test_plugin_abi PRIVATE
    TASK_GRAPH_DEMO_PLUGIN="${CMAKE_BINARY_DIR}/standalone/plugins/demo/demo_plugin.${_TASK_GRAPH_DEMO_PLUGIN_EXT}"
)

if(TASK_GRAPH_ENABLE_OPENCV)
    add_executable(test_opencv_convert tests/test_opencv_convert.cpp)
    target_link_libraries(test_opencv_convert PRIVATE task_graph ${OpenCV_LIBS} GTest::gtest_main)
    gtest_discover_tests(test_opencv_convert
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "test_opencv_convert."
    )
endif()

endif() # NOT EMSCRIPTEN AND TASK_GRAPH_BUILD_TESTS

# ============================================================
# 示例
# ============================================================
add_executable(example_basic examples/basic.cpp)
target_link_libraries(example_basic PRIVATE task_graph)

add_executable(example_parallel examples/parallel.cpp)
target_link_libraries(example_parallel PRIVATE task_graph)

add_executable(example_multi_output examples/multi_output.cpp)
target_link_libraries(example_multi_output PRIVATE task_graph)

endif() # NOT TASK_GRAPH_MOBILE

# ============================================================
# SDK 辅助函数（find/find_package/legacy 三种模式，见 cmake/SdkUtil.cmake）
# ============================================================
include(Subnode)
include(SdkUtil)

# ============================================================
# 子模块（subnode）：编译期链接的内置插件
# 默认 ON（保留现有开发工作流）。设为 OFF 时主仓库完全独立编译，
# 桌面插件改由运行时 dlopen（见 PluginBootstrap / PluginLoader）。
# ============================================================
if(TASK_GRAPH_BUILD_SUBMODULES)
    parse_subnode_json("${PROJECT_SOURCE_DIR}/subnode.json")
    foreach(submodule ${SUBMODULES})
        add_subnode_module(${submodule})
    endforeach()
endif()

if(NOT TASK_GRAPH_MOBILE)

# ============================================================
# ctest 注册：核心测试经 gtest_discover_tests 逐用例注册（见上方测试块），
# 子模块测试由各自 CMakeLists 的 add_test 注册进同一面板。
# ============================================================
enable_testing()

endif() # NOT TASK_GRAPH_MOBILE

# ============================================================
# SDK 安装规则：安装 task_graph 库 + 全部公共头文件 + CMake 包配置。
# 独立编译插件的子仓库用 find_package(task_graph) 拉取，
# 参见 cmake/SdkUtil.cmake 与 scripts/build_sdk.py / build_plugin_standalone.py。
# ============================================================
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(TASK_GRAPH_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/task_graph")

install(TARGETS task_graph
    EXPORT task_graphTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# task_graphConfig.cmake：反射安装目标 + 自带 use_task_graph_sdk() 函数
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/task_graphConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/task_graphConfig.cmake"
    INSTALL_DESTINATION "${TASK_GRAPH_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/task_graphConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(EXPORT task_graphTargets
    FILE task_graphTargets.cmake
    NAMESPACE task_graph::
    DESTINATION "${TASK_GRAPH_INSTALL_CMAKEDIR}"
)

install(FILES
    cmake/SdkUtil.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/task_graphConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/task_graphConfigVersion.cmake"
    DESTINATION "${TASK_GRAPH_INSTALL_CMAKEDIR}"
)
