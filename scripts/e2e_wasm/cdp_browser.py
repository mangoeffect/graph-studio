# -*- coding: utf-8 -*-
"""cdp_browser.py — 有头 Chrome + CDP 驱动（纯标准库 websocket 实现）。

为什么必须有头：多线程 WASM（Qt wasm_multithread）需要 cross-origin
isolation（SharedArrayBuffer），headless Chrome 不支持（仓库实测，
见 submodules/agents/AGENTS.md）。WS 客户端手写（RFC6455 帧编解码 +
握手），避免为 repo 工具引入第三方依赖（PEP 668 环境装包也麻烦）。

Qt 6.6.3 的 UI 挂在 `#qt-shadow-container` 的 shadowRoot 里 ——
document.querySelectorAll('canvas') 什么都看不到，必须探 shadowRoot。

网络边界：所有请求只打本机 Chrome DevTools 端点
（127.0.0.1:<cdp_port>，端口由本类选定），不接触任何外部地址。
HTTP 侧用 http.client 以分离的 host/port 连接（host 恒为常量
DEVTOOLS_HOST，port 恒为构造时校验过的 int），不走 URL 字符串。
"""

import base64
import http.client
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

# DevTools 端点只允许本机回环（防误配外发）
DEVTOOLS_HOST = "127.0.0.1"


class WebSocketError(RuntimeError):
    pass


class MiniWebSocket:
    """ws://127.0.0.1:port/path 的最小客户端：文本帧收发 + ping/pong。

    握手按 RFC 6455 发送 Sec-WebSocket-Key；不回验 Sec-WebSocket-Accept
    （该回验防的是"恶意对端服务器"，而这里对端是本脚本自己拉起的 Chrome，
    校验无意义，且 RFC 固定要求 SHA-1 参与，徒增一个弱哈希实现）。
    """

    def __init__(self, host: str, port: int, path: str, timeout: float = 30.0):
        if host != DEVTOOLS_HOST:
            raise WebSocketError(f"只允许连接本机 DevTools 端点，收到 {host}")
        self._rbuf = b""
        self._sock = socket.create_connection((host, port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\n"
               f"Host: {host}:{port}\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n")
        self._sock.sendall(req.encode())
        resp = self._readuntil(b"\r\n\r\n").decode("latin1")
        if " 101 " not in resp.splitlines()[0]:
            raise WebSocketError(f"websocket 握手失败: {resp.splitlines()[0]}")

    def _readuntil(self, delim: bytes) -> bytes:
        while delim not in self._rbuf:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise WebSocketError("连接被对端关闭")
            self._rbuf += chunk
        idx = self._rbuf.index(delim) + len(delim)
        out, self._rbuf = self._rbuf[:idx], self._rbuf[idx:]
        return out

    def _recv_exact(self, n: int) -> bytes:
        while len(self._rbuf) < n:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise WebSocketError("连接被对端关闭")
            self._rbuf += chunk
        out, self._rbuf = self._rbuf[:n], self._rbuf[n:]
        return out

    def send_text(self, text: str):
        payload = text.encode("utf-8")
        mask = os.urandom(4)
        header = bytes([0x81])  # FIN + text
        n = len(payload)
        if n < 126:
            header += bytes([0x80 | n])
        elif n < 65536:
            header += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self._sock.sendall(header + mask + masked)

    def recv_message(self) -> str:
        """读一条完整消息（自动处理分片与 ping）。"""
        payload = b""
        while True:
            b1, b2 = self._recv_exact(2)
            opcode = b1 & 0x0F
            fin = b1 & 0x80
            n = b2 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._recv_exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._recv_exact(8))[0]
            if b2 & 0x80:  # 服务端帧不掩码，出现即协议错误
                raise WebSocketError("收到掩码的服务端帧")
            data = self._recv_exact(n)
            if opcode == 9:  # ping -> pong
                mask = os.urandom(4)
                self._sock.sendall(bytes([0x8A, 0x80 | len(data)]) + mask
                                   + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
                continue
            if opcode == 8:  # close
                raise WebSocketError("对端发送 close 帧")
            payload += data
            if fin:
                return payload.decode("utf-8", "replace")

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass


class DevtoolsHttp:
    """DevTools HTTP 通道：host 恒为 DEVTOOLS_HOST，port 为校验过的 int。"""

    def __init__(self, port: int):
        if not isinstance(port, int) or not (1024 <= port <= 65535):
            raise ValueError(f"非法 DevTools 端口: {port}")
        self._port = port

    def get_json(self, path: str, timeout: float = 10.0):
        conn = http.client.HTTPConnection(DEVTOOLS_HOST, self._port,
                                          timeout=timeout)
        try:
            conn.request("GET", path)
            return json.loads(conn.getresponse().read())
        finally:
            conn.close()

    def put_json(self, path: str, timeout: float = 10.0):
        conn = http.client.HTTPConnection(DEVTOOLS_HOST, self._port,
                                          timeout=timeout)
        try:
            conn.request("PUT", path)
            return json.loads(conn.getresponse().read())
        finally:
            conn.close()

    def wait_ready(self, timeout: float = 30.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.get_json("/json/version", timeout=1)
                return
            except OSError:
                time.sleep(0.3)
        raise TimeoutError("CDP 端口未就绪")


class CdpTab:
    """一个 page target 上的同步 CDP 会话：命令逐个发送，事件缓冲。"""

    def __init__(self, ws_url: str):
        # ws://127.0.0.1:9333/devtools/page/<id>
        head, rest = ws_url.split("://", 1)
        if head != "ws":
            raise WebSocketError(f"非 ws URL: {ws_url}")
        hostport, path = rest.split("/", 1)
        host, port = hostport.rsplit(":", 1)
        self._ws = MiniWebSocket(host, int(port), "/" + path)
        self._next_id = 1
        self.console: list[tuple[str, str]] = []
        self.page_errors: list[str] = []

    def _dispatch(self, msg):
        method = msg.get("method", "")
        if method == "Runtime.consoleAPICalled":
            args = " ".join(str(a.get("value", a.get("description", "")))
                            for a in msg["params"].get("args", []))
            self.console.append((msg["params"].get("type", "log"), args))
        elif method == "Runtime.exceptionThrown":
            d = msg["params"].get("exceptionDetails", {})
            self.page_errors.append(d.get("exception", {}).get("description",
                                                               str(d)))
        elif method == "Log.entryAdded":
            entry = msg["params"].get("entry", {})
            self.console.append((entry.get("level", "log"),
                                 entry.get("text", "")))

    def _pump(self, want_id: int, timeout: float):
        """收消息直到拿到 want_id 的响应；期间事件入缓冲。"""
        deadline = time.time() + timeout
        while True:
            if time.time() > deadline:
                raise TimeoutError(f"等待 CDP 响应超时 (id={want_id})")
            msg = json.loads(self._ws.recv_message())
            if msg.get("id") == want_id:
                if "error" in msg:
                    raise RuntimeError(f"CDP {msg.get('error', {}).get('message')}")
                return msg.get("result", {})
            self._dispatch(msg)

    def _drain_pending(self, window: float = 0.2):
        """非阻塞收割积压事件（wait_* 轮询时必须主动调用，否则新 console
        事件永远留在浏览器侧缓冲里进不了本地缓冲）。"""
        old = self._ws._sock.gettimeout()
        try:
            self._ws._sock.settimeout(window)
            while True:
                try:
                    self._dispatch(json.loads(self._ws.recv_message()))
                except (socket.timeout, TimeoutError):
                    break
                except WebSocketError:
                    break
        finally:
            self._ws._sock.settimeout(old)

    def cmd(self, method: str, timeout: float = 30.0, **params):
        mid = self._next_id
        self._next_id += 1
        self._ws.send_text(json.dumps({"id": mid, "method": method,
                                       "params": params}))
        return self._pump(mid, timeout)

    # ---- 高层封装 ----

    def navigate(self, url: str, timeout: float = 60.0):
        self.cmd("Page.enable", timeout=timeout)
        self.console.clear()
        self.page_errors.clear()
        self.cmd("Page.navigate", url=url, timeout=timeout)

    def evaluate(self, expression: str, timeout: float = 30.0):
        """Runtime.evaluate，返回反序列化值；页面异常抛 RuntimeError。"""
        res = self.cmd("Runtime.evaluate", timeout=timeout,
                       expression=expression, returnByValue=True,
                       awaitPromise=True)
        detail = res.get("exceptionDetails")
        if detail:
            raise RuntimeError(f"页面执行异常: "
                               f"{detail.get('exception', {}).get('description', detail)}")
        return res.get("result", {}).get("value")

    def console_text(self) -> str:
        return "\n".join(t for _, t in self.console)

    def wait_console(self, pattern: str, timeout: float = 60.0) -> str:
        """等待 console 出现匹配行（正则），返回该行。每轮主动收割积压事件。"""
        import re
        deadline = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < deadline:
            self._drain_pending(0.1)
            for _, text in self.console:
                if rx.search(text):
                    return text
            time.sleep(0.2)
        raise TimeoutError(f"{timeout}s 内 console 未出现 {pattern!r}")

    def wait_expr(self, expression: str, timeout: float = 60.0,
                  poll: float = 0.4):
        """轮询页面表达式直到真值并返回它。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                val = self.evaluate(expression)
                if val:
                    return val
            except RuntimeError:
                pass  # 页面尚未就绪（shadow DOM 未挂载等）
            time.sleep(poll)
        raise TimeoutError(f"{timeout}s 内表达式未为真: {expression[:120]}")

    def screenshot(self, path: Path):
        data = self.cmd("Page.captureScreenshot", timeout=30.0,
                        format="png").get("data", "")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(base64.b64decode(data))

    # ---- 输入合成 ----

    def _geom(self):
        return self.evaluate(
            "(() => { const h = document.querySelector('#qt-shadow-container');"
            " if (!h) return null; const c = h.shadowRoot.querySelector('canvas');"
            " if (!c) return null; const r = c.getBoundingClientRect();"
            " return {left: r.left, top: r.top, width: r.width, height: r.height};"
            "})()")

    def canvas_to_page(self, x: float, y: float):
        geom = self._geom()
        if not geom:
            raise RuntimeError("找不到 Qt canvas（shadow DOM 未挂载？）")
        return geom["left"] + x, geom["top"] + y

    def mouse_move(self, x: float, y: float, buttons: int = 0):
        px, py = self.canvas_to_page(x, y)
        self.cmd("Input.dispatchMouseEvent",
                 type="mouseMoved", x=px, y=py, buttons=buttons)

    def mouse_down(self, x: float, y: float, click_count: int = 1):
        px, py = self.canvas_to_page(x, y)
        self.cmd("Input.dispatchMouseEvent",
                 type="mousePressed", x=px, y=py, button="left",
                 buttons=1, clickCount=click_count)

    def mouse_up(self, x: float, y: float, click_count: int = 1):
        px, py = self.canvas_to_page(x, y)
        self.cmd("Input.dispatchMouseEvent",
                 type="mouseReleased", x=px, y=py, button="left",
                 buttons=0, clickCount=click_count)

    def click(self, x: float, y: float):
        self.mouse_move(x, y)
        self.mouse_down(x, y)
        self.mouse_up(x, y)

    def drag(self, x0: float, y0: float, x1: float, y1: float, steps: int = 8):
        """按下的移动序列（Qt 画布端口拖线的真实输入路径）。"""
        self.mouse_move(x0, y0)
        self.mouse_down(x0, y0)
        for i in range(1, steps + 1):
            self.mouse_move(x0 + (x1 - x0) * i / steps,
                            y0 + (y1 - y0) * i / steps, buttons=1)
            time.sleep(0.02)
        self.mouse_up(x1, y1)

    def close(self):
        self._ws.close()


class CdpBrowser:
    """有头 Chrome 生命周期 + 开新 tab。仅与 127.0.0.1 的 DevTools 端点通信。"""

    def __init__(self, chrome: Path, cdp_port: int = 9333):
        self.http = DevtoolsHttp(cdp_port)
        self.chrome = chrome
        self.user_data = ""
        self.pid = 0

    def start(self, timeout: float = 30.0):
        self.user_data = tempfile.mkdtemp(prefix="gs_e2e_wasm_")
        cmd = [str(self.chrome), f"--user-data-dir={self.user_data}",
               f"--remote-debugging-port={self.http._port}",
               "--no-first-run", "--no-default-browser-check",
               "--disable-sync", "--window-size=1500,950",
               "about:blank"]
        # os.posix_spawn：参数列表直传（无 shell 解释）；长驻进程无需句柄，
        # 清理按唯一 user-data-dir pkill（见 stop）
        self.pid = os.posix_spawn(str(self.chrome), cmd, dict(os.environ))
        self.http.wait_ready(timeout)

    def new_tab(self, url: str) -> CdpTab:
        # /json/new 在 Chrome 111+ 要求 PUT；目标 URL 整段 quote 进 query
        from urllib.parse import quote
        target = self.http.put_json(f"/json/new?{quote(url, safe='')}")
        tab = CdpTab(target["webSocketDebuggerUrl"])
        tab.cmd("Page.enable")
        tab.cmd("Runtime.enable")
        tab.cmd("Log.enable")
        tab.navigate(url)
        return tab

    def stop(self):
        if self.pid:
            # 按 user-data-dir 唯一标识清理本次启动的实例
            subprocess.run(["pkill", "-f", self.user_data],
                           capture_output=True)
            self.pid = 0
        shutil.rmtree(self.user_data, ignore_errors=True)
