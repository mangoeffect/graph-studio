#!/usr/bin/env bash
#
# run_graph_studio.sh — 构建并启动 GraphStudio (Qt6 GUI)。
#
# 默认行为：
#   1) 先构建根库 task_graph（GraphStudio 运行时依赖 build/libtask_graph.dylib）
#   2) 在 app/graph_studio/build 用 CMake 配置 + 构建 graph_studio
#   3) 启动生成的应用（macOS 为 .app bundle，其它平台为可执行文件）
#
# 用法:
#   scripts/run_graph_studio.sh              # 构建并启动
#   scripts/run_graph_studio.sh -c           # 清空构建目录后全新构建
#   scripts/run_graph_studio.sh -j <N>       # 并行编译线程数（默认 CPU 核数）
#   scripts/run_graph_studio.sh --no-build   # 跳过构建，直接启动现有产物
#   scripts/run_graph_studio.sh --build-only # 只构建，不启动
#   scripts/run_graph_studio.sh -t           # 运行 GraphStudio 的单元测试（ctest）
#   scripts/run_graph_studio.sh --qt <path>  # 手动指定 Qt6 前缀（含 lib/cmake/Qt6）
#
# 退出码：0 表示成功，非 0 表示构建出错或未找到产物。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GS_DIR="${ROOT_DIR}/app/graph_studio"
GS_BUILD="${GS_DIR}/build"
LIB_BUILD="${ROOT_DIR}/build"

JOBS=""
CLEAN=0
NO_BUILD=0
BUILD_ONLY=0
RUN_TESTS=0
QT_PREFIX="${QT_PREFIX_PATH:-}"

if [[ -t 1 ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_BOLD=""; C_RESET=""
fi

usage() {
    sed -n '3,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs)     JOBS="$2"; shift 2 ;;
        -c|--clean)    CLEAN=1; shift ;;
        --no-build)    NO_BUILD=1; shift ;;
        --build-only)  BUILD_ONLY=1; shift ;;
        -t|--test)     RUN_TESTS=1; shift ;;
        --qt)          QT_PREFIX="$2"; shift 2 ;;
        -h|--help)     usage 0 ;;
        *) echo "${C_RED}未知参数: $1${C_RESET}" >&2; usage 1 ;;
    esac
done

cd "${ROOT_DIR}"

# ---- 并行度 ----
if [[ -z "${JOBS}" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    elif command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
fi

# ---- 清理 ----
if [[ "${CLEAN}" -eq 1 ]]; then
    echo "${C_BOLD}==> 清理 GraphStudio 构建目录${C_RESET}"
    rm -rf "${GS_BUILD}"
fi

if [[ "${NO_BUILD}" -eq 0 ]]; then
    # ---- 1) 构建根库 task_graph（GraphStudio 依赖 build/libtask_graph）----
    echo "${C_BOLD}==> 构建 task_graph 库${C_RESET}"
    cmake -B "${LIB_BUILD}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "${LIB_BUILD}" --target task_graph -j "${JOBS}"

    # ---- 2) 配置 + 构建 graph_studio ----
    CMAKE_ARGS=(-S "${GS_DIR}" -B "${GS_BUILD}" -DCMAKE_BUILD_TYPE=Debug)
    if [[ -n "${QT_PREFIX}" ]]; then
        CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
    fi
    echo "${C_BOLD}==> 配置 graph_studio${C_RESET}"
    cmake "${CMAKE_ARGS[@]}"

    echo "${C_BOLD}==> 构建 graph_studio (-j ${JOBS})${C_RESET}"
    cmake --build "${GS_BUILD}" -j "${JOBS}"
else
    if [[ ! -d "${GS_BUILD}" ]]; then
        echo "${C_RED}构建目录 ${GS_BUILD} 不存在，且指定了 --no-build${C_RESET}" >&2
        exit 1
    fi
fi

# ---- 运行单元测试 ----
if [[ "${RUN_TESTS}" -eq 1 ]]; then
    echo "${C_BOLD}==> 运行 GraphStudio 单元测试${C_RESET}"
    ( cd "${GS_BUILD}" && ctest --output-on-failure )
    exit $?
fi

# ---- 仅构建 ----
if [[ "${BUILD_ONLY}" -eq 1 ]]; then
    echo "${C_GREEN}${C_BOLD}==> 构建完成（--build-only）${C_RESET}"
    exit 0
fi

# ---- 定位并启动产物 ----
APP_BUNDLE="${GS_BUILD}/graph_studio.app"
BIN="${GS_BUILD}/graph_studio"

echo "${C_BOLD}==> 启动 GraphStudio${C_RESET}"
if [[ "$(uname)" == "Darwin" && -d "${APP_BUNDLE}" ]]; then
    # macOS：用 open 启动 .app bundle
    open "${APP_BUNDLE}"
elif [[ -x "${BIN}" ]]; then
    exec "${BIN}"
elif [[ -x "${APP_BUNDLE}/Contents/MacOS/graph_studio" ]]; then
    exec "${APP_BUNDLE}/Contents/MacOS/graph_studio"
else
    echo "${C_RED}未找到 GraphStudio 产物（${APP_BUNDLE} 或 ${BIN}）${C_RESET}" >&2
    exit 1
fi
