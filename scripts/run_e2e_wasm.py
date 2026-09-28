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
  python scripts/run_e2e_wasm.py --url https://studio.mangoeffect.net/web
  python scripts/run_e2e_wasm.py --chrome /path/to/chrome --port 8200

--url 直接测线上已部署版本（页面换成线上 graph_studio.html，本机仍起
图 server 供 ?open= 跨源拉图）：对发布链路的 coi-serviceworker COI、
打包模型预取等"只在部署形态才成立"的东西是真验收。
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
    """静态文件（COOP/COEP）+ /e2e/ 前缀的落位目录（图与资产副本）。

    CORS 头（ACAO + CORP）为 --url 线上模式服务：页面在线上 origin，图在
    本 server，?open= 的 fetch 是跨源请求——fetch 默认 mode=cors 需 ACAO，
    COEP require-corp 页面下再有 CORP cross-origin 双保险。本地同源模式
    多这两个头无害。"""

    stage_dir = ""   # 经 functools.partial 注入

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        super().end_headers()

    def do_OPTIONS(self):
        """PNA/LNA preflight（--url 线上模式）：https 页面（public 地址空间）
        fetch 本 server（local 地址空间）时，Chrome 先发 OPTIONS 且带
        `Access-Control-Request-Private-Network: true`，响应必须显式放行，
        否则请求整体 ERR_FAILED——与普通 CORS 头无关的独立检查。204 无体。"""
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Max-Age", "86400")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.end_headers()

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

    def handle(self):
        # 浏览器对 PNA/LNA 拒绝的请求会直接 RST 连接，ThreadingHTTPServer
        # 的 handler 线程异常打印到 stderr 很吵，静默吞掉
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def log_message(self, fmt, *args):
        pass


def prepare_content(args, root: Path) -> Path | None:
    """返回要 serve 的目录：dev 构建树，或解包的发布 zip；--url 线上
    模式返回 None（页面从线上来，本地 server 只剩 /e2e/ 图落位）。"""
    if args.url:
        return None
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


def normalize_page_url(raw: str) -> str:
    """--url 归一化为 graph_studio.html 完整页面 URL：以 .html 结尾原样
    使用，否则视为 base 目录追加 /graph_studio.html。"""
    raw = raw.strip().rstrip("/")
    if raw.lower().endswith(".html"):
        return raw
    return f"{raw}/graph_studio.html"


def probe_page_url(page_url: str, timeout: float = 20.0):
    """启动前确认线上页面可达（网络封锁/打错 URL 时给出可读错误，而不是
    每个场景超时后一片 fail）。重定向跟随，2xx/3xx 视为可达。"""
    import urllib.request
    try:
        with urllib.request.urlopen(page_url, timeout=timeout) as resp:
            status = getattr(resp, "status", 200)
    except Exception as e:  # noqa: BLE001 — 任何网络错误都归为不可达
        console.fail(f"线上页面不可达: {page_url}（{e}）——检查 URL / 网络")
        raise SystemExit(1) from e
    if status >= 400:
        console.fail(f"线上页面返回 HTTP {status}: {page_url}")
        raise SystemExit(1)


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
    ap.add_argument("--url", default="",
                    help="直接测线上已部署版本（graph_studio.html 的 base "
                         "URL 或完整页面 URL，如 "
                         "https://studio.mangoeffect.net/web）")
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
                    help="files 场景按路径子串过滤子模块图（如 opencv、render）")
    args = ap.parse_args()
    if args.url and args.from_zip:
        console.fail("--url（线上版本）与 --from-zip（发布 zip）互斥")
        return 1
    if args.url and not args.url.lower().startswith(("http://", "https://")):
        console.fail(f"--url 需要 http(s) URL，收到: {args.url}")
        return 1

    root = repo_root()
    # app 页面来源：--url 线上部署版（先预检可达），否则本地构建树/zip
    page_url = ""
    if args.url:
        page_url = normalize_page_url(args.url)
        console.step(f"线上模式: {page_url}")
        probe_page_url(page_url)
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
                   "url": args.url or "", "page_url": page_url,
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
    # 支持的官方参数。线上模式页面从线上来，静态部分指向空目录（/e2e/ 前
    # 缀不受 directory 影响，图落位照常）。
    if content_dir is None:
        content_dir = serve_root / "_no_local_app"
        content_dir.mkdir(parents=True, exist_ok=True)
    handler_cls = type("BoundE2eHandler", (E2eRequestHandler,),
                       {"stage_dir": str(serve_root)})
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", args.port),
        functools.partial(handler_cls, directory=str(content_dir)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    asset_origin = f"http://localhost:{args.port}"
    if page_url:
        console.ok(f"dev server（图落位，CORS 放行）: {asset_origin}/e2e/；"
                   f"页面: {page_url}")
    else:
        page_url = f"{asset_origin}/graph_studio.html"
        console.ok(f"dev server: {page_url} (COOP+COEP, /e2e/ 落位)")

    # 线上模式：页面在 https origin，?open= 拉图是 public→localhost 请求，
    # 绕过 Chrome 的 Private Network Access / Local Network Access 检查
    #（LNA 权限弹窗会挂起 fetch 直到人工响应，E2E 无人值守）；feature 名
    # 不存在时 Chrome 静默忽略。
    # 两枚防干扰 flag 恒加：长跑（线上全量 >30min）期间 Chrome 的组件
    # 更新/后台网络可能重启浏览器杀掉 CDP 连接（2026-09-28 全量跑实踩）。
    chrome_flags = [
        "--disable-background-networking",
        "--disable-component-update",
    ]
    if args.url:
        chrome_flags.append(
            "--disable-features="
            "BlockInsecurePrivateNetworkRequests,LocalNetworkAccessCheck")
    chrome = find_chrome(args.chrome)
    if chrome is None:
        return 1  # find_chrome 已打印原因（--chrome 路径错 / 无候选）
    browser = CdpBrowser(chrome, cdp_port=args.cdp_port, extra_args=chrome_flags)
    console.step(f"启动浏览器: {chrome.name}（有头 + CDP :{args.cdp_port}）")
    browser.start()

    ctx = {"browser": browser, "page_url": page_url, "report": report,
           "serve_root": serve_root, "artifacts": report.run_dir,
           # --url 模式 ?open= 需要指向本地图 server 的绝对 URL；本地模式
           # 页面与图同源，空前缀即相对路径
           "open_prefix": asset_origin if args.url else "",
           "max_graphs": args.max_graphs, "graphs_filter": args.graphs,
           # 图输入通道：url = ?open fetch（真实分享链接路径）；cdp = 图与
           # 资产字节经 DevTools 直写 MEMFS（--url 线上 https 页面拉
           # localhost 会被 Chrome PNA/LNA 拦截时的降级）
           "channel": "url"}

    if args.url:
        probe = (asset_origin + "/e2e/smoke/e2e_graph.json")
        if sc.fetch_channel_available(browser, page_url, probe):
            console.ok("图输入通道: URL fetch（?open 跨源放行）")
        else:
            ctx["channel"] = "cdp"
            console.step("图输入通道: CDP 直写（?open fetch localhost 被 "
                         "Chrome PNA/LNA 拦截，已自动降级）")
        report.meta["channel"] = ctx["channel"]
    results = {}
    try:
        for name in scenarios:
            console.step(f"场景 {name}")
            report.begin(name)
            try:
                if name in ("boot", "core"):
                    tab = browser.new_tab(page_url)
                    try:
                        detail = (sc.boot(tab, report.run_dir) if name == "boot"
                                  else sc.core(tab, report.run_dir))
                    finally:
                        tab.close()
                        time.sleep(0.5)  # 给上一个实例释放 worker 的间隔
                    results[name] = detail
                elif name == "files":
                    count = sc.files_run(browser, page_url, report, ctx)
                    aggregate(report, "files")
                    console.step(f"files: 执行 {count} 张图")
                    results[name] = f"files: {count} 张图"
                elif name == "project":
                    count = sc.project_run(browser, page_url, report, ctx)
                    aggregate(report, "project")
                    console.step(f"project: 执行 {count} 个工程包")
                    results[name] = f"project: {count} 个工程包"
                elif name == "models":
                    count = sc.models_run(browser, page_url, report, ctx)
                    aggregate(report, "models")
                    console.step(f"models: 执行 {count} 张模型图")
                    results[name] = f"models: {count} 张图"
                elif name == "run":
                    results[name] = sc.run_graph(browser, page_url, ctx)
                elif name == "twice":
                    results[name] = sc.twice(browser, page_url, ctx)
                # 聚合型场景（files/project/models）的状态已由 aggregate 按
                # 子用例写出，这里不再无条件记 pass——否则子用例失败时
                # 场景级会同时存在 fail 与 pass 两条记录，计数虚高
                if name not in ("files", "project", "models") and results[name]:
                    report.record(name, "pass", str(results[name]))
                if results[name]:
                    console.ok(str(results[name]))
            except OSError as e:
                # 浏览器/CDP 连接中断（Chrome 崩溃、被外部杀或自动更新重启，
                # DevTools 端口拒连）：后续场景全部无法执行，记 fail 并终止
                # 循环——保证 report.finish() 仍写出 summary（2026-09-28
                # 线上全量跑实踩：ConnectionRefusedError 未捕获直接 traceback，
                # 无 summary.json）。
                results[name] = None
                report.record(name, "fail",
                              f"{type(e).__name__}: {e}"
                              "（浏览器/CDP 连接中断，终止剩余场景）", exc=e)
                console.fail(f"场景 {name} 浏览器连接中断，终止剩余场景: {e}")
                break
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
