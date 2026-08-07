#!/usr/bin/env bash
#
# gen_mediapipe_test_graphs.sh — 从 *.json.example 模板生成本地测试图 JSON。
#
# 把每个 tests/graphs/*.json.example 复制为 *.json，并把 __MODELS_DIR__ 替换为
# mediapipe_vision/tests/models 的绝对路径。生成的 *.json 不入库（见 .gitignore）。
#
# 前置：先 scripts/download_mediapipe_models.sh 下载模型与测试图片。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODELS_DIR="${ROOT_DIR}/submodules/mediapipe/mediapipe_vision/tests/models"
GRAPHS_DIR="${ROOT_DIR}/submodules/mediapipe/mediapipe_vision/tests/graphs"

if [[ ! -d "${MODELS_DIR}" ]]; then
    echo "模型目录不存在: ${MODELS_DIR}" >&2
    echo "请先运行: scripts/download_mediapipe_models.sh" >&2
    exit 1
fi

mkdir -p "${GRAPHS_DIR}"

shopt -s nullglob
templates=("${GRAPHS_DIR}"/*.json.example)
if [[ ${#templates[@]} -eq 0 ]]; then
    echo "未找到 *.json.example 模板于 ${GRAPHS_DIR}" >&2
    exit 1
fi

for ex in "${templates[@]}"; do
    out="${ex%.example}"
    sed "s|__MODELS_DIR__|${MODELS_DIR}|g" "${ex}" > "${out}"
    echo "[ok] ${out}"
done

echo "完成。测试图 JSON 目录: ${GRAPHS_DIR}"
