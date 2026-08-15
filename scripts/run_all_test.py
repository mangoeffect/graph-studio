#!/usr/bin/env python3
"""run_all_test.py — 一键运行 task_graph 全部测试：框架 + 子模块 + GraphStudio UI（跨平台）。

在原有三个独立脚本（run_tests.py / run_all_submodules_test.py / run_ui_tests.py）之上
做一次「单根构建 + 多阶段 ctest」编排，避免重复配置/构建根库：

  阶段 1  根构建（一次性，SUBMODULES=ON + OpenCV + 平台特性宏）—— 共享给阶段 2/3
  阶段 2  框架测试    ：根 build/  ctest -E <子模块正则>（排除子模块，只跑框架测试）
  阶段 3  子模块测试  ：根 build/  ctest -R <子模块正则>（子模块子集）
  阶段 4  UI 测试     ：app/graph_studio/build/  ctest（4 个 Qt 测试，headless offscreen）

退出码：0 全部通过（或全部被跳过/门控）；非 0 表示任一非跳过阶段失败或构建出错。
默认 run-all（即便某阶段失败也继续，一次看全所有失败）；--fail-fast 时首失败即停。

用法:
  python scripts/run_all_test.py                       # 构建并跑全部三阶段
  python scripts/run_all_test.py -c                    # 清空两棵构建树后全新构建
  python scripts/run_all_test.py --no-build            # 复用现有产物，只跑测试
  python scripts/run_all_test.py --skip-ui             # 跳过 UI 阶段（无需 Qt）
  python scripts/run_all_test.py --phases framework,submodules
  python scripts/run_all_test.py --fail-fast           # 首失败即停
  python scripts/run_all_test.py --sdk                 # 额外构建 SDK+demo 插件，激活 test_plugin_abi
  python scripts/run_all_test.py --download-models     # 先下载 MediaPipe 模型再跑
  python scripts/run_all_test.py -j 8 -v               # 并行 8 线程 + ctest 详细输出
  python scripts/run_all_test.py --qt <prefix>         # 手动指定 Qt6 前缀
  python scripts/run_all_test.py --cmake <path>        # 手动指定 cmake 可执行文件
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root, toolchain  # noqa: E402
from gs import sdk  # noqa: E402
from gs.cmake import CMake  # noqa: E402

# 复用 run_all_submodules_test.py 维护的「子模块名 → 测试名正则」表与其收集/下载辅助，
# 避免双份维护。该脚本模块级仅定义数据与函数，main() 在 __name__ 守卫内，import 安全。
from run_all_submodules_test import (  # noqa: E402
    SUBMODULE_TESTS,
    download_models,
    list_ctest_tests,
)

# GraphStudio 的全部测试目标（沿用 run_ui_tests.py 的 ALL_TARGETS）。
UI_TARGETS = ["test_graph_view_model", "test_command_stack", "test_integration", "test_gui"]

PHASE_FRAMEWORK = "framework"
PHASE_SUBMODULES = "submodules"
PHASE_UI = "ui"
ALL_PHASES = [PHASE_FRAMEWORK, PHASE_SUBMODULES, PHASE_UI]


def submodule_regex() -> str:
    """合并 SUBMODULE_TESTS 全部正则为一条 ctest -R/-E 可用的前缀正则。

    用 ^ 前缀锚定但不加 $ 尾锚定：核心测试经 gtest_discover_tests 逐用例注册
    （名字形如 test_dag.xxx）、子模块图测试逐图注册（名字形如
    test_image_filtering_graph.single_filter），都带点号后缀，-E（排除）按
    前缀精确剔除子模块测试、-R（包含）按前缀精确命中。
    """
    return "^(" + "|".join(SUBMODULE_TESTS.values()) + ")"


def setup_runtime_lib_paths(lib_build: Path, config: str, opencv_dir, qt_prefix) -> None:
    """把 build/<Config>、OpenCV bin、Qt bin 前置到本平台的动态库搜索路径环境变量。"""
    lib_env = platform.runtime_lib_env()
    if not lib_env:
        return
    platform.prepend_env_path(lib_env, lib_build / config)
    if opencv_dir:
        platform.prepend_env_path(lib_env, opencv_dir / "bin")
    if qt_prefix:
        platform.prepend_env_path(lib_env, qt_prefix / "bin")


def mirror_root_lib_windows(lib_build: Path, config: str) -> None:
    """Windows VS 多配置生成器把 task_graph.lib 放在 build/<Config>/，而
    app/graph_studio 的 CMakeLists 用 link_directories(../build)，需要镜像一份到 build/。
    沿用 run_graph_studio.py 的做法；缺库时仅告警，交由后续链接步骤报真实错误。"""
    src = lib_build / config / "task_graph.lib"
    dst = lib_build / "task_graph.lib"
    if src.is_file():
        shutil.copyfile(src, dst)
        console.step(f"镜像 {src} -> {dst}")
    else:
        console.warn(f"task_graph.lib 未找到: {src}（UI 链接可能失败）")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(
        description="一键运行 task_graph 全部测试（框架 + 子模块 + GraphStudio UI）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="清空两棵构建目录后全新构建")
    ap.add_argument("--no-build", action="store_true", help="跳过构建，复用现有产物只跑测试")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    ap.add_argument("-v", "--verbose", action="store_true", help="ctest 详细输出")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--qt", default="", help="Qt6 前缀（含 lib/cmake/Qt6）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--skip-framework", action="store_true", help="跳过框架测试阶段")
    ap.add_argument("--skip-submodules", action="store_true", help="跳过子模块测试阶段")
    ap.add_argument("--skip-ui", action="store_true", help="跳过 UI 测试阶段")
    ap.add_argument("--phases", default="",
                    help="显式指定要运行的阶段（逗号分隔：framework,submodules,ui）")
    ap.add_argument("--fail-fast", action="store_true", help="某阶段失败立即停止（默认跑完全部）")
    ap.add_argument("--sdk", action="store_true",
                    help="额外构建 SDK + demo 插件，激活 test_plugin_abi")
    ap.add_argument("--download-models", action="store_true",
                    help="运行前先下载 MediaPipe 模型（mediapipe 测试需要）")
    args = ap.parse_args()

    root = repo_root()
    lib_build = root / "build"
    gs_dir = root / "app" / "graph_studio"
    gs_build = gs_dir / "build"
    jobs = args.jobs or platform.cpu_count()

    # ---- 阶段选择：--phases 优先；否则按 --skip-* 过滤 ----
    if args.phases:
        wanted = {p.strip() for p in args.phases.split(",") if p.strip()}
        bad = wanted - set(ALL_PHASES)
        if bad:
            console.fail(f"未知阶段: {', '.join(sorted(bad))}（可选: {', '.join(ALL_PHASES)}）")
            return 1
        phases = [p for p in ALL_PHASES if p in wanted]
    else:
        phases = list(ALL_PHASES)
        if args.skip_framework:
            phases.remove(PHASE_FRAMEWORK)
        if args.skip_submodules:
            phases.remove(PHASE_SUBMODULES)
        if args.skip_ui:
            phases.remove(PHASE_UI)
    if not phases:
        console.warn("未选择任何阶段")
        return 0

    need_root = PHASE_FRAMEWORK in phases or PHASE_SUBMODULES in phases
    need_ui = PHASE_UI in phases

    # ---- 工具链 / 依赖探测 ----
    cmake_exe = toolchain.find_cmake(args.cmake or None, build_dir=lib_build)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1
    ctest_exe = toolchain.find_ctest(cmake_exe) or Path("ctest")
    console.step(f"cmake: {cmake_exe}")
    qt_prefix = deps.find_qt(args.qt or None)
    opencv_dir = deps.find_opencv(args.opencv_dir or None)
    cm = CMake(cmake_exe)

    # ---- 清理 ----
    if args.clean:
        for d, label in ((lib_build, "根构建目录"), (gs_build, "GraphStudio 构建目录")):
            if d.exists():
                console.step(f"清理 {label}")
                shutil.rmtree(d, ignore_errors=True)

    # ---- 模型下载（可选）----
    if args.download_models:
        download_models(root)

    # ---- 根构建（一次性）----
    # 框架/子模块阶段需要完整根树；UI 阶段也需要 libtask_graph。
    root_defines = ["-DTASK_GRAPH_BUILD_SUBMODULES=ON", "-DTASK_GRAPH_ENABLE_OPENCV=ON"]
    if platform.is_macos():
        root_defines.append("-DTASK_GRAPH_ENABLE_METAL=ON")
    # Vulkan 按平台探测（Windows 看 VULKAN_SDK，Linux 看 libvulkan-dev）；
    # 未装 SDK 时 gpu 子模块自动 soft-skip。
    if platform.has_vulkan():
        root_defines.append("-DTASK_GRAPH_ENABLE_VULKAN=ON")
    if opencv_dir and (opencv_dir / "lib").is_dir():
        root_defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")

    if not args.no_build:
        if need_root:
            console.step("构建根库 + 子模块测试（SUBMODULES=ON）")
            if cm.configure(root, lib_build, defines=root_defines, build_type=args.config) != 0:
                return 1
            if cm.build(lib_build, config=args.config, jobs=jobs,
                        what="构建根库 + 子模块测试") != 0:
                return 1
        elif need_ui:
            # UI 阶段也依赖 libtask_graph；仅构建 task_graph 目标即可
            if not (lib_build / "CMakeCache.txt").is_file():
                if cm.configure(root, lib_build, defines=root_defines,
                                build_type=args.config) != 0:
                    return 1
            if cm.build(lib_build, target="task_graph", config=args.config, jobs=jobs,
                        what="构建 libtask_graph（供 UI 链接）") != 0:
                return 1

    if need_root and not (lib_build / "CTestTestfile.cmake").is_file():
        console.fail(f"根构建目录 {lib_build} 未配置或不存在，且指定了 --no-build")
        return 1
    if need_ui and not gs_build.is_dir() and args.no_build:
        console.fail(f"GraphStudio 构建目录 {gs_build} 不存在，且指定了 --no-build")
        return 1

    # ---- SDK + demo 插件（可选，激活 test_plugin_abi）----
    if args.sdk:
        console.step("构建 SDK 前缀 + 独立 demo 插件（--sdk）")
        code = sdk.build_sdk(root, jobs=jobs, config="Release", cmake=cmake_exe)
        if code != 0:
            return code
        result = sdk.build_plugin_standalone(
            root, str(root / sdk.DEMO_PLUGIN_SRC), jobs=jobs, config="Release", cmake=cmake_exe)
        if result.get("code", 1) != 0:
            return result["code"]
        product = result.get("product")
        if product:
            os.environ["TASK_GRAPH_DEMO_PLUGIN"] = str(product)
            console.step(f"TASK_GRAPH_DEMO_PLUGIN: {product}")
        else:
            console.warn("demo 插件产物未找到（test_plugin_abi 将 soft-skip）")

    # ---- 运行时库搜索路径（三阶段共用）----
    setup_runtime_lib_paths(lib_build, args.config, opencv_dir, qt_prefix)

    sub_re = submodule_regex()
    sub_rx = re.compile(sub_re)

    # ---- 各阶段执行器 ----
    def run_framework() -> int:
        console.step("阶段 · 框架测试 (ctest -E 子模块正则)")
        return cm.ctest(ctest_exe, lib_build, config=args.config,
                        exclude=sub_re, verbose=args.verbose)

    def run_submodules() -> int:
        console.step("阶段 · 子模块测试 (ctest -R 子模块正则)")
        registered = list_ctest_tests(ctest_exe, lib_build, args.config, cm.multi_config)
        hits = [t for t in registered if sub_rx.match(t)]
        if hits:
            console.step(f"子模块测试命中 {len(hits)} 个: {', '.join(hits)}")
        else:
            console.warn("未收集到子模块测试（可能被平台/依赖门控，或 OpenCV 关闭），跳过")
            return 0
        return cm.ctest(ctest_exe, lib_build, config=args.config,
                        filter=sub_re, verbose=args.verbose)

    def run_ui() -> int:
        console.step("阶段 · UI 测试 (GraphStudio, headless)")
        if not args.no_build:
            if platform.is_windows():
                mirror_root_lib_windows(lib_build, args.config)
            defines = []
            if qt_prefix:
                defines.append(f"-DCMAKE_PREFIX_PATH={qt_prefix}")
            if opencv_dir and (opencv_dir / "lib").is_dir():
                defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
            if cm.configure(gs_dir, gs_build, defines=defines, build_type=args.config) != 0:
                return 1
            if cm.build(gs_build, config=args.config, jobs=jobs, target=UI_TARGETS,
                        what="构建 GraphStudio 测试目标") != 0:
                return 1
        # test_gui.cpp 已内置 offscreen 回落；此处显式设置以确保无头确定性行为
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        # UI 阶段仅 4 个 Qt 测试套件，默认 -V 让 QTest 的逐用例输出可见
        return cm.ctest(ctest_exe, gs_build, config=args.config, verbose=True)

    runners = {PHASE_FRAMEWORK: run_framework, PHASE_SUBMODULES: run_submodules, PHASE_UI: run_ui}

    # ---- 按固定顺序跑各阶段（默认 run-all）----
    results = []  # [(phase, status, detail)]
    overall = 0
    for p in ALL_PHASES:
        if p not in phases:
            results.append((p, "SKIP", ""))
            continue
        print()
        console.step(f"========== 阶段 · {p} ==========")
        code = runners[p]()
        if code == 0:
            results.append((p, "PASS", ""))
        else:
            results.append((p, "FAIL", f"exit {code}"))
            overall = 1
            if args.fail_fast:
                console.fail(f"阶段 {p} 失败，--fail-fast 停止")
                break

    # ---- 汇总 ----
    print()
    console.step("测试汇总")
    for p, status, detail in results:
        line = f"    [{p:<11}] {status}"
        if detail:
            line += f"  ({detail})"
        print(line, flush=True)
    print()
    if overall == 0:
        console.ok("全部通过的阶段均通过（未通过=SKIP 表示被跳过）")
    else:
        console.fail(f"存在阶段失败 (exit {overall})")
    return overall


if __name__ == "__main__":
    sys.exit(main())
