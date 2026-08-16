#!/usr/bin/env python3
"""package_linux.py — 构建 GraphStudio 并打包为可分发的 Linux AppImage。

流程：
  1) 构建 task_graph 根库 + subnode 插件（RelWithDebInfo + OpenCV）。
  2) 拉取 sentry-native（可选，构建含崩溃上报的发布版）。
  3) 构建 graph_studio 可执行文件。
  4) 搭建 AppDir 布局（usr/bin/graph_studio + .desktop + 图标 + subnode 插件）。
  5) linuxdeployqt 收集 Qt/OpenCV 等依赖进 AppDir/usr/lib 并修正 rpath。
  6) 用自定义 AppRun 包一层，注入 TASK_GRAPH_PLUGINS_PATH（PluginBootstrap 从此加载 subnode 插件）。
  7) appimagetool 把 AppDir 打包成 .AppImage。

需要 Linux + Qt6（qmake 在 PATH 或 --qmake 指定）。linuxdeployqt / appimagetool
未在 PATH 时自动下载到 dist/tools/（CI 友好）。

用法:
  python scripts/package_linux.py                         # 默认 0.1.0
  python scripts/package_linux.py --version 0.1.0
  python scripts/package_linux.py -c                      # 清空构建目录后全新构建
  python scripts/package_linux.py -j 8
  python scripts/package_linux.py --no-sentry             # 不构建崩溃上报
  python scripts/package_linux.py --dsn <sentry_dsn>      # 嵌入 Sentry DSN
  python scripts/package_linux.py --sentry-release 0.1.0-beta.42
  python scripts/package_linux.py --skip-build            # 复用现有产物只打包
  python scripts/package_linux.py --out-dir dist/appimage
  python scripts/package_linux.py --qt <prefix>           # 指定 Qt6 前缀
  python scripts/package_linux.py --qmake <path>          # 指定 qmake
  python scripts/package_linux.py --opencv-dir <prefix>   # 指定 OpenCV 前缀

退出码：0 成功（打印最终 .AppImage 路径）；非 0 表示构建/打包出错。
"""

import argparse
import os
import shutil
import stat
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root, runner  # noqa: E402
from gs import sdk, toolchain  # noqa: E402
from gs import sentry as gs_sentry  # noqa: E402
from gs.cmake import CMake  # noqa: E402

APP_NAME = "graph_studio"

LINUXDEPLOYQT_URL = ("https://github.com/probonopd/linuxdeployqt/releases/download/"
                     "continuous/linuxdeployqt-continuous-x86_64.AppImage")
APPIMAGETOOL_URL = ("https://github.com/AppImage/AppImageKit/releases/download/"
                    "continuous/appimagetool-x86_64.AppImage")

DESKTOP_TEMPLATE = """[Desktop Entry]
Type=Application
Name=Graph Studio
Comment=Visual DAG editor and task execution framework built on task_graph.
Exec={exe}
Icon={icon}
Terminal=false
Categories=Development;
"""

APPRUN_TEMPLATE = """#!/usr/bin/env bash
# AppImage 入口：注入 subnode 插件路径与库搜索路径后启动 graph_studio。
# linuxdeployqt 已为各二进制设置 rpath，这里仅补充 PluginBootstrap 需要的环境变量。
self="$(readlink -f "$0")"
appdir="$(dirname "$self")"
export TASK_GRAPH_PLUGINS_PATH="${{appdir}}/usr/plugins${{TASK_GRAPH_PLUGINS_PATH:+:${{TASK_GRAPH_PLUGINS_PATH}}}}"
export LD_LIBRARY_PATH="${{appdir}}/usr/lib${{LD_LIBRARY_PATH:+:${{LD_LIBRARY_PATH}}}}"
exec "${{appdir}}/usr/bin/{exe}" "$@"
"""


def build_stack(cm: CMake, root: Path, lib_build: Path, gs_dir: Path, gs_build: Path,
                config: str, jobs: int, qt_prefix: Path, opencv_dir, sentry_dsn: str,
                sentry_release: str, clean: bool) -> int:
    """构建 task_graph 根库 + subnode 插件，再构建 graph_studio。返回退出码。"""
    if clean:
        for d in (lib_build, gs_build):
            if d.exists():
                console.step(f"清理 {d}")
                shutil.rmtree(d, ignore_errors=True)

    console.step("构建 task_graph 库 + subnode 插件")
    root_defines = ["-DTASK_GRAPH_ENABLE_OPENCV=ON"]
    if opencv_dir and (opencv_dir / "lib").is_dir():
        root_defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
    # Vulkan 与 app 侧的 find_package(Vulkan) 探测保持一致（run_graph_studio.py
    # 同款 platform.has_vulkan() 门）：CI 的 ubuntu 装了 libvulkan-dev，app 的
    # GpuBootstrap.cpp 会定义 TG_APP_HAS_VULKAN 并引用 VulkanGpuBackend；核心
    # 库若不开启本开关，链接 graph_studio 时报 undefined reference。
    if platform.has_vulkan():
        root_defines.append("-DTASK_GRAPH_ENABLE_VULKAN=ON")
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


def ensure_appimagetool(name: str, url: str, explicit: str, tools_dir: Path) -> Path:
    """返回可用的 linuxdeployqt / appimagetool 可执行文件路径；缺失则下载。"""
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return p
        console.warn(f"指定的 {name} 不存在: {p}，尝试自动获取")
    found = shutil.which(name)
    if found:
        return Path(found)
    target = tools_dir / (name + "-x86_64.AppImage")
    if target.is_file():
        return target
    tools_dir.mkdir(parents=True, exist_ok=True)
    console.step(f"下载 {name} -> {target}")
    code = runner.run(["curl", "-fsSL", "-o", str(target), url])
    if code != 0:
        console.fail(f"下载 {name} 失败 (exit {code})")
        return target
    target.chmod(target.stat().st_mode | stat.S_IEXEC)
    return target


def stage_appdir(appdir: Path, gs_build: Path, lib_build: Path, resources: Path,
                 config: str) -> int:
    """搭建 AppDir：bin + plugins + desktop + 图标。"""
    console.step(f"搭建 AppDir: {appdir}")
    (appdir / "usr" / "bin").mkdir(parents=True, exist_ok=True)
    (appdir / "usr" / "lib").mkdir(parents=True, exist_ok=True)
    (appdir / "usr" / "plugins").mkdir(parents=True, exist_ok=True)

    exe_src = gs_build / APP_NAME
    if not exe_src.is_file():
        console.fail(f"构建产物不存在: {exe_src}")
        return 1
    shutil.copy2(exe_src, appdir / "usr" / "bin" / APP_NAME)

    # crashpad_handler 需与主程序同目录（Sentry 构建产物；POST_BUILD 已拷到
    # gs_build，linuxdeployqt 只收集动态依赖、不会带上独立可执行文件）。
    handler_src = gs_build / "crashpad_handler"
    if handler_src.is_file():
        shutil.copy2(handler_src, appdir / "usr" / "bin" / "crashpad_handler")
        console.step("拷贝 crashpad_handler -> usr/bin/（与主程序同目录）")

    # subnode 插件 .so -> usr/plugins/
    plugin_dirs = sdk.plugin_build_dirs(lib_build, config)
    count = 0
    for d in plugin_dirs:
        for p in d.iterdir():
            if p.is_file() and p.suffix == ".so":
                shutil.copy2(p, appdir / "usr" / "plugins" / p.name)
                count += 1
    console.step(f"拷贝 subnode 插件 -> usr/plugins/（{count} 个）")

    # .desktop 文件（linuxdeployqt 据此定位 Exec/Icon）
    (appdir / "graph_studio.desktop").write_text(
        DESKTOP_TEMPLATE.format(exe=APP_NAME, icon=APP_NAME))

    # 图标：linuxdeployqt 需要 AppDir/<icon>.png（≥256x256）
    for cand in ("app_icon_512.png", "app_icon_256.png"):
        icon_src = resources / "icons" / cand
        if icon_src.is_file():
            shutil.copy2(icon_src, appdir / f"{APP_NAME}.png")
            break
    return 0


def run_linuxdeployqt(linuxdeployqt: Path, appdir: Path, qmake: Path) -> int:
    """linuxdeployqt：收集 Qt/非系统依赖进 AppDir/usr/lib，修正 rpath。"""
    desktop = appdir / "graph_studio.desktop"
    env = os.environ.copy()
    # CI 里 FUSE 常被禁用（无 /dev/fuse），让 AppImage 工具走提取模式运行。
    env.setdefault("APPIMAGE_EXTRACT_AND_RUN", "1")
    args = [str(linuxdeployqt), str(desktop), "-bundle-non-qt-libs",
            "-qmake=" + str(qmake), "-verbose=1"]
    console.step(f"运行 linuxdeployqt ({linuxdeployqt})")
    return runner.check(args, env=env, what="linuxdeployqt")


def write_apprun(appdir: Path) -> None:
    """覆盖 linuxdeployqt 生成的 AppRun，注入插件路径/库路径。"""
    apprun = appdir / "AppRun"
    apprun.write_text(APPRUN_TEMPLATE.format(exe=APP_NAME))
    apprun.chmod(apprun.stat().st_mode | stat.S_IEXEC)
    console.step(f"写入自定义 AppRun: {apprun}")


def run_appimagetool(appimagetool: Path, appdir: Path, out_path: Path) -> int:
    """appimagetool：把 AppDir 打包成 .AppImage。"""
    env = os.environ.copy()
    env.setdefault("APPIMAGE_EXTRACT_AND_RUN", "1")
    if out_path.exists():
        out_path.unlink()
    args = [str(appimagetool), str(appdir), str(out_path)]
    console.step(f"运行 appimagetool -> {out_path}")
    return runner.check(args, env=env, what="appimagetool")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建 GraphStudio 并打包为 Linux AppImage")
    ap.add_argument("--version", default="0.1.0", help="版本号（用于文件名，默认 0.1.0）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="清空两棵构建目录后全新构建")
    ap.add_argument("--config", "--build-type", default="RelWithDebInfo",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 RelWithDebInfo）")
    ap.add_argument("--skip-build", action="store_true", help="跳过构建，复用现有 graph_studio 打包")
    ap.add_argument("--no-sentry", action="store_true", help="不构建 Sentry 崩溃上报")
    ap.add_argument("--dsn", default="", help="嵌入的 Sentry DSN（默认不嵌入，运行时读 SENTRY_DSN 环境变量）")
    ap.add_argument("--sentry-release", default="",
                    help="Sentry release 版本（默认取根 project VERSION；发布时传完整渠道版本如 0.1.0-beta.42）")
    ap.add_argument("--out-dir", default="", help="输出目录（默认 dist/appimage）")
    ap.add_argument("--qt", default="", help="Qt6 前缀（含 lib/cmake/Qt6）")
    ap.add_argument("--qmake", default="", help="qmake 路径（默认 <qt>/bin/qmake）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--linuxdeployqt", default="", help="linuxdeployqt 路径（默认 PATH 或自动下载）")
    ap.add_argument("--appimagetool", default="", help="appimagetool 路径（默认 PATH 或自动下载）")
    args = ap.parse_args()

    if not platform.is_linux():
        console.fail("package_linux.py 仅支持 Linux。macOS 用 package_macos.py，"
                     "Windows 用 build_msix.ps1。")
        return 1

    root = repo_root()
    gs_dir = root / "app" / "graph_studio"
    gs_build = gs_dir / "build"
    lib_build = root / "build"
    resources = gs_dir / "resources"
    out_dir = Path(args.out_dir) if args.out_dir else root / "dist" / "appimage"
    out_dir.mkdir(parents=True, exist_ok=True)
    tools_dir = root / "dist" / "tools"
    jobs = args.jobs or platform.cpu_count()

    cmake_exe = toolchain.find_cmake(args.cmake or None, build_dir=lib_build)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake 或用 --cmake 指定。")
        return 1
    qt_prefix = deps.find_qt(args.qt or None)
    opencv_dir = deps.find_opencv(args.opencv_dir or None)
    if not qt_prefix:
        console.fail("Qt6 未找到。用 --qt <prefix> 或设置 QT_PREFIX_PATH。")
        return 1
    qmake = Path(args.qmake) if args.qmake else qt_prefix / "bin" / "qmake"
    if not qmake.is_file():
        console.fail(f"qmake 未找到: {qmake}")
        return 1
    console.step(f"Qt6: {qt_prefix}  qmake: {qmake}")
    cm = CMake(cmake_exe)

    if not args.skip_build:
        if gs_sentry.ensure_fetched(root, not args.no_sentry) != 0:
            console.fail("拉取 sentry-native 失败")
            return 1
        code = build_stack(cm, root, lib_build, gs_dir, gs_build, args.config, jobs,
                           qt_prefix, opencv_dir, args.dsn, args.sentry_release,
                           args.clean)
        if code != 0:
            return code

    appdir = out_dir / "AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)
    if stage_appdir(appdir, gs_build, lib_build, resources, args.config) != 0:
        return 1

    linuxdeployqt = ensure_appimagetool("linuxdeployqt", LINUXDEPLOYQT_URL,
                                        args.linuxdeployqt, tools_dir)
    appimagetool = ensure_appimagetool("appimagetool", APPIMAGETOOL_URL,
                                       args.appimagetool, tools_dir)
    if not linuxdeployqt.is_file() or not appimagetool.is_file():
        console.fail("linuxdeployqt / appimagetool 获取失败")
        return 1

    if run_linuxdeployqt(linuxdeployqt, appdir, qmake) != 0:
        return 1
    write_apprun(appdir)

    arch = os.environ.get("ARCH", "x86_64")
    out_path = out_dir / f"{APP_NAME}-{args.version}-{arch}.AppImage"
    if run_appimagetool(appimagetool, appdir, out_path) != 0:
        return 1
    console.ok(f"打包完成: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
