# -*- coding: utf-8 -*-
"""scenarios.py — GraphStudio WASM 浏览器端 E2E 场景。

每个场景收到一个已连接目标页面的 CdpTab。断言走两条通道：
  - window.__gsTest 测试桥（test_hooks.cpp 安装：状态计数 / 图加载 / 动作触发）
  - console 中的 "[gs]" 日志（MainWindow::onLogMessage 的 qInfo 镜像）
画布交互走 CDP Input 域真实鼠标事件（Qt 画布内命中测试照常生效）。
"""

import time
import zlib
import struct
from pathlib import Path

from .cdp_browser import CdpTab

BOOT_TIMEOUT = 120  # wasm 编译 + Qt 启动在冷缓存时可能较慢


class ScenarioError(RuntimeError):
    pass


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


# ---------- run：加载真实可执行图（opencv_image_read）并执行 ----------

def _tiny_png(width: int = 64, height: int = 64) -> bytes:
    """零依赖生成确定性 RGB PNG（E2E 输入图，写入 wasm MEMFS）。"""
    raw = b""
    for y in range(height):
        raw += b"\x00"  # filter type 0
        for x in range(width):
            raw += bytes(((x * 4) % 256, (y * 4) % 256, 128))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def run_graph(tab: CdpTab, artifacts: Path, base_url: str) -> str:
    wait_boot(tab)
    graph_json = """{
  "version": "2.0",
  "tasks": [
    { "id": "img", "type": "opencv_image_read", "params": { "file_path": "/e2e_test.png" } }
  ],
  "edges": [],
  "positions": { "img": { "x": 0, "y": 0, "type": "opencv_image_read" } }
}"""
    # 1) 输入图写进 MEMFS。页面全局没有 Module（MODULARIZE 闭包内），
    # 必须经 __gsTest.fsWrite 桥接；FS.writeFile 只收 Uint8Array/string，
    # 不能转普通 Array。图片从 dev server 同源 fetch。
    fetch_js = (
        f"(async () => {{ const r = await fetch({base_url!r} + '/e2e_test.png');"
        " const b = new Uint8Array(await r.arrayBuffer());"
        " window.__gsTest.fsWrite('/e2e_test.png', b); return b.length; })()")
    n = tab.evaluate(fetch_js)
    if not n:
        raise ScenarioError("MEMFS 写入输入图失败")

    # 2) 加载 + 执行 + 等 console 完成日志（onLogMessage 的 qInfo 镜像）
    if not tab.evaluate(f"window.__gsTest.loadGraph({graph_json!r})"):
        raise ScenarioError("run: loadGraph 失败")
    deadline = time.time() + 10
    while tab.evaluate("window.__gsTest.taskCount()") != 1 and time.time() < deadline:
        time.sleep(0.2)
    assert tab.evaluate("window.__gsTest.action('execute')"), "execute 触发失败"
    done = tab.wait_console(r"\[gs\] Execution finished: 1 ok, 0 failed",
                            timeout=60)
    if tab.evaluate("window.__gsTest.executing()"):
        raise ScenarioError("完成后 executing 仍为 true")

    shot = artifacts / "run_final.png"
    tab.screenshot(shot)
    return f"run OK ({done.strip()}) [{shot.name}]"


SCENARIOS = {
    "boot": boot,
    "core": core,
    "run": run_graph,
}
