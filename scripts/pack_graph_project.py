#!/usr/bin/env python3
"""把 graph.json 与其依赖打包成单个 .tgp 工程包（GraphStudio）。

格式：ZIP 容器 = manifest.json + 原名图 JSON + 资产文件。与 C++ 侧
src/project_bundle.cpp 同一契约：相对引用资产按引用原样落位；绝对路径 /
../ 越界引用若解析到已存在文件，收进包内 assets/（基名 _2/_3 去重）并把
【包内图副本】的引用重写为包内相对路径（原 json 不动；写出型任务的
file_path/out_path 永不重写）；解析不到记 manifest.missing。用途：批量分
发、WASM E2E 的单文件图输入、官网示例。

用法:
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


# 以下三个 helper 与 C++ 侧 RefBasename/UniqueAssetEntry/RewriteGraphRefs 同构
#（src/project_bundle.cpp），改动需两侧同步。被镜像的精确语义（tests/
# test_pack_graph_project.py 与 test_project_bundle.cpp 的相同期望值锚定）：
#   ref_basename       取首冒号后的残留（"C:y.png" 盘符形态），残留冒号/
#                      空名等不干净形态回退 "asset"；
#   unique_asset_entry stem/ext 按 std::filesystem 规则切分：最右点且非首
#                      字符才作扩展名分隔（前导点属文件名、尾点属扩展名），
#                      故 ".cube" 冲突去重为 ".cube_2" 而非 "_2.cube"。


def ref_basename(ref: str) -> str:
    """任意形态引用（posix 绝对 / 盘符反斜杠 / ../）→ 干净的文件基名。"""
    name = ref.replace("\\", "/").split("/")[-1]
    if ":" in name:
        name = name.split(":", 1)[1]  # 与 C++ 一致取首冒号（"C:y.png" 盘符残留）
    if not safe_entry_name(name):
        return "asset"
    return name


def _split_stem_ext(name: str) -> tuple[str, str]:
    """std::filesystem::path::stem/extension 语义（不能用 pathlib：其
    suffix 对 "foo." 返回 ''，与 fs 的 "." 不一致）。"""
    i = name.rfind(".")
    if i > 0:
        return name[:i], name[i:]
    return name, ""


def unique_asset_entry(basename: str, used: set[str]) -> str:
    """assets/ 内唯一条目名：基名冲突按 _2/_3 递增（确定性）。"""
    cand = f"assets/{basename}"
    if cand not in used:
        used.add(cand)
        return cand
    stem, ext = _split_stem_ext(basename)
    n = 2
    while True:
        cand = f"assets/{stem}_{n}{ext}"
        if cand not in used:
            used.add(cand)
            return cand
        n += 1


def rewrite_refs(data: dict, remap: dict[str, str]) -> bool:
    """把【包内副本】的图 JSON 中等于 remap 键的参数值替换为包内条目。

    写出型任务的 file_path/out_path 永不重写（重写会让运行期输出覆盖包内
    资产）。返回是否有改动。
    """
    changed = False
    for t in data.get("tasks", []):
        ttype = str(t.get("type", ""))
        is_writer = ttype.endswith("_write") or ttype == "video_writer"
        params = t.get("params")
        if not isinstance(params, dict):
            continue
        for key in list(params):
            v = params[key]
            if not isinstance(v, str) or v not in remap:
                continue
            if is_writer and key in ("file_path", "out_path"):
                continue
            params[key] = remap[v]
            changed = True
    return changed


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
    remapped: list[str] = []
    used: set[str] = {"manifest.json", entry}
    remap: dict[str, str] = {}
    for ref in collect_refs(data):
        if ref == entry:
            continue
        resolved = resolve_ref(graph, ref)
        if resolved is None:
            manifest["missing"].append(ref)
            continue
        if resolved.is_dir():
            skipped_dirs.append(ref)
            continue
        if safe_entry_name(ref):
            # 相对引用：按引用原样路径落位
            if ref in used:
                manifest["missing"].append(ref)  # 与已占条目撞车，防 zip 同名互覆
                continue
            used.add(ref)
            assets.append((ref, resolved))
            manifest["assets"].append({"path": ref, "size": resolved.stat().st_size})
            continue
        # 绝对路径 / ../ 越界引用：收进包内 assets/ 并重写包内图副本
        name = unique_asset_entry(ref_basename(ref), used)
        assets.append((name, resolved))
        manifest["assets"].append({"path": name, "size": resolved.stat().st_size,
                                   "source": ref})
        remap[ref] = name
        remapped.append(ref)
    if not manifest["missing"]:
        del manifest["missing"]

    # 存在重映射时，包内图副本改写为包内相对引用（无重映射保持原样字节）
    if remap:
        if rewrite_refs(data, remap):
            entry_bytes = json.dumps(data, indent=4).encode("utf-8")
        else:
            entry_bytes = graph.read_bytes()
    else:
        entry_bytes = graph.read_bytes()

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.writestr(entry, entry_bytes)
        for name, path in assets:
            z.write(path, name)

    print(f"packed {graph.name} -> {out} ({len(assets)} asset(s), "
          f"{out.stat().st_size} bytes)")
    if remapped:
        print(f"  bundled non-relative ref(s) into assets/: {', '.join(remapped)}")
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
        line = f"  asset  {a['path']}  {a['size']}B"
        if a.get("source"):
            line += f"  <- {a['source']}"
        print(line)
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
