#!/usr/bin/env bash
#
# upload_sentry_symbols.sh — 上传 GraphStudio 崩溃符号到 Sentry，用于后台
# 还原 minidump 中的函数名/行号。
#
#   macOS:  上传 .dSYM（缺失时用 dsymutil 从带 -g 的可执行文件/库现生成）
#   Windows: 上传 .pdb
# 覆盖产物：GraphStudio（app/graph_studio/build）+ task_graph 库 + subnode 插件
# （根 build/），这样崩溃发生在共享库/插件里也能符号化。
#
# 前置：
#   sentry-cli（brew install getsentry/tools/sentry-cli）
# 必需环境变量：SENTRY_AUTH_TOKEN
# 可选：SENTRY_ORG / SENTRY_PROJECT / GS_BUILD_DIR（默认 app/graph_studio/build）
#
# 用法：
#   scripts/upload_sentry_symbols.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_BUILD="${GS_BUILD_DIR:-${ROOT_DIR}/app/graph_studio/build}"
LIB_BUILD="${ROOT_DIR}/build"

command -v sentry-cli >/dev/null 2>&1 || {
    echo "sentry-cli 未安装，请先: brew install getsentry/tools/sentry-cli" >&2
    exit 1
}
[[ -z "${SENTRY_AUTH_TOKEN:-}" ]] && {
    echo "请设置 SENTRY_AUTH_TOKEN（sentry-cli 认证 token）" >&2
    exit 1
}
[[ -d "${APP_BUILD}" ]] || {
    echo "构建目录不存在: ${APP_BUILD}（先运行 scripts/run_graph_studio.sh）" >&2
    exit 1
}

ARGS=()
[[ -n "${SENTRY_ORG:-}" ]] && ARGS+=(--org "${SENTRY_ORG}")
[[ -n "${SENTRY_PROJECT:-}" ]] && ARGS+=(--project "${SENTRY_PROJECT}")

if [[ "$(uname)" == "Darwin" ]]; then
    # ---- 收集待上传对象 ----
    # 1) 优先复用已有的 .dSYM；2) 对每个 Mach-O 缺 dSYM 时用 dsymutil 生成。
    candidates=()

    graph_studio_bin="${APP_BUILD}/graph_studio.app/Contents/MacOS/graph_studio"
    [[ -f "${graph_studio_bin}" ]] && candidates+=("${graph_studio_bin}")
    candidates+=("${LIB_BUILD}/libtask_graph.dylib")
    # subnode 插件
    for so in "${LIB_BUILD}"/submodules/*/*.dylib; do
        [[ -f "${so}" ]] && candidates+=("${so}")
    done

    # 去重并生成 dSYM
    for bin in $(printf '%s\n' "${candidates[@]}" | sort -u); do
        sym="$(dirname "${bin}")/$(basename "${bin}").dSYM"
        if [[ ! -d "${sym}" ]]; then
            echo "==> 生成 ${sym}"
            dsymutil "${bin}" -o "${sym}"
        fi
        [[ -d "${sym}" ]] && printf '%s\n' "${sym}"
    done > "${TMPDIR:-/tmp}/gs_dsyms_$$.txt"

    dsyms=()
    while IFS= read -r line; do [[ -n "${line}" ]] && dsyms+=("${line}"); done < "${TMPDIR:-/tmp}/gs_dsyms_$$.txt"
    rm -f "${TMPDIR:-/tmp}/gs_dsyms_$$.txt"

    if [[ ${#dsyms[@]} -eq 0 ]]; then
        echo "未找到可上传的符号（构建需带 -g / DEBUG_INFO）" >&2
        exit 1
    fi

    echo "==> 上传 ${#dsyms[@]} 个 dSYM：$(printf '%s\n' "${dsyms[@]}")"
    sentry-cli debug-files upload -t dsym "${ARGS[@]}" "${dsyms[@]}"
else
    # Windows：PDB（app + 库 + 插件）
    echo "==> 上传 Windows PDB 到 Sentry"
    sentry-cli debug-files upload -t pdb "${ARGS[@]}" --include "${APP_BUILD}"
    sentry-cli debug-files upload -t pdb "${ARGS[@]}" --include "${LIB_BUILD}"
fi

echo "==> 符号上传完成"