"""工具发现：cmake / ctest / Windows SDK 工具。

查找顺序（与各 .ps1 一致）：
  1. 用户显式指定（--cmake / hint）
  2. PATH 上的可执行文件（shutil.which）
  3. Visual Studio 2022 自带的 cmake
  4. 已存在构建目录 CMakeCache.txt 里记录的 CMAKE_COMMAND（兜底）
"""

import os
import re
import shutil
from pathlib import Path
from typing import List, Optional

from .platform import is_windows


def vs2022_tool(name: str) -> Optional[Path]:
    """在 VS2022 内置 CMake 目录里找工具（cmake/ctest 等）。非 Windows 返回 None。"""
    if not is_windows():
        return None
    exe = name if name.lower().endswith(".exe") else name + ".exe"
    roots: List[str] = []
    for var in ("ProgramFiles", "ProgramFiles(x86)"):
        base = os.environ.get(var)
        if base:
            roots.append(os.path.join(base, "Microsoft Visual Studio", "2022"))
    for root in roots:
        top = Path(root)
        if not top.is_dir():
            continue
        for edition in top.iterdir():
            if not edition.is_dir():
                continue
            cand = edition / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / exe
            if cand.is_file():
                return cand
    return None


def find_tool(name: str) -> Optional[Path]:
    """PATH 查找，找不到时回退到 VS2022 内置目录。"""
    found = shutil.which(name)
    if found:
        return Path(found)
    vs = vs2022_tool(name)
    if vs:
        return vs
    return None


def _cache_cmake(build_dir: Optional[Path]) -> Optional[Path]:
    """从 CMakeCache.txt 提取 CMAKE_COMMAND（若存在）。"""
    if not build_dir or not build_dir.is_dir():
        return None
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    try:
        text = cache.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    m = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", text, re.MULTILINE)
    if m:
        return Path(m.group(1).strip())
    return None


def find_cmake(hint: Optional[str] = None, build_dir: Optional[Path] = None) -> Optional[Path]:
    """解析 cmake 可执行文件。"""
    if hint:
        return Path(hint)
    found = find_tool("cmake")
    if found:
        return found
    return _cache_cmake(build_dir)


def find_ctest(cmake: Optional[Path] = None, hint: Optional[str] = None) -> Optional[Path]:
    """解析 ctest：显式 hint -> PATH -> cmake 同目录。"""
    if hint:
        return Path(hint)
    found = find_tool("ctest")
    if found:
        return found
    if cmake:
        sibling = cmake.parent / ("ctest.exe" if is_windows() else "ctest")
        if sibling.is_file():
            return sibling
    return None


def find_sdk_tool(name: str) -> Optional[Path]:
    """Windows Kits\\10\\bin\\<版本>\\x64\\<name>.exe（makeappx / makepri / signtool 等）。"""
    if not is_windows():
        return None
    kits = Path(os.environ.get("ProgramFiles(x86)", "")) / "Windows Kits" / "10" / "bin"
    if not kits.is_dir():
        return None
    best: Optional[Path] = None
    for d in kits.iterdir():
        if not d.is_dir():
            continue
        if re.match(r"^\d+\.\d+\.\d+\.\d+$", d.name):
            ver = tuple(int(x) for x in d.name.split("."))
            if best is None or ver > tuple(int(x) for x in best.name.split(".")):
                best = d
    if best is None:
        return None
    exe = name if name.lower().endswith(".exe") else name + ".exe"
    cand = best / "x64" / exe
    return cand if cand.is_file() else None