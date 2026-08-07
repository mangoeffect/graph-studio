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
        face_landmarker)
            echo "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task"
            ;;
        hand_landmarker)
            echo "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/latest/hand_landmarker.task"
            ;;
        pose_landmarker)
            echo "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/latest/pose_landmarker_lite.task"
            ;;
        # Royalty-free test image (Unsplash license: free, no attribution required).
        portrait)
            echo "https://images.unsplash.com/photo-1500648767791-00dcc994a43e?w=640&q=80"
            ;;
        # Hand close-up. Attribution: "Hand Image" by Adams890, CC BY-SA 4.0,
        # via Wikimedia Commons (https://commons.wikimedia.org/wiki/File:Hand_Image.jpg).
        hand_image)
            echo "https://upload.wikimedia.org/wikipedia/commons/thumb/3/30/Hand_Image.jpg/960px-Hand_Image.jpg"
            ;;
        *)
            return 1 ;;
    esac
}

ALL_NAMES="object_detector face_landmarker hand_landmarker pose_landmarker portrait hand_image"

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
    # 保留 URL 自身的扩展名（去掉 query string 后取后缀；无扩展名的 CDN 链接默认 .jpg）
    local clean="${url%%\?*}"
    local ext="${clean##*.}"
    case "$ext" in
        jpg|jpeg|png|tflite|task) ;;  # 已知扩展名
        *) ext="jpg" ;;               # 如 images.unsplash.com/photo-<id>?...
    esac
    local out="${MODELS_DIR}/${name}.${ext}"
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
