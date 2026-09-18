#!/usr/bin/env python3
"""download_face_models.py — 下载/转换 face 子模块测试模型。

产出（gitignored，submodules/face/face_detect 图测试在缺失时软跳过）：
  ultraface_slim_320.mnn — UltraFace slim-320 检测模型（官方 MNN 产物，~1.1MB）
  face_landmark.mnn      — MediaPipe face_landmark.tflite（478 点）转换产物

模型链路：
  UltraFace: Linzaer 仓库官方 MNN 模型（MNN/model/version-slim/slim-320.mnn，
    免转换；模型内 scores 已融合 softmax，class1=face）
  face_landmark: storage.googleapis.com 官方 face_landmarker.task（zip）
    -> 解包取 face_landmark.tflite（fp16 源）
    -> mnnconvert --framework TFLITE（framework 名必须大写）

用法:
  python scripts/download_face_models.py [--force]
前置: curl；python -m pip install mnn（提供 mnnconvert——pip mnn 3.x 在部分环境
      解析失败时，可用 build/mnn/mnn-src 源码构建的 MNNConvert 目标替代）
      测试图 portrait.jpg 由 scripts/download_mediapipe_models.* 提供
"""

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root, runner  # noqa: E402

# 下载源白名单（https）
ULTRAFACE_URL = ("https://raw.githubusercontent.com/Linzaer/Ultra-Light-Fast-"
                 "Generic-Face-Detector-1MB/master/MNN/model/version-slim/"
                 "slim-320.mnn")
TASK_URL = ("https://storage.googleapis.com/mediapipe-models/face_landmarker/"
            "face_landmarker/float16/1/face_landmarker.task")
DOWNLOAD_HOSTS = ("github.com", "raw.githubusercontent.com",
                  "objects.githubusercontent.com", "storage.googleapis.com")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="下载/转换 face 测试模型")
    ap.add_argument("--force", action="store_true", help="已存在也重新下载/转换")
    args = ap.parse_args()

    import urllib.parse

    for u in (ULTRAFACE_URL, TASK_URL):
        parts = urllib.parse.urlsplit(u)
        if parts.scheme != "https" or parts.hostname not in DOWNLOAD_HOSTS:
            console.fail(f"下载源不在白名单内: {u}")
            return 1

    root = repo_root()
    models_dir = root / "tests" / "models" / "face"
    deps_dir = root / "build" / "face"
    det_out = models_dir / "ultraface_slim_320.mnn"
    lmk_out = models_dir / "face_landmark.mnn"

    if det_out.is_file() and lmk_out.is_file() and not args.force:
        console.ok(f"已存在 {det_out} 与 {lmk_out}，跳过（--force 强制重建）")
        return 0

    models_dir.mkdir(parents=True, exist_ok=True)
    deps_dir.mkdir(parents=True, exist_ok=True)

    # ---- UltraFace 检测（官方 MNN 产物，直接下载） ----
    if not det_out.is_file() or args.force:
        console.step(f"下载 {ULTRAFACE_URL}")
        if runner.check(["curl", "-L", "--fail", "--max-time", "600",
                         "-o", str(det_out), ULTRAFACE_URL],
                        what="下载 slim-320.mnn") != 0:
            return 1
        console.ok(f"产出 {det_out}")

    # ---- MediaPipe face_landmark（自 .task 解包 → mnnconvert） ----
    if not lmk_out.is_file() or args.force:
        mnnconvert = shutil.which("mnnconvert")
        if not mnnconvert:
            console.warn("找不到 mnnconvert（pip install mnn）；尝试源码构建的 MNNConvert")
            src = root / "build" / "mnn" / "mnn-src" / "build-macos-convert" / "MNNConvert"
            if not src.is_file():
                console.fail("也没有源码构建的 MNNConvert。请先: python -m pip install mnn")
                return 1
            mnnconvert = str(src)
        task_path = deps_dir / "face_landmarker.task"
        console.step(f"下载 {TASK_URL}")
        if runner.check(["curl", "-L", "--fail", "--max-time", "600",
                         "-o", str(task_path), TASK_URL],
                        what="下载 face_landmarker.task") != 0:
            return 1
        tflite_path = deps_dir / "face_landmark.tflite"
        with zipfile.ZipFile(task_path) as zf:
            names = [n for n in zf.namelist() if n.endswith(".tflite")]
            cand = [n for n in names
                    if "landmark" in n and "detection" not in n] or names
            if not cand:
                console.fail("face_landmarker.task 中未找到 tflite")
                return 1
            with zf.open(cand[0]) as src_f, open(tflite_path, "wb") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        console.step("mnnconvert TFLITE -> face_landmark.mnn")
        # 注意 framework 名必须大写（"tflite" 会报
        # "Framework Input ERROR or Not Support This Model Type Now!"）
        rc = runner.run([mnnconvert, "--framework", "TFLITE",
                         "--modelFile", str(tflite_path),
                         "--MNNModel", str(lmk_out)])
        if rc != 0:
            # pip mnn 的 mnnconvert（Windows）在转换完成后的进程退出期偶发
            # access violation（DLL 卸载崩溃，exit 0xC0000005）——产物已生成。
            # 产物存在且非空即放行（幂等：下次运行产物存在整段跳过）。
            if lmk_out.is_file() and lmk_out.stat().st_size > 0:
                console.warn(f"mnnconvert 退出码 {rc} 但产物已生成，按成功处理"
                             "（Windows 退出期 DLL 卸载崩溃）")
            else:
                console.fail("mnnconvert TFLITE 失败")
                return 1
        console.ok(f"产出 {lmk_out}")

    portrait = root / "tests" / "models" / "mediapipe" / "portrait.jpg"
    if not portrait.is_file():
        console.warn("测试图缺失：请先运行 scripts/download_mediapipe_models.sh "
                     "（portrait.jpg）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
