#!/usr/bin/env python3
"""run_ui_tests.py — 构建并运行 GraphStudio 的 UI 自动化测试 (test_gui)（跨平台）。

取代 scripts/run_ui_tests.sh。test_gui 用 QApplication 实例化真实 MainWindow，
QTest 驱动鼠标/键盘，内省 QGraphicsScene 与 GraphViewModel 状态；默认以
QT_QPA_PLATFORM=offscreen 无头运行（test_gui.cpp 内置回落，无需额外配置）。

流程:
  1) 确保根库 task_graph 已构建（test_gui 链接 build/libtask_graph）
  2) 配置 + 构建 graph_studio 的 test_gui 目标（--all 时构建全部测试目标）
  3) 用 ctest 运行 test_gui（--all 跑全部 graph_studio 测试）

用法:
  python scripts/run_ui_tests.py              # 构建 + 运行 test_gui
  python scripts/run_ui_tests.py --no-build   # 跳过构建，直接跑现有 test_gui
  python scripts/run_ui_tests.py --build-only # 只构建，不运行
  python scripts/run_ui_tests.py --all        # 跑全部 graph_studio 测试（含单元/集成）
  python scripts/run_ui_tests.py -c           # 清空 graph_studio 构建目录后全新构建
  python scripts/run_ui_tests.py -j <N>       # 并行编译线程数（默认 CPU 核数）
  python scripts/run_ui_tests.py -v           # ctest 详细输出
  python scripts/run_ui_tests.py --qt <path>  # 手动指定 Qt6 前缀（含 lib/cmake/Qt6）
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

ALL_TARGETS = ["test_graph_view_model", "test_command_stack", "test_integration", "test_gui"]


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建并运行 GraphStudio 的 UI 自动化测试 (test_gui)")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="清空 graph_studio 构建目录后全新构建")
    ap.add_argument("--no-build", action="store_true", help="跳过构建，直接运行")
    ap.add_argument("--build-only", action="store_true", help="只构建，不运行")
    ap.add_argument("--all", action="store_true", help="跑全部 graph_studio 测试（含单元/集成）")
    ap.add_argument("-v", "--verbose", action="store_true", help="ctest 详细输出")
    ap.add_argument("--qt", default="", help="Qt6 前缀（含 lib/cmake/Qt6）")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    args = ap.parse_args()

    root = repo_root()
    gs_dir = root / "app" / "graph_studio"
    gs_build = gs_dir / "build"
    lib_build = root / "build"
    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(build_dir=lib_build)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1
    ctest_exe = toolchain.find_ctest(cmake_exe) or Path("ctest")
    cm = CMake(cmake_exe)

    if args.clean and gs_build.exists():
        console.step("清理 GraphStudio 构建目录")
        shutil.rmtree(gs_build, ignore_errors=True)

    if not args.no_build:
        # 1) 确保根库 task_graph 已构建（test_gui 运行时依赖 libtask_graph）
        if not (lib_build / "CMakeCache.txt").is_file():
            console.step("配置 task_graph 库")
            if cm.configure(root, lib_build, build_type=args.config) != 0:
                return 1
        console.step(f"构建 task_graph 库 (-j {jobs})")
        if cm.build(lib_build, target="task_graph", config=args.config, jobs=jobs,
                    what="构建 task_graph 库") != 0:
            return 1

        # 2) 配置 + 构建 test_gui
        defines = []
        qt_prefix = deps.find_qt(args.qt or None)
        if qt_prefix:
            defines.append(f"-DCMAKE_PREFIX_PATH={qt_prefix}")
        console.step("配置 graph_studio")
        if cm.configure(gs_dir, gs_build, defines=defines, build_type=args.config) != 0:
            return 1

        targets = ALL_TARGETS if args.all else ["test_gui"]
        label = "构建全部测试目标" if args.all else "构建 test_gui"
        if cm.build(gs_build, config=args.config, jobs=jobs, target=targets, what=label) != 0:
            return 1
    elif not gs_build.is_dir():
        console.fail(f"构建目录 {gs_build} 不存在，且指定了 --no-build")
        return 1

    if args.build_only:
        console.ok("构建完成 (--build-only)")
        return 0

    # 3) 运行测试
    # test_gui.cpp 已在未设 QT_QPA_PLATFORM 时回落 offscreen；此处不强制覆盖，
    # 开发者可设 QT_QPA_PLATFORM=cocoa 观察画面（需 --no-build 复用已有产物）。
    filt = None if args.all else "test_gui"
    status = cm.ctest(ctest_exe, gs_build, config=args.config, filter=filt, verbose=args.verbose)
    print()
    if status == 0:
        console.ok("UI 测试通过" if not args.all else "全部测试通过")
    else:
        console.fail(f"UI 测试失败 (exit {status})" if not args.all
                     else f"存在测试失败 (exit {status})")
    return status


if __name__ == "__main__":
    sys.exit(main())