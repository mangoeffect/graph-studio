#!/usr/bin/env python3
"""run_graph_studio.py — 构建并启动 GraphStudio (Qt6 GUI)（跨平台）。

取代 scripts/run_graph_studio.sh 与 scripts/run_graph_studio.ps1：
  1) 先构建根库 task_graph（GraphStudio 运行时依赖 build/libtask_graph），
     默认开启 OpenCV + Metal(macOS) / Vulkan(Windows) 后端宏。
  2) 在 app/graph_studio/build 配置 + 构建 graph_studio。
  3) 设置运行时搜索路径并启动生成的应用（macOS 为 .app bundle，其它平台为可执行文件）。

用法:
  python scripts/run_graph_studio.py               # 构建并启动
  python scripts/run_graph_studio.py -c            # 清空构建目录后全新构建
  python scripts/run_graph_studio.py -j <N>        # 并行编译线程数（默认 CPU 核数）
  python scripts/run_graph_studio.py --no-build    # 跳过构建，直接启动现有产物
  python scripts/run_graph_studio.py --build-only  # 只构建，不启动
  python scripts/run_graph_studio.py -t            # 运行 GraphStudio 的单元测试（headless）
  python scripts/run_graph_studio.py --qt <path>   # 手动指定 Qt6 前缀（含 lib/cmake/Qt6）
  python scripts/run_graph_studio.py --config <C>  # 构建配置（默认 Debug）

退出码：0 表示成功，非 0 表示构建出错或未找到产物。
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import deps, platform, sdk, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402


def build_stack(cm: CMake, root: Path, lib_build: Path, gs_dir: Path, gs_build: Path,
                config: str, jobs: int, qt_prefix: Path, opencv_dir, disable_opencv: bool,
                clean: bool, skip_app: bool = False) -> int:
    """构建 task_graph 根库 + subnode 插件，再把 task_graph.lib 镜像上来（Windows quirk），
    然后构建 graph_studio。返回退出码。"""
    if clean and gs_build.exists():
        console.step("清理 GraphStudio 构建目录")
        shutil.rmtree(gs_build, ignore_errors=True)

    console.step("构建 task_graph 库 + subnode 插件")
    defines = platform.feature_macros()
    if disable_opencv:
        defines = ["-DTASK_GRAPH_ENABLE_OPENCV=OFF"]
    else:
        defines = ["-DTASK_GRAPH_ENABLE_OPENCV=ON"]
        if platform.is_macos():
            defines.append("-DTASK_GRAPH_ENABLE_METAL=ON")
        # Vulkan 按平台探测（Windows 看 VULKAN_SDK，Linux 看 libvulkan-dev）；
        # 未装 SDK 时 gpu 子模块自动 soft-skip。
        if platform.has_vulkan():
            defines.append("-DTASK_GRAPH_ENABLE_VULKAN=ON")
        if opencv_dir:
            defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
    if cm.configure(root, lib_build, defines=defines, build_type=config) != 0:
        return 1
    if cm.build(lib_build, config=config, jobs=jobs, what="构建 task_graph 库 + subnode 插件") != 0:
        return 1

    if platform.is_windows():
        # VS 多配置生成器把 task_graph.lib 放在 build\<Config>，而 app 的 CMakeLists
        # 用的是 link_directories(../build)，需要镜像一份上去。
        lib_src = lib_build / config / "task_graph.lib"
        lib_dst = lib_build / "task_graph.lib"
        if lib_src.is_file():
            shutil.copyfile(lib_src, lib_dst)
            console.step(f"镜像 {lib_src} -> {lib_dst}")
        else:
            console.fail(f"task_graph.lib 未找到: {lib_src}（链接可能失败）")

    if skip_app:
        return 0

    app_defines = []
    if qt_prefix:
        app_defines.append(f"-DCMAKE_PREFIX_PATH={qt_prefix}")
    if not disable_opencv and opencv_dir:
        app_defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
    console.step("配置 graph_studio")
    if cm.configure(gs_dir, gs_build, defines=app_defines, build_type=config) != 0:
        return 1
    console.step(f"构建 graph_studio (-j {jobs})")
    if cm.build(gs_build, config=config, jobs=jobs, what="构建 graph_studio") != 0:
        return 1
    return 0


def run() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建并启动 GraphStudio (Qt6 GUI)")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="清空构建目录后全新构建")
    ap.add_argument("--no-build", action="store_true", help="跳过构建，直接启动现有产物")
    ap.add_argument("--build-only", action="store_true", help="只构建，不启动")
    ap.add_argument("-t", "--test", action="store_true", help="运行 GraphStudio 单元测试")
    ap.add_argument("--qt", default="", help="Qt6 前缀（含 lib/cmake/Qt6）")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    ap.add_argument("--disable-opencv", action="store_true", help="关闭 OpenCV 依赖")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    args = ap.parse_args()

    root = repo_root()
    gs_dir = root / "app" / "graph_studio"
    gs_build = gs_dir / "build"
    lib_build = root / "build"

    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(args.cmake, build_dir=lib_build)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1
    ctest_exe = toolchain.find_ctest(cmake_exe) or Path("ctest")

    qt_prefix = deps.find_qt(args.qt or None)
    opencv_dir = deps.find_opencv(args.opencv_dir or None)

    cm = CMake(cmake_exe)

    if not args.no_build:
        code = build_stack(cm, root, lib_build, gs_dir, gs_build, args.config, jobs,
                           qt_prefix, opencv_dir, args.disable_opencv, args.clean)
        if code != 0:
            return code
    elif not gs_build.is_dir():
        console.fail(f"构建目录 {gs_build} 不存在，且指定了 --no-build")
        return 1

    # ---- 运行时搜索路径（Windows DLL / 各平台 Qt/OpenCV bin）----
    lib_env = platform.runtime_lib_env()
    if lib_env:
        platform.prepend_env_path(lib_env, lib_build / args.config)
        if qt_prefix:
            platform.prepend_env_path(lib_env, qt_prefix / "bin")
        if opencv_dir and not args.disable_opencv:
            platform.prepend_env_path(lib_env, opencv_dir / "bin")
        # MediaPipe vision.dll：GraphStudio exe 不在插件目录，依赖 DLL 需进
        # PATH（loader 标准搜索顺序：exe 目录 → system → PATH）
        mp_bin = lib_build / "mediapipe" / "install" / "bin"
        if mp_bin.is_dir():
            platform.prepend_env_path(lib_env, mp_bin)

    plugin_dirs = sdk.plugin_build_dirs(lib_build, args.config)
    if plugin_dirs:
        os.environ["TASK_GRAPH_PLUGINS_PATH"] = os.pathsep.join(str(p) for p in plugin_dirs)
        console.step(f"TASK_GRAPH_PLUGINS_PATH: {os.environ['TASK_GRAPH_PLUGINS_PATH']}")

    # dev 模式模型目录：下载过测试模型（scripts/download_mediapipe_models.py）
    # 时，让图里只填模型名即可解析（打包布局由 ModelBootstrap 自行推断）。
    dev_models = repo_root() / "submodules" / "mediapipe" / "mediapipe_vision" / "tests" / "models"
    if dev_models.is_dir():
        os.environ["GRAPH_STUDIO_MODELS_DIR"] = str(dev_models)
        console.step(f"GRAPH_STUDIO_MODELS_DIR: {os.environ['GRAPH_STUDIO_MODELS_DIR']}")

    # ---- 运行单元测试 ----
    if args.test:
        console.step("运行 GraphStudio 单元测试")
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        return cm.ctest(ctest_exe, gs_build, config=args.config)

    # ---- 仅构建 ----
    if args.build_only:
        console.ok("构建完成 (--build-only)")
        return 0

    # ---- 定位并启动产物 ----
    bundle = gs_build / "graph_studio.app"
    bin_dir = gs_build / args.config
    if platform.is_macos() and bundle.is_dir():
        console.step("启动 GraphStudio (.app bundle)")
        return subprocess.run(["open", str(bundle)]).returncode

    bins = []
    if platform.is_windows():
        bins.append(bin_dir / "graph_studio.exe")
    else:
        bins.append(gs_build / "graph_studio")
        bins.append(bundle / "Contents" / "MacOS" / "graph_studio")
    for b in bins:
        if b.is_file() and os.access(b, os.X_OK):
            console.step(f"启动 GraphStudio: {b}")
            return subprocess.run([str(b)]).returncode
    console.fail(f"未找到 GraphStudio 产物 ({bundle} 或 graph_studio)")
    return 1


def main() -> int:
    return run()


if __name__ == "__main__":
    sys.exit(main())