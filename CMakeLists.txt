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

option(TASK_GRAPH_ENABLE_OPENCV "Enable OpenCV support for image conversion" OFF)

if(TASK_GRAPH_ENABLE_OPENCV)
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

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(test_plugin
        PRIVATE
            stdc++fs
    )
endif()

add_executable(test_serializer
    tests/test_serializer.cpp
)

target_link_libraries(test_serializer
    PRIVATE
        task_graph
)

add_executable(test_json_debug
    tests/test_json_debug.cpp
)

target_link_libraries(test_json_debug
    PRIVATE
        task_graph
)

add_executable(test_logger
    tests/test_logger.cpp
)

target_link_libraries(test_logger
    PRIVATE
        task_graph
)

add_executable(test_dependency_injection
    tests/test_dependency_injection.cpp
)

target_link_libraries(test_dependency_injection
    PRIVATE
        task_graph
)

add_executable(test_check_input
    tests/test_check_input.cpp
)

target_link_libraries(test_check_input
    PRIVATE
        task_graph
)

add_executable(test_check_input_json
    tests/test_check_input_json.cpp
)

target_link_libraries(test_check_input_json
    PRIVATE
        task_graph
)

add_executable(test_data_types
    tests/test_data_types.cpp
)

target_link_libraries(test_data_types
    PRIVATE
        task_graph
)

add_executable(test_gpu_image
    tests/test_gpu_image.cpp
)

target_link_libraries(test_gpu_image
    PRIVATE
        task_graph
)

if(TASK_GRAPH_ENABLE_OPENCV)
    add_executable(test_opencv_convert
        tests/test_opencv_convert.cpp
    )

    target_link_libraries(test_opencv_convert
        PRIVATE
            task_graph
            ${OpenCV_LIBS}
    )
    
    add_executable(test_opencv_image_filtering
        tests/test_opencv_image_filtering.cpp
    )

    target_link_libraries(test_opencv_image_filtering
        PRIVATE
            task_graph
            ${OpenCV_LIBS}
            image_filtering
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

include(Subnode)

parse_subnode_json("${PROJECT_SOURCE_DIR}/subnode.json")

foreach(submodule ${SUBMODULES})
    add_subnode_module(${submodule})
endforeach()

add_executable(test_subnode
    tests/test_subnode.cpp
)

target_link_libraries(test_subnode
    PRIVATE
        task_graph
)

add_executable(test_task_type_id
    tests/test_task_type_id.cpp
)

target_link_libraries(test_task_type_id
    PRIVATE
        task_graph
)

add_executable(test_task_params
    tests/test_task_params.cpp
)

target_link_libraries(test_task_params
    PRIVATE
        task_graph
)

add_executable(test_profiler
    tests/test_profiler.cpp
)

target_link_libraries(test_profiler
    PRIVATE
        task_graph
)

foreach(submodule ${SUBMODULES})
    if(TARGET ${submodule})
        target_link_libraries(test_subnode
            PRIVATE
                ${submodule}
        )
    endif()
endforeach()

enable_testing()
add_test(NAME test_dag COMMAND test_dag)
add_test(NAME test_plugin COMMAND test_plugin)
add_test(NAME test_subnode COMMAND test_subnode)
