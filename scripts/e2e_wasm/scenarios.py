# -*- coding: utf-8 -*-
"""scenarios.py — GraphStudio WASM 浏览器端 E2E 场景。

每个场景收到一个已连接目标页面的 CdpTab（boot/core），或经 ctx 自管
tab（run/files，对齐 macOS E2E 的 CLI 冷启动）。断言走两条通道：
  - window.__gsTest 测试桥（test_hooks.cpp 安装：状态计数 / 图加载 / 动作触发）
  - console 中的 "[gs]" 日志（MainWindow::onLogMessage 的 qInfo 镜像）
画布交互走 CDP Input 域真实鼠标事件（Qt 画布内命中测试照常生效）。

图输入走 URL 启动参数（app 侧 ?open=<url>&run=1，见 entry.cpp——对齐桌面
--open/--run）：每张图一个新 tab（= 冷启动进程隔离），app 自动 fetch 图与
相对资产进 MEMFS 并立即执行，驱动只等 console 的 Execution finished。
"""

import re
import time
from pathlib import Path
from urllib.parse import quote

import e2e_graph_cases as gc
from .cdp_browser import CdpBrowser, CdpTab

BOOT_TIMEOUT = 120  # wasm 编译 + Qt 启动在冷缓存时可能较慢
FINISHED_RE = re.compile(r"\[gs\] Execution finished:\s*(\d+)\s*ok,\s*(\d+)\s*failed")

# wasm 侧不可执行的子模块（二进制 strings 实测的任务注册为准）：
#   image_processing = gpu 子模块（module 叶子名），GPU compute 的 WGSL
#   kernel 未移植，任务类型未注册；render_task 的 wasm 构建门未开；
#   mediapipe_vision 在 wasm 是无 libvision 的 stub。
WASM_UNSUPPORTED_MODULES = {
    "image_processing": "gpu 子模块未在 wasm 注册（compute WGSL 未移植）",
    "render_task": "render 子模块 wasm 构建门未开（任务类型未注册）",
    "mediapipe_vision": "wasm 为 libvision stub（模型推理不可用）",
}


class ScenarioError(RuntimeError):
    pass


class GraphCaseError(ScenarioError):
    """单图 URL 冷启动用例失败，携带退出前采集的现场（snapshot/artifacts）。"""

    def __init__(self, msg: str, snapshot: dict | None = None,
                 artifacts: list | None = None):
        super().__init__(msg)
        self.snapshot = snapshot or {}
        self.artifacts = artifacts or []


# ---------- boot：隔离上下文 + Qt 启动 + 测试桥就绪 ----------

def boot(tab: CdpTab, artifacts: Path) -> str:
    iso = tab.wait_expr("window.crossOriginIsolated === true", BOOT_TIMEOUT)
    if not iso:
        raise ScenarioError("crossOriginIsolated 为 false（COOP/COEP 未生效？）")
    # Qt 6.6.3 把 UI 挂在 shadowRoot 里，主 DOM 查不到 canvas
    tab.wait_expr(
        "(() => { const h = document.querySelector('#qt-shadow-container');"
        " return !!(h && h.shadowRoot && h.shadowRoot.querySelector('canvas')); })()",
        BOOT_TIMEOUT)
    tab.wait_expr("typeof window.__gsTest === 'object' && window.__gsTest.ready()",
                  BOOT_TIMEOUT)
    shot = artifacts / "boot.png"
    tab.screenshot(shot)
    return f"boot OK (isolated, canvas mounted, __gsTest ready) [{shot.name}]"


def wait_boot(tab: CdpTab):
    """boot 前置（供其他场景复用）：等 UI 与测试桥就绪。"""
    tab.wait_expr("window.crossOriginIsolated === true", BOOT_TIMEOUT)
    tab.wait_expr(
        "(() => { const h = document.querySelector('#qt-shadow-container');"
        " return !!(h && h.shadowRoot && h.shadowRoot.querySelector('canvas')); })()",
        BOOT_TIMEOUT)
    tab.wait_expr("typeof window.__gsTest === 'object' && window.__gsTest.ready()",
                  BOOT_TIMEOUT)


# ---------- core：建图 / 连线 / undo / redo / 删除（真实鼠标手势） ----------

# 未注册的 task type 走 GraphModel 占位任务回退（编辑器语义），无需插件。
# positions 元数据让节点落在确定坐标，端口拖拽锚点可预期。
_EDIT_GRAPH = """{
  "version": "2.0",
  "tasks": [
    { "id": "src", "type": "e2e_source", "params": {} },
    { "id": "dst", "type": "e2e_sink", "params": {} }
  ],
  "edges": [],
  "positions": {
    "src": { "x": -220, "y": -40, "type": "e2e_source" },
    "dst": { "x": 220, "y": -40, "type": "e2e_sink" }
  }
}"""


def core(tab: CdpTab, artifacts: Path) -> str:
    wait_boot(tab)

    def assert_eq(actual, expected, what):
        if actual != expected:
            shot = artifacts / "core_fail.png"
            tab.screenshot(shot)
            raise ScenarioError(
                f"{what}: 期望 {expected!r} 实得 {actual!r}（截图 {shot.name}）")

    # 1) 加载两节点空边图（含确定位置）
    if not tab.evaluate(f"window.__gsTest.loadGraph({_EDIT_GRAPH!r})"):
        raise ScenarioError("loadGraph 失败: " + tab.evaluate("window.__gsTest.lastError()"))
    deadline = time.time() + 10
    while tab.evaluate("window.__gsTest.taskCount()") != 2 and time.time() < deadline:
        time.sleep(0.2)
    assert_eq(tab.evaluate("window.__gsTest.taskCount()"), 2, "加载后节点数")
    assert_eq(tab.evaluate("window.__gsTest.edgeCount()"), 0, "加载后边数")

    # 2) 端口拖拽连边：src:out -> dst:in（viewport 坐标 = canvas 像素）。
    # 场景同步是队列事件，锚点解析需轮询等 MainWindow 建好 NodeItem。
    def wait_anchor(node, port, timeout=10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            val = tab.evaluate(f"window.__gsTest.nodeAnchor('{node}', '{port}')")
            if val:
                return val
            time.sleep(0.3)
        raise ScenarioError(f"{timeout}s 内锚点未就绪: {node}:{port}")

    a = wait_anchor("src", "out")
    b = wait_anchor("dst", "in")
    tab.drag(a["x"], a["y"], b["x"], b["y"], steps=12)
    time.sleep(0.6)
    assert_eq(tab.evaluate("window.__gsTest.edgeCount()"), 1, "拖线后边数")

    # 3) Undo 删边 -> Redo 恢复
    assert tab.evaluate("window.__gsTest.action('undo')"), "Undo 动作不可用"
    time.sleep(0.4)
    assert_eq(tab.evaluate("window.__gsTest.edgeCount()"), 0, "Undo 后边数")
    assert tab.evaluate("window.__gsTest.action('redo')"), "Redo 动作不可用"
    time.sleep(0.4)
    assert_eq(tab.evaluate("window.__gsTest.edgeCount()"), 1, "Redo 后边数")

    # 4) 点击选中 dst 节点 -> Delete（连带清理入边）。
    # 必须点节点中心（cx/cy）：x/y 是端口锚点，x+w/2 已越过节点体
    # （boundingRect 含 port margin），x+h/2 更是点在节点下方。
    c = wait_anchor("dst", "in")
    tab.click(c["cx"], c["cy"])
    time.sleep(0.4)
    assert tab.evaluate("window.__gsTest.action('delete')"), "Delete 动作不可用"
    time.sleep(0.6)
    assert_eq(tab.evaluate("window.__gsTest.taskCount()"), 1, "Delete 后节点数")
    assert_eq(tab.evaluate("window.__gsTest.edgeCount()"), 0, "Delete 后边数")

    shot = artifacts / "core_final.png"
    tab.screenshot(shot)
    return f"core OK (load/drag-edge/undo/redo/delete) [{shot.name}]"


# ---------- URL 冷启动执行（对齐 macOS cli_graph_case） ----------

def url_graph_case(browser: CdpBrowser, base_url: str, graph_url: str, *,
                   nodes=None, edges=None, timeout: float = 120.0,
                   artifacts: Path | None = None,
                   shot_name: str = "") -> tuple[int, int]:
    """`?open=<url>&run=1` 新 tab 冷启动执行一张图，返回 (ok, failed)。

    app 侧自动 fetch 图 + 相对资产进 MEMFS 并立即执行（entry.cpp 的
    EM_ASM 通道）；本函数只等 console 镜像的 Execution finished。完成行
    早于轮询开始也安全：CdpTab 的 console 缓冲从 tab 创建起累计。
    失败在关 tab 前截图 + 收 console 尾部，包进 GraphCaseError。
    """
    tab = browser.new_tab(
        f"{base_url}/graph_studio.html?open={quote(graph_url, safe='')}&run=1")
    snapshot: dict = {}
    shots: list[str] = []
    try:
        wait_boot(tab)
        if nodes is not None or edges is not None:
            deadline = time.time() + 15
            while time.time() < deadline:
                tc = tab.evaluate("window.__gsTest.taskCount()") or 0
                ec = tab.evaluate("window.__gsTest.edgeCount()") or 0
                if (nodes is None or tc == nodes) and (edges is None or ec == edges):
                    break
                time.sleep(0.3)
            else:
                raise ScenarioError(
                    f"图加载计数未达 (nodes={nodes}, edges={edges}，实得 "
                    f"{tab.evaluate('window.__gsTest.taskCount()')}/"
                    f"{tab.evaluate('window.__gsTest.edgeCount()')})")
        line = tab.wait_console(FINISHED_RE.pattern, timeout=timeout)
        m = FINISHED_RE.search(line)
        return int(m.group(1)), int(m.group(2))
    except (ScenarioError, TimeoutError, RuntimeError) as ex:
        tail = tab.console_text().strip().splitlines()
        snapshot = {"console_tail": "\n".join(tail[-30:])}
        if artifacts is not None:
            try:
                shot = artifacts / (shot_name or "graph_failure.png")
                tab.screenshot(shot)
                shots.append(str(shot))
            except Exception:
                pass
        raise GraphCaseError(f"{type(ex).__name__}: {ex}", snapshot, shots) from ex
    finally:
        tab.close()
        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔


def files_run(browser: CdpBrowser, base_url: str, report, ctx: dict) -> int:
    """逐张 URL 冷启动执行可跑的子模块单测图，每张一个用例
    files/graph:<module>/<name>（覆盖面对齐 e2e_windows / macOS；模块策略
    见 WASM_UNSUPPORTED_MODULES——wasm 未编译/未注册的子模块记 skip）。"""
    graphs = gc.discover_graphs()
    max_graphs = int(ctx.get("max_graphs") or 0)   # 0 = 全部
    graphs_filter = (ctx.get("graphs_filter") or "").strip().lower()
    serve_root: Path = ctx["serve_root"]
    artifacts: Path = ctx["artifacts"]

    runnable = []
    for e in graphs:
        full = f"files/graph:{e['module']}/{e['name']}"
        reason = WASM_UNSUPPORTED_MODULES.get(e["module"])
        if reason:
            report.record(full, "skip", reason)
            continue
        if e["missing"]:
            report.record(full, "skip",
                          f"夹具资产缺失: {', '.join(e['missing'][:3])}")
            continue
        runnable.append(e)

    if graphs_filter:
        runnable = [e for e in runnable if graphs_filter in str(e["path"]).lower()]
    elif max_graphs > 0:
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

    for e in runnable:
        full = f"files/graph:{e['module']}/{e['name']}"
        report.begin(full)
        try:
            graph_copy = gc.stage_copy(e, serve_root)
            graph_url = ("/e2e/" + graph_copy.relative_to(serve_root).as_posix())
            ok, failed = url_graph_case(
                browser, base_url, graph_url,
                nodes=len(e["tasks"]), edges=len(e["edges"]),
                timeout=120, artifacts=artifacts,
                shot_name=f"{e['name']}_failure.png")
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f"ok={ok} failed={failed}")
        except Exception as ex:
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          artifacts=list(getattr(ex, "artifacts", []) or []),
                          exc=ex, app_state=getattr(ex, "snapshot", None) or {})
    return len(runnable)


def run_graph(browser: CdpBrowser, base_url: str, ctx: dict) -> str:
    """单图冒烟：?open+run 冷启动 -> console 'Execution finished: 1 ok, 0 failed'。"""
    artifacts: Path = ctx["artifacts"]
    ok, failed = url_graph_case(browser, base_url, "/e2e/smoke/e2e_graph.json",
                                nodes=1, edges=0, timeout=90,
                                artifacts=artifacts, shot_name="run_fail.png")
    if ok != 1 or failed != 0:
        raise ScenarioError(f"期望 1 ok / 0 failed，实得 {ok} ok / {failed} failed")
    return f"run OK（?open+run: Execution finished: {ok} ok, {failed} failed）"


SCENARIOS = ["boot", "core", "files", "run"]
