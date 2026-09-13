# Android 端 graph E2E 的无头 runner（见 tg_e2e_runner.cpp 头注释）。
#
# 只由 scripts/build_android.py --e2e 显式构建（EXCLUDE_FROM_ALL）——dist
# SDK 产物不需要它。构建目录：build_android_<abi>/tests/android/tg_e2e_runner。
#
# 参与 e2e 的子模块 = subnode.json 里 Android 可跑集合：gpu/render 的
# CMakeLists 在 mobile 下直接 return（无 Android GPU 后端），mediapipe 在
# Android 只有 stub（无 libvision 预编译），video_io 依赖 opencv_videoio
# （build_opencv_android.py 的 BUILD_LIST 未含）——三者在驱动侧按模块记 skip。
if(NOT TASK_GRAPH_ANDROID OR NOT TASK_GRAPH_BUILD_SUBMODULES)
    return()
endif()

if(NOT TASK_GRAPH_ENABLE_OPENCV)
    message(STATUS "tg_e2e_runner: 未启用 OpenCV，跳过（e2e 图集依赖 OpenCV 系子模块）")
    return()
endif()

add_executable(tg_e2e_runner EXCLUDE_FROM_ALL tg_e2e_runner.cpp)

set(TG_E2E_SUBMODULES
    image_reader
    image_filtering
    image_writer
    image_geometry
    image_color
    image_color_grading
    js_task
)
set(_e2e_libs "")
foreach(_sub ${TG_E2E_SUBMODULES})
    if(TARGET ${_sub})
        list(APPEND _e2e_libs ${_sub})
    else()
        message(WARNING "tg_e2e_runner: 子模块目标不存在，跳过: ${_sub}")
    endif()
endforeach()

# 静态注册保活：--whole-archive 强制全量链接（__attribute__((constructor))
# 注册、无外部引用的 TU 会被链接器裁剪）。WASM app 同款先例见
# app/graph_studio/CMakeLists.txt。
#
# --start-group/--end-group：core 与子模块是静态库互相引用（子模块对象引用
# task_graph 符号），一次性顺序扫描会漏解——group 让链接器迭代到全部符号
# 落定（ELF 的标准解法）。
#
# --allow-multiple-definition：每个子模块都导出同名的
# extern "C" register_plugin/unregister_plugin（桌面 dlopen 入口），全量拉入
# 后彼此冲突（wasm-ld 无此选项、改用 #ifndef __EMSCRIPTEN__ 守卫；Android 的
# ld.lld 支持，且 Android 不做 dlopen 调用这两个入口，取任意一份即可）。
target_link_options(tg_e2e_runner PRIVATE -Wl,--allow-multiple-definition)
target_link_libraries(tg_e2e_runner PRIVATE
    "-Wl,--start-group"
    task_graph
    "-Wl,--whole-archive"
    ${_e2e_libs}
    "-Wl,--no-whole-archive"
    "-Wl,--end-group"
)
