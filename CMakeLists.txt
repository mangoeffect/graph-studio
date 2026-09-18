cmake_minimum_required(VERSION 3.18)

project(task_graph
    VERSION 0.1.0
    DESCRIPTION "A lightweight DAG task execution framework in C++"
    LANGUAGES C CXX   # C:纯 C API(tg_sdk_c.h)的头文件编译校验
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

# wgpu 统一 GPU 后端（wgpu-native C API，Metal/Vulkan/WebGPU 三端一张后端，
# shader 一律 WGSL）——默认后端：app（GpuBootstrap）与子模块测试默认优先
# 选它，Metal/Vulkan 保留为回退与对照（TG_GPU_BACKEND / TG_*_TEST_FORCE_*）。
# 依赖 scripts/fetch_wgpu.py 下载的预构建产物：
#   python scripts/fetch_wgpu.py
# 构建脚本会自动调用（幂等）；找不到产物时 warn + 跳过（与 OpenCV/MNN 的
# "依赖缺失即降级"约定一致，回退 Metal/Vulkan）。
option(TASK_GRAPH_ENABLE_WGPU "Enable wgpu-native unified GPU backend" ON)
if(TASK_GRAPH_ENABLE_WGPU AND NOT EMSCRIPTEN AND NOT TASK_GRAPH_MOBILE)
    if(WIN32)
        set(WGPU_ARCH_DIR "windows-x86_64")
    elseif(APPLE)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE _wgpu_mach
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_wgpu_mach STREQUAL "arm64")
            set(WGPU_ARCH_DIR "macos-aarch64")
        else()
            set(WGPU_ARCH_DIR "macos-x86_64")
        endif()
    else()
        execute_process(COMMAND uname -m OUTPUT_VARIABLE _wgpu_mach
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_wgpu_mach STREQUAL "aarch64")
            set(WGPU_ARCH_DIR "linux-aarch64")
        else()
            set(WGPU_ARCH_DIR "linux-x86_64")
        endif()
    endif()
    set(WGPU_INSTALL "${CMAKE_SOURCE_DIR}/build/wgpu/install/${WGPU_ARCH_DIR}")
    find_path(WGPU_INCLUDE_DIR webgpu/webgpu.h
        HINTS "${WGPU_INSTALL}/include" "$ENV{WGPU_SDK}/include")
    find_library(WGPU_LIBRARY NAMES wgpu_native
        HINTS "${WGPU_INSTALL}/lib" "$ENV{WGPU_SDK}/lib")
    if(WGPU_INCLUDE_DIR AND WGPU_LIBRARY)
        message(STATUS "wgpu-native found: ${WGPU_LIBRARY}")
        add_definitions(-DTASK_GRAPH_ENABLE_WGPU)
    else()
        message(WARNING "wgpu-native not found (run scripts/fetch_wgpu.py); "
                        "wgpu backend disabled")
    endif()
elseif(TASK_GRAPH_ENABLE_WGPU AND EMSCRIPTEN)
    # WASM：无 wgpu-native 产物——webgpu.h 来自 emscripten（链接 -sUSE_WEBGPU），
    # wgpu_compat.hpp 兼容层吸收 emsdk 3.1.37 旧命名。编译接线：后端编入 wasm，
    # init 在浏览器里优雅失败（真实初始化 = 独立里程碑）。
    add_definitions(-DTASK_GRAPH_ENABLE_WGPU)
endif()

# MNN 推理引擎（Alibaba MNN，直接编入核心库；非子模块插件）。
# 依赖 scripts/build_mnn.py 产出的预编译静态库（install/lib/libMNN.a）：
#   python scripts/build_mnn.py            # 本机桌面平台
# 找到库时定义 TASK_GRAPH_MNN_AVAILABLE（真实推理）；默认开启但未找到库时
# 任务照常注册（图可反序列化/编辑），execute 返回 FAILED 并给出可读错误。
option(TASK_GRAPH_ENABLE_MNN "Enable Alibaba MNN inference engine tasks" ON)
if(TASK_GRAPH_ENABLE_MNN)
    # 预编译库候选目录（对齐 OpenCV 预编译目录约定，build_mnn.py --platform 产出）
    if(TASK_GRAPH_IOS AND EXISTS "${CMAKE_SOURCE_DIR}/build_ios/mnn/install/include/MNN/Interpreter.hpp")
        set(MNN_INSTALL "${CMAKE_SOURCE_DIR}/build_ios/mnn/install")
    elseif(TASK_GRAPH_ANDROID)
        # build_mnn.py 按 ABI 分目录安装（install/<abi>/），优先当前 ABI
        if(EXISTS "${CMAKE_SOURCE_DIR}/build_android/mnn/install/${ANDROID_ABI}/include/MNN/Interpreter.hpp")
            set(MNN_INSTALL "${CMAKE_SOURCE_DIR}/build_android/mnn/install/${ANDROID_ABI}")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/build_android/mnn/install/include/MNN/Interpreter.hpp")
            set(MNN_INSTALL "${CMAKE_SOURCE_DIR}/build_android/mnn/install")
        endif()
    elseif(EMSCRIPTEN AND EXISTS "${CMAKE_SOURCE_DIR}/build_wasm/mnn/install/include/MNN/Interpreter.hpp")
        set(MNN_INSTALL "${CMAKE_SOURCE_DIR}/build_wasm/mnn/install")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/build/mnn/install/include/MNN/Interpreter.hpp")
        set(MNN_INSTALL "${CMAKE_SOURCE_DIR}/build/mnn/install")
    endif()

    if(MNN_INSTALL)
        if(MSVC)
            set(MNN_LIB_PATH "${MNN_INSTALL}/lib/MNN.lib")
        else()
            set(MNN_LIB_PATH "${MNN_INSTALL}/lib/libMNN.a")
        endif()
        if(EXISTS "${MNN_LIB_PATH}")
            message(STATUS "MNN inference enabled: ${MNN_LIB_PATH}")
            add_library(mnn_engine STATIC IMPORTED)
            set_target_properties(mnn_engine PROPERTIES
                IMPORTED_LOCATION "${MNN_LIB_PATH}"
                INTERFACE_INCLUDE_DIRECTORIES "${MNN_INSTALL}/include")
            set(TASK_GRAPH_MNN_AVAILABLE ON)
        else()
            message(WARNING "TASK_GRAPH_ENABLE_MNN=ON but MNN static lib not found "
                            "(expected ${MNN_INSTALL}/lib); building stub — run scripts/build_mnn.py")
        endif()
    else()
        message(WARNING "TASK_GRAPH_ENABLE_MNN=ON but no MNN install found "
                        "(candidates: build{,_ios,_android,_wasm}/mnn/install); building stub — "
                        "run scripts/build_mnn.py")
    endif()
endif()

# MediaPipe Vision 引擎（Google MediaPipe，直接编入核心库；原 submodules/
# mediapipe 插件已迁移至此，任务层与引擎 API 均为主框架公共能力）。依赖
# scripts/build_mediapipe.py 产出的预构建 C API 库（macOS/Linux:
# libvision.dylib|so，iOS/Android/WASM: libmediapipe_vision_c.a，Windows:
# vision.dll + vision.lib）。找到库时定义 TASK_GRAPH_MEDIAPIPE_AVAILABLE
#（真实推理）；默认开启但未找到库时任务照常注册（图可反序列化/编辑），
# execute 返回 FAILED 并给出可读错误。桌面预构建恒为 CPU-only
#（MEDIAPIPE_DISABLE_GPU——桌面 GPU 官方仅支持 Linux），额外定义
# TASK_GRAPH_MEDIAPIPE_CPU_ONLY 供引擎提前拦截 GPU delegate 请求
#（CPU-only 构建上请求 GPU 不报错而是挂死 graph 创建）。
option(TASK_GRAPH_ENABLE_MEDIAPIPE "Enable Google MediaPipe vision tasks" ON)
if(TASK_GRAPH_ENABLE_MEDIAPIPE)
    if(TASK_GRAPH_IOS)
        set(MP_CANDIDATES "${CMAKE_SOURCE_DIR}/build_ios/mediapipe/install")
    elseif(TASK_GRAPH_ANDROID)
        # build_mediapipe.py 按 ABI 分目录安装（install/<abi>/），优先当前 ABI
        set(MP_CANDIDATES
            "${CMAKE_SOURCE_DIR}/build_android/mediapipe/install/${ANDROID_ABI}"
            "${CMAKE_SOURCE_DIR}/build_android/mediapipe/install")
    elseif(EMSCRIPTEN)
        set(MP_CANDIDATES "${CMAKE_SOURCE_DIR}/build_wasm/mediapipe/install")
    else()
        set(MP_CANDIDATES "${CMAKE_SOURCE_DIR}/build/mediapipe/install")
    endif()

    set(MP_INSTALL "")
    foreach(mp_dir IN LISTS MP_CANDIDATES)
        if(EXISTS "${mp_dir}/include/mediapipe/tasks/c/vision/core/image.h")
            set(MP_INSTALL "${mp_dir}")
            break()
        endif()
    endforeach()

    if(MP_INSTALL)
        if(EXISTS "${MP_INSTALL}/lib/libvision.dylib")
            add_library(mediapipe_vision_c SHARED IMPORTED)
            set_target_properties(mediapipe_vision_c PROPERTIES
                IMPORTED_LOCATION "${MP_INSTALL}/lib/libvision.dylib"
                INTERFACE_INCLUDE_DIRECTORIES "${MP_INSTALL}/include")
            set(TASK_GRAPH_MEDIAPIPE_AVAILABLE ON)
            set(MEDIAPIPE_MP_DYLIB ON)
        elseif(EXISTS "${MP_INSTALL}/lib/libmediapipe_vision_c.a")
            add_library(mediapipe_vision_c STATIC IMPORTED)
            set_target_properties(mediapipe_vision_c PROPERTIES
                IMPORTED_LOCATION "${MP_INSTALL}/lib/libmediapipe_vision_c.a"
                INTERFACE_INCLUDE_DIRECTORIES "${MP_INSTALL}/include")
            # 合并静态库不含系统依赖：absl/XNNPACK 在 Android 需要 log/android，
            # iOS 侧 MediaPipe 计算器引用 Foundation/CoreGraphics 等（GPU 已禁用）
            if(ANDROID)
                set_property(TARGET mediapipe_vision_c APPEND PROPERTY
                    INTERFACE_LINK_LIBRARIES log android)
            elseif(TASK_GRAPH_IOS)
                # ios_opencv 静态 framework 的 linkopts 清单（third_party/opencv_ios.BUILD）
                # + MediaPipe 自身依赖的 Foundation
                set_property(TARGET mediapipe_vision_c APPEND PROPERTY
                    INTERFACE_LINK_LIBRARIES
                    "-framework Foundation" "-framework CoreFoundation"
                    "-framework CoreGraphics" "-framework CoreVideo"
                    "-framework CoreMedia" "-framework CoreImage"
                    "-framework Accelerate" "-framework AssetsLibrary"
                    "-framework AVFoundation" "-framework QuartzCore")
            endif()
            set(TASK_GRAPH_MEDIAPIPE_AVAILABLE ON)
        elseif(EXISTS "${MP_INSTALL}/lib/libvision.so")
            add_library(mediapipe_vision_c SHARED IMPORTED)
            set_target_properties(mediapipe_vision_c PROPERTIES
                IMPORTED_LOCATION "${MP_INSTALL}/lib/libvision.so"
                INTERFACE_INCLUDE_DIRECTORIES "${MP_INSTALL}/include"
                # 链接期：exe/插件链 libtask_graph.so 时，GNU ld 需定位其
                # DT_NEEDED 的 libvision.so（-L 不参与 DT_NEEDED 解析，须
                # -rpath-link 指向实际目录，否则 "needed by ... not found"）
                INTERFACE_LINK_OPTIONS "-Wl,-rpath-link,${MP_INSTALL}/lib")
            set(TASK_GRAPH_MEDIAPIPE_AVAILABLE ON)
            set(MEDIAPIPE_MP_SO "${MP_INSTALL}/lib/libvision.so")
        elseif(EXISTS "${MP_INSTALL}/lib/vision.lib" AND EXISTS "${MP_INSTALL}/bin/vision.dll")
            add_library(mediapipe_vision_c SHARED IMPORTED)
            set_target_properties(mediapipe_vision_c PROPERTIES
                IMPORTED_LOCATION "${MP_INSTALL}/bin/vision.dll"
                IMPORTED_IMPLIB "${MP_INSTALL}/lib/vision.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${MP_INSTALL}/include")
            set(TASK_GRAPH_MEDIAPIPE_AVAILABLE ON)
            set(MEDIAPIPE_MP_DLL "${MP_INSTALL}/bin/vision.dll")
        endif()
        if(TASK_GRAPH_MEDIAPIPE_AVAILABLE)
            message(STATUS "MediaPipe vision enabled: ${MP_INSTALL}")
        else()
            message(WARNING "TASK_GRAPH_ENABLE_MEDIAPIPE=ON but prebuilt lib not found "
                            "in ${MP_INSTALL}/lib (expected libvision.dylib|so / "
                            "libmediapipe_vision_c.a / vision.dll); building stub — "
                            "run scripts/build_mediapipe.py")
        endif()
    else()
        message(WARNING "TASK_GRAPH_ENABLE_MEDIAPIPE=ON but no prebuilt install found "
                        "(candidates: build{,_ios,_android,_wasm}/mediapipe/install); "
                        "building stub — run scripts/build_mediapipe.py")
    endif()
endif()

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
    elseif(TASK_GRAPH_ANDROID)
        # build_opencv_android.py 产出 OpenCV 官方 Android SDK 布局
        # （sdk/native/jni/OpenCVConfig.cmake）；lib/cmake/opencv4 为兼容保留
        if(EXISTS "${CMAKE_SOURCE_DIR}/build_android/opencv/install/sdk/native/jni/OpenCVConfig.cmake")
            set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_android/opencv/install/sdk/native/jni")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/build_android/opencv/install/lib/cmake/opencv4")
            set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/build_android/opencv/install/lib/cmake/opencv4")
        endif()
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
    api/src/dag_config.cpp
    api/src/sdk.cpp
    api/src/sdk_c.cpp
    src/logger.cpp
    src/model_finder.cpp
    src/data_types.cpp
    src/gpu_image_ops.cpp
    src/gpu_render_ops.cpp
    src/gpu_kernel_library.cpp
    src/gpu_image_task.cpp
    src/task_manager.cpp
    src/task_params.cpp
    src/profiler.cpp
    src/js/js_engine.cpp
    src/js/js_bindings.cpp
)

# 核心类（INode/DAG/DAGExecutor 等）没有显式 __declspec(dllexport)。
# macOS/Linux 默认 -fvisibility 就能导出，MSVC 必须显式导出；这里让
# CMake 自动生成 .def 导出 DLL 的全部符号（含这些 C++ 类的方法）。
# 内部对象（TypeRegistry 等）由此也可被测试程序正常导入。
if(WIN32 AND TASK_GRAPH_LIB_TYPE STREQUAL "SHARED")
    set_target_properties(task_graph PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ============================================================
# JS 脚本引擎（QuickJS，in-tree 纯 C，直接编入核心库——对照 src/mnn/ 先例）。
# 无 option / stub：源码在树内恒可用（miniz 同类），默认开启编译。
# 公共 API：include/task_graph/js/{js_engine,js_bindings}.hpp（保留给
# render 等子模块消费）；js_script 任务注册见 src/js/js_registry.cpp。
#
# vendored QuickJS 原生支持 GCC/Clang/MSVC（MSVC 适配见 third_party/quickjs
# 的 _MSC_VER 段：cutils.h 的 __forceinline/位扫描 intrinsic、quickjs.c 的
# gettimeofday 与 CONFIG_ATOMICS 禁用）。libbf.c 的 __int128 仅出现在
# LIMB_BITS==64 分支，MSVC 无 __SIZEOF_INT128__ 自动回退 32-bit limb，代码
# 被编译剔除。quickjs.c 无条件引用约 37 个 bf_* 符号，必须定义
# CONFIG_BIGNUM 让 libbf.c 编入 decimal 段（缺失=链接失败）。
# quickjs-libc 拖 OS 设施且 MSVC/WASM 不可用：维持排除。
# ============================================================
set(QUICKJS_ROOT "${PROJECT_SOURCE_DIR}/third_party/quickjs")
set(QUICKJS_SOURCES
    "${QUICKJS_ROOT}/quickjs.c"
    "${QUICKJS_ROOT}/libregexp.c"
    "${QUICKJS_ROOT}/libunicode.c"
    "${QUICKJS_ROOT}/cutils.c"
    "${QUICKJS_ROOT}/libbf.c"
)
if(NOT EMSCRIPTEN AND NOT MSVC)
    list(APPEND QUICKJS_SOURCES "${QUICKJS_ROOT}/quickjs-libc.c")
endif()
target_sources(task_graph PRIVATE ${QUICKJS_SOURCES})
set_source_files_properties(${QUICKJS_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "CONFIG_VERSION=\"2024-01-13\";_GNU_SOURCE;CONFIG_BIGNUM")
if(APPLE)
    set_property(SOURCE "${QUICKJS_ROOT}/quickjs.c" APPEND PROPERTY
        COMPILE_DEFINITIONS DUMP_LEAKS)
endif()
if(UNIX)
    target_link_libraries(task_graph PRIVATE m dl)
endif()
# JsEngine 公共 API 以 JSContext*/JSValue 为参数/返回值 -> quickjs 头必须
# PUBLIC（SDK 安装规则同步拷贝，见文件尾 install 块）
target_include_directories(task_graph PUBLIC
    $<BUILD_INTERFACE:${QUICKJS_ROOT}>
    $<INSTALL_INTERFACE:include>)

# JS 的 OpenCV 绑定（MatWrapper + cv.*）：随核心库 OpenCV 门控
if(TASK_GRAPH_ENABLE_OPENCV)
    target_sources(task_graph PRIVATE
        src/js/js_mat_wrapper.cpp
        src/js/js_cv_bindings.cpp
    )
endif()
# js_bindings.cpp 使用 M_PI；MSVC 的 <cmath> 把 M_PI 关在 _USE_MATH_DEFINES 后
if(MSVC)
    set_source_files_properties(src/js/js_bindings.cpp PROPERTIES
        COMPILE_DEFINITIONS _USE_MATH_DEFINES)
endif()

# Add GPU backend sources
if(TASK_GRAPH_ENABLE_METAL AND APPLE)
    target_sources(task_graph PRIVATE
        src/gpu_backends/metal_backend.mm
        src/gpu_backends/metal_render.mm
    )
    set_source_files_properties(src/gpu_backends/metal_backend.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    set_source_files_properties(src/gpu_backends/metal_render.mm PROPERTIES
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
        src/gpu_backends/vulkan_render.cpp
    )
endif()

if(TASK_GRAPH_ENABLE_WGPU AND (WGPU_LIBRARY OR EMSCRIPTEN))
    target_sources(task_graph PRIVATE
        src/gpu_backends/wgpu_backend.cpp
        src/gpu_backends/wgpu_render.cpp
    )
    if(WGPU_LIBRARY)
        target_include_directories(task_graph PRIVATE ${WGPU_INCLUDE_DIR})
        target_link_libraries(task_graph PRIVATE ${WGPU_LIBRARY})
        # dylib 位于 build/wgpu/install/<plat>/lib：构建树 rpath 直达（安装包
        # 分发时随包拷贝，参照 vulkan-1.dll 的分发策略另行处理）
        set_target_properties(task_graph PROPERTIES
            BUILD_RPATH "${WGPU_INSTALL}/lib"
            INSTALL_RPATH "${WGPU_INSTALL}/lib;$ENV{HOME}/.local/lib"
        )
        if(WIN32)
            # Windows 无 rpath 等价物：task_graph.dll 导入 wgpu_native.dll，
            # 拷到同目录（exe/插件链加载时的第一搜索站；CI 测试 exe 在别的
            # 构建树，0xc0000135 的根因之一。安装包侧 build_msix.ps1 另拷一份）
            add_custom_command(TARGET task_graph POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WGPU_INSTALL}/lib/wgpu_native.dll"
                    "$<TARGET_FILE_DIR:task_graph>"
                COMMENT "Copying wgpu_native.dll next to libtask_graph")
        endif()
    endif()
endif()

# MNN 引擎（直接编入核心库；stub/真实实现由 TASK_GRAPH_MNN_AVAILABLE 决定）。
# 任务层（mnn_inference/mnn_image_classifier）2026-09-18 移除——引擎仅供
# face/matting 子模块后端消费，公共 API 见 include/task_graph/mnn/。
if(TASK_GRAPH_ENABLE_MNN)
    target_sources(task_graph PRIVATE
        src/mnn/mnn_engine.cpp
    )
    if(TASK_GRAPH_MNN_AVAILABLE)
        if(MSVC)
            # MNN 只构建 Release（/MD）；MSVC Debug（/MDd）链它 = LNK2038 运行库
            # 不匹配（同 shaderc 先例）。Debug 配置退化为 stub，测试门控同步收窄。
            target_link_libraries(task_graph PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:mnn_engine>")
            target_compile_definitions(task_graph PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:TASK_GRAPH_MNN_AVAILABLE>")
        else()
            target_link_libraries(task_graph PRIVATE mnn_engine)
            target_compile_definitions(task_graph PRIVATE TASK_GRAPH_MNN_AVAILABLE)
        endif()
        if(APPLE)
            # MNN 静态库自身不携带框架依赖（照 MNN.podspec 清单；Metal 后端用）
            find_library(MNN_METAL_FW Metal)
            find_library(MNN_COREVIDEO_FW CoreVideo)
            find_library(MNN_COREFOUNDATION_FW CoreFoundation)
            find_library(MNN_FOUNDATION_FW Foundation)
            find_library(MNN_ACCELERATE_FW Accelerate)
            target_link_libraries(task_graph PUBLIC
                ${MNN_METAL_FW}
                ${MNN_COREVIDEO_FW}
                ${MNN_COREFOUNDATION_FW}
                ${MNN_FOUNDATION_FW}
                ${MNN_ACCELERATE_FW}
            )
        endif()
    endif()
endif()

# MediaPipe Vision 引擎（直接编入核心库；stub/真实实现由
# TASK_GRAPH_MEDIAPIPE_AVAILABLE 决定；Mp* C API 调用全部封在
# src/mediapipe/vision_engine.cpp 的 pimpl 内，公共头零泄漏）。
# 任务层（10 个 mp_* 任务）2026-09-18 移除——引擎仅供 face/matting
# 子模块后端消费，公共 API 见 include/task_graph/mediapipe/。
if(TASK_GRAPH_ENABLE_MEDIAPIPE)
    target_sources(task_graph PRIVATE
        src/mediapipe/vision_engine.cpp
    )
    if(TASK_GRAPH_MEDIAPIPE_AVAILABLE)
        if(MSVC)
            # 同 MNN/shaderc 先例：MediaPipe 预构建仅 Release（/MD），MSVC Debug
            #（/MDd）链它 = LNK2038。Debug 配置退化为 stub，测试门控同步收窄。
            target_link_libraries(task_graph PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:mediapipe_vision_c>")
            target_compile_definitions(task_graph PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:TASK_GRAPH_MEDIAPIPE_AVAILABLE>"
                "$<$<NOT:$<CONFIG:Debug>>:TASK_GRAPH_MEDIAPIPE_CPU_ONLY>")
        else()
            target_link_libraries(task_graph PRIVATE mediapipe_vision_c)
            target_compile_definitions(task_graph PRIVATE
                TASK_GRAPH_MEDIAPIPE_AVAILABLE
                TASK_GRAPH_MEDIAPIPE_CPU_ONLY)
        endif()
        if(APPLE AND MEDIAPIPE_MP_DYLIB)
            # libvision.dylib 是 bare install name（非 @rpath），dyld 对裸名不查
            # LC_RPATH（只解析 @rpath/ 前缀依赖）。两步组合（链接器不支持在
            # -dynamiclib 上用 -change 改写依赖记录，只能 POST_BUILD 用
            # install_name_tool 改）：
            #   1) 链接后把 libtask_graph.dylib 的依赖记录从裸名改为
            #      @rpath/libvision.dylib；
            #   2) LC_RPATH=@loader_path 让 dyld 以"声明依赖的镜像"自己所在目录
            #      解析 —— 测试 exe（build/）与子模块插件（build/submodules/*，
            #      dlopen 加载 libtask_graph）都能命中，旧子模块时代的
            #      per-test symlink hack 随之消亡。
            # 仅 macOS dylib 形态（MEDIAPIPE_MP_DYLIB）：iOS 静态库无 POST_BUILD。
            set_property(TARGET task_graph APPEND PROPERTY BUILD_RPATH "@loader_path")
            add_custom_command(TARGET task_graph POST_BUILD
                COMMAND install_name_tool
                    -change libvision.dylib @rpath/libvision.dylib
                    "$<TARGET_FILE:task_graph>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MP_INSTALL}/lib/libvision.dylib"
                    "$<TARGET_FILE_DIR:task_graph>/libvision.dylib"
                COMMENT "Rewriting libvision dependency to @rpath and copying it next to libtask_graph")
        endif()
        if(WIN32 AND MEDIAPIPE_MP_DLL)
            # Windows loader 不搜索被 LoadLibrary 的 DLL 自己所在目录；拷到
            # libtask_graph.dll 旁（exe 目录标准搜索序的第一站）。
            add_custom_command(TARGET task_graph POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MEDIAPIPE_MP_DLL}"
                    "$<TARGET_FILE_DIR:task_graph>/vision.dll"
                COMMENT "Copying vision.dll next to libtask_graph")
        endif()
        if(UNIX AND NOT APPLE AND MEDIAPIPE_MP_SO)
            # Linux 运行期：DT_NEEDED 只有 SONAME（libvision.so），ld.so 按
            # exe 的 RPATH → LD_LIBRARY_PATH → 系统目录解析；拷到
            # libtask_graph.so 旁（CMake 对构建树目标自动加同目录 RPATH，
            # 测试 exe 与 dlopen 插件链都能命中），与 Windows 分支同思路。
            add_custom_command(TARGET task_graph POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MEDIAPIPE_MP_SO}"
                    "$<TARGET_FILE_DIR:task_graph>/libvision.so"
                COMMENT "Copying libvision.so next to libtask_graph")
        endif()
    endif()
endif()

# ---- miniz（vendored，public domain，third_party/miniz）：.tgp 工程包
# （src/project_bundle.cpp，include/task_graph/project_bundle.hpp）的 zip
# 读写。源码直接编入 task_graph 目标：桌面 SHARED 构建对 .c 以
# -fvisibility=hidden 编译（miniz 符号不进导出表，不与消费者的 miniz 冲突）；
# 移动端/WASM STATIC 构建产物天然自含，libtask_graph 合并脚本无需改动。
set(MINIZ_ROOT "${PROJECT_SOURCE_DIR}/third_party/miniz")
set(MINIZ_SOURCES
    "${MINIZ_ROOT}/miniz.c"
    "${MINIZ_ROOT}/miniz_tdef.c"
    "${MINIZ_ROOT}/miniz_tinfl.c"
    "${MINIZ_ROOT}/miniz_zip.c"
)
target_sources(task_graph PRIVATE
    src/project_bundle.cpp
    ${MINIZ_SOURCES}
)
target_include_directories(task_graph PRIVATE "${MINIZ_ROOT}")
if(NOT MSVC)
    set_source_files_properties(${MINIZ_SOURCES} PROPERTIES
        COMPILE_FLAGS "-fvisibility=hidden")
endif()

target_include_directories(task_graph
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api/include>
        $<INSTALL_INTERFACE:include>
)

# 向消费者传播 C++20 要求(安装后的导出目标同样生效;纯 C 消费者不受影响,
# 其编译器用 C 方言编译 .c TU)
target_compile_features(task_graph PUBLIC cxx_std_20)

target_link_libraries(task_graph
    PUBLIC
        $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:Threads::Threads>
)

if(TASK_GRAPH_ENABLE_VULKAN AND NOT EMSCRIPTEN)
    target_link_libraries(task_graph PRIVATE Vulkan::Vulkan)
    if(MSVC)
        # vulkan-1.dll 延迟加载（配合 vulkan_backend.cpp 的 delayimp 失败钩子）：
        # 用户机器缺 loader 时后端 init() 干净失败走 CPU/soft-skip 降级，
        # 而不是 libtask_graph.dll 因缺 DLL 导致进程无法启动。
        # 安装包（.msix/.exe）不随包分发 vulkan-1.dll——由 GPU 驱动提供。
        target_link_options(task_graph PRIVATE "/DELAYLOAD:vulkan-1.dll")
        target_link_libraries(task_graph PRIVATE delayimp)
    endif()
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
        PROPERTIES TIMEOUT 300
    )
endfunction()

add_tg_test(test_dag)
add_tg_test(test_dag_config)
add_tg_test(test_sdk)
# 纯 C API 测试:附带一个以 C 编译的 TU 验证 tg_sdk_c.h 是合法 C 头
add_tg_test(test_sdk_c)
target_sources(test_sdk_c PRIVATE tests/sdk_c_header_check.c)
add_tg_test(test_ports)
add_tg_test(test_params)
add_tg_test(test_serializer)
add_tg_test(test_path_utils)
add_tg_test(test_data_types)
add_tg_test(test_task_params)
add_tg_test(test_profiler)
add_tg_test(test_type_registry)
add_tg_test(test_logger)
add_tg_test(test_model_finder)

# JS 引擎契约（核心库公共 API，无外部依赖）
add_tg_test(test_js_engine)

# .tgp 工程包（pack/open/zip-slip 防护）单元测试。测试自身用 miniz writer
# 构造恶意包（等长字节替换绕过 writer 的写侧防护）与超限包，需要 miniz
# 头与源码（测试进程内自有副本；核心库的 miniz 符号是 hidden 的）。
add_tg_test(test_project_bundle)
target_sources(test_project_bundle PRIVATE ${MINIZ_SOURCES})
target_include_directories(test_project_bundle PRIVATE "${MINIZ_ROOT}")

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
# 示例:纯 C 消费者 API(JSON graph + io 边界 + tg_register_c_task)
add_executable(example_basic examples/basic.c)
target_link_libraries(example_basic PRIVATE task_graph)

add_executable(example_parallel examples/parallel.c)
target_link_libraries(example_parallel PRIVATE task_graph)

add_executable(example_multi_output examples/multi_output.c)
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

# Android 无头 graph E2E runner（tests/android/，仅 Android 构建；
# 目标 EXCLUDE_FROM_ALL，由 scripts/build_android.py --e2e 显式构建——
# Android 侧没有 GraphStudio app 壳，图执行的 e2e 载体是这个 console 程序，
# 契约见 dev-docs/e2e-android.md）。
# ============================================================
if(TASK_GRAPH_ANDROID)
    add_subdirectory(tests/android)
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

# 消费者 API(api/include,与主 include 合并安装到同一 include/task_graph/):
# dag_config(只读加载)+ sdk(C++ 生命周期)+ tg_sdk_c(纯 C)
install(DIRECTORY api/include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# quickjs 头随 SDK 安装：include/task_graph/js/js_engine.hpp 的 API 面暴露
# JSContext*/JSValue，外部消费者（子模块独立编译）需要 <quickjs.h> 可解析
install(DIRECTORY third_party/quickjs/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
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
