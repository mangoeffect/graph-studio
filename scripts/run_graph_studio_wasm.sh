#!/usr/bin/env bash
#
# run_graph_studio_wasm.sh — 构建 WASM 版 GraphStudio 并启动 dev server。
#
# 流程：
#   1) 用 emsdk 多线程编译 libtask_graph.a（带 -pthread）到 build_wasm/
#   2) 用 qt-cmake (Qt/6.6.3/wasm_multithread) 配置 + 构建 graph_studio wasm 到
#      app/graph_studio/build_wasm/
#   3) 启动 Python dev server（带 COOP/COEP header，启用 SharedArrayBuffer）
#
# 用法:
#   scripts/run_graph_studio_wasm.sh                # 构建 + 启动 server
#   scripts/run_graph_studio_wasm.sh --build-only   # 只构建，不启动 server
#   scripts/run_graph_studio_wasm.sh --no-build     # 跳过构建，直接启 server
#   scripts/run_graph_studio_wasm.sh --port 9000    # 指定 server 端口
#   scripts/run_graph_studio_wasm.sh --clean        # 清空两个 build 目录
#
# 环境要求：
#   - /Users/wumango/emsdk/ 已安装并 activate（自动 source emsdk_env.sh）
#   - /Users/wumango/Qt/6.6.3/wasm_multithread + macos 已安装（或自定义 EMSDK_ROOT / QT_WASM_ROOT / QT_HOST_ROOT）
#   - Python 3（自带 http.server）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EMSDK_ROOT="${EMSDK_ROOT:-/Users/wumango/emsdk}"
QT_WASM_ROOT="${QT_WASM_ROOT:-/Users/wumango/Qt/6.6.3/wasm_multithread}"
QT_HOST_ROOT="${QT_HOST_ROOT:-/Users/wumango/Qt/6.6.3/macos}"

LIB_BUILD="${ROOT_DIR}/build_wasm"
GS_BUILD="${ROOT_DIR}/app/graph_studio/build_wasm"
JOBS=""
BUILD_ONLY=0
NO_BUILD=0
PORT="8000"
CLEAN=0

if [[ -t 1 ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_BOLD=""; C_RESET=""
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-only) BUILD_ONLY=1; shift ;;
        --no-build)   NO_BUILD=1; shift ;;
        --port)       PORT="$2"; shift 2 ;;
        --clean)      CLEAN=1; shift ;;
        -j|--jobs)    JOBS="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "${C_RED}未知参数: $1${C_RESET}" >&2; exit 1 ;;
    esac
done

[[ -z "${JOBS}" ]] && JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

cd "${ROOT_DIR}"

# 环境检查
if [[ ! -d "${EMSDK_ROOT}" ]]; then
    echo "${C_RED}找不到 emsdk：${EMSDK_ROOT}${C_RESET}" >&2
    exit 1
fi
if [[ ! -x "${QT_WASM_ROOT}/bin/qt-cmake" ]]; then
    echo "${C_RED}找不到 Qt wasm qt-cmake：${QT_WASM_ROOT}/bin/qt-cmake${C_RESET}" >&2
    exit 1
fi

# 清理
if [[ "${CLEAN}" -eq 1 ]]; then
    echo "${C_BOLD}==> 清理 WASM 构建目录${C_RESET}"
    rm -rf "${LIB_BUILD}" "${GS_BUILD}"
fi

if [[ "${NO_BUILD}" -eq 0 ]]; then
    # 1) source emsdk env
    echo "${C_BOLD}==> 激活 emsdk${C_RESET}"
    # shellcheck disable=SC1091
    source "${EMSDK_ROOT}/emsdk_env.sh" >/dev/null 2>&1 || true

    # 2) 构建 libtask_graph.a（多线程：-pthread）
    echo "${C_BOLD}==> 构建 libtask_graph.a (WASM + pthread)${C_RESET}"
    cmake -S . -B "${LIB_BUILD}" \
        -DCMAKE_TOOLCHAIN_FILE="${EMSDK_ROOT}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-pthread" \
        -DCMAKE_C_FLAGS="-pthread" \
        -DCMAKE_EXE_LINKER_FLAGS="-pthread -sUSE_PTHREADS=1" \
        -DTASK_GRAPH_ENABLE_OPENCV=OFF >/dev/null
    cmake --build "${LIB_BUILD}" --target task_graph -j "${JOBS}"

    # 3) 配置 + 构建 graph_studio WASM
    echo "${C_BOLD}==> 配置 graph_studio WASM${C_RESET}"
    EMSDK="${EMSDK_ROOT}" "${QT_WASM_ROOT}/bin/qt-cmake" \
        -S app/graph_studio -B "${GS_BUILD}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DQT_HOST_PATH="${QT_HOST_ROOT}"

    echo "${C_BOLD}==> 构建 graph_studio.wasm (-j ${JOBS})${C_RESET}"
    cmake --build "${GS_BUILD}" -j "${JOBS}"
fi

if [[ ! -f "${GS_BUILD}/graph_studio.html" ]]; then
    echo "${C_RED}未找到 graph_studio.html，构建失败${C_RESET}" >&2
    exit 1
fi

WASM_SIZE=$(du -h "${GS_BUILD}/graph_studio.wasm" | cut -f1)
echo "${C_GREEN}${C_BOLD}==> 构建完成：${GS_BUILD}/graph_studio.wasm (${WASM_SIZE})${C_RESET}"

if [[ "${BUILD_ONLY}" -eq 1 ]]; then
    exit 0
fi

# 4) 启动 dev server（COOP/COEP）
echo "${C_BOLD}==> 启动 dev server (COOP+COEP, port ${PORT})${C_RESET}"
echo "    浏览器访问：http://localhost:${PORT}/graph_studio.html"
echo "    Ctrl+C 停止"
exec env PORT="${PORT}" python3 "${SCRIPT_DIR}/wasm_dev_server.py" "${GS_BUILD}"
