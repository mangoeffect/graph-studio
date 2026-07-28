#!/usr/bin/env python3
"""
为 WASM 多线程构建提供 dev server，自动加 cross-origin isolation header。
localhost 是 secure context，HTTP + COOP/COEP 即可启用 SharedArrayBuffer。
"""
import http.server, socketserver, sys, os, functools

PORT = int(os.environ.get("PORT", "8000"))
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()

class COOPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)
    def end_headers(self):
        # Cross-origin isolation：多线程 WASM（SharedArrayBuffer）必需
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # 压缩相关 hint（让浏览器优先用 wasm 的 brotli/gzip 版本）
        self.send_header("Accept-Encoding", "gzip, deflate, br")
        super().end_headers()

class ReuseTCPServer(socketserver.TCPServer):
    allow_reuse_address = True

with ReuseTCPServer(("", PORT), COOPHandler) as httpd:
    print(f"WASM dev server (COOP+COEP enabled) serving {ROOT}")
    print(f"  http://localhost:{PORT}/graph_studio.html")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
