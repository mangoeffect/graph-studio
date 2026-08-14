"""外部依赖发现：Qt6 前缀与 OpenCV 前缀（跨平台探测）。"""

import os
import re
from pathlib import Path
from typing import Optional

from .platform import is_macos, is_windows


def validate_qt(prefix: Path) -> bool:
    """有效的 Qt6 前缀应包含 lib/cmake/Qt6。"""
    return (prefix / "lib" / "cmake" / "Qt6").is_dir()


def find_qt(hint: Optional[str] = None) -> Optional[Path]:
    """探测 Qt6 前缀。

    顺序：--qt 显式参数 -> 环境变量 QT_PREFIX_PATH -> 平台默认位置
    （Windows: C:\\Qt\\<version>\\msvc2022_64；macOS: Homebrew opt/qt）。
    找不到返回 None（由 CMake 自行 find_package 决定成败）。
    """
    if hint:
        p = Path(hint)
        return p if validate_qt(p) else p
    env = os.environ.get("QT_PREFIX_PATH")
    if env:
        p = Path(env)
        if validate_qt(p):
            return p
    if is_windows():
        base = Path("C:/Qt")
        if base.is_dir():
            best: Optional[Path] = None
            best_ver = (0, 0)
            for d in base.iterdir():
                if not d.is_dir() or not re.match(r"^\d+\.", d.name):
                    continue
                ver = tuple(int(x) for x in re.findall(r"\d+", d.name)[:3])
                cand = d / "msvc2022_64"
                if cand.is_dir() and validate_qt(cand) and ver > best_ver:
                    best, best_ver = cand, ver
            if best:
                return best
    if is_macos():
        # Homebrew 现行布局把 Qt6 拆成 qtbase/qtdeclarative 等子 formula（无整包 qt），
        # qmake/macosdeployqt 与 lib/cmake/Qt6 都在 qtbase 前缀内。
        for p in (Path("/opt/homebrew/opt/qt"), Path("/opt/homebrew/opt/qtbase"),
                  Path("/usr/local/opt/qt"), Path("/usr/local/opt/qtbase"),
                  Path("/opt/homebrew/Cellar/qt/6")):
            if validate_qt(p):
                return p
    return None


def find_opencv(hint: Optional[str] = None) -> Optional[Path]:
    """探测 OpenCV 安装前缀。

    顺序：--opencv-dir -> OPENCV_DIR -> Windows 默认 C:\\opencv\\build\\x64\\vc16。
    """
    if hint:
        return Path(hint)
    env = os.environ.get("OPENCV_DIR")
    if env:
        p = Path(env)
        if (p / "lib").is_dir():
            return p
    if is_windows():
        p = Path("C:/opencv/build/x64/vc16")
        if (p / "lib").is_dir():
            return p
    return None