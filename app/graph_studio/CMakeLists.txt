cmake_minimum_required(VERSION 3.20)
project(graph_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TASK_GRAPH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${TASK_GRAPH_ROOT}/include)

link_directories(${TASK_GRAPH_ROOT}/build)

# Qt6 路径配置 - macOS Homebrew 安装路径
if(APPLE)
    # 优先使用 qtbase (6.11.1)，其次完整 qt 包 (6.5.2)
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

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
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
    src/model/GraphModel.cpp
    src/viewmodel/GraphViewModel.cpp
    src/command/CommandStack.cpp
    src/view/MainWindow.cpp
    src/view/NodeItem.cpp
    src/view/EdgeItem.cpp
    src/view/GraphScene.cpp
    src/view/GraphView.cpp
)

qt_add_executable(graph_studio
    ${PROJECT_HEADERS}
    ${PROJECT_SOURCES}
)

target_link_libraries(graph_studio PRIVATE
    Qt6::Widgets
    task_graph
)

# macOS 部署配置
if(APPLE)
    set_target_properties(graph_studio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Info.plist.in"
    )
endif()

# ---- 单元测试与集成测试 ----
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
