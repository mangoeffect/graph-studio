#!/usr/bin/env bash
#
# build_sdk.sh — 把 task_graph 框架构建为可分发 SDK（头文件 + 动态库 + CMake 包配置），
# 供子模块插件独立编译时通过 find_package(task_graph) 引用。
# 注意：以 TASK_GRAPH_BUILD_SUBMODULES=OFF 独立构建，主仓库不编译任何内置子模块源码。
#
# 用法:
#   scripts/build_sdk.sh                     # 默认 SDK 前缀 -> build/sdk，Release
#   scripts/build_sdk.sh --prefix /opt/tg-sdk# 自定义前缀
#   scripts/build_sdk.sh -j 8                # 并行度
#   scripts/build_sdk.sh --build-type Debug  # 构建类型（默认 Release）
#   scripts/build_sdk.sh --no-opencv         # 关闭 OpenCV（默认跟随主项目 ON）
#   scripts/build_sdk.sh -h
#
# 产物: <prefix>/include/{plugin_api.hpp, task_graph/**}
#       <prefix>/lib/libtask_graph.{dylib,so}
#       <prefix>/lib/cmake/task_graph/{task_graphConfig.cmake, task_graphTargets.cmake, SdkUtil.cmake}

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="${ROOT_DIR}/build/sdk"
BUILD_DIR="${ROOT_DIR}/build/sdk-build"
BUILD_TYPE="Release"
JOBS=""
OPENCV=ON

usage() {
    sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)     PREFIX="$2"; shift 2 ;;
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --no-opencv)  OPENCV=OFF; shift ;;
        -j|--jobs)    JOBS="$2"; shift 2 ;;
        -h|--help)    usage 0 ;;
        *) echo "未知参数: $1" >&2; usage 1 ;;
    esac
done

if [[ -z "${JOBS}" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    elif command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
fi

echo "==> 配置 (独立构建，不含内置子模块)"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DTASK_GRAPH_BUILD_SUBMODULES=OFF \
    -DTASK_GRAPH_ENABLE_OPENCV="${OPENCV}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}"

echo "==> 构建 libtask_graph (-j ${JOBS})"
cmake --build "${BUILD_DIR}" --target task_graph -j "${JOBS}"

echo "==> 安装 SDK 到 ${PREFIX}"
cmake --install "${BUILD_DIR}"

echo
echo "SDK 就绪: ${PREFIX}"
echo "  独立编译插件时配置: -Dtask_graph_DIR=${PREFIX}/lib/cmake/task_graph"