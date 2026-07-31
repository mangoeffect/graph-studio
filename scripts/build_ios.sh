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

# OpenCV 可用性检查
OPENCV_FLAG=""
OPENCV_AVAILABLE=0
if [[ "${NO_OPENCV}" -eq 0 ]]; then
    if [[ -d "${ROOT_DIR}/build_ios/opencv/install/lib/cmake/opencv4" ]]; then
        OPENCV_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=ON"
        OPENCV_AVAILABLE=1
        echo "${C_BOLD}==> 检测到 OpenCV iOS 库，启用 OpenCV${C_RESET}"
    else
        echo "${C_BOLD}==> 未检测到 OpenCV iOS 库（运行 scripts/build_opencv_ios.sh 构建），跳过 OpenCV 子模块${C_RESET}"
    fi
fi

# Metal 标志
METAL_FLAG=""
if [[ "${NO_METAL}" -eq 0 ]]; then
    METAL_FLAG="-DTASK_GRAPH_ENABLE_METAL=ON"
else
    echo "${C_BOLD}==> 跳过 Metal GPU 子模块${C_RESET}"
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

    echo "${C_BOLD}==> 构建 iOS ${slice_name} (${arch}, ${platform})${C_RESET}"

    local cmake_args=(
        -S "${ROOT_DIR}" -B "${build_dir}"
        -DCMAKE_SYSTEM_NAME=iOS
        -DCMAKE_OSX_ARCHITECTURES="${arch}"
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOY_TARGET}"
        -DCMAKE_BUILD_TYPE=Release
        ${METAL_FLAG}
        ${OPENCV_FLAG}
    )

    cmake "${cmake_args[@]}"
    cmake --build "${build_dir}" --target task_graph -j "${JOBS}"

    # 构建子模块
    local submodules=(task1 task2 task_processor)
    [[ "${OPENCV_AVAILABLE}" -eq 1 ]] && submodules+=(image_filtering image_reader)
    [[ "${NO_METAL}" -eq 0 ]] && submodules+=(gpu_image_processing)

    for sub in "${submodules[@]}"; do
        cmake --build "${build_dir}" --target "${sub}" -j "${JOBS}" 2>/dev/null || true
    done
}

build_slice "device" "${DEVICE_BUILD}" "${DEVICE_ARCH}" "OS"

# ============================================================
# 构建 iOS simulator slice
# ============================================================
if [[ "${DEVICE_ONLY}" -eq 0 ]]; then
    build_slice "simulator" "${SIM_BUILD}" "${SIM_ARCH}" "Simulator"
fi

# ============================================================
# 合并静态库：核心 + 子模块 -> libtask_graph_full.a
# ============================================================
merge_static_libs() {
    local build_dir="$1"
    local output="${build_dir}/libtask_graph_full.a"
    local libs=("${build_dir}/libtask_graph.a")

    for sub in task1 task2 task_processor image_filtering image_reader gpu_image_processing; do
        local lib="${build_dir}/lib${sub}.a"
        [[ -f "${lib}" ]] && libs+=("${lib}")
    done

    # OpenCV 静态库（如果存在）
    if [[ "${OPENCV_AVAILABLE}" -eq 1 ]]; then
        local oc_dir="${ROOT_DIR}/build_ios/opencv/install/lib"
        for oc_lib in "${oc_dir}"/libopencv_*.a; do
            [[ -f "${oc_lib}" ]] && libs+=("${oc_lib}")
        done
    fi

    echo "${C_BOLD}==> 合并静态库 -> ${output##*/} (${#libs[@]} 个库)${C_RESET}"
    libtool -static -o "${output}" "${libs[@]}" 2>/dev/null
}

merge_static_libs "${DEVICE_BUILD}"
if [[ "${DEVICE_ONLY}" -eq 0 ]]; then
    merge_static_libs "${SIM_BUILD}"
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
