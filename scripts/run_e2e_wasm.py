#!/usr/bin/env python3
"""run_e2e_wasm.py — GraphStudio WASM 浏览器端 E2E（对打包产物/构建树）。

与 run_ui_tests_wasm.py 的分工：那边是"逻辑层 QTest 在浏览器里跑"；
本脚本对**真实应用**（graph_studio.wasm）做端到端黑盒——真实 Chrome、
真实鼠标事件、console 断言。

图输入走 URL 启动参数（app 侧 ?open=<url>&run=1，对齐桌面 --open/--run 与
macOS E2E 的 CLI 冷启动）：图与相对资产落位到运行目录经本地 server 的
/e2e/ 前缀供 app fetch，每张图一个新 tab（进程级隔离），完成断言读 console
的 [gs] 镜像日志。

前置:
  - app/graph_studio/build_wasm/graph_studio.html（缺则先跑
    run_graph_studio_wasm.py --build-only），或 --from-zip 指定发布 zip
  - 本机 Chrome（必须有头；多线程 WASM 需要 COI，headless 不支持）

用法:
  python scripts/run_e2e_wasm.py                        # 全部场景（files 全部可跑图）
  python scripts/run_e2e_wasm.py --scenario boot,run    # 选场景
  python scripts/run_e2e_wasm.py --scenario files --max-graphs 5
  python scripts/run_e2e_wasm.py --scenario files --graphs opencv
  python scripts/run_e2e_wasm.py --from-zip dist/web/GraphStudio-*-web.zip
  python scripts/run_e2e_wasm.py --chrome /path/to/chrome --port 8200
"""

import argparse
import functools
import http.server
import os
import shutil
import sys
import threading
import time
import urllib.parse
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from e2e_report import Report  # noqa: E402
from e2e_wasm import scenarios as sc  # noqa: E402
from e2e_wasm.cdp_browser import CdpBrowser  # noqa: E402
from e2e_wasm.scenarios import SCENARIOS, ScenarioError  # noqa: E402
from run_ui_tests_wasm import find_chrome  # noqa: E402


def _tiny_png(width: int = 64, height: int = 64) -> bytes:
    """零依赖生成确定性 RGB PNG（smoke 图的执行输入）。"""
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
    """静态文件（COOP/COEP）+ /e2e/ 前缀的落位目录（图与资产副本）。"""

    stage_dir = ""   # 经 functools.partial 注入

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def _serve_staged(self):
        root = Path(self.stage_dir).resolve()
        rel = urllib.parse.unquote(
            self.path.split("?", 1)[0].split("#", 1)[0][len("/e2e/"):])
        target = (root / rel).resolve()
        if (not str(target).startswith(str(root) + os.sep)
                and str(target) != str(root)) or not target.is_file():
            self.send_error(404)
            return
        body = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(str(target)))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/e2e/"):
            return self._serve_staged()
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


def aggregate(report: Report, scenario: str):
    """场景级状态聚合自子用例（files/graph:xxx…），对齐 macOS 入口。"""
    inner = [r for r in report.results if r.name.startswith(scenario + "/")]
    failed = [r for r in inner if r.status == "fail"]
    passed = [r for r in inner if r.status == "pass"]
    if failed:
        report.record(scenario, "fail", f"{len(failed)}/{len(inner)} 子用例失败")
    else:
        report.record(scenario, "pass",
                      f"{len(passed)} 个子用例通过" if inner else "")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio WASM 浏览器 E2E")
    ap.add_argument("--scenario", default="all",
                    help=f"{','.join(SCENARIOS)} 组合（逗号分隔），默认 all")
    ap.add_argument("--from-zip", default="",
                    help="直接测发布 zip（GraphStudio-*-web.zip）")
    ap.add_argument("--chrome", default=os.environ.get("GS_CHROME", ""),
                    help="Chrome 可执行文件路径（必须有头）")
    ap.add_argument("--port", type=int, default=8200, help="本地 server 端口")
    ap.add_argument("--cdp-port", type=int, default=9333, help="CDP 端口")
    ap.add_argument("--artifacts", default="",
                    help="report 输出根目录（默认 <repo>/dist/e2e_wasm）")
    ap.add_argument("--max-graphs", type=int, default=0,
                    help="files 场景纳入的子模块图数量上限（默认 0=全部，"
                         "read_image/unicode 优先）")
    ap.add_argument("--graphs", default="",
                    help="files 场景按路径子串过滤子模块图（如 opencv、js_task）")
    args = ap.parse_args()

    root = repo_root()
    content_dir = prepare_content(args, root)
    scenarios = list(SCENARIOS) if args.scenario == "all" else \
        [s.strip() for s in args.scenario.split(",") if s.strip()]
    for s in scenarios:
        if s not in SCENARIOS:
            console.fail(f"未知场景: {s}（可选 {', '.join(SCENARIOS)}）")
            return 1

    report = Report(Path(args.artifacts) if args.artifacts
                    else root / "dist" / "e2e_wasm",
                    title="GraphStudio WASM E2E 报告")
    report.meta = {"content": str(content_dir), "from_zip": args.from_zip or "",
                   "scenarios": ",".join(scenarios),
                   "max_graphs": args.max_graphs or "all",
                   "graphs_filter": args.graphs or ""}
    console.step(f"E2E 运行目录: {report.run_dir}")

    # 落位目录（/e2e/ 前缀 serve）：smoke 图 + files 的图与资产副本
    serve_root = report.run_dir / "serve"
    smoke_dir = serve_root / "smoke"
    smoke_dir.mkdir(parents=True, exist_ok=True)
    (smoke_dir / "input.png").write_bytes(_tiny_png())
    (smoke_dir / "e2e_graph.json").write_text(
        '{\n  "version": "2.0",\n  "tasks": [\n    { "id": "img", '
        '"type": "opencv_image_read",\n      "params": { "file_path": '
        '"input.png" } }\n  ],\n  "edges": [],\n  "positions": '
        '{ "img": { "x": 0, "y": 0, "type": "opencv_image_read" } }\n}\n')

    # stage_dir 不能经 functools.partial 传（BaseRequestHandler 不认多余
    # kwarg），用动态子类注入类属性；directory 是 SimpleHTTPRequestHandler
    # 支持的官方参数
    handler_cls = type("BoundE2eHandler", (E2eRequestHandler,),
                       {"stage_dir": str(serve_root)})
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", args.port),
        functools.partial(handler_cls, directory=str(content_dir)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base_url = f"http://localhost:{args.port}"
    console.ok(f"dev server: {base_url}/graph_studio.html (COOP+COEP, /e2e/ 落位)")

    chrome = find_chrome(args.chrome)
    browser = CdpBrowser(chrome, cdp_port=args.cdp_port)
    console.step(f"启动浏览器: {chrome.name}（有头 + CDP :{args.cdp_port}）")
    browser.start()

    ctx = {"browser": browser, "base_url": base_url, "report": report,
           "serve_root": serve_root, "artifacts": report.run_dir,
           "max_graphs": args.max_graphs, "graphs_filter": args.graphs}
    results = {}
    try:
        for name in scenarios:
            console.step(f"场景 {name}")
            report.begin(name)
            try:
                if name in ("boot", "core"):
                    tab = browser.new_tab(f"{base_url}/graph_studio.html")
                    try:
                        detail = (sc.boot(tab, report.run_dir) if name == "boot"
                                  else sc.core(tab, report.run_dir))
                    finally:
                        tab.close()
                        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔
                    results[name] = detail
                elif name == "files":
                    count = sc.files_run(browser, base_url, report, ctx)
                    aggregate(report, "files")
                    console.step(f"files: 执行 {count} 张图")
                    results[name] = f"files: {count} 张图"
                elif name == "project":
                    count = sc.project_run(browser, base_url, report, ctx)
                    aggregate(report, "project")
                    console.step(f"project: 执行 {count} 个工程包")
                    results[name] = f"project: {count} 个工程包"
                elif name == "run":
                    results[name] = sc.run_graph(browser, base_url, ctx)
                report.record(name, "pass", str(results[name]))
                console.ok(results[name])
            except (ScenarioError, TimeoutError, RuntimeError) as e:
                results[name] = None
                report.record(name, "fail", f"{type(e).__name__}: {e}", exc=e,
                              app_state=getattr(e, "snapshot", None) or {})
                console.fail(f"场景 {name} 失败: {e}")
                tail = getattr(e, "snapshot", {}).get("console_tail", "")
                for line in (tail or "").strip().splitlines()[-15:]:
                    print(f"    {line}")
    finally:
        browser.stop()
        server.shutdown()

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
