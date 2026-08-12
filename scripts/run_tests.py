#!/usr/bin/env python3
"""run_tests.py — 配置、构建并运行 task_graph 的全部单元测试（跨平台）。

取代 scripts/run_tests.sh 与 scripts/run_tests.ps1：
  - Unix 单配置生成器：configure 传 -DCMAKE_BUILD_TYPE
  - Windows VS 多配置生成器：build/ctest 传 --config / -C

用法:
  python scripts/run_tests.py                     # 构建并运行全部测试
  python scripts/run_tests.py -c                  # 先清空构建目录再全新构建
  python scripts/run_tests.py -b <dir>            # 指定构建目录（默认 build）
  python scripts/run_tests.py -j <N>              # 并行编译线程数（默认 CPU 核数）
  python scripts/run_tests.py -R <regex>          # 只运行名字匹配 regex 的测试
  python scripts/run_tests.py -l                  # 列出所有测试后退出，不运行
  python scripts/run_tests.py --config <C>        # 构建配置（默认 Debug；Unix 上等于 -DCMAKE_BUILD_TYPE）
  python scripts/run_tests.py --enable-opencv     # 打开 TASK_GRAPH_ENABLE_OPENCV（默认开，与主项目一致）
  python scripts/run_tests.py --disable-opencv    # 关闭 TASK_GRAPH_ENABLE_OPENCV
  python scripts/run_tests.py --opencv-dir <dir>  # OpenCV 前缀
  python scripts/run_tests.py --cmake <path>      # 指定 cmake 可执行文件
  python scripts/run_tests.py --sdk               # 先构建 SDK + 独立 demo 插件，再跑含 test_plugin_abi 的全部测试
  python scripts/run_tests.py --no-build          # 跳过配置/构建，直接跑现有二进制
  python scripts/run_tests.py -v                  # ctest 详细输出（--output-on-failure 默认已开）

退出码：0 表示全部通过，非 0 表示有测试失败或构建出错。
"""

import argparse
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import deps, platform, sdk, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建并运行 task_graph 的全部单元测试")
    ap.add_argument("-b", "--build-dir", default="build", help="构建目录（默认 build）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-R", "--filter", default="", help="只运行名字匹配 regex 的测试")
    ap.add_argument("-c", "--clean", action="store_true", help="先清空构建目录再全新构建")
    ap.add_argument("-l", "--list", action="store_true", help="列出所有测试后退出")
    ap.add_argument("--no-build", action="store_true", help="跳过配置/构建，直接运行")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    ap.add_argument("--enable-opencv", dest="opencv", action="store_true", default=True,
                    help="打开 TASK_GRAPH_ENABLE_OPENCV（默认开）")
    ap.add_argument("--disable-opencv", dest="opencv", action="store_false",
                    help="关闭 TASK_GRAPH_ENABLE_OPENCV")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--sdk", action="store_true",
                    help="先构建 SDK 前缀 + 独立 demo 插件，再跑含 test_plugin_abi 的全部测试")
    ap.add_argument("-v", "--verbose", action="store_true", help="ctest 详细输出")
    args = ap.parse_args()

    root = repo_root()
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = root / build_dir

    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(args.cmake, build_dir=build_dir)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1
    ctest_exe = toolchain.find_ctest(cmake_exe)
    if not ctest_exe:
        ctest_exe = Path("ctest")
    console.step(f"cmake: {cmake_exe}")

    # ---- 清理 ----
    if args.clean and build_dir.exists():
        console.step(f"清理构建目录 {build_dir}")
        shutil.rmtree(build_dir, ignore_errors=True)

    if not args.no_build:
        defines = [f"-DTASK_GRAPH_ENABLE_OPENCV={'ON' if args.opencv else 'OFF'}"]
        opencv_dir = deps.find_opencv(args.opencv_dir) if args.opencv else None
        if args.opencv and opencv_dir and (opencv_dir / "lib").is_dir():
            defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")

        cm = CMake(cmake_exe)
        if cm.configure(root, build_dir, defines=defines, build_type=args.config) != 0:
            return 1
        if cm.build(build_dir, config=args.config, jobs=jobs) != 0:
            return 1
    elif not build_dir.is_dir():
        console.fail(f"构建目录 {build_dir} 不存在，且指定了 --no-build")
        return 1

    # ---- SDK 模式：构建 SDK 前缀 + 独立 demo 插件，供 test_plugin_abi 加载 ----
    if args.sdk:
        console.step("构建 SDK 前缀 (build_sdk)")
        code = sdk.build_sdk(root, jobs=jobs, config="Release", cmake=cmake_exe)
        if code != 0:
            return code
        console.step("独立编译 demo 插件 (build_plugin_standalone)")
        result = sdk.build_plugin_standalone(
            root, str(root / sdk.DEMO_PLUGIN_SRC), jobs=jobs, config="Release",
            cmake=cmake_exe)
        product = result.get("product")
        if product:
            os.environ["TASK_GRAPH_DEMO_PLUGIN"] = str(product)
            console.step(f"TASK_GRAPH_DEMO_PLUGIN: {product}")
        else:
            console.warn("demo 插件产物未找到（test_plugin_abi 将 soft-skip）")

    # ---- 列出测试 ----
    cm = CMake(cmake_exe)
    if args.list:
        return cm.ctest(ctest_exe, build_dir, config=args.config, list_only=True)

    # ---- 运行时搜索路径（Windows: build\\Debug 与 OpenCV bin；Unix 通常依赖 RPATH）----
    platform.prepend_env_path(platform.runtime_lib_env() or "", build_dir / args.config)
    if args.opencv:
        opencv_dir = deps.find_opencv(args.opencv_dir) or deps.find_opencv()
        if opencv_dir:
            platform.prepend_env_path(platform.runtime_lib_env() or "", opencv_dir / "bin")

    # ---- 运行测试 ----
    status = cm.ctest(ctest_exe, build_dir, config=args.config,
                      filter=args.filter, verbose=args.verbose)
    print()
    if status == 0:
        console.ok("全部测试通过")
    else:
        console.fail(f"存在测试失败 (exit {status})")
    return status


if __name__ == "__main__":
    sys.exit(main())