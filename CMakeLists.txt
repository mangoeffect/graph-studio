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

find_package(Threads REQUIRED)

add_library(task_graph SHARED
    src/task.cpp
    src/dag.cpp
    src/compiler.cpp
    src/thread_pool.cpp
    src/executor.cpp
    src/context.cpp
    src/plugin.cpp
)

target_include_directories(task_graph
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(task_graph
    PUBLIC
        Threads::Threads
)

if(UNIX AND NOT APPLE)
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

add_executable(test_dag
    tests/test_dag.cpp
)

target_link_libraries(test_dag
    PRIVATE
        task_graph
)

add_executable(test_plugin
    tests/test_plugin.cpp
)

target_link_libraries(test_plugin
    PRIVATE
        task_graph
)

if(UNIX AND NOT APPLE)
    target_link_libraries(test_plugin
        PRIVATE
            stdc++fs
    )
endif()

add_executable(example_basic
    examples/basic.cpp
)

target_link_libraries(example_basic
    PRIVATE
        task_graph
)

add_executable(example_parallel
    examples/parallel.cpp
)

target_link_libraries(example_parallel
    PRIVATE
        task_graph
)

add_executable(example_plugin
    examples/plugin.cpp
)

target_link_libraries(example_plugin
    PRIVATE
        task_graph
)

add_library(task_plugin_example MODULE
    plugins/example_plugin.cpp
)

target_include_directories(task_plugin_example
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(task_plugin_example
    PRIVATE
        task_graph
)

set_target_properties(task_plugin_example
    PROPERTIES
        PREFIX ""
        SUFFIX ".so"
)

if(APPLE)
    set_target_properties(task_plugin_example
        PROPERTIES
            SUFFIX ".dylib"
    )
endif()

if(WIN32)
    set_target_properties(task_plugin_example
        PROPERTIES
            SUFFIX ".dll"
    )
endif()

enable_testing()
add_test(NAME test_dag COMMAND test_dag)
add_test(NAME test_plugin COMMAND test_plugin)
