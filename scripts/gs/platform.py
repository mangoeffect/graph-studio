"""平台相关信息：OS 判断、CPU 核数、动态库后缀、运行时搜索路径、CMake 特性宏。"""

import os
import sys
from pathlib import Path
from typing import List, Optional


def is_windows() -> bool:
    return os.name == "nt"


def is_macos() -> bool:
    return sys.platform == "darwin"


def is_linux() -> bool:
    return sys.platform.startswith("linux")


def cpu_count() -> int:
    """默认并行度：逻辑 CPU 核数，兜底 4。"""
    return os.cpu_count() or 4


def shlib_suffix() -> str:
    """本平台动态库后缀：.dll / .dylib / .so。"""
    if is_windows():
        return ".dll"
    if is_macos():
        return ".dylib"
    return ".so"


def multi_config_generator() -> bool:
    """Windows 上默认使用 VS 多配置生成器（构建传 --config，不传 -DCMAKE_BUILD_TYPE）。"""
    return is_windows()


def feature_macros() -> List[str]:
    """按平台启用桌面 GPU 后端特性宏（对应各平台的 sh/ps1 行为）。

    Vulkan 仅在检测到 SDK（VULKAN_SDK 环境变量，CI 由 setup-build-deps
    安装 LunarG SDK 后导出，本地由 LunarG 安装器写入）时启用：
    未装 SDK 的机器上 find_package(Vulkan) 会致命失败，跳过后
    gpu 子模块自动 soft-skip（与 Linux 一致）。
    """
    defines = ["-DTASK_GRAPH_ENABLE_OPENCV=ON"]
    if is_macos():
        defines.append("-DTASK_GRAPH_ENABLE_METAL=ON")
    if is_windows() and os.environ.get("VULKAN_SDK"):
        defines.append("-DTASK_GRAPH_ENABLE_VULKAN=ON")
    return defines


def runtime_lib_env() -> Optional[str]:
    """本平台用于追加动态库搜索路径的环境变量（Windows 用 PATH）。"""
    if is_windows():
        return "PATH"
    if is_macos():
        return "DYLD_LIBRARY_PATH"
    return "LD_LIBRARY_PATH"


def prepend_env_path(var: str, *entries: Path) -> None:
    """把若干目录插到环境变量（PATH / DYLD_LIBRARY_PATH / LD_LIBRARY_PATH）最前面。"""
    existing = os.environ.get(var, "")
    parts = [str(e) for e in entries if e and Path(e).is_dir()]
    if not parts:
        return
    if existing:
        parts.append(existing)
    os.environ[var] = os.pathsep.join(parts)


def host_prebuilt_prefix() -> str:
    """Android NDK / 其它工具链的 host 目录名（darwin-x86_64 / linux-x86_64 / windows-x86_64）。"""
    if is_windows():
        return "windows-x86_64"
    if is_macos():
        return "darwin-x86_64"
    return "linux-x86_64"