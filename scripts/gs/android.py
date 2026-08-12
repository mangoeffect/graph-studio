"""Android NDK 交叉编译辅助：NDK 发现、toolchain 文件、llvm-ar 定位。

供 build_android.py 与 build_opencv_android.py 共用。NDK 工具链本身跨 host
（macOS/Linux/Windows 均可跑），所以这两个脚本天然跨平台。
"""

import os
from pathlib import Path
from typing import Optional


def find_ndk() -> Optional[Path]:
    """从 ANDROID_NDK / ANDROID_NDK_HOME 环境变量解析 NDK 根目录。"""
    for var in ("ANDROID_NDK", "ANDROID_NDK_HOME"):
        v = os.environ.get(var)
        if v and Path(v).is_dir():
            return Path(v)
    return None


def ndk_toolchain(ndk: Path) -> Optional[Path]:
    """NDK 自带的 android.toolchain.cmake 路径（不存在返回 None）。"""
    tc = ndk / "build" / "cmake" / "android.toolchain.cmake"
    return tc if tc.is_file() else None


def find_llvm_ar(ndk: Path) -> Optional[Path]:
    """在 NDK 的 prebuilt/<host>/bin/ 里找 llvm-ar（兼容 .exe）。"""
    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt"
    if not prebuilt.is_dir():
        return None
    for d in sorted(prebuilt.iterdir()):
        bin_dir = d / "bin"
        if not bin_dir.is_dir():
            continue
        for name in ("llvm-ar", "llvm-ar.exe"):
            f = bin_dir / name
            if f.is_file():
                return f
    return None