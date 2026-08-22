"""models.py — 打包期 MediaPipe 模型文件随包步骤（三平台打包脚本共用）。

图/任务参数里只填模型名，GraphStudio 运行时经 ModelFinder（ModelBootstrap：
GRAPH_STUDIO_MODELS_DIR / exe 目录 models / macOS Resources/models 布局）查找。
打包脚本用 stage_models() 把模型放进包内 models 目录；只拷 .task/.tflite
模型文件，测试图片（jpg）留在仓库不进包。
"""

import shutil
import subprocess
import sys
from pathlib import Path

from . import console, repo_root

MODELS_REL = "submodules/mediapipe/mediapipe_vision/tests/models"
MODEL_EXTS = {".task", ".tflite"}


def stage_models(dst_dir: Path) -> bool:
    """确保模型就位（先跑下载脚本，幂等、已存在即跳过）并拷入 dst_dir。

    返回 True 成功；失败返回 False（原因已打印，调用方直接退出非 0）。
    """
    root = repo_root()
    src_dir = root / MODELS_REL
    dst_dir = Path(dst_dir)

    rc = subprocess.call([sys.executable, str(root / "scripts" / "download_mediapipe_models.py")])
    if rc != 0:
        console.fail("模型下载失败（网络？）：可重试打包，或 --skip-models 跳过随包")
        return False

    files = (sorted(p for p in src_dir.iterdir()
                    if p.is_file() and p.suffix.lower() in MODEL_EXTS)
             if src_dir.is_dir() else [])
    if not files:
        console.fail(f"模型目录为空: {src_dir}")
        return False

    dst_dir.mkdir(parents=True, exist_ok=True)
    for p in files:
        shutil.copy2(p, dst_dir / p.name)
    console.step(f"随包模型文件 -> {dst_dir}（{len(files)} 个）")
    return True
