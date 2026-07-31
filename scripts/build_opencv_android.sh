#!/usr/bin/env bash
#
# build_opencv_android.sh - 用 Android NDK 交叉编译 OpenCV 静态库。
#
# 产出 libopencv_{core,imgproc,imgcodecs}.a 安装到 build_android/opencv/install/，
# 供 task_graph Android build 链接。
#
# 用法：
#   scripts/build_opencv_android.sh                          # 自动 clone OpenCV 4.x
#   scripts/build_opencv_android.sh /path/to/opencv/src      # 指定本地 OpenCV 源码
#   scripts/build_opencv_android.sh --modules "core,imgproc" # 指定模块
#   scripts/build_opencv_android.sh --abi armeabi-v7a        # 指定 ABI（默认 arm64-v8a）
#   scripts/build_opencv_android.sh --api 24                 # Android API level（默认 21）
#
# 前置：设置 ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

OPENCV_MODULES="core,imgproc,imgcodecs"
OPENCV_SRC=""
ABI="arm64-v8a"
API_LEVEL="21"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --modules) OPENCV_MODULES="$2"; shift 2 ;;
        --abi)     ABI="$2"; shift 2 ;;
        --api)     API_LEVEL="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) OPENCV_SRC="$1"; shift ;;
    esac
done

ANDROID_NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}"
if [[ -z "${ANDROID_NDK}" ]]; then
    echo "ERROR: 找不到 Android NDK。请设置 ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量"
    exit 1
fi
if [[ ! -f "${ANDROID_NDK}/build/cmake/android.toolchain.cmake" ]]; then
    echo "ERROR: NDK toolchain 文件不存在：${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
    exit 1
fi

TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"

BUILD_DIR="${ROOT_DIR}/build_android/opencv"
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

echo "==> Configuring OpenCV Android (${ABI}, API ${API_LEVEL}, modules: ${OPENCV_MODULES})"
cmake -S "${OPENCV_SRC}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="android-${API_LEVEL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_LIST="${OPENCV_MODULES}" \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_opencv_gapi=OFF \
    -DWITH_ADE=OFF \
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
    -DCV_ENABLE_INTRINSICS=OFF \
    -DCPU_BASELINE="" \
    -DCPU_DISPATCH="" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

echo "==> Building OpenCV Android (-j $(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4))"
cmake --build "${BUILD_DIR}" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4)"

echo "==> Installing to ${INSTALL_DIR}"
cmake --install "${BUILD_DIR}"

echo ""
echo "=== OpenCV Android build complete ==="
echo "Install prefix: ${INSTALL_DIR}"
echo "Libraries:"
ls -1 "${INSTALL_DIR}/lib/"*.a 2>/dev/null || echo "  (no .a files found)"
echo ""
echo "To enable OpenCV in task_graph Android build:"
echo "  scripts/build_android.sh  (会自动检测 build_android/opencv/install/)"
