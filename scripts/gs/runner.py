"""subprocess 统一入口：等价 PowerShell 的 Invoke-Native，透传输出并返回退出码。"""

import subprocess
import sys
from pathlib import Path
from typing import Any, List, Optional, Sequence, Union

from . import console

Cmd = Union[str, Path]
ArgList = Sequence[Union[str, Path, Any]]


def run(args: ArgList, cwd: Optional[str] = None, env=None) -> int:
    """运行一个外部命令，stdout/stderr 直接透传到终端。

    返回退出码。找不到命令时打印错误并返回 127。
    """
    cmd = [str(a) for a in args]
    try:
        sys.stdout.flush()
        sys.stderr.flush()
        return subprocess.run(cmd, cwd=cwd, env=env).returncode
    except OSError as e:
        console.fail(f"无法启动 {cmd[0]}: {e}")
        return 127


def check(args: ArgList, cwd: Optional[str] = None, env=None, what: str = "") -> int:
    """运行命令，非零退出码时打印失败消息并返回退出码（不抛异常）。"""
    code = run(args, cwd=cwd, env=env)
    if code != 0:
        label = what or " ".join(str(a) for a in args)
        console.fail(f"{label} 失败 (exit {code})")
    return code