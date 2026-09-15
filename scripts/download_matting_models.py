#!/usr/bin/env python3
"""download_matting_models.py — 下载/转换 matting 子模块测试模型。

产出（gitignored，submodules/matting/matting 图测试在缺失时软跳过）：
  selfie_segmenter.tflite — MediaPipe 人像分割（ImageSegmenter，~250KB）
  modnet.mnn              — MODNet 人像 matting（onnx fp16 转换，~12MB）

模型链路：
  selfie_segmenter: storage.googleapis.com 官方产物，免转换直接使用
  modnet: HuggingFace MODNet photographic portrait onnx（ZHKKKe/MODNet 权重
    的社区 onnx 导出）-> mnnconvert -f ONNX --fp16
    （输入 512×512 RGB [-1,1]，输出 [1,1,H,W] alpha [0,1]）

用法:
  python scripts/download_matting_models.py [--force]
前置: curl；python -m pip install mnn（提供 mnnconvert——pip mnn 3.x 在部分环境
      解析失败时，可用 build/mnn/mnn-src 源码构建的 MNNConvert 目标替代）
      测试图 portrait.jpg 由 scripts/download_mediapipe_models.* 提供
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root, runner  # noqa: E402

# 下载源白名单（https）
SELFIE_URL = ("https://storage.googleapis.com/mediapipe-models/image_segmenter/"
              "selfie_segmenter/float16/latest/selfie_segmenter.tflite")
MODNET_URL = ("https://huggingface.co/DavG25/modnet-pretrained-models/resolve/"
              "main/models/modnet_photographic_portrait_matting.onnx")
DOWNLOAD_HOSTS = ("github.com", "raw.githubusercontent.com",
                  "objects.githubusercontent.com", "storage.googleapis.com",
                  "huggingface.co", "cdn-lfs.huggingface.co",
                  "cdn-lfs-us-1.huggingface.co", "us.aws.cdn.hf.co")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="下载/转换 matting 测试模型")
    ap.add_argument("--force", action="store_true", help="已存在也重新下载/转换")
    args = ap.parse_args()

    import urllib.parse

    for u in (SELFIE_URL, MODNET_URL):
        parts = urllib.parse.urlsplit(u)
        if parts.scheme != "https" or parts.hostname not in DOWNLOAD_HOSTS:
            console.fail(f"下载源不在白名单内: {u}")
            return 1

    root = repo_root()
    models_dir = root / "tests" / "models" / "matting"
    deps_dir = root / "build" / "matting"
    mp_out = models_dir / "selfie_segmenter.tflite"
    mnn_out = models_dir / "modnet.mnn"

    if mp_out.is_file() and mnn_out.is_file() and not args.force:
        console.ok(f"已存在 {mp_out} 与 {mnn_out}，跳过（--force 强制重建）")
        return 0

    models_dir.mkdir(parents=True, exist_ok=True)
    deps_dir.mkdir(parents=True, exist_ok=True)

    # ---- MediaPipe selfie_segmenter（官方 tflite，直接使用） ----
    if not mp_out.is_file() or args.force:
        console.step(f"下载 {SELFIE_URL}")
        if runner.check(["curl", "-L", "--fail", "--max-time", "600",
                         "-o", str(mp_out), SELFIE_URL],
                        what="下载 selfie_segmenter.tflite") != 0:
            return 1
        console.ok(f"产出 {mp_out}")

    # ---- MODNet（onnx -> mnnconvert） ----
    if not mnn_out.is_file() or args.force:
        onnx_path = deps_dir / "modnet_photographic_portrait_matting.onnx"
        console.step(f"下载 {MODNET_URL}")
        if runner.check(["curl", "-L", "--fail", "--max-time", "1200",
                         "-o", str(onnx_path), MODNET_URL],
                        what="下载 modnet onnx") != 0:
            return 1
        mnnconvert = shutil.which("mnnconvert")
        if not mnnconvert:
            console.warn("找不到 mnnconvert（pip install mnn）；尝试源码构建的 MNNConvert")
            src = root / "build" / "mnn" / "mnn-src" / "build-macos-convert" / "MNNConvert"
            if not src.is_file():
                console.fail("也没有源码构建的 MNNConvert。请先: python -m pip install mnn")
                return 1
            mnnconvert = str(src)
        console.step("mnnconvert ONNX -> modnet.mnn (fp16)")
        # framework 名必须大写（"onnx" 会报
        # "Framework Input ERROR or Not Support This Model Type Now!"）
        if runner.check([mnnconvert, "--framework", "ONNX",
                         "--modelFile", str(onnx_path),
                         "--MNNModel", str(mnn_out), "--fp16"],
                        what="mnnconvert ONNX") != 0:
            return 1
        console.ok(f"产出 {mnn_out}")

    portrait = root / "tests" / "models" / "mediapipe" / "portrait.jpg"
    if not portrait.is_file():
        console.warn("测试图缺失：请先运行 scripts/download_mediapipe_models.sh "
                     "（portrait.jpg）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
