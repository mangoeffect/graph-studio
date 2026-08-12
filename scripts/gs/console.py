"""控制台着色输出：仅在 TTY 且未设置 NO_COLOR=1 时启用 ANSI 颜色。"""

import os
import sys

_use_color = False


def init() -> None:
    """根据 stdout 是否为终端 + NO_COLOR 环境变量决定是否启用颜色。"""
    global _use_color
    try:
        _use_color = sys.stdout.isatty() and os.environ.get("NO_COLOR") != "1"
    except Exception:
        _use_color = False


def _c(code: str) -> str:
    return f"\x1b[{code}m" if _use_color else ""


def step(msg: str) -> None:
    """加粗的进度步骤标题。"""
    print(f"{_c('1')}==> {msg}{_c('0')}", flush=True)


def ok(msg: str) -> None:
    """绿色 + 加粗的成功提示。"""
    print(f"{_c('32')}{_c('1')}==> {msg}{_c('0')}", flush=True)


def warn(msg: str) -> None:
    """黄色警告（stderr）。"""
    print(f"{_c('33')}==> {msg}{_c('0')}", file=sys.stderr, flush=True)


def fail(msg: str) -> None:
    """红色错误（stderr）。"""
    print(f"{_c('31')}==> {msg}{_c('0')}", file=sys.stderr, flush=True)
