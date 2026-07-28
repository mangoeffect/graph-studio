#!/usr/bin/env bash
#
# build_opencv_wasm.sh — 用 emscripten 编译 OpenCV 静态库（WASM 多线程）。
#
# 产出 libopencv_{core,imgproc,imgcodecs}.a 安装到 build_wasm/opencv/install/，
# 供 task_graph WASM build 链接。
#
# 用法：
#   scripts/build_opencv_wasm.sh                          # 自动 clone OpenCV 4.x 到 tmp
#   scripts/build_opencv_wasm.sh /path/to/opencv/src      # 指定本地 OpenCV 源码
#   scripts/build_opencv_wasm.sh --modules "core,imgproc" # 指定模块（默认 core+imgproc+imgcodecs）
#
# 前置：source emsdk_env.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EMSDK_ROOT="${EMSDK_ROOT:-/Users/wumango/emsdk}"
OPENCV_MODULES="core,imgproc,imgcodecs"
OPENCV_SRC=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --modules) OPENCV_MODULES="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) OPENCV_SRC="$1"; shift ;;
    esac
done

# source emsdk
# shellcheck disable=SC1091
source "${EMSDK_ROOT}/emsdk_env.sh" 2>/dev/null || true

BUILD_DIR="${ROOT_DIR}/build_wasm/opencv"
INSTALL_DIR="${BUILD_DIR}/install"

# 如果没指定源码，自动 clone
if [[ -z "${OPENCV_SRC}" ]]; then
    OPENCV_SRC="${BUILD_DIR}/opencv-src"
    if [[ ! -d "${OPENCV_SRC}" ]]; then
        echo "==> Cloning OpenCV 4.x source to ${OPENCV_SRC}"
        git clone --depth 1 --branch 4.x https://github.com/opencv/opencv.git "${OPENCV_SRC}"
    else
        echo "==> Using existing OpenCV source at ${OPENCV_SRC}"
    fi
fi

if [[ ! -f "${OPENCV_SRC}/CMakeLists.txt" ]]; then
    echo "ERROR: OpenCV source not found at ${OPENCV_SRC}"
    exit 1
fi

echo "==> Configuring OpenCV WASM (modules: ${OPENCV_MODULES})"
emcmake cmake -S "${OPENCV_SRC}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_LIST="${OPENCV_MODULES}" \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_opencv_apps=OFF \
    -DWITH_FFMPEG=OFF \
    -DWITH_GTK=OFF \
    -DWITH_V4L=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_PROTOBUF=OFF \
    -DWITH_TBB=OFF \
    -DWITH_OPENMP=OFF \
    -DWITH_IPP=OFF \
    -DWITH_LAPACK=OFF \
    -DWITH_ITT=OFF \
    -DWITH_EIGEN=OFF \
    -DENABLE_PIC=OFF \
    -DCMAKE_C_FLAGS="-pthread -msimd128" \
    -DCMAKE_CXX_FLAGS="-pthread -msimd128" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

echo "==> Building OpenCV WASM (-j $(sysctl -n hw.ncpu 2>/dev/null || echo 4))"
cmake --build "${BUILD_DIR}" -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "==> Installing to ${INSTALL_DIR}"
cmake --install "${BUILD_DIR}"

echo ""
echo "=== OpenCV WASM build complete ==="
echo "Install prefix: ${INSTALL_DIR}"
echo "Libraries:"
ls -1 "${INSTALL_DIR}/lib/"*.a 2>/dev/null || echo "  (no .a files found)"
echo ""
echo "To enable OpenCV in task_graph WASM build:"
echo "  cmake ... -DTASK_GRAPH_ENABLE_OPENCV=ON"
echo "  (OpenCV_DIR will be auto-detected from ${INSTALL_DIR}/lib/cmake/opencv4)"
