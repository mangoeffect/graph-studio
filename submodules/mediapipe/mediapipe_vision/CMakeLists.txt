cmake_minimum_required(VERSION 3.16)

project(mediapipe_vision
    VERSION 1.0.0
    DESCRIPTION "MediaPipe Vision Tasks plugin for task_graph framework"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED TASK_GRAPH_ROOT)
    set(TASK_GRAPH_ROOT ${CMAKE_SOURCE_DIR})
endif()

set(MP_PREBUILT "")
if(APPLE AND NOT TASK_GRAPH_IOS)
    set(MP_CANDIDATE "${TASK_GRAPH_ROOT}/build/mediapipe/install")
elseif(TASK_GRAPH_ANDROID)
    set(MP_CANDIDATE "${TASK_GRAPH_ROOT}/build_android/mediapipe/install")
elseif(TASK_GRAPH_IOS)
    set(MP_CANDIDATE "${TASK_GRAPH_ROOT}/build_ios/mediapipe/install")
elseif(EMSCRIPTEN)
    set(MP_CANDIDATE "${TASK_GRAPH_ROOT}/build_wasm/mediapipe/install")
endif()

if(MP_CANDIDATE AND EXISTS "${MP_CANDIDATE}/lib")
    set(MP_PREBUILT "${MP_CANDIDATE}")
    message(STATUS "MediaPipe prebuilt found: ${MP_PREBUILT}")
else()
    message(STATUS "MediaPipe prebuilt not found, building stub (no C API calls)")
endif()

if(EMSCRIPTEN OR TASK_GRAPH_MOBILE)
    add_library(mediapipe_vision STATIC src/mediapipe_vision.cpp)
else()
    add_library(mediapipe_vision SHARED src/mediapipe_vision.cpp)
endif()

target_include_directories(mediapipe_vision
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${TASK_GRAPH_ROOT}/include
)

target_link_libraries(mediapipe_vision
    PRIVATE
        task_graph
)

if(MP_PREBUILT)
    set(MP_LIB_FOUND FALSE)
    if(EXISTS "${MP_PREBUILT}/lib/libvision.dylib")
        add_library(mediapipe_vision_c SHARED IMPORTED)
        set_target_properties(mediapipe_vision_c PROPERTIES
            IMPORTED_LOCATION "${MP_PREBUILT}/lib/libvision.dylib"
            INTERFACE_INCLUDE_DIRECTORIES "${MP_PREBUILT}/include"
        )
        set(MP_LIB_FOUND TRUE)
    elseif(EXISTS "${MP_PREBUILT}/lib/libmediapipe_vision_c.a")
        add_library(mediapipe_vision_c STATIC IMPORTED)
        set_target_properties(mediapipe_vision_c PROPERTIES
            IMPORTED_LOCATION "${MP_PREBUILT}/lib/libmediapipe_vision_c.a"
            INTERFACE_INCLUDE_DIRECTORIES "${MP_PREBUILT}/include"
        )
        set(MP_LIB_FOUND TRUE)
    endif()
    if(MP_LIB_FOUND)
        if(APPLE AND NOT TASK_GRAPH_IOS)
            set_target_properties(mediapipe_vision PROPERTIES
                BUILD_RPATH "${MP_PREBUILT}/lib"
                INSTALL_RPATH "${MP_PREBUILT}/lib"
            )
        endif()
        target_link_libraries(mediapipe_vision PRIVATE mediapipe_vision_c)
        target_compile_definitions(mediapipe_vision PRIVATE MEDIAPIPE_VISION_AVAILABLE)
    else()
        message(WARNING "MediaPipe prebuilt lib not found in ${MP_PREBUILT}, building stub")
    endif()
endif()

if(TASK_GRAPH_ENABLE_OPENCV)
    find_package(OpenCV REQUIRED)
    target_include_directories(mediapipe_vision PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mediapipe_vision PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(mediapipe_vision PRIVATE TASK_GRAPH_ENABLE_OPENCV)
endif()

target_compile_options(mediapipe_vision
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -O2
)

if(EMSCRIPTEN OR TASK_GRAPH_MOBILE)
    target_compile_options(mediapipe_vision PRIVATE -pthread)
endif()

if(NOT EMSCRIPTEN AND NOT TASK_GRAPH_MOBILE)
    set_target_properties(mediapipe_vision
        PROPERTIES
            PREFIX ""
            SUFFIX ".so"
    )

    if(APPLE)
        set_target_properties(mediapipe_vision
            PROPERTIES
                SUFFIX ".dylib"
        )
    endif()

    if(WIN32)
        set_target_properties(mediapipe_vision
            PROPERTIES
                SUFFIX ".dll"
        )
    endif()
endif()
