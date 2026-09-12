#!/usr/bin/env python3
"""run_e2e_wasm.py — GraphStudio WASM 浏览器端 E2E（对打包产物/构建树）。

与 run_ui_tests_wasm.py 的分工：那边是"逻辑层 QTest 在浏览器里跑"；
本脚本对**真实应用**（graph_studio.wasm）做端到端黑盒——真实 Chrome、
真实鼠标事件、console 断言，覆盖 boot / 编辑 core / 执行 run 三类场景。

前置:
  - app/graph_studio/build_wasm/graph_studio.html（缺则先跑
    run_graph_studio_wasm.py --build-only），或 --from-zip 指定发布 zip
  - 本机 Chrome（必须有头；多线程 WASM 需要 COI，headless 不支持）

用法:
  python scripts/run_e2e_wasm.py                        # 全部场景
  python scripts/run_e2e_wasm.py --scenario boot,run    # 选场景
  python scripts/run_e2e_wasm.py --from-zip dist/web/GraphStudio-*-web.zip
  python scripts/run_e2e_wasm.py --chrome /path/to/chrome --port 8200
"""

import argparse
import functools
import http.server
import io
import os
import shutil
import sys
import threading
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from e2e_wasm.cdp_browser import CdpBrowser  # noqa: E402
from e2e_wasm.scenarios import SCENARIOS, ScenarioError  # noqa: E402
from run_ui_tests_wasm import CHROME_CANDIDATES, find_chrome  # noqa: E402


def _tiny_png(width: int = 64, height: int = 64) -> bytes:
    """零依赖生成确定性 RGB PNG（run 场景的执行输入图）。"""
    import struct
    import zlib
    raw = b""
    for y in range(height):
        raw += b"\x00"
        for x in range(width):
            raw += bytes(((x * 4) % 256, (y * 4) % 256, 128))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class E2eRequestHandler(http.server.SimpleHTTPRequestHandler):
    """静态文件（COOP/COEP）+ 内嵌 /e2e_test.png。"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        if self.path == "/e2e_test.png":
            body = _tiny_png()
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def log_message(self, fmt, *args):
        pass


def prepare_content(args, root: Path) -> Path:
    """返回要 serve 的目录：dev 构建树，或解包的发布 zip。"""
    if args.from_zip:
        out = Path(args.from_zip).resolve().parent / f"{Path(args.from_zip).stem}-e2e"
        if out.is_dir():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        console.step(f"解包 {args.from_zip} -> {out}")
        with zipfile.ZipFile(args.from_zip) as zf:
            for member in zf.namelist():
                # 防路径穿越：拒绝绝对路径与 .. 上跳
                target = (out / member).resolve()
                if not str(target).startswith(str(out.resolve()) + os.sep):
                    raise ValueError(f"zip 内非法路径: {member}")
            zf.extractall(out)
        # zip 根即 index.html 所在层
        return out
    gs_build = root / "app" / "graph_studio" / "build_wasm"
    if not (gs_build / "graph_studio.html").is_file():
        console.fail("未找到 WASM 构建产物，先跑 "
                     "scripts/run_graph_studio_wasm.py --build-only")
        raise SystemExit(1)
    return gs_build


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio WASM 浏览器 E2E")
    ap.add_argument("--scenario", default="all",
                    help="boot,core,run 之一或组合（逗号分隔），默认 all")
    ap.add_argument("--from-zip", default="",
                    help="直接测发布 zip（GraphStudio-*-web.zip）")
    ap.add_argument("--chrome", default=os_environ_chrome(),
                    help="Chrome 可执行文件路径（必须有头）")
    ap.add_argument("--port", type=int, default=8200, help="本地 server 端口")
    ap.add_argument("--cdp-port", type=int, default=9333, help="CDP 端口")
    ap.add_argument("--artifacts", default="",
                    help="截图输出目录（默认 /tmp/gs_e2e_wasm）")
    args = ap.parse_args()

    root = repo_root()
    content_dir = prepare_content(args, root)
    scenarios = list(SCENARIOS) if args.scenario == "all" else \
        [s.strip() for s in args.scenario.split(",") if s.strip()]
    for s in scenarios:
        if s not in SCENARIOS:
            console.fail(f"未知场景: {s}（可选 {', '.join(SCENARIOS)}）")
            return 1

    artifacts = Path(args.artifacts) if args.artifacts \
        else Path("/tmp/gs_e2e_wasm")
    artifacts.mkdir(parents=True, exist_ok=True)

    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", args.port),
        functools.partial(E2eRequestHandler, directory=str(content_dir)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base_url = f"http://localhost:{args.port}"
    console.ok(f"dev server: {base_url}/graph_studio.html (COOP+COEP)")

    chrome = find_chrome(args.chrome)
    browser = CdpBrowser(chrome, cdp_port=args.cdp_port)
    console.step(f"启动浏览器: {chrome.name}（有头 + CDP :{args.cdp_port}）")
    browser.start()

    results = {}
    try:
        for name in scenarios:
            console.step(f"场景 {name}")
            tab = browser.new_tab(f"{base_url}/graph_studio.html")
            try:
                if name == "run":
                    results[name] = SCENARIOS[name](tab, artifacts, base_url)
                else:
                    results[name] = SCENARIOS[name](tab, artifacts)
                console.ok(results[name])
            except (ScenarioError, TimeoutError, RuntimeError) as e:
                shot = artifacts / f"{name}_fail.png"
                try:
                    tab.screenshot(shot)
                except Exception:
                    pass
                console.fail(f"场景 {name} 失败: {e}")
                results[name] = None
                tail = tab.console_text().strip().splitlines()[-15:]
                for line in tail:
                    print(f"    {line}")
            finally:
                tab.close()
                time.sleep(0.5)  # 给上一个实例释放 worker 的间隔
    finally:
        browser.stop()
        server.shutdown()

    print()
    failed = [k for k, v in results.items() if v is None]
    if failed:
        console.fail(f"E2E 失败场景: {', '.join(failed)}（截图在 {artifacts}）")
        return 1
    console.ok(f"E2E 全部通过（{len(results)} 场景，截图在 {artifacts}）")
    return 0


def os_environ_chrome() -> str:
    import os
    return os.environ.get("GS_CHROME", "")


if __name__ == "__main__":
    sys.exit(main())
