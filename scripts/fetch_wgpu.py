#!/usr/bin/env python3
"""fetch_wgpu.py — 下载 wgpu-native 预构建产物（钉版本），供 WgpuGpuBackend 链接。

安装到 build/wgpu/install/<platform>-<arch>/：
  include/webgpu/webgpu.h   标准化 WebGPU C API 头（wgpu-native 自带版本）
  include/webgpu/wgpu.h     wgpu-native 扩展头（instance/adapter 便利 API）
  lib/libwgpu_native.dylib|so|dll(+.lib)
  wgpu-native-meta/         上游版本元信息（git tag / webgpu spec）

平台映射（wgpu-native release 资产名）：
  macOS arm64 -> wgpu-macos-aarch64-release.zip
  Linux x64   -> wgpu-linux-x86_64-release.zip
  Windows x64 -> wgpu-windows-x86_64-msvc-release.zip

为什么预构建而不是源码/子模块：wgpu-native 是 Rust 项目，源码构建需要完整
Rust 工具链且耗时长；release 提供静态+动态库与 C 头，C ABI 无 CRT 匹配问题。

用法:
  python scripts/fetch_wgpu.py             # 本机平台
  python scripts/fetch_wgpu.py --force     # 忽略 sentinel 强制重下

根 CMakeLists 在 -DTASK_GRAPH_ENABLE_WGPU=ON 时探测 build/wgpu/install；
找不到产物时 warn 并按"依赖缺失即跳过"约定降级（不 fail configure）。
"""

import argparse
import platform
import shutil
import sys
import zipfile
from pathlib import Path
from urllib.request import urlopen

# 钉版本（升级 = 改这里 + 重跑本脚本；CMake/代码按此版本头注释对齐）。
# v29.0.1.1 对应 wgpu 29.0.1（webgpu spec 对应 Chrome ~143 时代）。
WGPU_NATIVE_VERSION = "v29.0.1.1"

RELEASE_URL = (
    "https://github.com/gfx-rs/wgpu-native/releases/download/"
    f"{WGPU_NATIVE_VERSION}/{{asset}}"
)

SENTINEL = ".wgpu-native-fetched"


def detect_platform() -> tuple[str, str, str]:
    """返回 (platform_tag, asset_name, install_dir_name)。"""
    system = platform.system().lower()
    machine = platform.machine().lower()
    if system == "darwin":
        arch = "aarch64" if machine == "arm64" else "x86_64"
        return "macos", f"wgpu-macos-{arch}-release.zip", f"macos-{arch}"
    if system == "linux":
        arch = "aarch64" if machine == "aarch64" else "x86_64"
        return "linux", f"wgpu-linux-{arch}-release.zip", f"linux-{arch}"
    if system == "windows":
        return "windows", "wgpu-windows-x86_64-msvc-release.zip", "windows-x86_64"
    raise SystemExit(f"unsupported platform: {system}/{machine}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=None,
                    help="repo root (default: auto-detect from script location)")
    ap.add_argument("--force", action="store_true",
                    help="re-download even if sentinel exists")
    args = ap.parse_args()

    root = Path(args.root) if args.root else Path(__file__).resolve().parent.parent
    plat, asset, dir_name = detect_platform()
    install_dir = root / "build" / "wgpu" / "install" / dir_name
    sentinel = install_dir / SENTINEL

    if sentinel.exists() and not args.force:
        print(f"[wgpu] already fetched ({WGPU_NATIVE_VERSION}) at {install_dir}")
        return 0
    if install_dir.exists():
        shutil.rmtree(install_dir)
    install_dir.mkdir(parents=True)

    url = RELEASE_URL.format(asset=asset)
    print(f"[wgpu] downloading {url}")
    zip_path = install_dir / asset
    with urlopen(url) as resp, open(zip_path, "wb") as f:  # noqa: S310
        shutil.copyfileobj(resp, f)

    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(install_dir)
    zip_path.unlink()

    (install_dir / SENTINEL).write_text(WGPU_NATIVE_VERSION + "\n")
    print(f"[wgpu] installed {WGPU_NATIVE_VERSION} -> {install_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
