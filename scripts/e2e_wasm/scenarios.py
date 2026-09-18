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

# wasm 侧不可执行的子模块（二进制 strings 实测的任务注册为准；当前构建
# 已把 gpu/render 子模块编入 wasm，清单为空，保留机制供未来使用）。
WASM_UNSUPPORTED_MODULES: dict[str, str] = {}

# 需要 WebGPU 后端的模块（module 叶子名）。wasm 构建已编入 gpu/render
# 任务（--wgpu）；浏览器无 WebGPU（Safari/Firefox 等）时整模块运行时探测
# skip——任务会注册但执行失败，skip 比逐图 fail 更可读。
WEBGPU_MODULES = {
    "image_processing": "gpu 子模块",
    "render_task": "render 子模块",
}

# URL 单文件通道无法枚举目录内容：目录形 effects_path（shaders/ 整目录
# manifest 库）的夹具记 skip——与 .tgp 打包器"目录引用不打包"是同一条
# v1 边界（文件形 *.effect.json 的兄弟 shader 源已由 entry.cpp 补取）。
WASM_UNSUPPORTED_GRAPHS = {
    "render_manifest_pipeline.json":
        "目录形 effects_path（shaders/）无法经 URL 通道枚举预取",
}


def webgpu_available(browser: CdpBrowser, base_url: str) -> bool:
    """浏览器是否暴露 navigator.gpu。必须在与 app 同源的文档里探测：
    空白新 tab（about:blank，无 opener）isSecureContext=false、WebGPU 不
    暴露；本机 server 的任意路径（含 404 页）都是 localhost 安全上下文。"""
    tab = browser.new_tab(base_url.rstrip("/") + "/__webgpu_probe__")
    try:
        val = tab.evaluate("JSON.stringify('gpu' in navigator)")
        return val == '"true"' or val == "true"
    except Exception:
        return False
    finally:
        tab.close()


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
        ok, failed = int(m.group(1)), int(m.group(2))
        if failed > 0:
            # 执行完成但有任务失败：带上 console 尾部现场（[gs] 镜像里有
            # 每个失败任务的原因行），否则 fail 记录只有干巴巴的计数
            tail = tab.console_text().strip().splitlines()
            raise GraphCaseError(
                f"执行有 {failed} 个任务失败（ok={ok}）",
                {"console_tail": "\n".join(tail[-40:])})
        return ok, failed
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

    gpu_ok = webgpu_available(browser, base_url)

    runnable = []
    for e in graphs:
        full = f"files/graph:{e['module']}/{e['name']}"
        reason = WASM_UNSUPPORTED_MODULES.get(e["module"])
        if reason:
            report.record(full, "skip", reason)
            continue
        if e["module"] in WEBGPU_MODULES and not gpu_ok:
            report.record(full, "skip",
                          f"浏览器无 WebGPU（navigator.gpu 缺失），"
                          f"{WEBGPU_MODULES[e['module']]}需要 GPU 后端")
            continue
        if e["name"] in gc.EXPECTED_FAILURE_GRAPHS:
            report.record(full, "skip", gc.EXPECTED_FAILURE_GRAPHS[e["name"]])
            continue
        if e["name"] in WASM_UNSUPPORTED_GRAPHS:
            report.record(full, "skip", WASM_UNSUPPORTED_GRAPHS[e["name"]])
            continue
        if e["missing"]:
            report.record(full, "skip",
                          f"夹具资产缺失: {', '.join(e['missing'][:3])}")
            continue
        runnable.append(e)

    # 裁剪：--graphs 子串过滤优先，否则 --max-graphs 按 PRIORITY + 跨模块轮转选取
    # （规则见 e2e_graph_cases.select_graphs，三端 E2E 共用）
    runnable = gc.select_graphs(runnable, max_graphs, graphs_filter)

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


# ---------- .tgp 工程包：单文件自带全部依赖 ----------

def project_run(browser: CdpBrowser, base_url: str, report, ctx: dict) -> int:
    """工程包 URL 冷启动执行：挑 WASM 可跑且带资产引用的夹具图，经
    scripts/pack_graph_project.py 的同一打包逻辑（zip+manifest+按引用路径
    落位）打成单个 .tgp，?open=<url>.tgp&run=1 冷启动断言 0 failed——
    单文件包不需任何资产预取/配对，是 .tgp 的核心价值验证。每包一个用例
    project/graph:<module>/<name>。"""
    import pack_graph_project as pgp  # scripts/ 已在 sys.path（同 gc）
    serve_root: Path = ctx["serve_root"]
    artifacts: Path = ctx["artifacts"]

    gpu_ok = webgpu_available(browser, base_url)
    gpu_blocked = set(WASM_UNSUPPORTED_MODULES)
    if not gpu_ok:
        gpu_blocked |= set(WEBGPU_MODULES)

    candidates = sorted(
        (e for e in gc.discover_graphs()
         if e["module"] not in gpu_blocked
         and e["name"] not in gc.EXPECTED_FAILURE_GRAPHS
         and e["name"] not in WASM_UNSUPPORTED_GRAPHS
         and not e["missing"] and e["refs"]),
        key=lambda e: (e["module"], e["name"]))
    if not candidates:
        report.record("project", "skip", "没有带资产引用的可跑夹具图")
        return 0
    # 控制时长：每包一次 tab 冷启动（~15s），取前 2 张（跨模块时按序自然分散）
    candidates = candidates[:2]

    proj_dir = serve_root / "projects"
    proj_dir.mkdir(parents=True, exist_ok=True)
    ran = 0
    for e in candidates:
        full = f"project/graph:{e['module']}/{e['name']}"
        report.begin(full)
        try:
            tgp = proj_dir / (Path(e["name"]).stem + ".tgp")
            if pgp.pack(Path(e["path"]), tgp) != 0:
                raise ScenarioError(f"打包失败: {tgp}")
            graph_url = "/e2e/projects/" + tgp.name
            ok, failed = url_graph_case(
                browser, base_url, graph_url,
                nodes=len(e["tasks"]), edges=len(e["edges"]),
                timeout=120, artifacts=artifacts,
                shot_name=f"{e['name']}_tgp_failure.png")
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f".tgp 单文件 ok={ok} failed={failed}")
        except Exception as ex:
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          artifacts=list(getattr(ex, "artifacts", []) or []),
                          exc=ex, app_state=getattr(ex, "snapshot", None) or {})
        ran += 1
    return ran


SCENARIOS = ["boot", "core", "files", "run", "project"]
