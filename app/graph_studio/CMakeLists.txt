cmake_minimum_required(VERSION 3.20)
project(graph_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TASK_GRAPH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${TASK_GRAPH_ROOT}/include)

# 根据是否为 WASM 编译选择对应的 task_graph 库目录
if(EMSCRIPTEN)
    link_directories(${TASK_GRAPH_ROOT}/build_wasm)
else()
    link_directories(${TASK_GRAPH_ROOT}/build)
endif()

# Qt6 路径配置
if(EMSCRIPTEN)
    # WASM: 由外部传入 CMAKE_PREFIX_PATH 或 Qt6_DIR
    message(STATUS "Building for WebAssembly with Qt6")
elseif(APPLE)
    foreach(QT_PATH
            "/opt/homebrew/Cellar/qtbase/6.11.1"
            "/opt/homebrew/Cellar/qt/6.5.2"
            "/usr/local/opt/qt"
            "$ENV{QT_PREFIX_PATH}")
        if(EXISTS "${QT_PATH}/lib/cmake/Qt6")
            list(APPEND CMAKE_PREFIX_PATH "${QT_PATH}")
            message(STATUS "Found Qt6 at: ${QT_PATH}")
            break()
        endif()
    endforeach()
endif()

if(EMSCRIPTEN)
    set(QT_COMPONENTS Widgets OpenGL OpenGLWidgets)
else()
    set(QT_COMPONENTS Widgets Test OpenGL OpenGLWidgets)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})
qt_standard_project_setup()

# 桌面端显式链接 OpenCV：UI 层需直接 any_cast<cv::Mat> 并做零拷贝 QImage 转换。
# WASM 端在文件末尾的 EMSCRIPTEN 块已条件链接，此处不重复。
if(NOT EMSCRIPTEN)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    include_directories(${OpenCV_INCLUDE_DIRS})
endif()

set(PROJECT_HEADERS
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    include/view/MainWindow.h
    include/view/NodeItem.h
    include/view/EdgeItem.h
    include/view/GraphScene.h
    include/view/GraphView.h
    include/view/GanttChart.h
    include/view/ProfilePanel.h
    include/view/GpuImageViewer.h
    include/GpuBootstrap.h
    include/CrashReporter.h
)

set(PROJECT_SOURCES
    src/platform/macos/entry.cpp
    src/PluginBootstrap.cpp
    src/GpuBootstrap.cpp
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
    src/view/MainWindow.cpp
    src/view/NodeItem.cpp
    src/view/EdgeItem.cpp
    src/view/GraphScene.cpp
    src/view/GraphView.cpp
    src/view/GanttChart.cpp
    src/view/ProfilePanel.cpp
    src/view/GpuImageViewer.cpp
)

# ---- 崩溃上报模块（Sentry + Crashpad，仅桌面端）----
# sentry-native 及其递归子模块 crashpad 由 scripts/fetch_sentry.py 拉取到
# third_party/sentry-native（固定版本，gitignored）。未拉取时构建照常进行，
# 仅无崩溃上报能力（与 OpenCV WASM 缺失时跳过依赖子模块同理）。
set(GRAPH_STUDIO_SENTRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sentry-native")
set(GRAPH_STUDIO_HAS_SENTRY OFF)
if(NOT EMSCRIPTEN AND EXISTS "${GRAPH_STUDIO_SENTRY_ROOT}/CMakeLists.txt")
    set(GRAPH_STUDIO_HAS_SENTRY ON)
    add_compile_definitions(GRAPH_STUDIO_HAS_SENTRY)
endif()
# CrashReporter 始终编译：未启用 sentry（含全部 WASM 构建）时内部编译为
# no-op stub，调用方（GraphViewModel 等）无需 #ifdef 分支。
list(APPEND PROJECT_SOURCES src/CrashReporter.cpp)

if(EMSCRIPTEN)
    # WASM：GpuImageViewer 依赖桌面 core profile（QOpenGLFunctions_3_3_Core），
    # Qt wasm 构建只有 OpenGL ES、没有该头文件。排除后 MainWindow 内部
    # __EMSCRIPTEN__ 分支退化为 QLabel 展示结果图。
    list(REMOVE_ITEM PROJECT_SOURCES src/view/GpuImageViewer.cpp)
    list(REMOVE_ITEM PROJECT_HEADERS include/view/GpuImageViewer.h)
endif()

if(EMSCRIPTEN)
    # WASM：用 qt_add_executable 才能自动生成 .html + qtloader.js。
    # Qt 的 wasm_multithread 构建已在 qt_add_executable 内部设置了
    # -pthread / -sUSE_PTHREADS / PROXY_TO_PTHREAD 等链接标志，
    # 不应额外添加（否则 PROXY_TO_PTHREAD 与 Qt 内部线程模型冲突，导致
    # QApplication 不在 main thread / visualViewport undefined → 空白页）。
    qt_add_executable(graph_studio
        ${PROJECT_HEADERS}
        ${PROJECT_SOURCES}
    )
    # 仅添加 Qt 不默认设置的资源/内存参数
    set_target_properties(graph_studio PROPERTIES
        QT_WASM_INITIAL_MEMORY "200MB"
    )
else()
    qt_add_executable(graph_studio
        ${PROJECT_HEADERS}
        ${PROJECT_SOURCES}
    )
endif()

target_link_libraries(graph_studio PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    task_graph
)
# 桌面端显式链接 OpenCV（WASM 在末尾 EMSCRIPTEN 块条件链接）
if(NOT EMSCRIPTEN)
    target_link_libraries(graph_studio PRIVATE ${OpenCV_LIBS})
endif()

# Vulkan 后端头（GpuBootstrap.cpp 直接 include vulkan_backend.hpp）：非 Apple
# 桌面端按需开启；找不到 SDK/开发包时该源文件编译为无 GPU 后端的空实现。
if(NOT APPLE AND NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_compile_definitions(graph_studio PRIVATE TG_APP_HAS_VULKAN=1)
        # 只为头文件搜索路径；vulkan-1 由 libtask_graph PRIVATE 链接并在其内部解析
        target_include_directories(graph_studio PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
endif()

# ============================================================
# Sentry 崩溃上报（crashpad backend，仅桌面端）
# 依赖 scripts/fetch_sentry.py 拉取；未拉取则跳过（无崩溃上报能力）。
# ============================================================
if(GRAPH_STUDIO_HAS_SENTRY)
    # 崩溃元信息源：版本 + git 短哈希 + 环境（用于 Sentry release 分组）。
    # 版本默认解析根 project() 的 VERSION（声明跨多行，需正则跨行匹配）；
    # 发布脚本可用 -DGRAPH_STUDIO_SENTRY_VERSION 传完整渠道版本
    # （如 0.1.0-beta.42），使 Sentry release 与 GitHub 发布 tag 一致。
    set(_GS_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../")
    set(_GS_DEFAULT_SENTRY_VERSION "0.0.0")
    file(READ "${_GS_PROJECT_ROOT}/CMakeLists.txt" _GS_ROOT_CMAKE)
    string(REGEX MATCH
           "task_graph[ \t\r\n]+VERSION[ \t\r\n]+([0-9][0-9A-Za-z._+-]*)"
           _GS_VERSION_MATCH "${_GS_ROOT_CMAKE}")
    if(CMAKE_MATCH_1)
        set(_GS_DEFAULT_SENTRY_VERSION "${CMAKE_MATCH_1}")
    endif()
    set(GRAPH_STUDIO_SENTRY_VERSION "${_GS_DEFAULT_SENTRY_VERSION}" CACHE STRING
        "Sentry release version（默认根 project VERSION；发布时传完整渠道版本）")
    set(GRAPH_STUDIO_GIT_HASH "unknown")
    execute_process(COMMAND git -C "${_GS_PROJECT_ROOT}" rev-parse --short HEAD
        OUTPUT_VARIABLE _GS_GIT_SHORT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _GS_GIT_RC)
    if(_GS_GIT_RC EQUAL 0 AND _GS_GIT_SHORT_HASH)
        set(GRAPH_STUDIO_GIT_HASH "${_GS_GIT_SHORT_HASH}")
    endif()
    string(TOLOWER "${CMAKE_BUILD_TYPE}" GRAPH_STUDIO_BUILD_TYPE_LOWER)
    if(GRAPH_STUDIO_BUILD_TYPE_LOWER STREQUAL "release"
       OR GRAPH_STUDIO_BUILD_TYPE_LOWER STREQUAL "relwithdebinfo"
       OR GRAPH_STUDIO_BUILD_TYPE_LOWER STREQUAL "minsizerel")
        set(GRAPH_STUDIO_SENTRY_ENVIRONMENT "production")
    else()
        set(GRAPH_STUDIO_SENTRY_ENVIRONMENT "development")
    endif()

    set(SENTRY_BACKEND "crashpad" CACHE STRING "sentry crash backend" FORCE)
    set(SENTRY_TRANSPORT "none" CACHE STRING "sentry transport" FORCE)
    set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "static sentry" FORCE)
    set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(SENTRY_INTEGRATION_QT OFF CACHE BOOL "" FORCE)
    # 可选：编译期嵌入 DSN（发布包免设 SENTRY_DSN 环境变量）。
    # DSN 是公开 client key 可嵌入；运行期 SENTRY_DSN 环境变量优先级更高。
    set(GRAPH_STUDIO_SENTRY_DSN "" CACHE STRING
        "Embedded Sentry DSN for release builds (optional)")
    add_subdirectory("${GRAPH_STUDIO_SENTRY_ROOT}" "${CMAKE_BINARY_DIR}/sentry")

    target_link_libraries(graph_studio PRIVATE sentry)
    set(GS_CRASH_COMPILE_DEFS
        GRAPH_STUDIO_SENTRY_VERSION="${GRAPH_STUDIO_SENTRY_VERSION}"
        GRAPH_STUDIO_SENTRY_BUILD_HASH="${GRAPH_STUDIO_GIT_HASH}"
        GRAPH_STUDIO_SENTRY_ENVIRONMENT="${GRAPH_STUDIO_SENTRY_ENVIRONMENT}"
    )
    if(NOT GRAPH_STUDIO_SENTRY_DSN STREQUAL "")
        list(APPEND GS_CRASH_COMPILE_DEFS
            GRAPH_STUDIO_SENTRY_DSN="${GRAPH_STUDIO_SENTRY_DSN}")
    endif()
    target_compile_definitions(graph_studio PRIVATE ${GS_CRASH_COMPILE_DEFS})

    # crashpad handler 需与可执行文件同目录（crashpad 按可执行文件相对路径查找）。
    if(TARGET crashpad_handler)
        if(APPLE)
            # macOS .app bundle → Contents/MacOS/（与 graph_studio 可执行文件同目录）
            add_custom_command(TARGET graph_studio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:crashpad_handler>"
                    "$<TARGET_BUNDLE_DIR:graph_studio>/Contents/MacOS/crashpad_handler"
                COMMENT "Copying crashpad_handler into app bundle"
            )
        elseif(WIN32)
            add_custom_command(TARGET graph_studio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:crashpad_handler>"
                    "$<TARGET_FILE_DIR:graph_studio>/crashpad_handler.exe"
                COMMENT "Copying crashpad_handler.exe next to executable"
            )
        else()
            # Linux：拷到可执行文件同目录。AppImage 打包时 package_linux.py
            # 会把它带入 AppDir（linuxdeployqt 只收集动态依赖，不含该独立程序）。
            add_custom_command(TARGET graph_studio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:crashpad_handler>"
                    "$<TARGET_FILE_DIR:graph_studio>/crashpad_handler"
                COMMENT "Copying crashpad_handler next to executable"
            )
        endif()
    endif()
endif()

# macOS 部署配置（非 WASM）
if(APPLE AND NOT EMSCRIPTEN)
    set_target_properties(graph_studio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Info.plist.in"
    )
endif()

# ---- 应用图标资源 ----
qt_add_resources(ICON_RESOURCES resources/resources.qrc)
target_sources(graph_studio PRIVATE ${ICON_RESOURCES})

# macOS: 将 .icns 打入 app bundle 的 Resources 目录
if(APPLE AND NOT EMSCRIPTEN)
    set_source_files_properties(resources/icons/app_icon.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(graph_studio PRIVATE resources/icons/app_icon.icns)
endif()

# Windows: 通过 RC 文件嵌入 .ico
if(WIN32 AND NOT EMSCRIPTEN)
    target_sources(graph_studio PRIVATE resources/icons/app_icon.rc)
endif()

# ---- 单元测试与集成测试（仅在非 WASM 平台编译） ----
if(NOT EMSCRIPTEN)
enable_testing()

# ViewModel 单元测试
qt_add_executable(test_graph_view_model
    tests/test_graph_view_model.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/CrashReporter.cpp
)
target_link_libraries(test_graph_view_model PRIVATE Qt6::Test Qt6::Gui task_graph ${OpenCV_LIBS})
add_test(NAME test_graph_view_model COMMAND test_graph_view_model)

# CommandStack 单元测试
qt_add_executable(test_command_stack
    tests/test_command_stack.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/CrashReporter.cpp
    src/command/CommandStack.cpp
)
target_link_libraries(test_command_stack PRIVATE Qt6::Test Qt6::Gui task_graph ${OpenCV_LIBS})
add_test(NAME test_command_stack COMMAND test_command_stack)

# 集成测试
qt_add_executable(test_integration
    tests/test_integration.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/CrashReporter.cpp
    src/command/CommandStack.cpp
)
target_link_libraries(test_integration PRIVATE Qt6::Test Qt6::Gui task_graph ${OpenCV_LIBS})
add_test(NAME test_integration COMMAND test_integration)

# GUI 集成测试：用 QApplication 实例化 MainWindow，QTest 驱动鼠标/键盘，
# 内省 QGraphicsScene 的 NodeItem/EdgeItem 与 GraphViewModel 状态。
# 默认以 QT_QPA_PLATFORM=offscreen 无头运行（见 test_gui.cpp）。
qt_add_executable(test_gui
    tests/test_gui.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    include/view/MainWindow.h
    include/view/GraphScene.h
    include/view/GraphView.h
    include/view/NodeItem.h
    include/view/EdgeItem.h
    include/view/GanttChart.h
    include/view/ProfilePanel.h
    include/view/GpuImageViewer.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/CrashReporter.cpp
    src/command/CommandStack.cpp
    src/view/MainWindow.cpp
    src/view/GraphScene.cpp
    src/view/GraphView.cpp
    src/view/NodeItem.cpp
    src/view/EdgeItem.cpp
    src/view/GanttChart.cpp
    src/view/ProfilePanel.cpp
    src/view/GpuImageViewer.cpp
)
target_link_libraries(test_gui PRIVATE Qt6::Widgets Qt6::Test Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets task_graph ${OpenCV_LIBS})
add_test(NAME test_gui COMMAND test_gui)

# GraphViewModel 依赖 CrashReporter：测试目标需一并链接 sentry（拿到 sentry.h
# 头文件路径；测试内未配置 DSN，sentry 保持 no-op，无需 crashpad_handler）。
if(GRAPH_STUDIO_HAS_SENTRY)
    foreach(_t test_graph_view_model test_command_stack test_integration test_gui)
        target_link_libraries(${_t} PRIVATE sentry)
    endforeach()
endif()
endif()

# ============================================================
# WASM：subnode 内置插件静态链接进主 wasm
# 用 __attribute__((constructor)) 在 main 前注册到 PluginRegistry。
# ============================================================
if(EMSCRIPTEN)
    set(TG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)
    include(${TG_ROOT}/cmake/Subnode.cmake)
    # subnode 的 CMakeLists 调用 use_task_graph_sdk()（桌面主仓库构建由根
    # CMakeLists include(SdkUtil) 提供）。此处 app 自身是顶层工程，必须显式引入；
    # SdkUtil 走 legacy 模式（TASK_GRAPH_ROOT + 预构建 build_wasm/libtask_graph.a）。
    include(${TG_ROOT}/cmake/SdkUtil.cmake)
    parse_subnode_json("${TG_ROOT}/subnode.json")

    # OpenCV WASM 静态库若已用 scripts/build_opencv_wasm.py 构建到
    # build_wasm/opencv/install/，则依赖 OpenCV 的 submodule 自动启用；否则跳过。
    set(_OPENCV_WASM_INSTALL "${TG_ROOT}/build_wasm/opencv/install")
    set(_OPENCV_WASM_AVAILABLE OFF)
    if(EXISTS "${_OPENCV_WASM_INSTALL}/lib/cmake/opencv4")
        set(_OPENCV_WASM_AVAILABLE ON)
        set(OpenCV_DIR "${_OPENCV_WASM_INSTALL}/lib/cmake/opencv4")
        # 依赖 OpenCV 的 submodule CMakeLists 用该开关判断是否 return() 跳过
        set(TASK_GRAPH_ENABLE_OPENCV ON)
        find_package(OpenCV REQUIRED COMPONENTS core imgproc)
        message(STATUS "WASM: OpenCV found at ${_OPENCV_WASM_INSTALL}")
    else()
        message(STATUS "WASM: OpenCV not built (run scripts/build_opencv_wasm.py); OpenCV submodules skipped")
    endif()

    # 逐个处理 subnode.json 声明的 submodule。URL 相对 TG_ROOT 解析。
    foreach(submodule ${SUBMODULES})
        get_filename_component(_sub_path "${SUBMODULE_${submodule}_URL}"
                               ABSOLUTE BASE_DIR "${TG_ROOT}")
        if(NOT EXISTS "${_sub_path}/CMakeLists.txt")
            message(STATUS "WASM: skipping ${submodule} (no CMakeLists at ${_sub_path})")
            continue()
        endif()
        # 依赖 OpenCV 的 submodule（路径位于 submodules/opencv/ 下）仅在
        # OpenCV WASM 库可用时加入。
        if(_sub_path MATCHES "/submodules/opencv/")
            if(NOT _OPENCV_WASM_AVAILABLE)
                message(STATUS "WASM: skipping ${submodule} (OpenCV not built)")
                continue()
            endif()
        endif()
        add_subdirectory("${_sub_path}" "${CMAKE_BINARY_DIR}/submodules/${submodule}")
        if(TARGET ${submodule})
            # WASM 静态库的 __attribute__((constructor)) 默认会被链接器丢弃
            # （archive 中未引用的 object file 不被拉入）。用 --whole-archive
            # 强制全量链接，确保 constructor 注册到 PluginRegistry。
            list(APPEND _wasm_subnode_libs ${submodule})
        endif()
    endforeach()

    if(_OPENCV_WASM_AVAILABLE AND _wasm_subnode_libs)
        # OpenCV 库供依赖它的 submodule 链接
        target_link_libraries(graph_studio PRIVATE ${OpenCV_LIBS})
    endif()
    if(_wasm_subnode_libs)
        target_link_libraries(graph_studio PRIVATE
            "-Wl,--whole-archive"
            ${_wasm_subnode_libs}
            "-Wl,--no-whole-archive"
        )
        # 每个子节点库都定义 extern "C" register_plugin/unregister_plugin
        # （桌面 dlopen 入口），--whole-archive 全量拉入会彼此冲突；子节点
        # 源码里这两个入口已用 #ifndef __EMSCRIPTEN__ 守卫（WASM 静态链接下
        # 仅靠 constructor 注册，PluginBootstrap 不做 dlopen）。
    endif()
endif()
