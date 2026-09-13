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


def _is_emsdk_root(p: Path) -> bool:
    """emsdk 根目录特征：upstream/emscripten 工具链 + 激活脚本。

    Homebrew 的 emscripten 两者皆无（binary 在 <ver>/bin，配置在 libexec），
    which 反推时用它排除伪装。
    """
    return ((p / "upstream" / "emscripten").is_dir()
            and ((p / "emsdk_env.sh").is_file() or (p / "emsdk_env.bat").is_file()))


def find_emsdk_root(hint: Optional[str] = None) -> Optional[Path]:
    """解析 emsdk 根目录：显式 hint -> $EMSDK_ROOT -> $EMSDK
    -> 默认安装位置 ~/emsdk -> emcmake 反推（校验确为 emsdk 布局——
    PATH 上 Homebrew emscripten 抢先时，反推出的 Cellar 目录没有
    upstream/ 与 emsdk_env.sh，不能再当 emsdk 用）。"""
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
    # emsdk 官方文档的标准安装位置；本仓库 wasm 工具链（Qt 6.6.3 +
    # emsdk 3.1.37）按此布局安装
    default = Path.home() / "emsdk"
    if _is_emsdk_root(default):
        return default
    # emcmake 在 PATH 上时反推：通常在 <emsdk>/upstream/emscripten/emcmake
    found = shutil.which("emcmake")
    if found:
        cand = Path(found).resolve().parents[2]
        if _is_emsdk_root(cand):
            return cand
    return None


def find_emcmake(emsdk_root: Optional[Path] = None) -> Optional[Path]:
    """emcmake 可执行文件：<emsdk>/upstream/emscripten/emcmake(.bat)
    -> PATH。root 优先——PATH 上 Homebrew emscripten 抢先时不能取
    （其 libc++ 与钉版工具链不兼容）。"""
    if emsdk_root:
        name = "emcmake.bat" if is_windows() else "emcmake"
        cand = emsdk_root / "upstream" / "emscripten" / name
        if cand.is_file():
            return cand
    return Path(shutil.which("emcmake")) if shutil.which("emcmake") else None


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