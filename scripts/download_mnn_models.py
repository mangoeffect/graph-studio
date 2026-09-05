#!/usr/bin/env python3
"""download_mnn_models.py — 下载/转换 MNN 测试模型与 ImageNet 标签。

产出（gitignored，tests/test_mnn_inference 在缺失时软跳过）：
  mobilenet_v2.mnn  — MobileNetV2-12（fp16，~7MB）
  labels.txt        — ImageNet 1000 类（ONNX zoo synset.txt）

模型链路：ONNX 官方 zoo（github.com/onnx/models，public LFS）
  -> 下载 mobilenetv2-12.onnx（约 14MB）
  -> mnnconvert（pip install mnn 自带）转 .mnn
MNN 仓库自带的 benchmark 模型是权重剥离的跑不动（runSession 报
"has no weight or bias"），所以走转换链路。

用法:
  python scripts/download_mnn_models.py [--force]
前置: curl（macOS / Win10+ / CI runner 均自带）；
      python -m pip install mnn（提供 mnnconvert，与 build_mnn.py 的 MNN
      版本需兼容即可，模型格式跨版本稳定）
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root, runner  # noqa: E402

# 下载源为固定的官方 ONNX zoo 地址（public LFS，https）
LABELS_URL = "https://github.com/onnx/models/raw/main/validated/vision/classification/synset.txt"
ONNX_URL = ("https://github.com/onnx/models/raw/main/validated/vision/"
            "classification/mobilenet/model/mobilenetv2-12.onnx")
DOWNLOAD_HOSTS = ("github.com",)  # 下载主机白名单


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="下载/转换 MNN 测试模型与标签")
    ap.add_argument("--force", action="store_true", help="已存在也重新下载/转换")
    args = ap.parse_args()

    import urllib.parse

    for u in (LABELS_URL, ONNX_URL):
        parts = urllib.parse.urlsplit(u)
        if parts.scheme != "https" or parts.hostname not in DOWNLOAD_HOSTS:
            console.fail(f"下载源不在白名单内: {u}")
            return 1

    root = repo_root()
    models_dir = root / "tests" / "models" / "mnn"
    deps_dir = root / "build" / "mnn"
    labels_out = models_dir / "labels.txt"
    model_out = models_dir / "mobilenet_v2.mnn"
    onnx_path = deps_dir / "mobilenetv2-12.onnx"

    if model_out.is_file() and labels_out.is_file() and not args.force:
        console.ok(f"已存在 {model_out} 与 labels.txt，跳过（--force 强制重建）")
        return 0

    labels_ok = labels_out.is_file()
    if not labels_ok or args.force:
        console.step(f"下载 {LABELS_URL}")
        models_dir.mkdir(parents=True, exist_ok=True)
        if runner.check(["curl", "-L", "--fail", "--max-time", "600",
                         "-o", str(labels_out), LABELS_URL],
                        what="下载 labels.txt") != 0:
            return 1
        labels_ok = labels_out.is_file()

    if model_out.is_file() and not args.force:
        console.ok(f"已存在 {model_out}，跳过转换")
        return 0 if labels_ok else 1

    mnnconvert = shutil.which("mnnconvert")
    if not mnnconvert:
        console.fail("找不到 mnnconvert。请先: python -m pip install mnn")
        return 1

    console.step(f"下载 {ONNX_URL}")
    deps_dir.mkdir(parents=True, exist_ok=True)
    if runner.check(["curl", "-L", "--fail", "--max-time", "600",
                     "-o", str(onnx_path), ONNX_URL],
                    what="下载 mobilenetv2-12.onnx") != 0:
        return 1

    console.step(f"转换 ONNX -> .mnn ({mnnconvert})")
    code = runner.check(
        [mnnconvert, "-f", "ONNX", "--modelFile", str(onnx_path),
         "--MNNModel", str(model_out), "--fp16"],
        what="mnnconvert 转换",
    )
    if code != 0 or not model_out.is_file():
        console.fail(f"mnnconvert 失败 (exit {code})")
        return 1
    print(f"    {model_out} ({model_out.stat().st_size} bytes)")

    console.ok("MNN 测试模型就绪")
    return 0 if labels_ok else 1


if __name__ == "__main__":
    sys.exit(main())
