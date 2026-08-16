"""scenarios_files.py — 文件打开 / 拖拽打开（直接消费子模块 tests/graphs/*.json）。

发现规则:
  - 枚举 submodules/**/tests/graphs/*.json（version 2.0 夹具，约 40+）；
  - 解析 tasks[].params 里的相对路径引用（file_path/script_path/lut_file/model…），
    全部存在于图目录 → 纳入；有缺失（如 video_io 的 data/synth.mp4 由测试驱动
    生成）→ 记 skip 并注明缺失项；
  - 图 + 被引用文件复制到运行目录再打开——image_writer/video_writer 类图的
    输出写到副本里，不污染仓库 submodule 树。

执行分类:
  - 默认要求 Run 后 0 failed；
  - mediapipe 图（安装包为 stub 构建，模型链路必然失败）与 GPU 未初始化时的
    gpu 图 → allow-fail：执行完成即通过，附注实际 ok/failed。

拖拽子集: read_image / read_image_unicode（中文资产名）/ filter_cascade，
经 WM_DROPFILES 走与 Explorer 拖放相同的 dropEvent 路径；unicode 图额外执行，
验证相对路径资产解析。
"""

from __future__ import annotations

import json
import shutil

from gs import console, repo_root
from .app import AppSession, wait_until

SKIP_GRAPHS = {"js_error.json"}            # 故意失败的反例夹具，不进正向流
PRIORITY_GRAPHS = ["read_image.json", "read_image_unicode.json"]


def _resolve_ref(graph_path, ref: str) -> bool:
    """相对路径引用是否存在。

    夹具布局：图在 <tests>/graphs/*.json，资产在 <tests>/{data,models,scripts}/…，
    测试驱动把两者拷到同一目录再跑（框架按图自身目录解析相对路径）。
    所以引用要按 graphs/ 的若干级祖先探测。
    """
    for base in (graph_path.parent, graph_path.parent.parent,
                 graph_path.parent.parent.parent):
        if (base / ref).is_file():
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
                        "tasks": [], "edges": [], "refs": [], "missing": [f"解析失败: {e}"]})
            continue
        refs, missing = [], []
        for t in data.get("tasks", []):
            ttype = str(t.get("type", ""))
            is_writer = ttype.endswith("_write") or ttype == "video_writer"
            for key, v in (t.get("params") or {}).items():
                if not isinstance(v, str) or not v:
                    continue
                if ":" in v[:2] or v.startswith(("/", "\\")):
                    continue          # 绝对路径 / 盘符：不属于随图资产
                looks_like_path = ("/" in v or "\\" in v or v.endswith(
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


def _find_ref_src(graph_path, ref: str) -> Path | None:
    """引用文件的源路径（多级祖先探测，与 _resolve_ref 同序）。"""
    for base in (graph_path.parent, graph_path.parent.parent,
                 graph_path.parent.parent.parent):
        if (base / ref).is_file():
            return base / ref
    return None


def _stage_copy(entry: dict, report) -> Path:
    """把图与被引用文件复制到运行目录（扁平布局：图与 data/models 同级，
    框架按图自身目录解析相对路径），返回副本 json 路径。输出写到副本，不污染仓库。"""
    dest_dir = report.path("submodule_graphs") / entry["module"]
    dest_dir.mkdir(parents=True, exist_ok=True)
    for ref in entry["refs"]:
        target = dest_dir / ref
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            src = _find_ref_src(entry["path"], ref)
            if src:
                shutil.copy2(src, target)
    dest_json = dest_dir / entry["name"]
    shutil.copy2(entry["path"], dest_json)
    return dest_json


def run(pkg, report, fixtures, ctx) -> None:
    graphs = discover_graphs()
    max_graphs = int(ctx.get("max_graphs") or 10)
    graphs_filter = (ctx.get("graphs_filter") or "").strip().lower()

    # 资产缺失的图统一记 skip（含 video_io 等由测试驱动生成资产的模块）
    runnable = []
    for e in graphs:
        full = f"files/open:{e['module']}/{e['name']}"
        if e["missing"]:
            report.record(full, "skip", f"夹具资产缺失: {', '.join(e['missing'][:3])}")
        else:
            runnable.append(e)

    if graphs_filter:
        runnable = [e for e in runnable if graphs_filter in str(e["path"]).lower()]
    else:
        # 代表性小图优先，再跨模块轮转补足到 max_graphs（避免单一模块占满）
        by_name = {e["name"]: e for e in runnable}
        selected = [by_name[n] for n in PRIORITY_GRAPHS if n in by_name]
        rest = [e for e in runnable if e["name"] not in PRIORITY_GRAPHS]
        by_mod: dict[str, list] = {}
        for e in sorted(rest, key=lambda e: e["name"]):
            by_mod.setdefault(e["module"], []).append(e)
        while len(selected) < max_graphs and any(by_mod.values()):
            for m in sorted(by_mod):
                if len(selected) >= max_graphs:
                    break
                if by_mod[m]:
                    selected.append(by_mod[m].pop(0))
        runnable = selected

    s = AppSession(pkg, report)
    s.launch_and_attach()
    try:
        gpu_ok = "initialized" in s.log_text() and "GPU backend:" in s.log_text()

        for e in runnable:
            full = f"files/open:{e['module']}/{e['name']}"
            report.begin(full)
            artifacts: list[str] = []
            try:
                graph_copy = _stage_copy(e, report)
                allow_fail = ("mediapipe" in str(e["path"]).lower()
                              or ("gpu" in e["module"].lower() and not gpu_ok))

                s.new_graph()
                s.menu_open_file(graph_copy)
                wait_until(lambda: e["name"] in s.win.window_text(),
                           desc=f"标题含 {e['name']}")
                counts = wait_until(
                    lambda: (lambda c: c == (len(e["tasks"]), len(e["edges"]))
                             and c)(s.status_counts()),
                    desc=f"Nodes/Edges == {len(e['tasks'])}/{len(e['edges'])}")

                ok, failed, _log = s.run_and_wait(timeout=90)
                if allow_fail:
                    report.record(full, "pass",
                                  f"ok={ok} failed={failed}（allow-fail：stub/无GPU 预期）")
                    continue
                if failed != 0:
                    raise AssertionError(f"执行有 {failed} 个任务失败（ok={ok}）")

                # 图像管线图：结果下拉应有条目（mp 图 allow-fail 已 continue）
                if any(str(t.get("type", "")).startswith("opencv_image_read")
                       for t in e["tasks"]):
                    wait_until(lambda: (s.result_combo_texts() or None),
                               timeout=20, interval=0.5, desc="结果下拉条目")
                report.record(full, "pass", f"ok={ok} failed={failed}",
                              artifacts=artifacts)
            except Exception as ex:
                try:
                    artifacts.append(s.screenshot(f"{e['name']}_failure.png"))
                    s.dump_ui(f"{e['name']}_failure_ui.txt")
                    state = s.state_snapshot()
                except Exception:
                    state = {}
                report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                              artifacts=artifacts, exc=ex, app_state=state)
                try:
                    s.new_graph()
                except Exception:
                    pass

        # ---- 拖拽子集（WM_DROPFILES → MainWindow::dropEvent 同路径）----
        by_name = {e["name"]: e for e in runnable}
        drop_names = [n for n in PRIORITY_GRAPHS + ["filter_cascade.json"]
                      if n in by_name]
        for name in drop_names:
            e = by_name[name]
            full = f"files/drop:{e['module']}/{e['name']}"
            report.begin(full)
            try:
                graph_copy = _stage_copy(e, report)
                s.new_graph()
                # 真实用户拖放：Explorer 为源的真 OLE 拖拽（Qt 忽略 WM_DROPFILES）
                s.drop_file_via_explorer(graph_copy)
                wait_until(lambda: name in s.win.window_text(), timeout=15,
                           desc=f"拖拽后标题含 {name}")
                wait_until(lambda: s.status_counts() == (len(e["tasks"]), len(e["edges"])),
                           desc="拖拽后节点/边计数")
                detail = "标题 + 计数"
                if "unicode" in name:
                    # 中文资产名 + 相对路径解析：执行验证
                    ok, failed, _log = s.run_and_wait()
                    if failed != 0:
                        raise AssertionError(f"unicode 图执行 {failed} 失败（ok={ok}）")
                    detail = f"标题 + 计数 + 执行 ok={ok}（中文路径资产解析）"
                report.record(full, "pass", detail)
            except Exception as ex:
                try:
                    s.screenshot(f"drop_{name}_failure.png")
                    state = s.state_snapshot()
                except Exception:
                    state = {}
                report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                              exc=ex, app_state=state)

        s.screenshot("files_final.png")
    finally:
        s.close()
