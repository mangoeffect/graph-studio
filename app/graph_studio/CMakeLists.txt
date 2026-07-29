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
    set(QT_COMPONENTS Widgets)
else()
    set(QT_COMPONENTS Widgets Test)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})
qt_standard_project_setup()

set(PROJECT_HEADERS
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    include/view/MainWindow.h
    include/view/NodeItem.h
    include/view/EdgeItem.h
    include/view/GraphScene.h
    include/view/GraphView.h
)

set(PROJECT_SOURCES
    src/platform/macos/entry.cpp
    src/PluginBootstrap.cpp
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
    src/view/MainWindow.cpp
    src/view/NodeItem.cpp
    src/view/EdgeItem.cpp
    src/view/GraphScene.cpp
    src/view/GraphView.cpp
)

set(PROJECT_SOURCES
    src/platform/macos/entry.cpp
    src/PluginBootstrap.cpp
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
    src/view/MainWindow.cpp
    src/view/NodeItem.cpp
    src/view/EdgeItem.cpp
    src/view/GraphScene.cpp
    src/view/GraphView.cpp
)

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
    task_graph
)

# macOS 部署配置（非 WASM）
if(APPLE AND NOT EMSCRIPTEN)
    set_target_properties(graph_studio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Info.plist.in"
    )
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
)
target_link_libraries(test_graph_view_model PRIVATE Qt6::Test task_graph)
add_test(NAME test_graph_view_model COMMAND test_graph_view_model)

# CommandStack 单元测试
qt_add_executable(test_command_stack
    tests/test_command_stack.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
)
target_link_libraries(test_command_stack PRIVATE Qt6::Test task_graph)
add_test(NAME test_command_stack COMMAND test_command_stack)

# 集成测试
qt_add_executable(test_integration
    tests/test_integration.cpp
    include/model/GraphModel.h
    include/viewmodel/GraphViewModel.h
    include/command/CommandStack.h
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
)
target_link_libraries(test_integration PRIVATE Qt6::Test task_graph)
add_test(NAME test_integration COMMAND test_integration)
endif()

# ============================================================
# WASM：subnode 内置插件静态链接进主 wasm
# 用 __attribute__((constructor)) 在 main 前注册到 PluginRegistry。
# ============================================================
if(EMSCRIPTEN)
    set(TG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)
    include(${TG_ROOT}/cmake/Subnode.cmake)
    parse_subnode_json("${TG_ROOT}/subnode.json")
    # Subnode 路径相对 CMAKE_SOURCE_DIR 解析（这里指向 graph_studio 子目录），
    # 因此需要临时让 ABS_PATH 基于 TG_ROOT 计算：手动处理每个 local 子模块。
    foreach(submodule ${SUBMODULES})
        # 直接基于 subnode.json 中已知结构，从 TG_ROOT 解析 local 子模块路径
        if(EXISTS "${TG_ROOT}/submodules/${submodule}/CMakeLists.txt")
            add_subdirectory("${TG_ROOT}/submodules/${submodule}"
                             "${CMAKE_BINARY_DIR}/submodules/${submodule}")
        elseif(submodule STREQUAL "image_filtering")
            # image_filtering 依赖 OpenCV。WASM 下若 OpenCV 静态库已用
            # scripts/build_opencv_wasm.sh 构建到 build_wasm/opencv/install/，
            # 则自动启用；否则跳过。
            set(_OPENCV_WASM_INSTALL "${TG_ROOT}/build_wasm/opencv/install")
            if(EXISTS "${_OPENCV_WASM_INSTALL}/lib/cmake/opencv4")
                message(STATUS "WASM: OpenCV found at ${_OPENCV_WASM_INSTALL}, enabling image_filtering")
                set(OpenCV_DIR "${_OPENCV_WASM_INSTALL}/lib/cmake/opencv4")
                # image_filtering/CMakeLists.txt 依赖此开关，否则会 return() 跳过
                set(TASK_GRAPH_ENABLE_OPENCV ON)
                find_package(OpenCV REQUIRED COMPONENTS core imgproc)
                add_subdirectory("${TG_ROOT}/submodules/opencv/image_processing/image_filtering"
                                 "${CMAKE_BINARY_DIR}/submodules/image_filtering")
                target_link_libraries(graph_studio PRIVATE image_filtering ${OpenCV_LIBS})
            else()
                message(STATUS "WASM: skipping image_filtering (OpenCV not built; run scripts/build_opencv_wasm.sh)")
            endif()
        endif()
        if(TARGET ${submodule})
            # WASM 静态库的 __attribute__((constructor)) 默认会被链接器丢弃
            # （archive 中未引用的 object file 不被拉入）。用 --whole-archive
            # 强制全量链接，确保 constructor 注册到 PluginRegistry。
            list(APPEND _wasm_subnode_libs ${submodule})
        endif()
    endforeach()
    if(_wasm_subnode_libs)
        target_link_libraries(graph_studio PRIVATE
            "-Wl,--whole-archive"
            ${_wasm_subnode_libs}
            "-Wl,--no-whole-archive"
        )
    endif()
endif()
