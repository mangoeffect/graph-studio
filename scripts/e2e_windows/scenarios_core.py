"""scenarios_core.py — 核心编辑+执行流（数据驱动，用例见 plugin_cases.py）。

统一流程（每个 PluginCase）：
  File>New → 逐节点右键菜单创建（画布横向布局）→ 选中节点按标签设参数 →
  端口拖拽连线 → Run → 断言（0 failed / 结果下拉 / 磁盘产物 / 日志子串）。

每个用例独立记录（core/<name>），单例失败不阻断后续用例；
最后附一次 Save As 落盘 JSON 往返校验（core/save_roundtrip）。
"""

from __future__ import annotations

import json

from gs import console
from .app import AppSession, wait_until
from .plugin_cases import CASES, JS_ARITH

NODE_SPACING = 220   # 画布横向布局间距（节点宽 140 + 间隙）


def _node_positions(center: tuple[int, int], n: int) -> list[tuple[int, int]]:
    """n 个节点围绕画布中心横向等距排布。"""
    return [(center[0] + int((i - (n - 1) / 2) * NODE_SPACING), center[1])
            for i in range(n)]


def _substitute(value: str, subs: dict[str, str]) -> str:
    """占位符替换：整串相等或 $KEY/ 前缀（如 $OUT/write_out.png）。"""
    for key, real in subs.items():
        if value == key:
            return str(real)
        if value.startswith(key + "/") or value.startswith(key + "\\"):
            return str(real).replace("\\", "/") + "/" + value[len(key) + 1:].replace("\\", "/")
    return value


def run(pkg, report, fixtures, ctx) -> None:
    # 内嵌 JS 夹具与输出目录
    js_path = report.path("core_js/engine_arith.js")
    js_path.parent.mkdir(parents=True, exist_ok=True)
    js_path.write_text(JS_ARITH, encoding="utf-8")
    out_dir = report.path("core_out")
    out_dir.mkdir(parents=True, exist_ok=True)
    subs = {"$ASSET": fixtures.asset_abs, "$JS": js_path, "$OUT": out_dir}

    s = AppSession(pkg, report)
    s.launch_and_attach()
    try:
        # 0) 任务库 = 安装态插件加载证据（一次性记录）
        report.begin("core/plugin_library")
        texts = wait_until(
            lambda: (t := s.task_library_texts()) and ("Input" in t or "OpenCV Filter" in t) and t,
            timeout=30, desc="任务库插件分类出现")
        gpu_log = s.log_text()
        gpu_ok = "GPU backend:" in gpu_log and "initialized" in gpu_log
        report.record("core/plugin_library", "pass",
                      f"{len(texts)} 个分类；GPU backend "
                      f"{'已初始化' if gpu_ok else '未初始化'}")

        for case in CASES:
            full = f"core/{case.name}"
            report.begin(full)
            if not case.enabled:
                report.record(full, "skip", "注册表中标记 disabled")
                continue
            if case.requires_gpu and not gpu_ok:
                report.record(full, "skip", "GPU 后端未初始化（CI/无 GPU 机器预期行为）")
                continue

            artifacts: list[str] = []
            try:
                s.new_graph()
                pos = _node_positions(s.canvas_center(), len(case.nodes))

                # 建节点
                for spec, pt in zip(case.nodes, pos):
                    s.add_node_at(spec["type"], pt, category=spec["category"])
                wait_until(lambda: s.status_counts() == (len(case.nodes), 0),
                           desc=f"{case.name}: 节点数 {len(case.nodes)}")

                # 设参数（选中节点 → 校验选中成功 → 逐标签输入 → 点画布去焦点）
                for i, spec in enumerate(case.nodes):
                    if not spec.get("params"):
                        continue
                    for attempt in range(3):   # 选中点击可能被光标干扰抢走
                        s.select_node(pos[i])
                        try:
                            wait_until(lambda: s.selected_node_id()
                                       .startswith(spec["type"] + "_"),
                                       timeout=3, desc=f"选中 {spec['type']}")
                            break
                        except Exception:
                            if attempt == 2:
                                raise
                    for label, value in spec["params"].items():
                        s.set_param_value(label, _substitute(str(value), subs))
                    s.defocus_to_canvas()

                # 连线
                for a, b in case.edges:
                    s.connect_ports(pos[a], pos[b])
                wait_until(lambda: s.status_counts() == (len(case.nodes), len(case.edges)),
                           desc=f"{case.name}: 连线数 {len(case.edges)}")

                # 执行
                ok, failed, _log = s.run_and_wait()
                if failed != 0:
                    raise AssertionError(f"执行有 {failed} 个任务失败（ok={ok}）")

                # 断言：结果下拉
                if case.expect_results is not None:
                    items = wait_until(lambda: (s.result_combo_texts() or None),
                                       timeout=30, interval=0.5, desc="结果下拉条目")
                    for sub in case.expect_results:
                        if not any(sub in it for it in items):
                            raise AssertionError(f"结果下拉缺少 {sub!r}: {items}")

                # 断言：磁盘产物
                for rel in case.expect_files:
                    p = report.path(_substitute(rel, subs) if rel.startswith("$OUT")
                                    else rel)
                    if not p.is_file():
                        raise AssertionError(f"执行后未见输出文件: {p}")
                    artifacts.append(str(p))

                # 断言：日志子串（执行后停在 Profile 页，先切回 Log 可读）
                s.ensure_log_visible()
                log = s.log_text()
                combined = log + s.output_text()
                for sub in case.expect_log:
                    if sub not in combined:
                        raise AssertionError(f"日志缺少 {sub!r}（尾部: "
                                         f"{log.splitlines()[-5:] if log else '空'}）")

                report.record(full, "pass", f"ok={ok} failed={failed}",
                              artifacts=artifacts)
            except Exception as e:
                try:
                    artifacts.append(s.screenshot(f"{case.name}_failure.png"))
                    s.dump_ui(f"{case.name}_failure_ui.txt")
                    state = s.state_snapshot()
                except Exception:
                    state = {}
                report.record(full, "fail", f"{type(e).__name__}: {e}",
                              artifacts=artifacts, exc=e, app_state=state)
                try:  # 现场可能残留弹窗/选中态：清一次，避免影响下一用例
                    s.new_graph()
                except Exception:
                    pass

        # 末尾：Save As 落盘 JSON 往返（用当前画布=最后一个用例的图）
        report.begin("core/save_roundtrip")
        try:
            saved = report.path("core_saved.json")
            s.menu_save_as(saved)
            wait_until(lambda: saved.is_file(), desc=f"保存文件出现 {saved}")
            data = json.loads(saved.read_text(encoding="utf-8"))
            if data.get("version") != "2.0":
                raise AssertionError(f"version != 2.0: {data.get('version')}")
            n_tasks, n_edges = s.status_counts()
            if len(data.get("tasks", [])) != n_tasks:
                raise AssertionError(f"保存任务数 {len(data.get('tasks', []))} "
                                     f"!= 画布 {n_tasks}")
            if len(data.get("edges", [])) != n_edges:
                raise AssertionError(f"保存边数 {len(data.get('edges', []))} "
                                     f"!= 画布 {n_edges}")
            if "core_saved.json" not in s.win.window_text():
                raise AssertionError(f"标题未更新: {s.win.window_text()}")
            report.record("core/save_roundtrip", "pass",
                          f"{n_tasks} tasks / {n_edges} edges 已校验",
                          artifacts=[str(saved)])
        except Exception as e:
            try:
                s.screenshot("save_roundtrip_failure.png")
                state = s.state_snapshot()
            except Exception:
                state = {}
            report.record("core/save_roundtrip", "fail", f"{type(e).__name__}: {e}",
                          exc=e, app_state=state)

        s.screenshot("core_final.png")
    finally:
        s.close()
