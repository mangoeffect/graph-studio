# -*- coding: utf-8 -*-
"""scenarios.py — GraphStudio macOS 安装态 E2E 场景（AXAPI + CGEvent）。

断言锚点（MainWindow 为外部自动化显式准备的）：
  - accessibleName：Task Library / Graph Canvas / Log Panel / Image Results /
    Output Panel（Qt 暴露为 AXDescription）
  - countsLabel 文本 "Nodes: N | Edges: M"（AXStaticText 的 AXValue）
  - 窗口标题 "<file> - Graph Studio"
画布节点几何：NodeItem 中心 = 创建时点击点，宽 140（端口在 ±70）。
"""

import time
from pathlib import Path

from .ax_driver import (AxDriverError, MacDriver, kCGEventFlagMaskCommand,
                        kCGEventFlagMaskShift, kVK_ANSI_G, kVK_ANSI_Q,
                        kVK_ANSI_R, kVK_ANSI_Z)


class ScenarioError(RuntimeError):
    pass


NODE_WIDTH = 140
TIMEOUT = 30.0


def _counts(drv: MacDriver, window) -> tuple[int, int]:
    el = drv.find_static_text_starting(window, "Nodes:", TIMEOUT)
    if el is None:
        raise ScenarioError("找不到计数标签（Nodes: N | Edges: M）")
    text = el.value_text()
    nodes = edges = -1
    for part in text.replace("|", " ").split():
        if part.startswith("Nodes:"):
            nodes = int(part.split(":")[1])
        if part.startswith("Edges:"):
            edges = int(part.split(":")[1])
    return nodes, edges


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

    # 1) 右键画布中心 -> 上下文菜单 -> 建第一类节点
    drv.click(*center, right=True)
    time.sleep(0.8)
    drv.press_context_menu_item("opencv_image_read")
    _wait_counts(drv, window, nodes=1, edges=0)

    # 2) 偏移处建第二个节点（同一点击方式，位置可预期）
    second = (center[0] + 260, center[1])
    drv.click(*second, right=True)
    time.sleep(0.8)
    drv.press_context_menu_item("opencv_image_filtering")
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


def open_graph_file(drv: MacDriver, window, graph_path: Path):
    """File > Open -> NSOpenPanel：Cmd+Shift+G 输路径 -> Return -> Return。"""
    if not drv.menu_click("File", "Open"):
        raise ScenarioError("File > Open 菜单未命中")
    time.sleep(1.2)  # 等原生面板
    drv.key_combo(kVK_ANSI_G, kCGEventFlagMaskCommand | kCGEventFlagMaskShift)
    time.sleep(0.8)
    drv.type_text(str(graph_path))
    time.sleep(0.4)
    drv.key_combo(0x24)  # Return：前往
    time.sleep(0.8)
    drv.key_combo(0x24)  # Return：Open 默认按钮
    time.sleep(1.0)


def files(drv: MacDriver, window, artifacts: Path, graph_path: Path) -> str:
    """打开图文件 -> 标题带文件名。"""
    open_graph_file(drv, window, graph_path)
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        for w in drv.windows():
            if w.title().startswith(graph_path.stem):
                return f"files OK (title={w.title()!r})"
        time.sleep(0.4)
    raise ScenarioError(f"窗口标题未更新为 {graph_path.stem!r}")


def run_graph(drv: MacDriver, window, artifacts: Path, graph_path: Path) -> str:
    """打开图 -> Cmd+R -> 等日志 'Execution finished: 1 ok, 0 failed'。"""
    open_graph_file(drv, window, graph_path)
    drv.key_combo(kVK_ANSI_R, kCGEventFlagMaskCommand)
    log = drv.find_by_description(window, "Log Panel", TIMEOUT)
    if log is None:
        raise ScenarioError("找不到日志面板（AXDescription 'Log Panel'）")
    deadline = time.time() + 60
    while time.time() < deadline:
        if "Execution finished: 1 ok, 0 failed" in log.value_text():
            return "run OK (Execution finished: 1 ok, 0 failed)"
        time.sleep(0.5)
    raise ScenarioError("60s 内日志未出现 Execution finished（log 尾部: "
                        f"{log.value_text()[-200:]!r}）")


def lifecycle(drv: MacDriver, window, artifacts: Path) -> str:
    """Cmd+Q 正常退出（进程由入口脚本 join 校验退出码）。"""
    drv.key_combo(kVK_ANSI_Q, kCGEventFlagMaskCommand)
    time.sleep(1.0)
    return "lifecycle OK (Cmd+Q submitted)"


SCENARIOS = ["core", "files", "run", "lifecycle"]
