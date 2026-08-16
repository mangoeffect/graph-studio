"""scenarios_core.py — 核心编辑+执行流（数据驱动，用例见 plugin_cases.py）。

统一流程（每个 PluginCase）：
  File>New → 逐节点右键菜单创建（画布横向布局）→ 选中节点按标签设参数 →
  端口拖拽连线 → Run → 断言（0 failed / 结果下拉 / 磁盘产物 / 日志子串）。

每个用例独立记录（core/<name>），单例失败不阻断后续用例；
最后附一次 Save As 落盘 JSON 往返校验（core/save_roundtrip）。
"""

from __future__ import annotations

import json
import time

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


def _run_case(s: AppSession, case, report, fixtures, subs, gpu_ok) -> None:
    """单个 PluginCase 的完整交互流（异常上抛，由调用方决定重试/记录）。

    每个真实输入步骤后做效果校验并就地重试：建点逐个校验计数、选中校验
    面板 ID、参数提交回读、连线逐条校验计数——单步被偷当场补做，避免
    错误传播到执行阶段才以更难排查的方式失败。
    """
    s.new_graph()
    pos = _node_positions(s.canvas_center(), len(case.nodes))

    # 建节点（逐个：计数到 i+1 才算成功，未到则重试该节点）
    for i, (spec, pt) in enumerate(zip(case.nodes, pos)):
        for attempt in range(3):
            s.add_node_at(spec["type"], pt, category=spec["category"])
            try:
                wait_until(lambda: s.status_counts()[0] == i + 1,
                           timeout=4, desc=f"{case.name}: 节点数 {i + 1}")
                break
            except Exception:
                if attempt == 2:
                    raise
                time.sleep(0.3)

    # 设参数（选中 → 校验选中 → 逐标签输入[带回读] → 点画布去焦点）
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

    # 连线（逐条：计数到 j+1 才算成功，未到则重试该条拖拽；重试间重新
    # 锚定窗口焦点——拖拽路径长，被外部光标/焦点干扰概率最高）
    for j, (a, b) in enumerate(case.edges):
        for attempt in range(4):
            if attempt:
                s.win.set_focus()
                time.sleep(0.3)
            s.connect_ports(pos[a], pos[b])
            try:
                wait_until(lambda: s.status_counts() == (len(case.nodes), j + 1),
                           timeout=4, desc=f"{case.name}: 连线数 {j + 1}")
                break
            except Exception:
                if attempt == 3:
                    raise
                time.sleep(0.3)

    # 执行
    ok, failed, _log = s.run_and_wait()
    if failed != 0:
        raise AssertionError(f"执行有 {failed} 个任务失败（ok={ok}）")

    # 断言：结果下拉
    if case.expect_results is not None:
        items = wait_until(lambda: (s.result_combo_texts() or None),
                           timeout=15, interval=0.5, desc="结果下拉条目")
        for sub in case.expect_results:
            if not any(sub in it for it in items):
                raise AssertionError(f"结果下拉缺少 {sub!r}: {items}")

    # 断言：磁盘产物
    for rel in case.expect_files:
        p = report.path(_substitute(rel, subs) if rel.startswith("$OUT")
                        else rel)
        if not p.is_file():
            raise AssertionError(f"执行后未见输出文件: {p}")

    # 断言：日志子串（执行后停在 Profile 页，先切回 Log 可读）
    if case.expect_log:
        s.ensure_log_visible()
        log = s.log_text()
        combined = log + s.output_text()
        for sub in case.expect_log:
            if sub not in combined:
                raise AssertionError(f"日志缺少 {sub!r}（尾部: "
                                     f"{log.splitlines()[-5:] if log else '空'}）")


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

            # 用例级重试：真实输入可能被外部光标/焦点干扰整段偷走（实测
            # 连续多个用例窗口期失败），单次失败先清场整例重来一遍再记失败
            last_exc: Exception | None = None
            for case_attempt in range(2):
                try:
                    _run_case(s, case, report, fixtures, subs, gpu_ok)
                    report.record(full, "pass")
                    last_exc = None
                    break
                except Exception as e:
                    last_exc = e
                    try:  # 清场，重试或留给失败记录
                        s.new_graph()
                    except Exception:
                        pass
            if last_exc is not None:
                e = last_exc
                try:
                    s.screenshot(f"{case.name}_failure.png")
                    s.dump_ui(f"{case.name}_failure_ui.txt")
                    state = s.state_snapshot()
                except Exception:
                    state = {}
                report.record(full, "fail", f"{type(e).__name__}: {e}",
                              exc=e, app_state=state)

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
