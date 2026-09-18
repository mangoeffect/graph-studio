"""models.py — 打包期模型文件随包步骤（三平台打包脚本 + Web 打包共用）。

图/任务参数里只填模型名，GraphStudio 运行时经 ModelFinder（ModelBootstrap：
GRAPH_STUDIO_MODELS_DIR / exe 目录 models / macOS Resources/models 布局；
WASM 为 MEMFS /models，由启动期 fetch 填充）查找。

模型集（下载脚本幂等，产物 gitignored）：
  mediapipe — tests/models/mediapipe（.task/.tflite；face/matting 的
              mediapipe 后端模型来源——mp_* 任务层已移除，但后端仍在）
  face      — tests/models/face（.mnn；face_detect 的 mnn 后端）
  matting   — tests/models/matting（.mnn；matting 的 mnn 后端。目录里的
              selfie_segmenter.tflite 是 mp 后端模型，Web 不随包——
              mediapipe 引擎在 wasm 是 stub）

stage_models() 把指定集合拷入包内 models 目录；只拷模型文件，测试图片
（jpg）留在仓库不进包。stage_web_models() 额外写 manifest.json，供
wasm 启动期按清单逐个 fetch 进 MEMFS。
"""

import json
import shutil
import subprocess
import sys
from pathlib import Path

from . import console, repo_root

# (源目录相对仓库根, 下载脚本, 随包扩展名白名单)
MODEL_SETS = {
    "mediapipe": ("tests/models/mediapipe", "download_mediapipe_models.py",
                  {".task", ".tflite"}),
    "face": ("tests/models/face", "download_face_models.py", {".mnn"}),
    "matting": ("tests/models/matting", "download_matting_models.py", {".mnn"}),
}

# 向后兼容：桌面随包长期只有 mediapipe 集
MODELS_REL = "tests/models/mediapipe"
MODEL_EXTS = {".task", ".tflite"}


def _ensure_set(root: Path, name: str) -> bool:
    """运行该集合的下载脚本（幂等，已存在即跳过）。"""
    rel, script, _exts = MODEL_SETS[name]
    rc = subprocess.call([sys.executable, str(root / "scripts" / script)])
    if rc != 0:
        console.fail(f"{name} 模型下载失败（网络？）：可重试打包，或 --skip-models 跳过随包")
        return False
    _ = rel
    return True


def _collect_set(root: Path, name: str) -> list:
    rel, _script, exts = MODEL_SETS[name]
    src_dir = root / rel
    if not src_dir.is_dir():
        console.fail(f"模型目录不存在: {src_dir}")
        return []
    return sorted(p for p in src_dir.iterdir()
                  if p.is_file() and p.suffix.lower() in exts)


def stage_models(dst_dir: Path, sets=("mediapipe",)) -> bool:
    """确保模型就位（先跑下载脚本，幂等、已存在即跳过）并拷入 dst_dir。

    返回 True 成功；失败返回 False（原因已打印，调用方直接退出非 0）。
    """
    root = repo_root()
    dst_dir = Path(dst_dir)

    files = []
    for name in sets:
        if not _ensure_set(root, name):
            return False
        got = _collect_set(root, name)
        if not got:
            console.fail(f"{name} 模型目录为空: {root / MODEL_SETS[name][0]}")
            return False
        files.extend(got)

    dst_dir.mkdir(parents=True, exist_ok=True)
    for p in files:
        shutil.copy2(p, dst_dir / p.name)
    console.step(f"随包模型文件 -> {dst_dir}（{len(files)} 个）")
    return True


def stage_web_models(dst_dir: Path, write_manifest: bool = True) -> bool:
    """Web 随包：face + matting 的 .mnn（mediapipe 在 wasm 是 stub，不随包）。

    write_manifest 时额外生成 manifest.json（[{name, size}]），wasm 启动期
    按清单 fetch 进 MEMFS /models。
    """
    root = repo_root()
    dst_dir = Path(dst_dir)

    files = []
    for name in ("face", "matting"):
        if not _ensure_set(root, name):
            return False
        got = _collect_set(root, name)
        if not got:
            console.fail(f"{name} 模型目录为空: {root / MODEL_SETS[name][0]}")
            return False
        files.extend(got)

    dst_dir.mkdir(parents=True, exist_ok=True)
    for p in files:
        shutil.copy2(p, dst_dir / p.name)
    if write_manifest:
        manifest = [{"name": p.name, "size": p.stat().st_size} for p in files]
        (dst_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    total = sum(p.stat().st_size for p in files)
    console.step(f"随包模型文件 -> {dst_dir}（{len(files)} 个，"
                 f"{total / (1024 * 1024):.1f}MB）")
    return True
