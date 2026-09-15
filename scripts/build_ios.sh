#!/usr/bin/env bash
#
# build_ios.sh - 交叉编译 task_graph + 子模块为 iOS 静态库 / .xcframework。
#
# 流程：
#   1) 构建 iOS device slice (arm64) -> build_ios_device/libtask_graph.a + 子模块 .a
#   2) 构建 iOS simulator slice (arm64 / x86_64) -> build_ios_sim/libtask_graph.a + 子模块 .a
#   3) 合并核心库 + 子模块为 libtask_graph_full.a（libtool -static）
#   4) 用 xcodebuild -create-xcframework 生成 .xcframework
#   5) 复制公开头文件到 dist/ios/include/
#
# 用法:
#   scripts/build_ios.sh                         # 构建 device + simulator + xcframework
#   scripts/build_ios.sh --device-only           # 仅构建 device slice
#   scripts/build_ios.sh --no-opencv             # 跳过 OpenCV 子模块
#   scripts/build_ios.sh --no-metal              # 跳过 Metal GPU 子模块
#   scripts/build_ios.sh --no-xcframework        # 不生成 xcframework，仅输出 .a
#   scripts/build_ios.sh --sim-arch x86_64       # 指定模拟器架构（默认 arm64）
#   scripts/build_ios.sh --deploy-target 14.0    # 最低 iOS 版本（默认 13.0）
#   scripts/build_ios.sh --clean                 # 清空构建目录
#   scripts/build_ios.sh -j <N>                  # 并行编译线程数
#
# 环境要求：
#   - macOS + Xcode + CMake 3.16+
#   - (可选) scripts/build_opencv_ios.sh 已执行，产出 build_ios/opencv/install/
#
# 退出码：0 成功，非 0 失败。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEVICE_BUILD="${ROOT_DIR}/build_ios_device"
SIM_BUILD="${ROOT_DIR}/build_ios_sim"
DIST_DIR="${ROOT_DIR}/dist/ios"

DEVICE_ARCH="arm64"
SIM_ARCH=""
DEPLOY_TARGET="13.0"
JOBS=""
CLEAN=0
DEVICE_ONLY=0
NO_OPENCV=0
NO_METAL=0
NO_XCFRAMEWORK=0

if [[ -t 1 ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_BOLD=""; C_RESET=""
fi

usage() {
    sed -n '3,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --device-only)   DEVICE_ONLY=1; shift ;;
        --no-opencv)     NO_OPENCV=1; shift ;;
        --no-metal)      NO_METAL=1; shift ;;
        --no-xcframework) NO_XCFRAMEWORK=1; shift ;;
        --sim-arch)      SIM_ARCH="$2"; shift 2 ;;
        --deploy-target) DEPLOY_TARGET="$2"; shift 2 ;;
        -j|--jobs)       JOBS="$2"; shift 2 ;;
        --clean)         CLEAN=1; shift ;;
        -h|--help)       usage 0 ;;
        *) echo "${C_RED}未知参数: $1${C_RESET}" >&2; usage 1 ;;
    esac
done

# 默认模拟器架构：Apple Silicon 用 arm64，Intel 用 x86_64
if [[ -z "${SIM_ARCH}" ]]; then
    if [[ "$(uname -m)" == "arm64" ]]; then
        SIM_ARCH="arm64"
    else
        SIM_ARCH="x86_64"
    fi
fi

if [[ -z "${JOBS}" ]]; then
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

cd "${ROOT_DIR}"

# 环境检查
if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "${C_RED}找不到 xcodebuild，请安装 Xcode${C_RESET}" >&2
    exit 1
fi

# OpenCV 可用性检查（per-slice：device / simulator 各自探测各自的预编译库；
# 无论可用与否都显式传 flag：旧 CMakeCache 可能缓存了 TASK_GRAPH_ENABLE_OPENCV=ON，
# 省略 flag 会让 option() 沿用缓存并在 find_package(OpenCV REQUIRED) 处配置失败。
# device-only 库绝不能并入 sim slice——异平台对象会让 create-xcframework 报错）
OPENCV_DEVICE_INSTALL="${ROOT_DIR}/build_ios/opencv/install"
OPENCV_SIM_INSTALL="${ROOT_DIR}/build_ios_sim/opencv/install"
# OpenCV 系子模块（依赖 OpenCV 预编译库）；video_io 需要opencv_videoio 模块
# （build_opencv_ios.sh 的 BUILD_LIST 只编 core/imgproc/imgcodecs），暂不打包
OPENCV_SUBMODULES="image_filtering image_reader image_writer image_geometry image_color image_color_grading"
OPENCV_DEVICE_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=OFF"
OPENCV_SIM_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=OFF"
OPENCV_DEVICE_OK=0
OPENCV_SIM_OK=0
if [[ "${NO_OPENCV}" -eq 0 ]]; then
    if [[ -d "${OPENCV_DEVICE_INSTALL}/lib/cmake/opencv4" ]]; then
        OPENCV_DEVICE_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=ON"
        OPENCV_DEVICE_OK=1
        echo "${C_BOLD}==> 检测到 OpenCV iOS device 库，启用 OpenCV${C_RESET}"
    else
        echo "${C_BOLD}==> 未检测到 OpenCV iOS device 库（运行 scripts/build_opencv_ios.sh 构建），跳过 OpenCV 子模块${C_RESET}"
    fi
    if [[ -d "${OPENCV_SIM_INSTALL}/lib/cmake/opencv4" ]]; then
        OPENCV_SIM_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=ON"
        OPENCV_SIM_OK=1
        echo "${C_BOLD}==> 检测到 OpenCV iOS simulator 库，sim slice 启用 OpenCV${C_RESET}"
    else
        echo "${C_BOLD}==> 未检测到 OpenCV iOS simulator 库（运行 scripts/build_opencv_ios.sh --sim），sim 产物不含 OpenCV${C_RESET}"
    fi
fi

# Metal 标志（同理显式传 ON/OFF，防陈旧缓存）
METAL_FLAG="-DTASK_GRAPH_ENABLE_METAL=OFF"
if [[ "${NO_METAL}" -eq 0 ]]; then
    METAL_FLAG="-DTASK_GRAPH_ENABLE_METAL=ON"
else
    echo "${C_BOLD}==> 跳过 Metal GPU 子模块${C_RESET}"
fi

# MNN 推理引擎预编译库（TASK_GRAPH_ENABLE_MNN 默认 ON，CMake 自动探测
# build_ios/mnn/install；缺失时核心库以 stub 编译，任务 execute 报错降级）。
MNN_LIB="${ROOT_DIR}/build_ios/mnn/install/lib/libMNN.a"
if [[ ! -f "${MNN_LIB}" ]]; then
    echo "${C_BOLD}==> 构建 MNN iOS 静态库（build_mnn.py --platform ios）${C_RESET}"
    python3 "${SCRIPT_DIR}/build_mnn.py" --platform ios -j "${JOBS}" \
        || echo "${C_RED}==> MNN iOS 构建失败，task_graph 以 stub 降级（仅影响 MNN 任务）${C_RESET}" >&2
fi

# MediaPipe C API 预编译库（build_mediapipe.py --platform ios 产 arm64 device-only
# 静态库；缺失时 mp_* 任务照常注册，execute 报错降级）。
MP_LIB="${ROOT_DIR}/build_ios/mediapipe/install/lib/libmediapipe_vision_c.a"
if [[ ! -f "${MP_LIB}" ]]; then
    echo "${C_BOLD}==> 构建 MediaPipe iOS 静态库（build_mediapipe.py --platform ios）${C_RESET}"
    python3 "${SCRIPT_DIR}/build_mediapipe.py" --platform ios -j "${JOBS}" \
        || echo "${C_RED}==> MediaPipe iOS 构建失败，task_graph 以 stub 降级（仅影响 mp_* 任务）${C_RESET}" >&2
fi
# device-only 静态库：sim slice 必须显式关闭（否则模拟器产物引用无法解析的
# Mp* 符号，同 MNN 的 sim 处理）。
MP_SIM_FLAG="-DTASK_GRAPH_ENABLE_MEDIAPIPE=OFF"
if [[ ! -f "${MP_LIB}" ]]; then
    MP_SIM_FLAG=""
elif [[ "${DEVICE_ONLY}" -eq 0 ]]; then
    echo "${C_BOLD}==> libmediapipe_vision_c.a 为 device-only，模拟器产物禁用 MediaPipe${C_RESET}"
fi

# libMNN 平台探测：build_mnn.py --platform ios 目前只产出 device slice。
# 只有模拟器版 libMNN 才允许并入 sim slice；否则 sim slice 必须 -DTASK_GRAPH_ENABLE_MNN=OFF，
# 否则模拟器产物会引用无法解析的 MNN 符号（且 device 平台对象混入会让
# xcodebuild -create-xcframework 报 "identifier 'ios-arm64' already exists"）。
MNN_SIM_OK=0
if [[ -f "${MNN_LIB}" ]]; then
    TMP_OBJ_DIR="$(mktemp -d)"
    # 纯 bash 取第一个 .o 成员（跳过 __.SYMDEF 符号表成员；不用 head -1 截断
    # 上游管道，避免 pipefail 下的 SIGPIPE；ar x 失败按"非模拟器"处理）
    AR_MEMBERS="$(ar t "${MNN_LIB}" 2>/dev/null || true)"
    FIRST_OBJ=""
    while IFS= read -r member; do
        case "${member}" in *.o) FIRST_OBJ="${member}"; break ;; esac
    done <<< "${AR_MEMBERS}"
    if [[ -n "${FIRST_OBJ}" ]] \
        && (cd "${TMP_OBJ_DIR}" && ar x "${MNN_LIB}" "${FIRST_OBJ}" 2>/dev/null) \
        && [[ -f "${TMP_OBJ_DIR}/${FIRST_OBJ}" ]]; then
        MNN_PLAT="$(vtool -show-build "${TMP_OBJ_DIR}/${FIRST_OBJ}" 2>/dev/null | awk '/^ *platform /{print $2}')"
        if [[ "${MNN_PLAT}" == "IOSSIMULATOR" ]]; then
            MNN_SIM_OK=1
        fi
    fi
    rm -rf "${TMP_OBJ_DIR}"
fi

# 清理
if [[ "${CLEAN}" -eq 1 ]]; then
    echo "${C_BOLD}==> 清理 iOS 构建目录${C_RESET}"
    rm -rf "${DEVICE_BUILD}" "${SIM_BUILD}" "${DIST_DIR}"
fi

# ============================================================
# 构建 iOS device slice
# ============================================================
build_slice() {
    local slice_name="$1"
    local build_dir="$2"
    local arch="$3"
    local platform="$4"
    local sysroot="$5"
    local mnn_flag="${6:-}"
    local opencv_flag="${7:-}"
    local opencv_ok="${8:-0}"
    local mp_flag="${9:-}"

    echo "${C_BOLD}==> 构建 iOS ${slice_name} (${arch}, ${platform}, sysroot=${sysroot})${C_RESET}"

    local cmake_args=(
        -S "${ROOT_DIR}" -B "${build_dir}"
        -DCMAKE_SYSTEM_NAME=iOS
        # 显式指定 SDK：CMake 对 arm64 默认选 iphoneos（只有 x86_64 自动选模拟器），
        # 不显式传会把 sim slice 编成 device 平台产物
        -DCMAKE_OSX_SYSROOT="${sysroot}"
        -DCMAKE_OSX_ARCHITECTURES="${arch}"
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOY_TARGET}"
        -DCMAKE_BUILD_TYPE=Release
        ${METAL_FLAG}
        ${opencv_flag}
        ${mnn_flag}
        ${mp_flag}
    )

    cmake "${cmake_args[@]}"
    cmake --build "${build_dir}" --target task_graph -j "${JOBS}"

    # 构建子模块（OpenCV 系子模块仅在该 slice 自身的 OpenCV 预编译库可用时构建）
    local submodules=()
    if [[ "${opencv_ok}" -eq 1 ]]; then
        for sub in ${OPENCV_SUBMODULES}; do submodules+=("${sub}"); done
    fi
    [[ "${NO_METAL}" -eq 0 ]] && submodules+=(gpu_image_processing)

    for sub in "${submodules[@]}"; do
        cmake --build "${build_dir}" --target "${sub}" -j "${JOBS}" \
            || echo "${C_RED}==> 子模块 ${sub} 构建失败（忽略，不影响其余产物）${C_RESET}" >&2
    done
}

build_slice "device" "${DEVICE_BUILD}" "${DEVICE_ARCH}" "OS" "iphoneos" "" "${OPENCV_DEVICE_FLAG}" "${OPENCV_DEVICE_OK}"

# ============================================================
# 构建 iOS simulator slice
# ============================================================
if [[ "${DEVICE_ONLY}" -eq 0 ]]; then
    SIM_MNN_FLAG=""
    if [[ "${MNN_SIM_OK}" -eq 0 && -f "${MNN_LIB}" ]]; then
        SIM_MNN_FLAG="-DTASK_GRAPH_ENABLE_MNN=OFF"
        echo "${C_BOLD}==> libMNN.a 为 device-only（无 IOSSIMULATOR slice），模拟器产物禁用 MNN${C_RESET}"
    fi
    build_slice "simulator" "${SIM_BUILD}" "${SIM_ARCH}" "Simulator" "iphonesimulator" "${SIM_MNN_FLAG}" "${OPENCV_SIM_FLAG}" "${OPENCV_SIM_OK}" "${MP_SIM_FLAG}"
fi

# ============================================================
# 合并静态库：核心 + 子模块 -> libtask_graph_full.a
# ============================================================
merge_static_libs() {
    local build_dir="$1"
    local include_mnn="$2"
    local opencv_install="${3:-}"
    local include_mp="${4:-0}"
    local output="${build_dir}/libtask_graph_full.a"
    local libs=("${build_dir}/libtask_graph.a")

    # 子模块 .a 在 add_subdirectory 的二进制目录 submodules/<name>/ 下（顶层没有）
    for sub in ${OPENCV_SUBMODULES} gpu_image_processing; do
        local lib="${build_dir}/submodules/${sub}/lib${sub}.a"
        [[ -f "${lib}" ]] || lib="${build_dir}/lib${sub}.a"
        [[ -f "${lib}" ]] && libs+=("${lib}")
    done

    # OpenCV 静态库——仅并入与该 slice 平台匹配的预编译库
    if [[ -n "${opencv_install}" && -d "${opencv_install}/lib" ]]; then
        for oc_lib in "${opencv_install}/lib"/libopencv_*.a; do
            [[ -f "${oc_lib}" ]] && libs+=("${oc_lib}")
        done
    fi

    # MNN 推理引擎静态库——仅并入平台匹配的 slice（device-only 库不进 sim slice）
    if [[ "${include_mnn}" -eq 1 && -f "${ROOT_DIR}/build_ios/mnn/install/lib/libMNN.a" ]]; then
        libs+=("${ROOT_DIR}/build_ios/mnn/install/lib/libMNN.a")
    fi

    # MediaPipe C API 静态库——device-only（build_mediapipe.py --platform ios），
    # 只并入 device slice；sim slice 已在 CMake 侧禁用 MediaPipe
    if [[ "${include_mp}" -eq 1 && -f "${ROOT_DIR}/build_ios/mediapipe/install/lib/libmediapipe_vision_c.a" ]]; then
        libs+=("${ROOT_DIR}/build_ios/mediapipe/install/lib/libmediapipe_vision_c.a")
    fi

    echo "${C_BOLD}==> 合并静态库 -> ${output##*/} (${#libs[@]} 个库)${C_RESET}"
    libtool -static -o "${output}" "${libs[@]}" 2>/dev/null
}

DEV_OC_DIR=""
[[ "${OPENCV_DEVICE_OK}" -eq 1 ]] && DEV_OC_DIR="${OPENCV_DEVICE_INSTALL}"
SIM_OC_DIR=""
[[ "${OPENCV_SIM_OK}" -eq 1 ]] && SIM_OC_DIR="${OPENCV_SIM_INSTALL}"

merge_static_libs "${DEVICE_BUILD}" 1 "${DEV_OC_DIR}" 1
if [[ "${DEVICE_ONLY}" -eq 0 ]]; then
    merge_static_libs "${SIM_BUILD}" "${MNN_SIM_OK}" "${SIM_OC_DIR}" 0
fi

# ============================================================
# 复制头文件
# ============================================================
echo "${C_BOLD}==> 复制头文件到 ${DIST_DIR}/include/${C_RESET}"
mkdir -p "${DIST_DIR}/include"
cp -R "${ROOT_DIR}/include/"* "${DIST_DIR}/include/"

# ============================================================
# 生成 .xcframework
# ============================================================
if [[ "${NO_XCFRAMEWORK}" -eq 0 && "${DEVICE_ONLY}" -eq 0 ]]; then
    echo "${C_BOLD}==> 生成 libtask_graph.xcframework${C_RESET}"
    rm -rf "${DIST_DIR}/libtask_graph.xcframework"

    xcodebuild -create-xcframework \
        -library "${DEVICE_BUILD}/libtask_graph_full.a" \
        -headers "${DIST_DIR}/include" \
        -library "${SIM_BUILD}/libtask_graph_full.a" \
        -headers "${DIST_DIR}/include" \
        -output "${DIST_DIR}/libtask_graph.xcframework" 2>&1 | tail -5

    echo "${C_GREEN}${C_BOLD}==> 构建完成：${C_RESET}"
    echo "    ${DIST_DIR}/libtask_graph.xcframework"
    echo "    ${DIST_DIR}/include/"
elif [[ "${DEVICE_ONLY}" -eq 1 ]]; then
    echo "${C_GREEN}${C_BOLD}==> 构建完成（仅 device）：${C_RESET}"
    echo "    ${DEVICE_BUILD}/libtask_graph_full.a"
    echo "    ${DIST_DIR}/include/"
else
    echo "${C_GREEN}${C_BOLD}==> 构建完成（无 xcframework）：${C_RESET}"
    echo "    ${DEVICE_BUILD}/libtask_graph_full.a"
    [[ -f "${SIM_BUILD}/libtask_graph_full.a" ]] && echo "    ${SIM_BUILD}/libtask_graph_full.a"
    echo "    ${DIST_DIR}/include/"
fi
