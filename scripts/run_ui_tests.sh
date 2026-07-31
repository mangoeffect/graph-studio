#!/usr/bin/env bash
#
# run_ui_tests.sh - 构建并运行 GraphStudio 的 UI 自动化测试 (test_gui)。
#
# test_gui 用 QApplication 实例化真实 MainWindow，QTest 驱动鼠标/键盘，
# 内省 QGraphicsScene 与 GraphViewModel 状态；默认以 QT_QPA_PLATFORM=offscreen
# 无头运行（test_gui.cpp 内置回落，无需额外配置）。
#
# 流程：
#   1) 确保根库 task_graph 已构建（test_gui 链接 build/libtask_graph.dylib）
#   2) 配置 + 构建 graph_studio 的 test_gui 目标
#   3) 用 ctest 运行 test_gui（默认仅 UI 测试，--all 跑全部 graph_studio 测试）
#
# 用法:
#   scripts/run_ui_tests.sh              # 构建 + 运行 test_gui
#   scripts/run_ui_tests.sh --no-build   # 跳过构建，直接跑现有 test_gui
#   scripts/run_ui_tests.sh --build-only # 只构建，不运行
#   scripts/run_ui_tests.sh --all        # 跑全部 graph_studio 测试（含单元/集成）
#   scripts/run_ui_tests.sh -c           # 清空 graph_studio 构建目录后全新构建
#   scripts/run_ui_tests.sh -j <N>       # 并行编译线程数（默认 CPU 核数）
#   scripts/run_ui_tests.sh -v           # ctest 详细输出
#   scripts/run_ui_tests.sh --qt <path>  # 手动指定 Qt6 前缀（含 lib/cmake/Qt6）
#
# 退出码：0 表示测试通过，非 0 表示构建出错或测试失败。

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
RUN_ALL=0
VERBOSE=0
QT_PREFIX="${QT_PREFIX_PATH:-}"

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
        -j|--jobs)     JOBS="$2"; shift 2 ;;
        -c|--clean)    CLEAN=1; shift ;;
        --no-build)    NO_BUILD=1; shift ;;
        --build-only)  BUILD_ONLY=1; shift ;;
        --all)         RUN_ALL=1; shift ;;
        -v|--verbose)  VERBOSE=1; shift ;;
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
    # ---- 1) 确保根库 task_graph 已构建（test_gui 运行时依赖 libtask_graph）----
    if [[ ! -f "${LIB_BUILD}/CMakeCache.txt" ]]; then
        echo "${C_BOLD}==> 配置 task_graph 库${C_RESET}"
        cmake -B "${LIB_BUILD}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
    fi
    echo "${C_BOLD}==> 构建 task_graph 库 (-j ${JOBS})${C_RESET}"
    cmake --build "${LIB_BUILD}" --target task_graph -j "${JOBS}"

    # ---- 2) 配置 + 构建 test_gui ----
    CMAKE_ARGS=(-S "${GS_DIR}" -B "${GS_BUILD}" -DCMAKE_BUILD_TYPE=Debug)
    if [[ -n "${QT_PREFIX}" ]]; then
        CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
    fi
    echo "${C_BOLD}==> 配置 graph_studio${C_RESET}"
    cmake "${CMAKE_ARGS[@]}"

    # --all 需要构建全部测试目标；默认仅构建 test_gui（其它测试可执行体不存在时
    # ctest 会报 "Unable to find executable"，故按需构建）。
    if [[ "${RUN_ALL}" -eq 1 ]]; then
        BUILD_TARGETS=(test_graph_view_model test_command_stack test_integration test_gui)
        echo "${C_BOLD}==> 构建全部测试目标 (-j ${JOBS})${C_RESET}"
    else
        BUILD_TARGETS=(test_gui)
        echo "${C_BOLD}==> 构建 test_gui (-j ${JOBS})${C_RESET}"
    fi
    cmake --build "${GS_BUILD}" --target "${BUILD_TARGETS[@]}" -j "${JOBS}"
else
    if [[ ! -d "${GS_BUILD}" ]]; then
        echo "${C_RED}构建目录 ${GS_BUILD} 不存在，且指定了 --no-build${C_RESET}" >&2
        exit 1
    fi
fi

# ---- 仅构建 ----
if [[ "${BUILD_ONLY}" -eq 1 ]]; then
    echo "${C_GREEN}${C_BOLD}==> 构建完成（--build-only）${C_RESET}"
    exit 0
fi

# ---- 3) 运行测试 ----
# test_gui.cpp 已在未设 QT_QPA_PLATFORM 时回落 offscreen；此处不强制覆盖，
# 开发者可设 QT_QPA_PLATFORM=cocoa 观察画面（需 --no-build 复用已有产物）。
CTEST_ARGS=(--output-on-failure)
if [[ "${RUN_ALL}" -eq 0 ]]; then
    CTEST_ARGS+=(-R test_gui)
fi
if [[ "${VERBOSE}" -eq 1 ]]; then
    CTEST_ARGS+=(-V)
fi

echo "${C_BOLD}==> 运行 UI 测试 (ctest ${CTEST_ARGS[*]})${C_RESET}"
set +e
( cd "${GS_BUILD}" && ctest "${CTEST_ARGS[@]}" )
STATUS=$?
set -e

echo
if [[ "${STATUS}" -eq 0 ]]; then
    echo "${C_GREEN}${C_BOLD}==> UI 测试通过${C_RESET}"
else
    echo "${C_RED}${C_BOLD}==> UI 测试失败 (exit ${STATUS})${C_RESET}"
fi
exit "${STATUS}"
