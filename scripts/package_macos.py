#!/usr/bin/env python3
"""package_macos.py — 构建 GraphStudio 并打包为可分发的 macOS .dmg（跨平台脚本）。

流程：
  1) 构建 task_graph 根库 + subnode 插件（RelWithDebInfo + Metal + OpenCV）。
  2) 拉取 sentry-native（可选，构建含崩溃上报的发布版）。
  3) 构建 graph_studio.app。
  4) macdeployqt 把 Qt 框架/插件打入 .app 并修正 rpath。
  5) 把 subnode 插件 .dylib 拷进 Contents/PlugIns/。
  6) dylibbundler 收集 OpenCV 等非系统动态库进 Contents/Frameworks/ 并修正 install name。
  7) create-dmg 生成最终 .dmg（带 Applications 快捷方式）。

需要 macOS + Homebrew 工具：`brew install dylibbundler create-dmg`。

用法:
  python scripts/package_macos.py                         # 默认 0.1.0
  python scripts/package_macos.py --version 0.1.0
  python scripts/package_macos.py -c                      # 清空构建目录后全新构建
  python scripts/package_macos.py -j 8
  python scripts/package_macos.py --no-sentry             # 不构建崩溃上报
  python scripts/package_macos.py --dsn <sentry_dsn>      # 嵌入 Sentry DSN
  python scripts/package_macos.py --sentry-release 0.1.0-beta.42
  python scripts/package_macos.py --skip-build            # 复用现有产物只打包
  python scripts/package_macos.py --out-dir dist/dmg
  python scripts/package_macos.py --qt <prefix>           # 指定 Qt6 前缀
  python scripts/package_macos.py --opencv-dir <prefix>   # 指定 OpenCV 前缀

退出码：0 成功（打印最终 .dmg 路径）；非 0 表示构建/打包出错。
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root, runner  # noqa: E402
from gs import sdk, toolchain  # noqa: E402
from gs import sentry as gs_sentry  # noqa: E402
from gs.cmake import CMake  # noqa: E402

APP_NAME = "graph_studio"


def build_stack(cm: CMake, root: Path, lib_build: Path, gs_dir: Path, gs_build: Path,
                config: str, jobs: int, qt_prefix: Path, opencv_dir, sentry_dsn: str,
                sentry_release: str, clean: bool) -> int:
    """构建 task_graph 根库 + subnode 插件，再构建 graph_studio.app。返回退出码。"""
    if clean:
        for d in (lib_build, gs_build):
            if d.exists():
                console.step(f"清理 {d}")
                shutil.rmtree(d, ignore_errors=True)

    console.step("构建 task_graph 库 + subnode 插件")
    root_defines = ["-DTASK_GRAPH_ENABLE_OPENCV=ON", "-DTASK_GRAPH_ENABLE_METAL=ON"]
    if opencv_dir and (opencv_dir / "lib").is_dir():
        root_defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
    if cm.configure(root, lib_build, defines=root_defines, build_type=config) != 0:
        return 1
    if cm.build(lib_build, config=config, jobs=jobs,
                what="构建 task_graph 库 + subnode 插件") != 0:
        return 1

    app_defines = ["-DCMAKE_PREFIX_PATH={}".format(qt_prefix)] if qt_prefix else []
    if opencv_dir and (opencv_dir / "lib").is_dir():
        app_defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
    app_defines += gs_sentry.cmake_defines(dsn=sentry_dsn, release=sentry_release)
    if sentry_dsn:
        console.step(f"嵌入 Sentry DSN: {sentry_dsn}")
    console.step("配置 graph_studio")
    if cm.configure(gs_dir, gs_build, defines=app_defines, build_type=config) != 0:
        return 1
    console.step(f"构建 graph_studio (-j {jobs})")
    if cm.build(gs_build, config=config, jobs=jobs, what="构建 graph_studio") != 0:
        return 1
    return 0


def run_deployqt(qt_prefix: Path, app_bundle: Path) -> int:
    """macdeployqt：把 Qt 框架/插件打入 .app，修正 rpath。"""
    deployqt = qt_prefix / "bin" / "macdeployqt"
    if not deployqt.is_file():
        found = shutil.which("macdeployqt")
        deployqt = Path(found) if found else deployqt
    if not deployqt.is_file():
        console.fail(f"macdeployqt 未找到: {deployqt}（Qt6 bin 目录）")
        return 1
    console.step(f"运行 macdeployqt ({deployqt})")
    # -always-overwrite：复用构建时强制重写已存在的框架
    # -executable=... 让 macdeployqt 也修正插件/二级可执行文件的 @rpath
    args = [str(deployqt), str(app_bundle), "-always-overwrite"]
    return runner.check(args, what="macdeployqt")


def copy_plugins(app_bundle: Path, lib_build: Path, config: str) -> int:
    """把 subnode 插件 .dylib 拷进 Contents/PlugIns/（PluginBootstrap 从这里加载）。"""
    plugin_dirs = sdk.plugin_build_dirs(lib_build, config)
    if not plugin_dirs:
        console.warn("未找到 subnode 插件（build/submodules/<name>/ 下无 .dylib）")
        return 0
    dst = app_bundle / "Contents" / "PlugIns"
    dst.mkdir(parents=True, exist_ok=True)
    count = 0
    for d in plugin_dirs:
        for p in d.iterdir():
            if p.is_file() and p.suffix == ".dylib":
                shutil.copy2(p, dst / p.name)
                count += 1
    console.step(f"拷贝 subnode 插件 -> {dst}（{count} 个）")
    return 0


def _otool_deps(binary: Path) -> list:
    """otool -L 列出的依赖（跳过第一行 install name；保留原始引用字符串）。"""
    out = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True)
    deps = []
    for i, line in enumerate(out.stdout.splitlines()):
        line = line.strip()
        if not line or i <= 1 and line.startswith(str(binary)):
            continue
        deps.append(line.split(" (compatibility")[0].strip())
    return deps


def fix_bare_dep_names(target: Path, search_dirs) -> int:
    """把「裸名依赖」（无路径前缀，如 mediapipe 的 libvision.dylib）预改写为搜索目录
    中的绝对路径。dylibbundler 对裸名依赖无法定位时会进入交互式询问（EOF 下报错
    中止，且该 target 的所有改写都不生效），先改写成绝对路径即可走常规捆绑流程。
    """
    install_name_tool = shutil.which("install_name_tool")
    if not install_name_tool:
        console.fail("install_name_tool 未找到（需要 Xcode CLT）")
        return 1
    code = 0
    for dep in _otool_deps(target):
        if dep.startswith("@") or "/" in dep:
            continue  # @rpath/@executable_path/绝对/相对含斜杠的引用交给 dylibbundler
        # 裸名：在搜索目录里按文件名查找
        for d in search_dirs:
            cand = Path(d) / dep
            if cand.is_file():
                args = [install_name_tool, "-change", dep, str(cand.resolve()), str(target)]
                if runner.check(args, what=f"裸名依赖改写({target.name}:{dep})") != 0:
                    code = 1
                break
        else:
            console.warn(f"裸名依赖 {dep}（{target.name}）在搜索目录中未找到，"
                         "dylibbundler 可能无法捆绑它")
    return code


def run_dylibbundler(app_bundle: Path, search_dirs) -> int:
    """dylibbundler：收集所有非系统动态库（OpenCV 等）进 Contents/Frameworks/ 并修正 install name。

    search_dirs：dylibbundler 解析不了的依赖（@rpath/libtask_graph.dylib 等）按
    basename 在这些目录里查找（-s）。缺省会进入交互式询问 y/n，脚本/CI 环境会
    挂死，因此必须把 root build 目录与各插件目录传入。
    """
    dylibbundler = shutil.which("dylibbundler")
    if not dylibbundler:
        console.fail("dylibbundler 未找到。请运行: brew install dylibbundler")
        return 1
    exe = app_bundle / "Contents" / "MacOS" / APP_NAME
    frameworks = app_bundle / "Contents" / "Frameworks"
    frameworks.mkdir(parents=True, exist_ok=True)
    console.step(f"运行 dylibbundler（收集非系统依赖 -> {frameworks}）")
    # 先处理主可执行文件；再批量处理 PlugIns 下的插件 dylib。
    targets = [exe]
    plugins_dir = app_bundle / "Contents" / "PlugIns"
    if plugins_dir.is_dir():
        targets += [p for p in plugins_dir.iterdir() if p.suffix == ".dylib"]
    search_args: list = []
    for d in search_dirs:
        if Path(d).is_dir():
            search_args += ["-s", str(d)]
    code = 0
    for t in targets:
        # 裸名依赖先改写为绝对路径（见 fix_bare_dep_names 注释）
        if fix_bare_dep_names(t, search_dirs) != 0:
            code = 1
        # -b 打包依赖、-cd 必要时创建目标目录、-d 目标目录、-p 相对路径前缀、
        # -i 忽略系统路径、-of 已存在文件直接覆盖（逐 target 调用时第二个起会遇到
        #     同名库，缺省交互式询问，脚本/CI 会挂死）、-s 额外依赖搜索目录。
        args = [dylibbundler, "-b", "-x", str(t), "-cd", "-d", str(frameworks),
                "-p", "@executable_path/../Frameworks", "-i", "/usr/lib", "-i", "/System",
                "-of"] + search_args
        if runner.check(args, what=f"dylibbundler({t.name})") != 0:
            code = 1
    return code


def run_create_dmg(app_bundle: Path, out_dir: Path, version: str, volname: str) -> tuple:
    """create-dmg：生成带 Applications 快捷方式的 .dmg。返回 (退出码, .dmg 路径)。"""
    create_dmg = shutil.which("create-dmg")
    dmg_name = f"{APP_NAME}-{version}-macos.dmg"
    dmg_path = out_dir / dmg_name
    if dmg_path.exists():
        dmg_path.unlink()
    staging = out_dir / "dmg_staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    shutil.copytree(app_bundle, staging / app_bundle.name, symlinks=True)
    if create_dmg:
        console.step(f"运行 create-dmg -> {dmg_path}")
        args = [create_dmg, "--volname", volname, "--window-size", "500", "300",
                "--icon-size", "96", "--app-drop-link", "425", "120",
                "--no-internet-enable", str(dmg_path), str(staging)]
        code = runner.check(args, what="create-dmg")
    else:
        # 兜底：没有 create-dmg 时用 hdiutil 生成只含 .app 的 dmg
        console.warn("create-dmg 未找到，回退到 hdiutil（无 Applications 快捷方式）。"
                     "建议: brew install create-dmg")
        console.step(f"hdiutil 生成 .dmg -> {dmg_path}")
        tmp_dmg = out_dir / f"{APP_NAME}-tmp.dmg"
        if tmp_dmg.exists():
            tmp_dmg.unlink()
        code = runner.check(
            ["hdiutil", "create", "-volname", volname, "-srcfolder", str(staging),
             "-fs", "HFS+", "-format", "UDZO", "-imagekey", "zlib-level=9",
             str(tmp_dmg)],
            what="hdiutil create")
        if code == 0:
            shutil.move(str(tmp_dmg), str(dmg_path))
    return code, dmg_path


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建 GraphStudio 并打包为 macOS .dmg")
    ap.add_argument("--version", default="0.1.0", help="版本号（用于 .dmg 文件名，默认 0.1.0）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="清空两棵构建目录后全新构建")
    ap.add_argument("--config", "--build-type", default="RelWithDebInfo",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 RelWithDebInfo）")
    ap.add_argument("--no-sentry", action="store_true", help="不构建 Sentry 崩溃上报")
    ap.add_argument("--dsn", default="", help="嵌入的 Sentry DSN")
    ap.add_argument("--sentry-release", default="",
                    help="Sentry release 版本（默认取根 project VERSION；发布时传完整渠道版本如 0.1.0-beta.42）")
    ap.add_argument("--skip-build", action="store_true", help="跳过构建，复用现有 .app 打包")
    ap.add_argument("--out-dir", default="", help="输出目录（默认 dist/dmg）")
    ap.add_argument("--qt", default="", help="Qt6 前缀（含 lib/cmake/Qt6）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--volname", default="GraphStudio", help="DMG 卷名（默认 GraphStudio）")
    args = ap.parse_args()

    if platform.is_windows():
        console.fail("package_macos.py 仅支持 macOS。Windows 请用 scripts/build_msix.ps1。")
        return 1

    root = repo_root()
    gs_dir = root / "app" / "graph_studio"
    gs_build = gs_dir / "build"
    lib_build = root / "build"
    out_dir = Path(args.out_dir) if args.out_dir else root / "dist" / "dmg"
    out_dir.mkdir(parents=True, exist_ok=True)
    jobs = args.jobs or platform.cpu_count()
    dsn = args.dsn or ""

    cmake_exe = toolchain.find_cmake(args.cmake or None, build_dir=lib_build)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake 或用 --cmake 指定。")
        return 1
    qt_prefix = deps.find_qt(args.qt or None)
    opencv_dir = deps.find_opencv(args.opencv_dir or None)
    if not qt_prefix:
        console.fail("Qt6 未找到。用 --qt <prefix> 或设置 QT_PREFIX_PATH。")
        return 1
    console.step(f"Qt6: {qt_prefix}")
    if opencv_dir:
        console.step(f"OpenCV: {opencv_dir}")
    cm = CMake(cmake_exe)

    app_bundle = gs_build / f"{APP_NAME}.app"

    if not args.skip_build:
        if gs_sentry.ensure_fetched(root, not args.no_sentry) != 0:
            console.fail("拉取 sentry-native 失败")
            return 1
        code = build_stack(cm, root, lib_build, gs_dir, gs_build, args.config, jobs,
                           qt_prefix, opencv_dir, dsn, args.sentry_release, args.clean)
        if code != 0:
            return code
    elif not app_bundle.is_dir():
        console.fail(f"{app_bundle} 不存在，且指定了 --skip-build")
        return 1

    if not app_bundle.is_dir():
        console.fail(f"构建产物不存在: {app_bundle}")
        return 1

    if run_deployqt(qt_prefix, app_bundle) != 0:
        return 1
    copy_plugins(app_bundle, lib_build, args.config)
    # 依赖搜索路径：root build（libtask_graph.dylib）+ 各插件目录（libvision.dylib 等）
    search_dirs = [lib_build] + sdk.plugin_build_dirs(lib_build, args.config)
    if run_dylibbundler(app_bundle, search_dirs) != 0:
        console.warn("dylibbundler 部分失败，.dmg 仍会生成（可能缺少部分第三方依赖）")

    code, dmg_path = run_create_dmg(app_bundle, out_dir, args.version, args.volname)
    if code != 0 or not dmg_path.is_file():
        console.fail(f".dmg 生成失败 (exit {code}): {dmg_path}")
        return 1
    console.ok(f"打包完成: {dmg_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
