#!/usr/bin/env bash
#
# fetch_sentry.sh — 拉取 sentry-native（含递归 git 子模块 crashpad）到
# app/graph_studio/third_party/sentry-native，供 GraphStudio 崩溃上报使用。
#
# sentry-native 的 crashpad backend 依赖 Chromium crashpad，仓库内含多层
# git 子模块（external/crashpad → mini_chromium/third_party），CMake 3.22 的
# FetchContent 无法可靠递归拉取，因此用固定版本脚本克隆（与
# download_mediapipe_models.sh 同一约定，结果目录被 gitignore）。
#
# 用法:
#   scripts/fetch_sentry.sh              # 拉取固定版本 0.16.2
#
# 环境变量:
#   SENTRY_NATIVE_REF  覆盖版本/tag（默认 0.16.2）
#
# 若目录已存在则跳过（可用 -f 强制重拉，见下）。
#   scripts/fetch_sentry.sh -f           # 删除已有目录后重新克隆

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_DIR="${ROOT_DIR}/app/graph_studio/third_party/sentry-native"

REF="${SENTRY_NATIVE_VERSION:-0.16.2}"
FORCE=0
[[ "${1:-}" == "-f" ]] && FORCE=1

if [[ -d "${DEST_DIR}" && "${FORCE}" -ne 1 ]]; then
    echo "sentry-native 已存在于 ${DEST_DIR}，跳过（用 -f 强制重拉）"
    exit 0
fi

if [[ -d "${DEST_DIR}" ]]; then
    rm -rf "${DEST_DIR}"
fi

mkdir -p "$(dirname "${DEST_DIR}")"
echo "==> 克隆 sentry-native@${REF}（递归子模块，首次较慢）"

# --recursive 拉取 external/crashpad 及其嵌套子模块；--depth 1 只取一个提交。
git clone --depth 1 --branch "${REF}" --recursive \
    https://github.com/getsentry/sentry-native.git "${DEST_DIR}"

echo "==> sentry-native@${REF} 已就绪: ${DEST_DIR}"