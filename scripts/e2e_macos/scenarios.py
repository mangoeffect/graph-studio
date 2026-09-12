# -*- coding: utf-8 -*-
"""scenarios.py — GraphStudio macOS 安装态 E2E 场景（AXAPI + CGEvent）。

断言锚点（MainWindow 为外部自动化显式准备的）：
  - accessibleName：Task Library / Graph Canvas / Log Panel / Image Results /
    Output Panel（Qt 暴露为 AXDescription）
  - countsLabel 文本 "Nodes: N | Edges: M"（AXStaticText 的 AXValue）
  - 窗口标题 "<file> - Graph Studio"
画布节点几何：NodeItem 中心 = 创建时点击点，宽 140（端口在 ±70）。

场景：
  - core      画布手势（右键建点 -> 端口拖线 -> undo/redo）
  - files     全部子模块单测图逐张 --open/--run 冷启动执行（发现/落位见
              graph_cases.py，对齐 e2e_windows/scenarios_files.py 的覆盖面）
  - run       单图冒烟（生成的 tiny png 图 --open --run -> 日志断言）
  - lifecycle Cmd+Q 正常退出

图输入走 CLI 启动参数（app 侧 entry.cpp 的 --open/--run）：每张图一个干净
进程，无 NSOpenPanel 自动化、无键盘焦点竞态；批跑期间仅做 AX 读取。
"""

import os
import re
import time
from pathlib import Path

import e2e_graph_cases as gc
from .ax_driver import (AxDriverError, MacDriver, kCGEventFlagMaskCommand,
                        kCGEventFlagMaskShift, kVK_ANSI_Q, kVK_ANSI_Z)
from .session import MacSession, SessionError, wait_finished_file


class ScenarioError(RuntimeError):
    pass


class GraphCaseError(SessionError):
    """单图冷启动用例失败，携带退出前采集的现场（snapshot/artifacts）。"""

    def __init__(self, msg: str, snapshot: dict | None = None,
                 artifacts: list | None = None):
        super().__init__(msg)
        self.snapshot = snapshot or {}
        self.artifacts = artifacts or []


NODE_WIDTH = 140
TIMEOUT = 30.0
COUNTS_RE = re.compile(r"Nodes:\s*(\d+)\s*\|\s*Edges:\s*(\d+)")


def _counts(drv: MacDriver, window) -> tuple[int, int]:
    el = drv.find_static_text_starting(window, "Nodes:", TIMEOUT)
    if el is None:
        raise ScenarioError("找不到计数标签（Nodes: N | Edges: M）")
    m = COUNTS_RE.search(el.value_text())
    if not m:
        raise ScenarioError(f"计数标签解析失败: {el.value_text()!r}")
    return int(m.group(1)), int(m.group(2))


def _wait_counts(drv, window, nodes=None, edges=None, timeout=TIMEOUT):
    deadline = time.time() + timeout
    last = (-1, -1)
    while time.time() < deadline:
        last = _counts(drv, window)
        if (nodes is None or last[0] == nodes) and \
           (edges is None or last[1] == edges):
            return last
        time.sleep(0.3)
    raise ScenarioError(f"计数未达到 ({nodes}, {edges})，停在 {last}")


def _canvas_geom(drv, window):
    el = drv.find_by_description(window, "Graph Canvas", TIMEOUT)
    if el is None:
        raise ScenarioError("找不到画布（AXDescription 'Graph Canvas'）")
    pos, size = el.position(), el.size()
    return pos.x, pos.y, size.w, size.h


def core(drv: MacDriver, window, artifacts: Path) -> str:
    """建两节点 -> 端口拖线 -> undo/redo -> 删除（真实鼠标手势 + 菜单）。"""
    cx, cy, w, h = _canvas_geom(drv, window)
    center = (cx + w / 2, cy + h / 2)

    # 1) 右键画布中心 -> 上下文菜单（两级：分类子菜单 -> task 叶子，
    #    子菜单需悬停展开）-> 建第一类节点
    drv.click(*center, right=True)
    time.sleep(0.8)
    drv.press_context_menu("Input", "opencv_image_read")
    _wait_counts(drv, window, nodes=1, edges=0)

    # 2) 偏移处建第二个节点（同一点击方式，位置可预期）
    second = (center[0] + 260, center[1])
    drv.click(*second, right=True)
    time.sleep(0.8)
    drv.press_context_menu("OpenCV Filter", "opencv_blur_filter")
    _wait_counts(drv, window, nodes=2, edges=0)

    # 3) 端口拖线：node1 out(+70,0) -> node2 in(-70,0)
    p1 = (center[0] + NODE_WIDTH / 2, center[1])
    p2 = (second[0] - NODE_WIDTH / 2, center[1])
    drv.drag(p1[0], p1[1], p2[0], p2[1])
    time.sleep(0.6)
    _wait_counts(drv, window, nodes=2, edges=1)

    # 4) Undo / Redo
    drv.key_combo(kVK_ANSI_Z, kCGEventFlagMaskCommand)
    time.sleep(0.5)
    _wait_counts(drv, window, nodes=2, edges=0)
    drv.key_combo(kVK_ANSI_Z, kCGEventFlagMaskCommand | kCGEventFlagMaskShift)
    time.sleep(0.5)
    _wait_counts(drv, window, nodes=2, edges=1)
    return "core OK (menu-create x2 / port-drag / undo / redo)"


def cli_graph_case(runner, graph_path: Path, *, nodes=None, edges=None,
                   timeout: float = 120.0, screenshot=None,
                   shot_name: str = "") -> tuple[int, int]:
    """--open + --run 冷启动执行一张图，返回 (ok, failed)。

    流程：启动（stderr 镜像到 app_logs/<日志>）-> 等主窗口（标题含文件名
    = 打开成功）-> 可选计数校验 -> 轮询镜像日志等 Execution finished ->
    SIGTERM 收尾。完成断言走 stderr 镜像文件而非 AX 读 Log 面板——执行
    结束后底栏自动切 Profile 页，隐藏的 Log 页控件会从 AX 树消失。
    失败在进程退出前采集 state_snapshot（+可选截图），包进 GraphCaseError。
    """
    logs_dir = runner.run_dir / "app_logs"
    log_file = logs_dir / f"{graph_path.parent.name}_{graph_path.name}.log"
    runner.start("--open", str(graph_path), "--run", log=log_file)
    session = MacSession(MacDriver(runner.pid))
    snapshot: dict = {}
    artifacts: list = []
    try:
        session.main_window(timeout=45)      # 窗口就位（标题可能已带文件名）
        session._wait_title(graph_path.name, timeout=15)
        if nodes is not None or edges is not None:
            session.wait_counts(nodes, edges, timeout=15)
        return wait_finished_file(log_file, timeout=timeout)
    except (SessionError, AxDriverError) as ex:
        try:
            snapshot = session.state_snapshot()
        except Exception:
            pass
        snapshot.setdefault("app_log", "")
        try:
            snapshot["app_log"] = log_file.read_text(errors="replace")[-1500:]
        except OSError:
            pass
        if screenshot is not None:
            try:
                artifacts.append(screenshot(shot_name or "graph_failure.png"))
            except Exception:
                pass
        raise GraphCaseError(f"{type(ex).__name__}: {ex}", snapshot,
                             artifacts) from ex
    finally:
        runner.stop()


def files_run(runner, report, ctx: dict) -> int:
    """逐张冷启动执行全部子模块单测图，每张一个用例 files/graph:<module>/<name>。

    对齐 e2e_windows/scenarios_files.py 的覆盖面；差异在输入通道——CLI
    启动参数（--open/--run）而非 File>Open 自动化。skip 规则：夹具资产
    缺失。返回实际执行的图数量。
    """
    graphs = gc.discover_graphs()
    max_graphs = int(ctx.get("max_graphs") or 0)   # 0 = 全部
    graphs_filter = (ctx.get("graphs_filter") or "").strip().lower()

    runnable = []
    for e in graphs:
        full = f"files/graph:{e['module']}/{e['name']}"
        if e["missing"]:
            report.record(full, "skip",
                          f"夹具资产缺失: {', '.join(e['missing'][:3])}")
            continue
        runnable.append(e)

    if graphs_filter:
        runnable = [e for e in runnable if graphs_filter in str(e["path"]).lower()]
    elif max_graphs > 0:
        # 代表性小图优先，再跨模块轮转补足到 max_graphs（避免单一模块占满）
        by_name = {e["name"]: e for e in runnable}
        selected = [by_name[n] for n in gc.PRIORITY_GRAPHS if n in by_name]
        rest = [e for e in runnable if e["name"] not in gc.PRIORITY_GRAPHS]
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

    # GPU 可用性：CGEvent 注入已要求真实 GUI 会话，macOS 侧 Metal 必在；
    # 且 GpuBootstrap 的初始化日志早于 VM 日志 sink 注册、不会出现在 Log
    # Panel，Windows 式的日志探测不适用。无 GPU 环境可用环境变量把 gpu 图
    # 降级为 allow-fail。
    gpu_ok = os.environ.get("TG_E2E_MACOS_ALLOW_GPU_FAIL", "") != "1"

    for e in runnable:
        full = f"files/graph:{e['module']}/{e['name']}"
        report.begin(full)
        try:
            # 图 + 资产复制到运行目录：writer 类任务的输出写进副本，不污染仓库
            graph_copy = gc.stage_copy(e, report.run_dir)
            allow_fail = gc.is_gpu_graph(e) and not gpu_ok

            ok, failed = cli_graph_case(
                runner, graph_copy, nodes=len(e["tasks"]), edges=len(e["edges"]),
                timeout=120, screenshot=ctx.get("screenshot"),
                shot_name=f"{e['name']}_failure.png")
            if allow_fail:
                report.record(full, "pass",
                              f"ok={ok} failed={failed}（allow-fail：无GPU 预期）")
                continue
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f"ok={ok} failed={failed}")
        except Exception as ex:
            state = getattr(ex, "snapshot", None) or {}
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          artifacts=list(getattr(ex, "artifacts", []) or []),
                          exc=ex, app_state=state)
    return len(runnable)


def run_graph(runner, report, ctx: dict, graph_path: Path) -> str:
    """单图冒烟：--open --run 冷启动 -> 日志 'Execution finished: 1 ok, 0 failed'。"""
    ok, failed = cli_graph_case(runner, graph_path, nodes=1, edges=0, timeout=60)
    if ok != 1 or failed != 0:
        raise ScenarioError(f"期望 1 ok / 0 failed，实得 {ok} ok / {failed} failed")
    return f"run OK（--open --run: Execution finished: {ok} ok, {failed} failed）"


def lifecycle(drv: MacDriver, window, artifacts: Path) -> str:
    """Cmd+Q 正常退出（进程由入口脚本 join 校验退出码）。"""
    drv.key_combo(kVK_ANSI_Q, kCGEventFlagMaskCommand)
    time.sleep(1.0)
    return "lifecycle OK (Cmd+Q submitted)"


SCENARIOS = ["core", "files", "run", "lifecycle"]
