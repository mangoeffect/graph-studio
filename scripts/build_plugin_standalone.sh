#!/usr/bin/env bash
#
# build_plugin_standalone.sh — 独立编译一个 task_graph 插件为运行时动态库。
# 仅依赖 SDK 前缀（默认 <root>/build/sdk），不引用主仓库源码。
#
# 用法:
#   scripts/build_sdk.sh                                   # 先生成 SDK 前缀
#   scripts/build_plugin_standalone.sh examples/plugins/demo
#   scripts/build_plugin_standalone.sh submodules/opencv/image_processing/image_filtering --opencv
#   scripts/build_plugin_standalone.sh <src> --sdk /opt/tg-sdk -j 8
#   scripts/build_plugin_standalone.sh -h
#
# 产物: <root>/build/standalone/plugins/<name>/<name>.{dylib,so,dll}
#       （该目录已被 gitignore，与 scripts/build_sdk.sh 的 build/ 一致）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SDK_DIR="${ROOT_DIR}/build/sdk"
OUT_ROOT="${ROOT_DIR}/build/standalone/plugins"
JOBS=""
OPENCV=OFF

usage() {
    sed -n '3,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

if [[ $# -lt 1 ]]; then
    usage 1
fi

SRC_DIR="$1"; shift

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdk)       SDK_DIR="$2"; shift 2 ;;
        --out-root)  OUT_ROOT="$2"; shift 2 ;;
        --opencv)    OPENCV=ON; shift ;;
        -j|--jobs)   JOBS="$2"; shift 2 ;;
        -h|--help)   usage 0 ;;
        *) echo "未知参数: $1" >&2; usage 1 ;;
    esac
done

SRC_DIR="$(cd "${SRC_DIR}" && pwd)"
if [[ ! -f "${SRC_DIR}/CMakeLists.txt" ]]; then
    echo "错误: ${SRC_DIR} 下没有 CMakeLists.txt" >&2
    exit 1
fi
if [[ ! -f "${SDK_DIR}/lib/cmake/task_graph/task_graphConfig.cmake" ]]; then
    echo "错误: 未找到 SDK 包 ${SDK_DIR}/lib/cmake/task_graph。请先运行 scripts/build_sdk.sh" >&2
    exit 1
fi

if [[ -z "${JOBS}" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    elif command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
fi

NAME="$(basename "${SRC_DIR}")"
OUT_DIR="${OUT_ROOT}/${NAME}"

echo "==> 独立配置插件 ${NAME}（task_graph_DIR=${SDK_DIR}/lib/cmake/task_graph）"
cmake -S "${SRC_DIR}" -B "${OUT_DIR}" \
    -Dtask_graph_DIR="${SDK_DIR}/lib/cmake/task_graph" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTASK_GRAPH_ENABLE_OPENCV="${OPENCV}"

echo "==> 构建 ${NAME} (-j ${JOBS})"
cmake --build "${OUT_DIR}" -j "${JOBS}"

echo
echo "插件产物: ${OUT_DIR}"
echo "运行时 dlopen 该文件即可加载（见 PluginLoader / test_plugin_abi）"