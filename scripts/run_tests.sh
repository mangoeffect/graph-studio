#!/usr/bin/env bash
#
# run_tests.sh — 配置、构建并运行 task_graph 的全部单元测试。
#
# 默认行为：在 build/ 目录用 CMake 配置 + 构建，然后用 ctest 跑全部测试。
#
# 用法:
#   scripts/run_tests.sh                 # 构建并运行全部测试
#   scripts/run_tests.sh -c              # 先清空 build 目录再全新构建
#   scripts/run_tests.sh -b <dir>        # 指定构建目录（默认 build）
#   scripts/run_tests.sh -j <N>          # 并行编译线程数（默认 CPU 核数）
#   scripts/run_tests.sh -R <regex>      # 只运行名字匹配 regex 的测试
#   scripts/run_tests.sh -l              # 列出所有测试后退出，不运行
#   scripts/run_tests.sh --opencv        # 打开 TASK_GRAPH_ENABLE_OPENCV
#   scripts/run_tests.sh --sdk           # 先构建 SDK 前缀 + 独立 demo 插件，再跑含 test_plugin_abi 的全部测试
#   scripts/run_tests.sh --no-build      # 跳过配置/构建，直接跑现有二进制
#   scripts/run_tests.sh -v              # ctest 详细输出（--output-on-failure 默认已开）
#   scripts/run_tests.sh -R port -v      # 组合：只跑 port 相关测试 + 详细输出
#
# 退出码：0 表示全部通过，非 0 表示有测试失败或构建出错。

set -euo pipefail

# ---- 定位仓库根目录（脚本位于 <root>/scripts/）----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ---- 默认参数 ----
BUILD_DIR="build"
JOBS=""
FILTER=""
CLEAN=0
LIST_ONLY=0
NO_BUILD=0
VERBOSE=0
OPENCV=0
SDK=0

# ---- 颜色（仅在 TTY 下启用）----
if [[ -t 1 ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_BOLD=""; C_RESET=""
fi

usage() {
    sed -n '3,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

# ---- 参数解析 ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build-dir) BUILD_DIR="$2"; shift 2 ;;
        -j|--jobs)      JOBS="$2"; shift 2 ;;
        -R|--filter)    FILTER="$2"; shift 2 ;;
        -c|--clean)     CLEAN=1; shift ;;
        -l|--list)      LIST_ONLY=1; shift ;;
        --no-build)     NO_BUILD=1; shift ;;
        --opencv)       OPENCV=1; shift ;;
        --sdk)          SDK=1; shift ;;
        -v|--verbose)   VERBOSE=1; shift ;;
        -h|--help)      usage 0 ;;
        *) echo "${C_RED}未知参数: $1${C_RESET}" >&2; usage 1 ;;
    esac
done

cd "${ROOT_DIR}"

# ---- 并行度默认取 CPU 核数 ----
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
    echo "${C_BOLD}==> 清理构建目录 ${BUILD_DIR}${C_RESET}"
    rm -rf "${BUILD_DIR}"
fi

# ---- 配置 + 构建 ----
if [[ "${NO_BUILD}" -eq 0 ]]; then
    CMAKE_ARGS=(-B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug)
    if [[ "${OPENCV}" -eq 1 ]]; then
        CMAKE_ARGS+=(-DTASK_GRAPH_ENABLE_OPENCV=ON)
    fi
    echo "${C_BOLD}==> 配置 (cmake ${CMAKE_ARGS[*]})${C_RESET}"
    cmake "${CMAKE_ARGS[@]}"

    echo "${C_BOLD}==> 构建 (-j ${JOBS})${C_RESET}"
    cmake --build "${BUILD_DIR}" -j "${JOBS}"
else
    if [[ ! -d "${BUILD_DIR}" ]]; then
        echo "${C_RED}构建目录 ${BUILD_DIR} 不存在，且指定了 --no-build${C_RESET}" >&2
        exit 1
    fi
fi

# ---- 独立插件（--sdk）：构建 SDK 前缀 + 独立 demo 插件，供 test_plugin_abi 加载 ----
if [[ "${SDK}" -eq 1 ]]; then
    echo "${C_BOLD}==> 构建 SDK 前缀 (scripts/build_sdk.sh)${C_RESET}"
    "${SCRIPT_DIR}/build_sdk.sh" -j "${JOBS}"

    echo "${C_BOLD}==> 独立编译 demo 插件 (scripts/build_plugin_standalone.sh)${C_RESET}"
    "${SCRIPT_DIR}/build_plugin_standalone.sh" "${ROOT_DIR}/examples/plugins/demo" -j "${JOBS}"
fi

# ---- 列出测试 ----
if [[ "${LIST_ONLY}" -eq 1 ]]; then
    echo "${C_BOLD}==> 可用测试:${C_RESET}"
    ( cd "${BUILD_DIR}" && ctest -N )
    exit 0
fi

# ---- 运行测试 ----
CTEST_ARGS=(--output-on-failure)
if [[ -n "${FILTER}" ]]; then
    CTEST_ARGS+=(-R "${FILTER}")
fi
if [[ "${VERBOSE}" -eq 1 ]]; then
    CTEST_ARGS+=(-V)
fi

echo "${C_BOLD}==> 运行测试 (ctest ${CTEST_ARGS[*]})${C_RESET}"
set +e
( cd "${BUILD_DIR}" && ctest "${CTEST_ARGS[@]}" )
STATUS=$?
set -e

echo
if [[ "${STATUS}" -eq 0 ]]; then
    echo "${C_GREEN}${C_BOLD}==> 全部测试通过${C_RESET}"
else
    echo "${C_RED}${C_BOLD}==> 存在测试失败 (exit ${STATUS})${C_RESET}"
fi
exit "${STATUS}"
