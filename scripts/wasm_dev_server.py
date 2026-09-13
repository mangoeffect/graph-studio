#!/usr/bin/env python3
"""
为 WASM 多线程构建提供 dev server，自动加 cross-origin isolation header + 压缩传输。
localhost 是 secure context，HTTP + COOP/COEP 即可启用 SharedArrayBuffer。
支持 brotli/gzip 预压缩文件（.br/.gz）：若请求的 .wasm/.js/.html 旁有同名 .br/.gz，
按 Accept-Encoding 优先返回压缩版，省网络带宽。
设 OPEN_URL 环境变量时，server 起来后自动用默认浏览器打开该地址
（run_graph_studio_wasm.py 传入，含 --no-browser 时不设）。
"""
import http.server, socketserver, sys, os, webbrowser

PORT = int(os.environ.get("PORT", "8000"))
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
OPEN_URL = os.environ.get("OPEN_URL", "")

class COOPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        # Cross-origin isolation：多线程 WASM（SharedArrayBuffer）必需
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        # 尝试 brotli / gzip 预压缩文件
        accept = self.headers.get("Accept-Encoding", "")
        path = self.translate_path(self.path)
        if os.path.isfile(path):
            if "br" in accept and os.path.isfile(path + ".br"):
                self._serve_compressed(path + ".br", "br")
                return
            if "gzip" in accept and os.path.isfile(path + ".gz"):
                self._serve_compressed(path + ".gz", "gzip")
                return
        super().do_GET()

    def _serve_compressed(self, compressed_path, encoding):
        stat = os.stat(compressed_path)
        self.send_response(200)
        ctype = self.guess_type(self.translate_path(self.path))
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Encoding", encoding)
        self.send_header("Content-Length", str(stat.st_size))
        self.end_headers()
        with open(compressed_path, "rb") as f:
            self.wfile.write(f.read())

class ReuseTCPServer(socketserver.TCPServer):
    allow_reuse_address = True

try:
    httpd = ReuseTCPServer(("", PORT), COOPHandler)
except OSError as e:
    # 常见于上次运行残留的 dev server 还占着端口
    print(f"错误: 端口 {PORT} 绑定失败（{e}）。"
          f"用 lsof -nP -iTCP:{PORT} -sTCP:LISTEN 查看占用进程，"
          f"或设 PORT 环境变量换端口。", file=sys.stderr)
    sys.exit(1)

with httpd:
    print(f"WASM dev server (COOP+COEP enabled) serving {ROOT}")
    print(f"  http://localhost:{PORT}/graph_studio.html")
    if OPEN_URL:
        # 此刻 socket 已 bind+listen（内核 accept 队列兜底），打开浏览器无竞态
        webbrowser.open(OPEN_URL)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
