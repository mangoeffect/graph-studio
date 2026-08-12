#!/usr/bin/env python3
"""build_plugin_standalone.py — 独立编译一个 task_graph 插件为运行时动态库（跨平台）。

取代 scripts/build_plugin_standalone.sh 与 scripts/build_plugin_standalone.ps1。
仅依赖 SDK 前缀（默认 <root>/build/sdk），不引用主仓库源码。

用法:
  python scripts/build_sdk.py                                    # 先生成 SDK 前缀
  python scripts/build_plugin_standalone.py examples/plugins/demo
  python scripts/build_plugin_standalone.py submodules/opencv/image_processing/image_filtering --enable-opencv
  python scripts/build_plugin_standalone.py <src> --sdk /opt/tg-sdk -j 8
  python scripts/build_plugin_standalone.py -h

产物: <root>/build/standalone/plugins/<name>/<name>.{dylib,so,dll}
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import platform, sdk, toolchain  # noqa: E402


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="独立编译一个 task_graph 插件为运行时动态库")
    ap.add_argument("src_dir", help="插件源目录（须含 CMakeLists.txt）")
    ap.add_argument("--sdk", default="", dest="sdk_dir", help="task_graph SDK 前缀（默认 <root>/build/sdk）")
    ap.add_argument("--out-root", default="", help="输出根目录（默认 <root>/build/standalone/plugins）")
    ap.add_argument("--config", "--build-type", default="Release",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建类型（默认 Release）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("--enable-opencv", action="store_true", help="打开 OpenCV 依赖（默认 OFF）")
    ap.add_argument("--clean", action="store_true", help="先清空插件构建目录")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(args.cmake)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1

    result = sdk.build_plugin_standalone(
        root, args.src_dir, sdk_dir=args.sdk_dir, out_root=args.out_root,
        config=args.config, jobs=jobs, cmake=cmake_exe,
        enable_opencv=args.enable_opencv, clean=args.clean)
    return int(result.get("code", 1))


if __name__ == "__main__":
    sys.exit(main())