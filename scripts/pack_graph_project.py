#!/usr/bin/env python3
"""把 graph.json 与其相对路径依赖打包成单个 .tgp 工程包（GraphStudio）。

格式：ZIP 容器 = manifest.json + 原名图 JSON + 按图内引用路径原样落位的资产。
与 C++ 侧 app/graph_studio/src/project/GraphProject.cpp 同一契约（依赖发现
启发式、图目录 + 两级祖先探测、绝对引用不打包记 missing）。用途：批量分发、
WASM E2E 的单文件图输入、官网示例。

用法：
    python3 scripts/pack_graph_project.py <graph.json> [-o out.tgp]
    python3 scripts/pack_graph_project.py --inspect <x.tgp>
"""

import argparse
import json
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# 与 GraphProject.cpp kAssetExts / e2e_graph_cases.py 对齐（此处为最全集）
ASSET_EXTS = (
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".mp4", ".avi", ".mov",
    ".js", ".json", ".cube", ".task", ".tflite", ".mnn", ".onnx",
    ".metal", ".vert", ".frag", ".wgsl",
)
ANCESTORS = (0, 1, 2)  # 图目录 + 两级祖先（与 resolve_asset_path 一致）
FORMAT_ID = "graph-studio.project"
VERSION = 1


def collect_refs(data: dict) -> list[str]:
    refs: list[str] = []
    for t in data.get("tasks", []):
        ttype = str(t.get("type", ""))
        is_writer = ttype.endswith("_write") or ttype == "video_writer"
        for key, v in (t.get("params") or {}).items():
            if not isinstance(v, str) or not v or "://" in v:
                continue
            looks_path = v.startswith("/") or "/" in v or v.lower().endswith(ASSET_EXTS)
            if not looks_path:
                continue
            if is_writer and key in ("file_path", "out_path"):
                continue
            if v not in refs:
                refs.append(v)
    return refs


def safe_entry_name(name: str) -> bool:
    if not name or name.startswith(("/", "\\")) or "\\" in name or ":" in name:
        return False
    return all(p not in ("", ".", "..") for p in name.split("/"))


def resolve_ref(graph_path: Path, ref: str) -> Path | None:
    base = graph_path.parent
    for up in ANCESTORS:
        cand = base
        for _ in range(up):
            cand = cand.parent
        cand = cand / ref
        if cand.exists():
            return cand
    return None


def pack(graph: Path, out: Path) -> int:
    try:
        data = json.loads(graph.read_text(encoding="utf-8"))
    except (ValueError, OSError) as e:
        print(f"error: cannot parse {graph}: {e}", file=sys.stderr)
        return 1

    entry = graph.name
    manifest = {
        "format": FORMAT_ID,
        "version": VERSION,
        "entry": entry,
        "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "app": "pack_graph_project.py",
        "assets": [],
        "missing": [],
    }
    assets: list[tuple[str, Path]] = []
    skipped_dirs: list[str] = []
    for ref in collect_refs(data):
        if not safe_entry_name(ref):
            manifest["missing"].append(ref)  # 绝对路径 / .. 引用：v1 不打包
            continue
        if ref == entry:
            continue
        resolved = resolve_ref(graph, ref)
        if resolved is None:
            manifest["missing"].append(ref)
            continue
        if resolved.is_dir():
            skipped_dirs.append(ref)
            continue
        if any(p == ref for p, _ in assets):
            continue
        assets.append((ref, resolved))
        manifest["assets"].append({"path": ref, "size": resolved.stat().st_size})
    if not manifest["missing"]:
        del manifest["missing"]

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.write(graph, entry)
        for name, path in assets:
            z.write(path, name)

    print(f"packed {graph.name} -> {out} ({len(assets)} asset(s), "
          f"{out.stat().st_size} bytes)")
    if skipped_dirs:
        print(f"  skipped directory refs: {', '.join(skipped_dirs)}")
    if manifest.get("missing"):
        print(f"  MISSING (recorded in manifest): "
              f"{', '.join(manifest['missing'])}")
    return 0


def inspect(tgp: Path) -> int:
    try:
        with zipfile.ZipFile(tgp) as z:
            names = z.namelist()
            man = json.loads(z.read("manifest.json"))
    except (zipfile.BadZipFile, KeyError, ValueError) as e:
        print(f"error: not a valid project bundle: {e}", file=sys.stderr)
        return 1
    print(f"{tgp}: format={man.get('format')} version={man.get('version')} "
          f"entry={man.get('entry')} created={man.get('created')}")
    for a in man.get("assets", []):
        print(f"  asset  {a['path']}  {a['size']}B")
    for m in man.get("missing", []):
        print(f"  miss   {m}")
    extra = [n for n in names if n != "manifest.json" and n != man.get("entry")
             and not any(a["path"] == n for a in man.get("assets", []))]
    for n in extra:
        print(f"  extra  {n}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("graph", type=Path, help="graph.json 路径（或 --inspect 的 .tgp）")
    ap.add_argument("-o", "--out", type=Path, default=None,
                    help="输出 .tgp 路径（默认：图同目录 <name>.tgp）")
    ap.add_argument("--inspect", action="store_true", help="检视 .tgp 包内容")
    args = ap.parse_args()

    if args.inspect:
        return inspect(args.graph)
    out = args.out or args.graph.with_suffix(".tgp")
    return pack(args.graph, out)


if __name__ == "__main__":
    sys.exit(main())
