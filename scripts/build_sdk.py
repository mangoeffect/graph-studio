#!/usr/bin/env python3
"""build_sdk.py — 把 task_graph 框架构建为可分发 SDK 前缀（跨平台）。

取代 scripts/build_sdk.sh 与 scripts/build_sdk.ps1。
以 TASK_GRAPH_BUILD_SUBMODULES=OFF 独立构建，主仓库不编译任何内置子模块源码。

用法:
  python scripts/build_sdk.py                      # 默认 SDK 前缀 -> build/sdk，Release
  python scripts/build_sdk.py --prefix /opt/tg-sdk # 自定义前缀
  python scripts/build_sdk.py -j 8                 # 并行度
  python scripts/build_sdk.py --config Debug       # 构建类型（默认 Release）
  python scripts/build_sdk.py --disable-opencv     # 关闭 OpenCV（默认跟随主项目 ON）
  python scripts/build_sdk.py --clean              # 先清空构建目录（sdk-build 被污染时建议）
  python scripts/build_sdk.py -h

产物: <prefix>/include/{plugin_api.hpp, task_graph/**}
      <prefix>/lib/{libtask_graph.dylib | libtask_graph.so | task_graph.dll + task_graph.lib}
      <prefix>/lib/cmake/task_graph/{task_graphConfig.cmake, task_graphTargets.cmake, SdkUtil.cmake}
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import deps, platform, sdk, toolchain  # noqa: E402


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="把 task_graph 框架构建为可分发 SDK")
    ap.add_argument("--prefix", default="", help="SDK 安装前缀（默认 <root>/build/sdk）")
    ap.add_argument("--build-dir", default="", help="CMake 构建目录（默认 <root>/build/sdk-build）")
    ap.add_argument("--config", "--build-type", default="Release",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建类型（默认 Release）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("--disable-opencv", action="store_true", help="关闭 OpenCV（默认开启）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--clean", action="store_true",
                    help="先清空构建目录（sdk-build 被完整构建污染时建议使用）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(args.cmake)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1

    opencv_dir = None
    if not args.disable_opencv:
        opencv_dir = deps.find_opencv(args.opencv_dir or None)

    return sdk.build_sdk(
        root, prefix=args.prefix, build_dir=args.build_dir, config=args.config,
        jobs=jobs, cmake=cmake_exe, disable_opencv=args.disable_opencv,
        opencv_dir=opencv_dir, clean=args.clean)


if __name__ == "__main__":
    sys.exit(main())