#!/usr/bin/env bash
#
# download_mediapipe_models.sh — 下载 MediaPipe 测试模型到 mediapipe_vision/tests/models/
#
# 模型文件不入库（大体积二进制走下载更干净）。
# 测试在模型缺失时 SKIP，不强制依赖。
#
# 用法:
#   scripts/download_mediapipe_models.sh                  # 下载全部
#   scripts/download_mediapipe_models.sh object_detector   # 仅下载 object detector
#   scripts/download_mediapipe_models.sh -l                # 列出可用模型
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODELS_DIR="${ROOT_DIR}/submodules/mediapipe/mediapipe_vision/tests/models"

url_for() {
    case "$1" in
        object_detector)
            echo "https://storage.googleapis.com/mediapipe-tasks/object_detector/efficientdet_lite0_uint8.tflite"
            ;;
        *)
            return 1 ;;
    esac
}

ALL_NAMES="object_detector"

LIST_ONLY=0
TARGET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -l|--list) LIST_ONLY=1; shift ;;
        -h|--help)
            sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) TARGET="$1"; shift ;;
    esac
done

if [[ "${LIST_ONLY}" -eq 1 ]]; then
    echo "可用模型:"
    for name in ${ALL_NAMES}; do echo "  - $name"; done
    exit 0
fi

mkdir -p "${MODELS_DIR}"

download() {
    local name="$1"
    local url
    url="$(url_for "$name")" || { echo "未知模型: $name" >&2; exit 1; }
    local out="${MODELS_DIR}/${name}.tflite"
    if [[ -f "${out}" ]]; then
        echo "[skip] 已存在: ${out}"
        return 0
    fi
    echo "[download] ${name} <- ${url}"
    curl -fL --retry 3 -o "${out}.tmp" "${url}"
    mv "${out}.tmp" "${out}"
    echo "[ok] ${out} ($(du -h "${out}" | cut -f1))"
}

if [[ -n "${TARGET}" ]]; then
    download "${TARGET}"
else
    for name in ${ALL_NAMES}; do
        download "${name}"
    done
fi

echo "完成。模型目录: ${MODELS_DIR}"
