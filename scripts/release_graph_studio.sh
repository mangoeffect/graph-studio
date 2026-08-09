#!/usr/bin/env bash
#
# release_graph_studio.sh — 构建发布版 GraphStudio（生产崩溃上报环境）。
#
# 与 run_graph_studio.sh 的差别：
#   * CMAKE_BUILD_TYPE=RelWithDebInfo（-O2 仍含 -g 调试信息，供 Sentry 符号化）
#   * 可选嵌入 Sentry DSN：-e <dsn> 或环境变量 GRAPH_STUDIO_SENTRY_DSN
#     （DSN 为公开 client key，可嵌入；运行期 SENTRY_DSN 环境变量优先级更高）
#   * environment=production（由 CMake 依据 build type 自动判定）
#
# 用法:
#   scripts/release_graph_studio.sh                     # 默认 DSN 嵌入自 env
#   scripts/release_graph_studio.sh -e <dsn>            # 显式嵌入 DSN
#   scripts/release_graph_studio.sh -c                  # 清理后全新构建
#   scripts/release_graph_studio.sh -j <N>              # 并行度
#   scripts/release_graph_studio.sh --upload-symbols    # 构建后上传符号（需 SENTRY_AUTH_TOKEN）
#
# 环境变量（可选）：
#   GRAPH_STUDIO_SENTRY_DSN   嵌入的 Sentry DSN
#   SENTRY_ORG / SENTRY_PROJECT / SENTRY_AUTH_TOKEN   上传符号用

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GS_DIR="${ROOT_DIR}/app/graph_studio"
GS_BUILD="${GS_DIR}/build"
LIB_BUILD="${ROOT_DIR}/build"

JOBS=""
CLEAN=0
UPLOAD_SYMBOLS=0
DSN="${GRAPH_STUDIO_SENTRY_DSN:-}"
QT_PREFIX="${QT_PREFIX_PATH:-}"

[[ -n "${GRAPH_STUDIO_SENTRY_DSN:-}" ]] && DSN="${GRAPH_STUDIO_SENTRY_DSN}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -e|--dsn)         DSN="$2"; shift 2 ;;
        -j|--jobs)        JOBS="$2"; shift 2 ;;
        -c|--clean)       CLEAN=1; shift ;;
        --upload-symbols) UPLOAD_SYMBOLS=1; shift ;;
        --qt)             QT_PREFIX="$2"; shift 2 ;;
        -h|--help)        echo "用法: $0 [-e <dsn>] [-j N] [-c] [--upload-symbols]"; exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
done

cd "${ROOT_DIR}"

if [[ -z "$JOBS" ]]; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi
if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf "${GS_BUILD}"
fi

echo "==> 构建 task_graph 库 + subnode 插件 (RelWithDebInfo)"
cmake -B "${LIB_BUILD}" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTASK_GRAPH_ENABLE_OPENCV=ON -DTASK_GRAPH_ENABLE_METAL=ON >/dev/null
cmake --build "${LIB_BUILD}" -j "${JOBS}" >/dev/null

echo "==> 配置 graph_studio (RelWithDebInfo)"
GS_CMAKE_ARGS=(-S "${GS_DIR}" -B "${GS_BUILD}" -DCMAKE_BUILD_TYPE=RelWithDebInfo)
if [[ -n "$QT_PREFIX" ]]; then
    GS_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
fi
if [[ -n "$DSN" ]]; then
    echo "  嵌入 Sentry DSN: ${DSN}"
    GS_CMAKE_ARGS+=("-DGRAPH_STUDIO_SENTRY_DSN=${DSN}")
fi
cmake "${GS_CMAKE_ARGS[@]}"

echo "==> 构建 graph_studio (-j ${JOBS})"
cmake --build "${GS_BUILD}" -j "${JOBS}"

if [[ "$UPLOAD_SYMBOLS" -eq 1 ]]; then
    echo "==> 上传符号"
    DSN="${DSN}" "${SCRIPT_DIR}/upload_sentry_symbols.sh"
fi

APP_BUNDLE="${GS_BUILD}/graph_studio.app"
if [[ -d "${APP_BUNDLE}" ]]; then
    echo "==> 产物: ${APP_BUNDLE}"
else
    echo "==> 产物: ${GS_BUILD}/graph_studio"
fi
echo "==> 发布构建完成（environment=production，embedded DSN=${DSN:+yes}）"