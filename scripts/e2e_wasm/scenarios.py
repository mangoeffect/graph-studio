# -*- coding: utf-8 -*-
"""scenarios.py — GraphStudio WASM 浏览器端 E2E 场景。

每个场景收到一个已连接目标页面的 CdpTab（boot/core），或经 ctx 自管
tab（run/files，对齐 macOS E2E 的 CLI 冷启动）。断言走两条通道：
  - window.__gsTest 测试桥（test_hooks.cpp 安装：状态计数 / 图加载 / 动作触发）
  - console 中的 "[gs]" 日志（MainWindow::onLogMessage 的 qInfo 镜像）
画布交互走 CDP Input 域真实鼠标事件（Qt 画布内命中测试照常生效）。

图输入走 URL 启动参数（app 侧 ?open=<url>&run=1，见 entry.cpp——对齐桌面
--open/--run）：每张图一个新 tab（= 冷启动进程隔离），app 自动 fetch 图与
相对资产进 MEMFS 并立即执行，驱动只等 console 的 Run N finished。
页面 origin 有两种（run_e2e_wasm.py 装配）：本地构建树/发布 zip server，
或 --url 指定的线上部署版——后者图 URL 是跨源绝对 URL（本地图 server 带
CORS/CORP 头放行），其余断言通道（__gsTest 桥 + console 镜像）不变。
"""

import json
import re
import time
from pathlib import Path
from urllib.parse import quote

import e2e_graph_cases as gc
from .cdp_browser import CdpBrowser, CdpTab

BOOT_TIMEOUT = 120  # wasm 编译 + Qt 启动在冷缓存时可能较慢
# 完成行契约（ff18697 运行模型扩展后，GraphViewModel::onRunSummary）：
# 每轮一条 "[gs] Run <i> finished: <ok> ok, <failed> failed (<ms> ms)"，
# 单次执行（?run=1）恰好一条。
FINISHED_RE = re.compile(r"\[gs\] Run \d+ finished:\s*(\d+)\s*ok,\s*(\d+)\s*failed")

# wasm 侧不可执行的子模块（二进制 strings 实测的任务注册为准；当前构建
# 已把 gpu/render 子模块编入 wasm，清单为空，保留机制供未来使用）。
WASM_UNSUPPORTED_MODULES: dict[str, str] = {}

# 引擎为 stub 的模块（module 叶子名）：wasm 构建不链 mediapipe 引擎
#（build_mediapipe.py 无 wasm 目标，vision C API 未编 emscripten），
# mp-only 任务（face_detector/facelandmark/...）execute 恒 FAILED——
# 按模块记 skip 比逐图 fail 可读。face/matting 的 auto 后端不受影响
#（自动降级 mnn）。mp wasm 引擎接线后（research 4.5 节路线 A）移除。
MP_STUB_MODULES = {
    "mediapipe_vision":
        "mediapipe 引擎在 wasm 是 stub（未编 emscripten），mp-only 任务"
        "必然 FAILED；face/matting 的 auto 后端自动降级 mnn 不受影响",
}

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


def webgpu_available(browser: CdpBrowser, page_url: str) -> bool:
    """浏览器是否暴露 navigator.gpu。在与 app 同 origin 的任意文档里探测
    （page_url 的 origin + 404 探测页）：空白新 tab（about:blank，无
    opener）isSecureContext=false、WebGPU 不暴露；localhost 与线上 https
    站点的任意路径（含 404 页）都是安全上下文。"""
    from urllib.parse import urlsplit
    origin = urlsplit(page_url)._replace(path="", query="",
                                         fragment="").geturl()
    tab = browser.new_tab(origin + "/__webgpu_probe__")
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


def _wait_graph_counts(tab: CdpTab, nodes, edges, timeout: float = 15.0):
    """轮询 __gsTest 计数至期望（图加载是异步队列事件，见 test_hooks 注释）。"""
    if nodes is None and edges is None:
        return
    deadline = time.time() + timeout
    while time.time() < deadline:
        tc = tab.evaluate("window.__gsTest.taskCount()") or 0
        ec = tab.evaluate("window.__gsTest.edgeCount()") or 0
        if (nodes is None or tc == nodes) and (edges is None or ec == edges):
            return
        time.sleep(0.3)
    raise ScenarioError(
        f"图加载计数未达 (nodes={nodes}, edges={edges}，实得 "
        f"{tab.evaluate('window.__gsTest.taskCount()')}/"
        f"{tab.evaluate('window.__gsTest.edgeCount()')})")


def _collect_failure(tab: CdpTab, ex: Exception, artifacts: Path | None,
                     shot_name: str) -> GraphCaseError:
    """关 tab 前的现场采集（console 尾部 + 截图），包成 GraphCaseError。"""
    snapshot = {"console_tail": "\n".join(
        tab.console_text().strip().splitlines()[-30:])}
    shots: list[str] = []
    if artifacts is not None:
        try:
            shot = artifacts / (shot_name or "graph_failure.png")
            tab.screenshot(shot)
            shots.append(str(shot))
        except Exception:
            pass
    return GraphCaseError(f"{type(ex).__name__}: {ex}", snapshot, shots)


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

def url_graph_case(browser: CdpBrowser, page_url: str, graph_url: str, *,
                   nodes=None, edges=None, timeout: float = 120.0,
                   artifacts: Path | None = None,
                   shot_name: str = "") -> tuple[int, int]:
    """`?open=<url>&run=1` 新 tab 冷启动执行一张图，返回 (ok, failed)。

    page_url 是 app 页面完整 URL（本地构建树 server 或 --url 线上部署版，
    均不含 query）；graph_url 可相对（同源 /e2e/）或绝对（--url 模式指向
    本地图 server，跨源由 server 侧 CORS/CORP 头放行）。

    app 侧自动 fetch 图 + 相对资产进 MEMFS 并立即执行（entry.cpp 的
    EM_ASM 通道；资产前缀取图 URL 的目录段，绝对 URL 天然正确）；本函数
    只等 console 镜像的 Run N finished。完成行早于轮询开始也安全：CdpTab
    的 console 缓冲从 tab 创建起累计。失败在关 tab 前截图 + 收 console
    尾部，包进 GraphCaseError。
    """
    tab = browser.new_tab(
        f"{page_url}?open={quote(graph_url, safe='')}&run=1")
    try:
        wait_boot(tab)
        _wait_graph_counts(tab, nodes, edges)
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
        raise _collect_failure(tab, ex, artifacts, shot_name) from ex
    finally:
        tab.close()
        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔


# ---------- CDP 直写通道（--url 线上模式，fetch 被拦时的降级） ----------

def fetch_channel_available(browser: CdpBrowser, page_url: str,
                            probe_url: str) -> bool:
    """探测线上页面的 ?open fetch 通道是否可用（https 页面拉本机 localhost
    图 server 会被 Chrome 的 Private/Local Network Access 拦截——报
    Failed to fetch / ERR_FAILED，与 CORS 头无关）。探测不过则整场降级
    CDP 直写通道。"""
    tab = browser.new_tab(page_url)
    try:
        wait_boot(tab)
        val = tab.evaluate(
            "fetch(" + json.dumps(probe_url) + ")"
            ".then(function(r){return r.status;})"
            ".catch(function(e){return 'ERR:' + e.message;})", timeout=30)
        return isinstance(val, (int, float)) and 200 <= int(val) < 300
    except Exception:
        return False
    finally:
        tab.close()
        time.sleep(0.5)


def _js_bytes(data: bytes) -> str:
    """bytes -> 求值出 Uint8Array 的 JS 表达式（base64 经 atob 解码）。"""
    import base64
    return ("Uint8Array.from(atob('" + base64.b64encode(data).decode()
            + "'), function(c){return c.charCodeAt(0);})")


def cdp_stage_dir(tab: CdpTab, staged_dir: Path, skip: Path | None = None):
    """staged 落位目录整树 fsWrite 进 MEMFS **根**（'/'+basename，扁平），
    字节走 DevTools websocket 不经网络栈——CORS/COEP/PNA 全部无关。

    为什么扁平而不是保持目录结构：fsWrite 桥只 writeFile 不建父目录，
    且 Module/FS 在页面全局不可见（test_hooks 的 EM_ASM 闭包工厂作用域
    私有，实测 typeof Module/FS === 'undefined'，mkdir 无法在桥外调）；
    带子目录的资产（data/xxx.png）必须靠 _flatten_refs 把图 JSON 引用
    同步改写为 basename。skip 通常传图 json 自身（loadGraph 直接收文本）。
    staged 目录里只有图与资产（stage_copy/smoke 落位），整树写不挑文件；
    同名 basename 冲突（不同子目录同名文件）会静默覆盖，提前检测报错。"""
    seen: dict[str, str] = {}
    for f in sorted(staged_dir.rglob("*")):
        if not f.is_file():
            continue
        if skip is not None and f.resolve() == skip.resolve():
            continue
        prior = seen.get(f.name)
        if prior and prior != str(f):
            raise ScenarioError(
                f"直写通道 basename 冲突: {f.name} 同时来自 {prior} 与 {f}"
                f"（扁平落位会互相覆盖，改图夹具或走 URL 通道）")
        seen[f.name] = str(f)
        tab.evaluate(
            f"window.__gsTest.fsWrite('/{f.name}', {_js_bytes(f.read_bytes())})",
            timeout=60)


def _flatten_refs(text: str, refs: list[str]) -> str:
    """图 JSON 结构化重写：refs 相对引用 → basename（配合 cdp_stage_dir
    的扁平落位）。只改恰好等于某 ref 的参数值（模型名等非路径值不会撞上
    完整相对路径）；写出型参数不在 refs 里，天然不受影响。.effect.json
    的 shader 源按 stem 约定（无 shader_prefix 字段，夹具实测）在同目录
    解析，扁平化后从根直中。"""
    uniq = sorted(set(refs))
    names = [Path(r).name for r in uniq]
    if len(set(names)) != len(names):
        dup = sorted({n for n in names if names.count(n) > 1})
        raise ScenarioError(f"直写通道 basename 冲突（refs）: {', '.join(dup)}")
    mapping = dict(zip(uniq, names))
    data = json.loads(text)
    for t in data.get("tasks", []):
        params = t.get("params") or {}
        for k, v in list(params.items()):
            if isinstance(v, str) and v in mapping:
                params[k] = mapping[v]
    return json.dumps(data, ensure_ascii=False)


def cdp_load_graph(tab: CdpTab, graph_file: Path, refs: list[str] | None = None,
                   stage_root: Path | None = None):
    """装图（直写通道）：staged 目录整树进 MEMFS 根后 loadGraph(改写后的
    json, '/')。graph_file 必须在 staged 目录树里；stage_root 默认取图所在
    目录，图夹具带越界引用（../models/x.jpg 之类，stage_copy 会落到
    submodule_graphs 兄弟目录甚至 run_dir 根）时传整个 run_dir——越界资产
    也在树内才会被 fsWrite。"""
    cdp_stage_dir(tab, stage_root or graph_file.parent, skip=graph_file)
    text = _flatten_refs(graph_file.read_text(encoding="utf-8"), refs or [])
    if not tab.evaluate(f"window.__gsTest.loadGraph({json.dumps(text)}, '/')"):
        raise ScenarioError("loadGraph 失败: "
                            + str(tab.evaluate("window.__gsTest.lastError()")))


def cdp_graph_case(browser: CdpBrowser, page_url: str, graph_file: Path, *,
                   refs: list[str] | None = None,
                   nodes=None, edges=None, timeout: float = 120.0,
                   artifacts: Path | None = None,
                   shot_name: str = "",
                   stage_root: Path | None = None,
                   wait_models: bool = False) -> tuple[int, int]:
    """CDP 直写通道的冷启动单图执行：loadGraph + action('execute')，断言
    与 url_graph_case 完全一致（同一条 FINISHED_RE 完成行契约）。

    wait_models：execute 前等 window.__gsModelsReady（evaluate
    awaitPromise）。旧版构建该 promise = 启动全量预取（~19MB，冷缓存数十
    秒——不等会踩竞态：execute 早于预取落盘，MNN 打开 /models/xxx.mnn
    直接失败）；新版 = manifest+cache 回填（亚秒，网络下载由 ModelFinder
    在 execute 路径按需等待）。仅模型依赖图启用——每图新 tab 都等全量
    预取会把 files 场景拖死。"""
    tab = browser.new_tab(page_url)
    try:
        wait_boot(tab)
        cdp_load_graph(tab, graph_file, refs, stage_root=stage_root)
        if wait_models:
            tab.evaluate(
                "Promise.resolve(window.__gsModelsReady || 0)"
                ".then(function() { return 1; })", timeout=90)
        _wait_graph_counts(tab, nodes, edges)
        # execute 的桥返回值不可靠：WASM 主线程同步 run_all() 靠 Asyncify
        # 泵 UI 事件，C++ 让出时调用栈 unwind，Module._gs_test_action() 会
        # 先返回假值 0（多任务图触发，单任务纯同步不触发）；成败只认
        # FINISHED_RE 完成行
        tab.evaluate("window.__gsTest.action('execute')")
        line = tab.wait_console(FINISHED_RE.pattern, timeout=timeout)
        m = FINISHED_RE.search(line)
        ok, failed = int(m.group(1)), int(m.group(2))
        if failed > 0:
            tail = tab.console_text().strip().splitlines()
            raise GraphCaseError(
                f"执行有 {failed} 个任务失败（ok={ok}）",
                {"console_tail": "\n".join(tail[-40:])})
        return ok, failed
    except (ScenarioError, TimeoutError, RuntimeError) as ex:
        raise _collect_failure(tab, ex, artifacts, shot_name) from ex
    finally:
        tab.close()
        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔


def files_run(browser: CdpBrowser, page_url: str, report, ctx: dict) -> int:
    """逐张 URL 冷启动执行可跑的子模块单测图，每张一个用例
    files/graph:<module>/<name>（覆盖面对齐 e2e_windows / macOS；模块策略
    见 WASM_UNSUPPORTED_MODULES——wasm 未编译/未注册的子模块记 skip）。"""
    graphs = gc.discover_graphs()
    max_graphs = int(ctx.get("max_graphs") or 0)   # 0 = 全部
    graphs_filter = (ctx.get("graphs_filter") or "").strip().lower()
    serve_root: Path = ctx["serve_root"]
    artifacts: Path = ctx["artifacts"]
    # --url 模式下图在另一台 origin（本地图 server）：?open= 需要绝对 URL
    open_prefix = str(ctx.get("open_prefix") or "")
    # 图输入通道：URL fetch（真实分享链接路径）或 CDP 直写（线上 https 页
    # 面拉 localhost 被 PNA/LNA 拦截时自动降级，见 fetch_channel_available）
    channel = str(ctx.get("channel") or "url")

    gpu_ok = webgpu_available(browser, page_url)

    runnable = []
    for e in graphs:
        full = f"files/graph:{e['module']}/{e['name']}"
        reason = WASM_UNSUPPORTED_MODULES.get(e["module"])
        if reason:
            report.record(full, "skip", reason)
            continue
        if e["module"] in MP_STUB_MODULES:
            report.record(full, "skip", MP_STUB_MODULES[e["module"]])
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
            if channel == "cdp":
                # 每图独立 staged 目录（直写通道扁平落位怕跨图同名资产）。
                # 直写根取整个 per_root：图夹具的越界引用（../models/x.jpg）
                # 被 stage_copy 落到 submodule_graphs 兄弟目录甚至 per_root
                # 根，只有整树 fsWrite 才能带上它们
                per_root = serve_root / "cdp_stage" / (
                    f"{e['module']}__{Path(e['name']).stem}")
                graph_copy = gc.stage_copy(e, per_root)
                ok, failed = cdp_graph_case(
                    browser, page_url, graph_copy, refs=e["refs"],
                    nodes=len(e["tasks"]), edges=len(e["edges"]),
                    timeout=120, artifacts=artifacts,
                    shot_name=f"{e['name']}_failure.png",
                    stage_root=per_root)
            else:
                graph_copy = gc.stage_copy(e, serve_root)
                graph_url = (open_prefix + "/e2e/"
                             + graph_copy.relative_to(serve_root).as_posix())
                ok, failed = url_graph_case(
                    browser, page_url, graph_url,
                    nodes=len(e["tasks"]), edges=len(e["edges"]),
                    timeout=120, artifacts=artifacts,
                    shot_name=f"{e['name']}_failure.png")
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f"ok={ok} failed={failed}")
        except OSError as ex:
            # 浏览器/CDP 连接中断（Chrome 崩溃或被外部杀，DevTools 端口
            # 拒连）：逐图重试只会连环超时，记 fail 后终止本场景
            report.record(full, "fail", f"{type(ex).__name__}: {ex}"
                          "（浏览器/CDP 连接中断）", exc=ex)
            break
        except Exception as ex:
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          artifacts=list(getattr(ex, "artifacts", []) or []),
                          exc=ex, app_state=getattr(ex, "snapshot", None) or {})
    return len(runnable)


def run_graph(browser: CdpBrowser, page_url: str, ctx: dict) -> str:
    """单图冒烟：?open+run 冷启动 -> console 'Run 0 finished: 1 ok, 0 failed'。"""
    artifacts: Path = ctx["artifacts"]
    open_prefix = str(ctx.get("open_prefix") or "")
    smoke = ctx["serve_root"] / "smoke" / "e2e_graph.json"
    if str(ctx.get("channel") or "url") == "cdp":
        ok, failed = cdp_graph_case(browser, page_url, smoke, refs=["input.png"],
                                    nodes=1, edges=0, timeout=90,
                                    artifacts=artifacts, shot_name="run_fail.png")
    else:
        ok, failed = url_graph_case(
            browser, page_url, open_prefix + "/e2e/smoke/e2e_graph.json",
            nodes=1, edges=0, timeout=90,
            artifacts=artifacts, shot_name="run_fail.png")
    if ok != 1 or failed != 0:
        raise ScenarioError(f"期望 1 ok / 0 failed，实得 {ok} ok / {failed} failed")
    return f"run OK（冷启动: Run 0 finished: {ok} ok, {failed} failed）"


# ---------- 同页连续执行两遍（回归：wasm_mt 第二遍整页死锁） ----------

def twice(browser: CdpBrowser, page_url: str, ctx: dict) -> str:
    """同页 ?open 装图后连续 execute 两遍。回归防线：wasm_multithread 构建
    每会话重建的 ThreadPool 需把 pthread 重宿主到已退出的 Web Worker，而
    主线程此刻正阻塞在调度等待（主线程 futex 模拟为忙等、事件循环停转），
    重宿主永不完成——第二遍的 'Run N finished' 永不出现且主线程整页卡死
    （2026-09-27 在线部署版实测；修复见 f110d3b：ThreadPool 所有 WASM
    构建退化为 inline）。每会话 RunLoop 计数归零，两轮完成行都是
    'Run 0 finished'，按累计条数断言。"""
    artifacts: Path = ctx["artifacts"]
    open_prefix = str(ctx.get("open_prefix") or "")
    smoke = ctx["serve_root"] / "smoke" / "e2e_graph.json"
    cdp_channel = str(ctx.get("channel") or "url") == "cdp"
    tab = browser.new_tab(
        page_url if cdp_channel else
        f"{page_url}"
        f"?open={quote(open_prefix + '/e2e/smoke/e2e_graph.json', safe='')}")
    try:
        wait_boot(tab)
        if cdp_channel:
            cdp_load_graph(tab, smoke, refs=["input.png"])
        tab.wait_expr("window.__gsTest.taskCount() === 1", 15)

        # 第一遍：execute + 完成行（0 failed）。execute 的桥返回值在
        # Asyncify 让出时是假 0（见 cdp_graph_case 注释），只认完成行
        tab.evaluate("window.__gsTest.action('execute')")
        first = tab.wait_console(FINISHED_RE.pattern, timeout=90)
        m = FINISHED_RE.search(first)
        if int(m.group(2)) != 0:
            raise ScenarioError(f"第一遍存在失败任务: {first.strip()}")
        tab.wait_expr("window.__gsTest.executing() === false", 30)

        # 第二遍：回归点。死锁构建上本调用即超时（主线程卡死在 wasm 里）；
        # Asyncify 让出的假 0 返回不是失败，两条路径都由完成行计数兜底。
        try:
            tab.evaluate("window.__gsTest.action('execute')", timeout=30)
        except TimeoutError as ex:
            raise ScenarioError(
                "第二次 execute 调用未返回（主线程整页卡死）——wasm 第二遍"
                "执行死锁回归，参照修复 f110d3b（ThreadPool WASM inline 化）"
            ) from ex
        lines = tab.wait_console_count(FINISHED_RE.pattern, 2, timeout=60)
        m2 = FINISHED_RE.search(lines[-1])
        if int(m2.group(2)) != 0:
            raise ScenarioError(f"第二遍存在失败任务: {lines[-1].strip()}")

        shot = artifacts / "twice_final.png"
        tab.screenshot(shot)
        return (f"twice OK（同页两遍均完成: "
                f"{' / '.join(l.strip().split('] ')[-1] for l in lines)}）")
    except (ScenarioError, TimeoutError, RuntimeError) as ex:
        snapshot = {"console_tail": "\n".join(
            tab.console_text().strip().splitlines()[-30:])}
        try:
            tab.screenshot(artifacts / "twice_failure.png")
        except Exception:
            pass  # 主线程卡死时截图也会超时（回归现场本身就是这个症状）
        err = ScenarioError(f"{type(ex).__name__}: {ex}")
        err.snapshot = snapshot
        raise err from ex
    finally:
        tab.close()
        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔


# ---------- .tgp 工程包：单文件自带全部依赖 ----------

def project_run(browser: CdpBrowser, page_url: str, report, ctx: dict) -> int:
    """工程包 URL 冷启动执行：挑 WASM 可跑且带资产引用的夹具图，经
    scripts/pack_graph_project.py 的同一打包逻辑（zip+manifest+按引用路径
    落位）打成单个 .tgp，?open=<url>.tgp&run=1 冷启动断言 0 failed——
    单文件包不需任何资产预取/配对，是 .tgp 的核心价值验证。每包一个用例
    project/graph:<module>/<name>。"""
    import pack_graph_project as pgp  # scripts/ 已在 sys.path（同 gc）
    serve_root: Path = ctx["serve_root"]
    artifacts: Path = ctx["artifacts"]
    open_prefix = str(ctx.get("open_prefix") or "")
    if str(ctx.get("channel") or "url") == "cdp":
        # .tgp 打开入口只有 ?open 的 URL/拖放通道（entry.cpp），__gsTest 桥
        # 没有 openProject；fetch 被拦时本场景无法执行，skip 注明原因
        report.record("project", "skip",
                      "CDP 直写通道无 .tgp 打开入口（线上 fetch localhost 被 "
                      "PNA/LNA 拦截）")
        return 0

    gpu_ok = webgpu_available(browser, page_url)
    gpu_blocked = set(WASM_UNSUPPORTED_MODULES) | set(MP_STUB_MODULES)
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
            graph_url = open_prefix + "/e2e/projects/" + tgp.name
            ok, failed = url_graph_case(
                browser, page_url, graph_url,
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


SCENARIOS = ["boot", "core", "files", "run", "twice", "project", "models"]


# ---------- 模型按需延迟加载（部署包 models/ 经 ModelFinder 按需拉取） ----------

# face/matting 的 mnn 后端默认模型名（submodule 源里的 kDefaultMnn* 常量；
# 参数留空即用默认名，正好走 ModelFinder → __gsModelLoad 按需下载链路：
# MEMFS /models miss → manifest 命中 → fetch/cache → MNN 从 MEMFS 加载）。
# 图内联（submodules/face、matting 没有 tests/graphs 夹具），输入图用宿主
# tests/models/mediapipe/portrait.jpg（有人脸/人像，结果是否有目标不影响
# COMPLETED 判定）。face_detect 链 ultraface+landmark 两模型，matting 链
# modnet.mnn（13MB，首次下载最重）。注意 reader 类型名是 opencv_image_read
#（写错会被 DAGSerializer 用 LambdaNode 占位静默顶替——占位"COMPLETED"
# 但输出空 any，下游 face/matting 报 missing image input，且占位节点走
# LambdaNode 包装日志（task.cpp 的 Starting execution），实测踩过）。
# 旧版启动预取构建上 MEMFS 直接命中，本场景同样 pass——驱动对新旧加载
# 行为都兼容，按需行为差异看 console 的"[gs] 模型下载/缓存回填"日志
#（report 快照可见）。
_MODEL_GRAPH_TEMPLATES = {
    "face_detect": {
        "version": "2.0",
        "tasks": [
            {"id": "read", "type": "opencv_image_read",
             "params": {"file_path": "portrait.jpg"}},
            {"id": "face", "type": "face_detect", "params": {}},
        ],
        "edges": [{"from": "read", "from_port": "out",
                   "to": "face", "to_port": "in"}],
    },
    "matting": {
        "version": "2.0",
        "tasks": [
            {"id": "read", "type": "opencv_image_read",
             "params": {"file_path": "portrait.jpg"}},
            {"id": "mat", "type": "matting", "params": {}},
        ],
        "edges": [{"from": "read", "from_port": "out",
                   "to": "mat", "to_port": "in"}],
    },
}


def models_run(browser: CdpBrowser, page_url: str, report, ctx: dict) -> int:
    """模型按需加载链路验证：face_detect / matting 内联图执行 0 failed。
    每张一个用例 models/graph:<name>。"""
    import shutil
    serve_root: Path = ctx["serve_root"]
    artifacts: Path = ctx["artifacts"]
    open_prefix = str(ctx.get("open_prefix") or "")
    channel = str(ctx.get("channel") or "url")

    portrait = (Path(__file__).resolve().parents[2]
                / "tests" / "models" / "mediapipe" / "portrait.jpg")
    if not portrait.is_file():
        report.record("models", "skip",
                      f"缺输入图 {portrait}（跑 download_mediapipe_models.py）")
        return 0

    ran = 0
    for name, graph in _MODEL_GRAPH_TEMPLATES.items():
        full = f"models/graph:{name}"
        report.begin(full)
        try:
            if channel == "cdp":
                per_root = serve_root / "cdp_stage" / f"models__{name}"
                per_root.mkdir(parents=True, exist_ok=True)
                graph_file = per_root / f"{name}.json"
                graph_file.write_text(json.dumps(graph), encoding="utf-8")
                shutil.copy2(portrait, per_root / "portrait.jpg")
                ok, failed = cdp_graph_case(
                    browser, page_url, graph_file, refs=["portrait.jpg"],
                    nodes=2, edges=1, timeout=180, artifacts=artifacts,
                    shot_name=f"model_{name}_failure.png", stage_root=per_root,
                    wait_models=True)
            else:
                model_dir = serve_root / "e2e" / "models"
                model_dir.mkdir(parents=True, exist_ok=True)
                graph_file = model_dir / f"{name}.json"
                graph_file.write_text(json.dumps(graph), encoding="utf-8")
                shutil.copy2(portrait, model_dir / "portrait.jpg")
                ok, failed = url_graph_case(
                    browser, page_url,
                    open_prefix + "/e2e/models/" + graph_file.name,
                    nodes=2, edges=1, timeout=180, artifacts=artifacts,
                    shot_name=f"model_{name}_failure.png")
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f"ok={ok} failed={failed}")
        except Exception as ex:
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          artifacts=list(getattr(ex, "artifacts", []) or []),
                          exc=ex, app_state=getattr(ex, "snapshot", None) or {})
        ran += 1
    return ran
