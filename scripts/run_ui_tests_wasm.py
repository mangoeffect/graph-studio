#!/usr/bin/env python3
"""run_ui_tests_wasm.py — 在真实浏览器中运行 GraphStudio WASM 逻辑层 QTest。

三套纯逻辑测试（test_graph_view_model / test_command_stack / test_integration）
编译为 wasm（见 app/graph_studio/CMakeLists.txt 末尾 EMSCRIPTEN 测试块）后，
由本脚本启动带 COOP/COEP 的本地 server + 有头 Chrome 加载，收集 QTest 输出。

为什么不是 ctest / headless：wasm 侧没有 ctest；QTest 输出经 emscripten 打到
浏览器 console，故页面向 server 回传 console 缓冲。多线程 WASM（Qt
wasm_multithread）必须 cross-origin isolation（SharedArrayBuffer），headless
Chrome 不支持 —— 必须有头 Chrome（GUI 窗口会一闪而过，属正常现象）。

工作方式:
  1) 缺构建树时委托 run_graph_studio_wasm.py --build-only（全量工具链搭建）
  2) 用 host cmake 增量构建三个测试目标（同一 build_wasm 树）
  3) 向生成的 test_*.html 注入 console 捕获钩子（幂等）：缓冲所有 console 行，
     见到 QTest "Totals:" 行后 POST 回 server
  4) 启动本地 server（GET 静态 + POST /__gs_qtest_report__/<name> 回传）
  5) 启动有头 Chrome 一次性打开所有测试页，等待各 suite 回传或超时
  6) 解析 "Totals: X passed, Y failed" 得出退出码

用法:
  python scripts/run_ui_tests_wasm.py                       # 全流程
  python scripts/run_ui_tests_wasm.py --no-build            # 跳过第 2 步
  python scripts/run_ui_tests_wasm.py --filter command      # 只跑匹配的 suite
  python scripts/run_ui_tests_wasm.py --chrome /path/to/Google Chrome
  python scripts/run_ui_tests_wasm.py --timeout 300         # 每套件超时秒数
"""

import argparse
import functools
import http.server
import os
import re
import shutil
import socket
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, platform, repo_root, runner, toolchain  # noqa: E402

TEST_TARGETS = ["test_graph_view_model", "test_command_stack", "test_integration"]
REPORT_PREFIX = "/__gs_qtest_report__/"

# 注入到测试页的 console 捕获钩子。
# 必须在 qtloader 之前生效：head 内联脚本先于 app 启动执行；
# emscripten 运行期才查找 console.log，故运行中的重定向对它生效。
REPORT_HOOK = """<script>window.__gsQTestHook=true;(function(){
var lines=[];var orig={};
['log','warn','error','info','debug'].forEach(function(m){
  orig[m]=console[m]?console[m].bind(console):function(){};
  console[m]=function(){var a=[].slice.call(arguments).map(function(x){
    try{return String(x);}catch(e){return '[unstringifiable]';}});
    lines.push(a.join(' '));orig[m].apply(null,arguments);};
});
window.addEventListener('error',function(e){lines.push('pageerror: '+e.message);});
window.addEventListener('unhandledrejection',function(e){lines.push('unhandledrejection: '+e.reason);});
function report(){
  if(window.__gsQTestReported)return;window.__gsQTestReported=true;
  try{fetch('%REPORT_PREFIX%%NAME%',{method:'POST',body:lines.join('\\n')});}catch(e){}
}
var timer=setInterval(function(){
  if(/Totals:\\s*\\d+ passed/.test(lines.join('\\n'))){
    clearInterval(timer);setTimeout(report,300);
  }
},300);
setTimeout(function(){if(!window.__gsQTestReported)report();},%MAX_MS%);})();
</script>"""

# 平台默认 Chrome 位置（有头，禁 headless —— 多线程 wasm 必须 COI）
CHROME_CANDIDATES = {
    "darwin": [
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    ],
    "linux": [
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
    ],
}


def find_chrome(explicit: str) -> Path:
    if explicit:
        p = Path(explicit).expanduser()
        if p.is_file():
            return p
        console.fail(f"--chrome 指定的浏览器不存在: {p}")
    for cand in CHROME_CANDIDATES.get(sys.platform, []):
        p = Path(cand)
        if p.is_file():
            return p
    which = shutil.which("google-chrome") or shutil.which("chromium")
    if which:
        return Path(which)
    console.fail(
        "找不到 Chrome/Chromium。多线程 WASM 需要有头浏览器（COI），"
        "请用 --chrome 指定路径。"
    )


def port_open(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.3)
        return s.connect_ex(("127.0.0.1", port)) == 0


class TestRequestHandler(http.server.SimpleHTTPRequestHandler):
    """GET：静态文件（COOP/COEP）；POST：测试页回传 console 缓冲。"""

    def __init__(self, *args, reports=None, **kwargs):
        self._reports = reports if reports is not None else {}
        super().__init__(*args, **kwargs)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_POST(self):
        if not self.path.startswith(REPORT_PREFIX):
            self.send_response(404)
            self.end_headers()
            return
        name = self.path[len(REPORT_PREFIX):]
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", "replace")
        self._reports[name] = body
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, fmt, *args):  # 静默访问日志
        pass


def parse_totals(output: str):
    m = None
    for m in re.finditer(r"Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped", output):
        pass
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def patch_test_html(html_path: Path, name: str, max_ms: int):
    html = html_path.read_text(encoding="utf-8")
    if "__gsQTestHook" in html:
        return
    hook = (REPORT_HOOK
            .replace("%REPORT_PREFIX%", REPORT_PREFIX)
            .replace("%NAME%", name)
            .replace("%MAX_MS%", str(max_ms)))
    if "</head>" in html:
        html = html.replace("</head>", hook + "</head>", 1)
    else:
        html = hook + html
    html_path.write_text(html, encoding="utf-8")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="浏览器中运行 GraphStudio WASM 逻辑 QTest")
    ap.add_argument("--no-build", action="store_true", help="跳过测试目标构建")
    ap.add_argument("--skip-app-build", action="store_true",
                    help="缺构建树时也不委托 run_graph_studio_wasm.py")
    ap.add_argument("--filter", default="", help="只跑名字含该子串的 suite")
    ap.add_argument("--chrome", default=os.environ.get("GS_CHROME", ""),
                    help="Chrome/Chromium 可执行文件路径（必须有头）")
    ap.add_argument("--port", type=int, default=8123, help="本地 server 端口")
    ap.add_argument("--timeout", type=int, default=240, help="每套件等待秒数")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数")
    args = ap.parse_args()

    root = repo_root()
    gs_build = root / "app" / "graph_studio" / "build_wasm"

    targets = [t for t in TEST_TARGETS if args.filter in t] or TEST_TARGETS

    # 1) 缺构建树 → 委托全量 wasm 构建（emsdk/Qt 工具链都在那里面）
    if not (gs_build / "graph_studio.html").is_file():
        if args.skip_app_build:
            console.fail(f"构建树不存在: {gs_build}（先跑 run_graph_studio_wasm.py --build-only）")
            return 1
        console.step("缺 WASM 构建树，委托 run_graph_studio_wasm.py --build-only")
        code = runner.check(
            [sys.executable, str(root / "scripts" / "run_graph_studio_wasm.py"), "--build-only"],
            cwd=str(root), what="构建 WASM GraphStudio")
        if code != 0:
            return code

    # 2) 增量构建测试目标（同一 build_wasm 树，host cmake 即可）
    if not args.no_build:
        console.step(f"构建 WASM 测试目标: {' '.join(targets)}")
        cmake = toolchain.find_cmake(build_dir=gs_build)
        if not cmake:
            console.fail("找不到 cmake")
            return 1
        jobs = args.jobs or platform.cpu_count()
        for t in targets:
            code = runner.check([str(cmake), "--build", str(gs_build),
                                 "--target", t, "-j", str(jobs)],
                                cwd=str(root), what=f"构建 {t}")
            if code != 0:
                return code

    # 3) 注入 console 捕获钩子（幂等）
    hook_ms = max(args.timeout, 60) * 1000  # 页内兜底上报 ≥ runner 超时
    pages = []
    for t in targets:
        html = gs_build / f"{t}.html"
        if not html.is_file():
            console.fail(f"未找到 {html}（目标未构建？）")
            return 1
        patch_test_html(html, t, hook_ms)
        pages.append((t, html))
        console.ok(f"钩子就绪: {html.name}")

    # 4) 本地 server（COOP/COEP + POST 回传）
    if port_open(args.port):
        console.fail(f"端口 {args.port} 已被占用（另一个实例在跑？换 --port）")
        return 1
    reports: dict = {}
    handler_cls = functools.partial(TestRequestHandler, reports=reports,
                                    directory=str(gs_build))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler_cls)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    console.ok(f"dev server: http://localhost:{args.port}/ (COOP+COEP)")

    # 5) 有头 Chrome 一次性打开所有测试页。后台线程阻塞式启动（浏览器是
    # 长驻进程，等它退出没有意义）；唯一 user-data-dir 兼作清理定位标识。
    chrome = find_chrome(args.chrome)
    user_data = tempfile.mkdtemp(prefix="gs_wasm_tests_")
    base = f"http://localhost:{args.port}"
    chrome_cmd = [str(chrome), f"--user-data-dir={user_data}", "--no-first-run",
                  "--no-default-browser-check", "--disable-sync",
                  "--disable-features=Translate"]
    chrome_cmd += [f"{base}/{t}.html" for t, _ in pages]
    console.step(f"启动浏览器: {chrome.name}（有头，多线程 WASM 必须 COI）")
    threading.Thread(target=runner.run, args=(chrome_cmd,), daemon=True).start()

    # 6) 等待各 suite 回传
    pending = {t: float(args.timeout) for t, _ in pages}
    try:
        while pending:
            for t in list(pending):
                if t in reports:
                    del pending[t]
            if not pending:
                break
            time.sleep(0.5)
            for t in list(pending):
                pending[t] -= 0.5
                if pending[t] <= 0:
                    console.warn(f"{t}: 超时（{args.timeout}s）未见 Totals 行")
                    reports.setdefault(t, "")
                    del pending[t]
    finally:
        server.shutdown()
        # 按 user-data-dir 唯一标识清理本次启动的浏览器实例（忽略未命中）
        runner.run(["pkill", "-f", user_data])
        shutil.rmtree(user_data, ignore_errors=True)

    # 7) 解析结果
    print()
    all_ok = True
    failed_suites = []
    for t, _ in pages:
        out = reports.get(t, "")
        totals = parse_totals(out)
        finished = "Finished testing" in out
        if totals and totals[1] == 0 and finished:
            console.ok(f"{t}: {totals[0]} passed, 0 failed")
        else:
            all_ok = False
            failed_suites.append(t)
            tail = out.strip().splitlines()[-25:]
            console.fail(f"{t}: 失败或异常退出（totals={totals} finished={finished}）")
            for line in tail:
                print(f"    {line}")
    print()
    if all_ok:
        console.ok(f"全部 {len(pages)} 套件通过")
        return 0
    console.fail(f"失败套件: {', '.join(failed_suites)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
