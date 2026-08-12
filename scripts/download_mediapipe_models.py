#!/usr/bin/env python3
"""download_mediapipe_models.py — 下载 MediaPipe 测试模型到
submodules/mediapipe/mediapipe_vision/tests/models/（跨平台）。

取代 scripts/download_mediapipe_models.sh。模型文件不入库（大体积二进制走下载更干净）。
测试在模型缺失时 SKIP，不强制依赖。用标准库 urllib 替代 curl，三平台通用。

用法:
  python scripts/download_mediapipe_models.py                   # 下载全部
  python scripts/download_mediapipe_models.py object_detector   # 仅下载 object detector
  python scripts/download_mediapipe_models.py -l                # 列出可用模型
"""

import argparse
import os
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402

MODELS_REL = "submodules/mediapipe/mediapipe_vision/tests/models"

URLS = {
    "object_detector": "https://storage.googleapis.com/mediapipe-tasks/object_detector/efficientdet_lite0_uint8.tflite",
    "face_detector": "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite",
    "face_landmarker": "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task",
    "hand_landmarker": "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/latest/hand_landmarker.task",
    "pose_landmarker": "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/latest/pose_landmarker_lite.task",
    "gesture_recognizer": "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/gesture_recognizer/float16/latest/gesture_recognizer.task",
    "holistic_landmarker": "https://storage.googleapis.com/mediapipe-models/holistic_landmarker/holistic_landmarker/float16/latest/holistic_landmarker.task",
    "image_classifier": "https://storage.googleapis.com/mediapipe-models/image_classifier/efficientnet_lite0/float32/latest/efficientnet_lite0.tflite",
    "image_embedder": "https://storage.googleapis.com/mediapipe-models/image_embedder/mobilenet_v3_small/float32/latest/mobilenet_v3_small.tflite",
    "image_segmenter": "https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/latest/selfie_segmenter.tflite",
    # Royalty-free test image (Unsplash license: free, no attribution required).
    "portrait": "https://images.unsplash.com/photo-1500648767791-00dcc994a43e?w=640&q=80",
    # Hand close-up. Attribution: "Hand Image" by Adams890, CC BY-SA 4.0,
    # via Wikimedia Commons (https://commons.wikimedia.org/wiki/File:Hand_Image.jpg).
    "hand_image": "https://upload.wikimedia.org/wikipedia/commons/thumb/3/30/Hand_Image.jpg/960px-Hand_Image.jpg",
}

ALL_NAMES = list(URLS.keys())
KNOWN_EXT = {"jpg", "jpeg", "png", "tflite", "task"}


def derive_ext(url: str) -> str:
    """保留 URL 自身的扩展名（去掉 query string 后取后缀；无已知扩展名默认 .jpg）。"""
    clean = url.split("?", 1)[0]
    base = clean.rsplit("/", 1)[-1]
    if "." in base:
        ext = base.rsplit(".", 1)[-1].lower()
        if ext in KNOWN_EXT:
            return ext
    return "jpg"


def human_size(n: int) -> str:
    for unit in ("B", "K", "M", "G"):
        if n < 1024:
            return f"{n}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n //= 1024
    return f"{n}G"


def download_one(name: str, models_dir: Path, retries: int = 3) -> int:
    url = URLS[name]
    out = models_dir / f"{name}.{derive_ext(url)}"
    if out.is_file():
        print(f"[skip] 已存在: {out}")
        return 0
    print(f"[download] {name} <- {url}")
    tmp = out.with_suffix(out.suffix + ".tmp")
    last_err = None
    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "graph-studio-fetch/1.0"})
            with urllib.request.urlopen(req, timeout=60) as resp, open(tmp, "wb") as f:
                while True:
                    chunk = resp.read(64 * 1024)
                    if not chunk:
                        break
                    f.write(chunk)
            os.replace(tmp, out)
            print(f"[ok] {out} ({human_size(out.stat().st_size)})")
            return 0
        except Exception as e:
            last_err = e
            if attempt < retries:
                print(f"  重试 {attempt + 1}/{retries} ...", flush=True)
                time.sleep(1)
            else:
                if tmp.exists():
                    tmp.unlink(missing_ok=True)
    console.fail(f"下载 {name} 失败: {last_err}")
    return 1


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="下载 MediaPipe 测试模型")
    ap.add_argument("model", nargs="?", default="", help="仅下载该模型（不指定则下载全部）")
    ap.add_argument("-l", "--list", action="store_true", help="列出可用模型后退出")
    args = ap.parse_args()

    models_dir = repo_root() / MODELS_REL

    if args.list:
        print("可用模型:")
        for name in ALL_NAMES:
            print(f"  - {name}")
        return 0

    models_dir.mkdir(parents=True, exist_ok=True)

    if args.model:
        if args.model not in URLS:
            console.fail(f"未知模型: {args.model}")
            return 1
        return download_one(args.model, models_dir)

    status = 0
    for name in ALL_NAMES:
        if download_one(name, models_dir) != 0:
            status = 1
    print(f"完成。模型目录: {models_dir}")
    return status


if __name__ == "__main__":
    sys.exit(main())