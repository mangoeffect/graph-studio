#!/usr/bin/env python3
"""run_graph_studio_wasm.py — 构建 WASM 版 GraphStudio 并启动 dev server（跨平台）。

取代 scripts/run_graph_studio_wasm.sh。

流程:
  1) 用 emsdk 多线程编译 libtask_graph.a（带 -pthread）到 build_wasm/
  2) 用 qt-cmake (Qt/<ver>/wasm_multithread) 配置 + 构建 graph_studio wasm 到
     app/graph_studio/build_wasm/
  3) 启动 Python dev server（带 COOP/COEP header，启用 SharedArrayBuffer）

用法:
  python scripts/run_graph_studio_wasm.py                # 构建 + 启动 server
  python scripts/run_graph_studio_wasm.py --build-only   # 只构建，不启动 server
  python scripts/run_graph_studio_wasm.py --no-build     # 跳过构建，直接启 server
  python scripts/run_graph_studio_wasm.py --port 9000    # 指定 server 端口
  python scripts/run_graph_studio_wasm.py --clean        # 清空两个 build 目录

环境要求:
  - emsdk 已安装并设 EMSDK_ROOT（或让 emcmake 在 PATH 上）
  - Qt wasm + host 已安装，设 QT_WASM_ROOT / QT_HOST_ROOT（或让其在 PATH / 默认位置）
  - Python 3（自带 http.server，wasm_dev_server.py）
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, emsdk, platform, repo_root, runner, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402


def human_size(n: int) -> str:
    for unit in ("B", "K", "M", "G"):
        if n < 1024:
            return f"{n}B" if unit == "B" else f"{n:.1f}{unit}"
        n //= 1024
    return f"{n}G"


def find_qt_cmake(qt_wasm_root: Path) -> Path:
    name = "qt-cmake.bat" if platform.is_windows() else "qt-cmake"
    return qt_wasm_root / "bin" / name


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建 WASM 版 GraphStudio 并启动 dev server")
    ap.add_argument("--build-only", action="store_true", help="只构建，不启动 server")
    ap.add_argument("--no-build", action="store_true", help="跳过构建，直接启 server")
    ap.add_argument("--port", default="8000", help="dev server 端口（默认 8000）")
    ap.add_argument("--clean", action="store_true", help="清空两个 build 目录")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("--emsdk-root", default="", help="emsdk 根目录（默认 $EMSDK_ROOT/$EMSDK）")
    ap.add_argument("--qt-wasm-root", default="", help="Qt wasm 前缀（默认 $QT_WASM_ROOT）")
    ap.add_argument("--qt-host-root", default="", help="Qt host 前缀（默认 $QT_HOST_ROOT）")
    args = ap.parse_args()

    root = repo_root()
    lib_build = root / "build_wasm"
    gs_build = root / "app" / "graph_studio" / "build_wasm"
    jobs = args.jobs or platform.cpu_count()

    emsdk_root = emsdk.find_emsdk_root(args.emsdk_root or None)
    qt_wasm_root = Path(args.qt_wasm_root or os.environ.get("QT_WASM_ROOT", ""))
    qt_host_root = Path(args.qt_host_root or os.environ.get("QT_HOST_ROOT", ""))

    if not emsdk_root:
        console.fail("找不到 emsdk。请安装 emsdk 并设 EMSDK_ROOT 环境变量。")
        return 1
    qt_cmake = find_qt_cmake(qt_wasm_root)
    if not qt_cmake.is_file():
        console.fail(f"找不到 Qt wasm qt-cmake: {qt_cmake}（设 QT_WASM_ROOT）")
        return 1

    if args.clean:
        console.step("清理 WASM 构建目录")
        shutil.rmtree(lib_build, ignore_errors=True)
        shutil.rmtree(gs_build, ignore_errors=True)

    if not args.no_build:
        console.step("激活 emsdk")
        emsdk.activate(emsdk_root)
        cmake_exe = toolchain.find_cmake()
        if not cmake_exe or not cmake_exe.is_file():
            console.fail("cmake not found.")
            return 1
        emcmake = emsdk.find_emcmake(emsdk_root)

        # 1) 构建 libtask_graph.a（多线程：-pthread）
        # 若 OpenCV WASM 静态库已构建，则核心库也开 OpenCV。
        lib_defines = [
            f"-DCMAKE_TOOLCHAIN_FILE={emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_FLAGS=-pthread",
            "-DCMAKE_C_FLAGS=-pthread",
            "-DCMAKE_EXE_LINKER_FLAGS=-pthread -sUSE_PTHREADS=1",
        ]
        if (root / "build_wasm" / "opencv" / "install" / "lib" / "cmake" / "opencv4").is_dir():
            console.step("检测到 OpenCV WASM 库，核心库启用 OpenCV")
            lib_defines.append("-DTASK_GRAPH_ENABLE_OPENCV=ON")
        else:
            lib_defines.append("-DTASK_GRAPH_ENABLE_OPENCV=OFF")

        console.step("构建 libtask_graph.a (WASM + pthread)")
        # emscripten toolchain 强制单配置 Makefiles
        cm = CMake(cmake_exe, multi_config=False)
        if cm.configure(root, lib_build, defines=lib_defines, build_type="Release") != 0:
            return 1
        if cm.build(lib_build, target="task_graph", jobs=jobs, what="构建 libtask_graph.a") != 0:
            return 1

        # 2) 配置 + 构建 graph_studio WASM（用 qt-cmake）
        console.step("配置 graph_studio WASM")
        qt_args = [str(qt_cmake), "-S", str(root / "app" / "graph_studio"), "-B", str(gs_build),
                   "-DCMAKE_BUILD_TYPE=Release", f"-DQT_HOST_PATH={qt_host_root}"]
        env = dict(os.environ, EMSDK=str(emsdk_root))
        code = runner.check(qt_args, env=env, what="配置 graph_studio WASM")
        if code != 0:
            return code
        code = cm.build(gs_build, jobs=jobs, what=f"构建 graph_studio.wasm (-j {jobs})")
        if code != 0:
            return code

    if not (gs_build / "graph_studio.html").is_file():
        console.fail("未找到 graph_studio.html，构建失败")
        return 1

    wasm = gs_build / "graph_studio.wasm"
    if wasm.is_file():
        console.ok(f"构建完成: {wasm} ({human_size(wasm.stat().st_size)})")

    # 预压缩 .wasm / .js / .html（dev server 会按 Accept-Encoding 返回 .br 版本）
    brotli = None
    if shutil.which("brotli"):
        console.step("brotli 预压缩")
        for ext in ("wasm", "js", "html"):
            f = gs_build / f"graph_studio.{ext}"
            if f.is_file():
                subprocess.run(["brotli", "-q", "11", "-k", "-f", str(f)], check=False)
        br = gs_build / "graph_studio.wasm.br"
        if br.is_file():
            print(f"    brotli: graph_studio.wasm.br ({human_size(br.stat().st_size)})")

    if args.build_only:
        return 0

    # 3) 启动 dev server（COOP/COEP）
    console.step(f"启动 dev server (COOP+COEP, port {args.port})")
    print(f"    浏览器访问: http://localhost:{args.port}/graph_studio.html")
    print("    Ctrl+C 停止")
    dev_server = Path(__file__).resolve().parent / "wasm_dev_server.py"
    server_env = dict(os.environ, PORT=args.port)
    return subprocess.run([sys.executable, str(dev_server), str(gs_build)], env=server_env).returncode


if __name__ == "__main__":
    sys.exit(main())