#!/usr/bin/env bash
#
# build_opencv_ios.sh - 用 CMake 交叉编译 OpenCV 静态库（iOS arm64）。
#
# 产出 libopencv_{core,imgproc,imgcodecs}.a 安装到 build_ios/opencv/install/，
# 供 task_graph iOS build 链接。
#
# 用法：
#   scripts/build_opencv_ios.sh                          # 自动 clone OpenCV 4.x
#   scripts/build_opencv_ios.sh /path/to/opencv/src      # 指定本地 OpenCV 源码
#   scripts/build_opencv_ios.sh --modules "core,imgproc" # 指定模块
#   scripts/build_opencv_ios.sh --sim                    # 构建模拟器版本（arm64 sim）
#   scripts/build_opencv_ios.sh --deploy-target 14.0     # 最低 iOS 版本（默认 13.0）
#
# 前置：已安装 Xcode + CMake 3.16+

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

OPENCV_MODULES="core,imgproc,imgcodecs"
OPENCV_SRC=""
SIM=0
DEPLOY_TARGET="13.0"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --modules)       OPENCV_MODULES="$2"; shift 2 ;;
        --sim)           SIM=1; shift ;;
        --deploy-target) DEPLOY_TARGET="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) OPENCV_SRC="$1"; shift ;;
    esac
done

if [[ "${SIM}" -eq 1 ]]; then
    BUILD_DIR="${ROOT_DIR}/build_ios_sim/opencv"
    ARCH="arm64"
    SDK="iphonesimulator"
else
    BUILD_DIR="${ROOT_DIR}/build_ios/opencv"
    ARCH="arm64"
    SDK="iphoneos"
fi
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

echo "==> Configuring OpenCV iOS (${SDK} ${ARCH}, modules: ${OPENCV_MODULES})"
cmake -S "${OPENCV_SRC}" -B "${BUILD_DIR}" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOY_TARGET}" \
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

echo "==> Building OpenCV iOS (-j $(sysctl -n hw.ncpu 2>/dev/null || echo 4))"
cmake --build "${BUILD_DIR}" -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "==> Installing to ${INSTALL_DIR}"
cmake --install "${BUILD_DIR}"

echo ""
echo "=== OpenCV iOS build complete ==="
echo "Install prefix: ${INSTALL_DIR}"
echo "Libraries:"
ls -1 "${INSTALL_DIR}/lib/"*.a 2>/dev/null || echo "  (no .a files found)"
echo ""
echo "To enable OpenCV in task_graph iOS build:"
echo "  scripts/build_ios.sh  (会自动检测 build_ios/opencv/install/)"
