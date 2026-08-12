"""emsdk (emscripten) 跨平台激活辅助。

bash 脚本用 `source emsdk_env.sh` 激活；Windows 用 `emsdk_env.bat`。
这里用子进程 + env dump 的方式跨平台捕获激活后的环境变量并合并进 os.environ，
使随后的 emcmake / cmake 能直接找到 emscripten 工具链。
"""

import os
import shutil
import subprocess
from pathlib import Path
from typing import Optional

from .platform import is_windows


def find_emsdk_root(hint: Optional[str] = None) -> Optional[Path]:
    """解析 emsdk 根目录：显式 hint -> $EMSDK_ROOT -> $EMSDK -> emcmake 反推。"""
    for key in (("__hint__", hint), ("env", "EMSDK_ROOT"), ("env", "EMSDK")):
        kind, val = key
        if kind == "__hint__" and val:
            p = Path(val)
            if p.is_dir():
                return p
        elif kind == "env" and val:
            v = os.environ.get(val)
            if v and Path(v).is_dir():
                return Path(v)
    # emcmake 在 PATH 上时反推：通常在 <emsdk>/upstream/emscripten/emcmake
    found = shutil.which("emcmake")
    if found:
        return Path(found).resolve().parents[2]
    return None


def find_emcmake(emsdk_root: Optional[Path] = None) -> Optional[Path]:
    """emcmake 可执行文件：PATH -> <emsdk>/upstream/emscripten/emcmake(.bat)。"""
    found = shutil.which("emcmake")
    if found:
        return Path(found)
    if emsdk_root:
        name = "emcmake.bat" if is_windows() else "emcmake"
        cand = emsdk_root / "upstream" / "emscripten" / name
        if cand.is_file():
            return cand
    return None


def activate(emsdk_root: Path) -> bool:
    """Best-effort 激活 emsdk 环境（合并进 os.environ）。返回是否执行了激活脚本。"""
    if is_windows():
        script = emsdk_root / "emsdk_env.bat"
        if not script.is_file():
            return False
        cmd = ["cmd", "/c", f'"{script}" && set']
    else:
        script = emsdk_root / "emsdk_env.sh"
        if not script.is_file():
            return False
        cmd = ["bash", "-c", f"source '{script}' >/dev/null 2>&1 && env"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=False).stdout
    except OSError:
        return False
    for line in out.splitlines():
        if "=" not in line:
            continue
        k, _, v = line.partition("=")
        if k and k != "_":
            os.environ[k] = v
    return True