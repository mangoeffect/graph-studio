#!/usr/bin/env python3
"""
为 WASM 多线程构建提供 dev server，自动加 cross-origin isolation header + 压缩传输。
localhost 是 secure context，HTTP + COOP/COEP 即可启用 SharedArrayBuffer。
支持 brotli/gzip 预压缩文件（.br/.gz）：若请求的 .wasm/.js/.html 旁有同名 .br/.gz，
按 Accept-Encoding 优先返回压缩版，省网络带宽。
"""
import http.server, socketserver, sys, os

PORT = int(os.environ.get("PORT", "8000"))
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()

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

with ReuseTCPServer(("", PORT), COOPHandler) as httpd:
    print(f"WASM dev server (COOP+COEP enabled) serving {ROOT}")
    print(f"  http://localhost:{PORT}/graph_studio.html")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
