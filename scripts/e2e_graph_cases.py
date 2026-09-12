# -*- coding: utf-8 -*-
"""e2e_graph_cases.py — 子模块单测图夹具的发现与落位（对齐 e2e_windows/scenarios_files.py）。

（macOS/WASM 两侧 E2E 共用——原先在 e2e_macos/graph_cases.py，WASM E2E
对齐 mac 方案后提升到 scripts/ 层。）

发现规则:
  - 枚举 submodules/**/tests/graphs/*.json（version 2.0 夹具，当前 69 张/10 模块）；
  - 解析 tasks[].params 里的相对路径引用（file_path/script_path/lut_file/model…），
    全部存在于图目录的若干级祖先 → 纳入；有缺失（如 video_io 的 data/synth.mp4
    由测试驱动生成）→ 由调用方记 skip 并注明缺失项；
  - 图 + 被引用文件复制到运行目录再打开——image_writer/video_writer 类图的
    输出写到副本里，不污染仓库 submodule 树。

与 Windows 版的差异: gpu 模块的图在 submodules/gpu/image_processing/ 下（module
叶子名是 image_processing），GPU 分类判断按路径前缀而非 module 名。
"""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from gs import repo_root

SKIP_GRAPHS = {"js_error.json"}            # 故意失败的反例夹具，不进正向流
PRIORITY_GRAPHS = ["read_image.json", "read_image_unicode.json"]

# 相对路径引用的探测祖先层级：夹具布局是 <tests>/graphs/*.json + 资产在
# <tests>/{data,models,scripts}/…（框架按图自身目录解析相对路径）。
_ANCESTORS = (0, 1, 2)   # graphs/ 自身、tests/、subnode 根


def _resolve_ref(graph_path: Path, ref: str) -> bool:
    """相对路径引用是否存在（按 graphs/ 的若干级祖先探测）。

    目录也算存在（render_pipeline 的 effects_path 是 shaders/ 这样的
    目录引用，is_file() 会把它误判成缺失）。
    """
    for up in _ANCESTORS:
        base = graph_path.parent
        for _ in range(up):
            base = base.parent
        if (base / ref).exists():
            return True
    return False


def discover_graphs() -> list[dict]:
    """枚举子模块图夹具，返回 [{path, module, name, tasks, edges, refs, missing}]。"""
    out = []
    for g in sorted((repo_root() / "submodules").rglob("tests/graphs/*.json")):
        if g.name in SKIP_GRAPHS:
            continue
        try:
            data = json.loads(g.read_text(encoding="utf-8"))
        except (ValueError, OSError) as e:
            out.append({"path": g, "module": g.parents[2].name, "name": g.name,
                        "tasks": [], "edges": [], "refs": [],
                        "missing": [f"解析失败: {e}"]})
            continue
        refs, missing = [], []
        for t in data.get("tasks", []):
            ttype = str(t.get("type", ""))
            is_writer = ttype.endswith("_write") or ttype == "video_writer"
            for key, v in (t.get("params") or {}).items():
                if not isinstance(v, str) or not v:
                    continue
                if ":" in v[:2] or v.startswith("/"):
                    continue          # 绝对路径：不属于随图资产
                looks_like_path = ("/" in v or v.endswith(
                    (".png", ".jpg", ".jpeg", ".json", ".js", ".mp4",
                     ".task", ".tflite", ".cube", ".webp")))
                if not looks_like_path:
                    continue
                if is_writer and key in ("file_path", "out_path"):
                    continue          # 写出型任务的路径参数是输出，无需预存在
                if _resolve_ref(g, v):
                    refs.append(v)
                else:
                    missing.append(v)
        out.append({"path": g, "module": g.parents[2].name, "name": g.name,
                    "tasks": data.get("tasks", []), "edges": data.get("edges", []),
                    "refs": refs, "missing": sorted(set(missing))})
    return out


def is_gpu_graph(entry: dict) -> bool:
    """gpu 子模块的图（module 叶子名是 image_processing，按路径判断）。"""
    return "submodules/gpu/" in str(entry["path"]).replace("\\", "/")


def _find_ref_src(graph_path: Path, ref: str) -> Path | None:
    """引用文件的源路径（多级祖先探测，与 _resolve_ref 同序）。"""
    for up in _ANCESTORS:
        base = graph_path.parent
        for _ in range(up):
            base = base.parent
        if (base / ref).exists():
            return base / ref
    return None


def stage_copy(entry: dict, run_dir: Path) -> Path:
    """把图与被引用文件复制到运行目录（扁平布局：图与 data/models 同级，
    框架按图自身目录解析相对路径），返回副本 json 路径。

    目录引用整目录 copytree（shaders/）；`.effect.json` 清单虽然本身是
    文件，但 render 层按 stem 约定加载同目录的 .metal/.vert/.frag/.wgsl
    源——清单所在目录整体随迁。
    """
    dest_dir = run_dir / "submodule_graphs" / entry["module"]
    dest_dir.mkdir(parents=True, exist_ok=True)
    for ref in entry["refs"]:
        src = _find_ref_src(entry["path"], ref)
        if src is None:
            continue
        if src.is_dir():
            shutil.copytree(src, dest_dir / ref, dirs_exist_ok=True)
            continue
        if src.name.endswith(".effect.json"):
            shutil.copytree(src.parent, dest_dir / Path(ref).parent,
                            dirs_exist_ok=True)
            continue
        target = dest_dir / ref
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            shutil.copy2(src, target)
    dest_json = dest_dir / entry["name"]
    shutil.copy2(entry["path"], dest_json)
    return dest_json
