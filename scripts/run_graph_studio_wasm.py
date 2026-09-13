#!/usr/bin/env python3
"""run_graph_studio_wasm.py — 构建 WASM 版 GraphStudio 并启动 dev server（跨平台）。

取代 scripts/run_graph_studio_wasm.sh。

流程:
  1) 用 emsdk 多线程编译 libtask_graph.a（带 -pthread）到 build_wasm/
  2) 用 qt-cmake (Qt/<ver>/wasm_multithread) 配置 + 构建 graph_studio wasm 到
     app/graph_studio/build_wasm/
  3) 启动 Python dev server（带 COOP/COEP header，启用 SharedArrayBuffer）

用法:
  python scripts/run_graph_studio_wasm.py                # 构建 + 启动 server + 自动开浏览器
  python scripts/run_graph_studio_wasm.py --build-only   # 只构建，不启动 server
  python scripts/run_graph_studio_wasm.py --no-build     # 跳过构建，直接启 server
  python scripts/run_graph_studio_wasm.py --no-browser   # server 起来后不自动开浏览器
  python scripts/run_graph_studio_wasm.py --port 9000    # 指定 server 端口（占用即报错；
                                                         # 默认 8000 被占时自动顺延找空闲）
  python scripts/run_graph_studio_wasm.py --clean        # 清空两个 build 目录

环境要求:
  - emsdk 已安装并设 EMSDK_ROOT（或让 emcmake 在 PATH 上）
  - Qt wasm + host 已安装：显式设 QT_WASM_ROOT / QT_HOST_ROOT，或放在 Qt
    在线安装器默认布局（~/Qt/<ver>/wasm_multithread + 同版本本机 kit）自动探测
  - Python 3（自带 http.server，wasm_dev_server.py）
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, emsdk, platform, repo_root, runner, toolchain  # noqa: E402
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


def port_bindable(port: int) -> bool:
    """与 wasm_dev_server 同语义的占用探测（AF_INET + SO_REUSEADDR + 全接口）。"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("", port))
            return True
        except OSError:
            return False


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建 WASM 版 GraphStudio 并启动 dev server")
    ap.add_argument("--build-only", action="store_true", help="只构建，不启动 server")
    ap.add_argument("--no-build", action="store_true", help="跳过构建，直接启 server")
    ap.add_argument("--port", default="",
                    help="dev server 端口（默认 8000，被占用时自动顺延找空闲端口）")
    ap.add_argument("--no-browser", action="store_true",
                    help="server 启动后不自动打开浏览器")
    ap.add_argument("--clean", action="store_true", help="清空两个 build 目录")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("--wgpu", action="store_true",
                    help="wasm 编入 wgpu 统一后端（核心库 WGPU=ON + 链接 -sUSE_WEBGPU；"
                         "浏览器内初始化优雅失败——真实初始化是独立里程碑。"
                         "默认关闭：发布 CI 在浏览器验证完成前不受影响）")
    ap.add_argument("--emsdk-root", default="", help="emsdk 根目录（默认 $EMSDK_ROOT/$EMSDK）")
    ap.add_argument("--qt-wasm-root", default="",
                    help="Qt wasm 前缀（默认 $QT_WASM_ROOT，回退探测 ~/Qt/<ver>/wasm_multithread）")
    ap.add_argument("--qt-host-root", default="",
                    help="Qt host 前缀（默认 $QT_HOST_ROOT/$QT_HOST_PATH，回退 wasm 同版本目录的本机 kit）")
    args = ap.parse_args()

    root = repo_root()
    lib_build = root / "build_wasm"
    gs_build = root / "app" / "graph_studio" / "build_wasm"
    jobs = args.jobs or platform.cpu_count()

    emsdk_root = emsdk.find_emsdk_root(args.emsdk_root or None)
    qt_wasm_root = deps.find_qt_wasm(args.qt_wasm_root or None)
    # QT_HOST_ROOT 为本仓库约定；QT_HOST_PATH 是 Qt 官方变量名，一并识别；
    # 都没设时回退 wasm 前缀同版本目录下的本机 kit（~/Qt/<ver>/macos 等）
    qt_host_root = deps.find_qt_host(args.qt_host_root or None, qt_wasm_root)

    if not emsdk_root:
        console.fail("找不到 emsdk。请安装 emsdk 并设 EMSDK_ROOT 环境变量。")
        return 1
    qt_cmake = find_qt_cmake(qt_wasm_root) if qt_wasm_root else None
    if not qt_cmake or not qt_cmake.is_file():
        console.fail("找不到 Qt wasm qt-cmake：设 QT_WASM_ROOT 或 --qt-wasm-root"
                     "（默认探测 ~/Qt/<ver>/wasm_multithread，需含 bin/qt-cmake）")
        return 1
    if not qt_host_root or not deps.validate_qt(qt_host_root):
        console.fail("找不到 Qt host 前缀：设 QT_HOST_ROOT / QT_HOST_PATH 或"
                     " --qt-host-root（wasm 交叉构建需同版本桌面 Qt，"
                     "默认取 ~/Qt/<ver>/macos 等本机 kit）")
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

        # 0) MNN WASM 引擎（TASK_GRAPH_ENABLE_MNN 默认 ON，核心库自动探测
        # build_wasm/mnn/install；缺失时 stub 降级，仅影响 MNN 任务）
        mnn_lib = root / "build_wasm" / "mnn" / "install" / "lib" / "libMNN.a"
        if not mnn_lib.is_file():
            console.step("构建 MNN WASM 静态库（build_mnn.py --platform wasm）")
            code = runner.check(
                [sys.executable, str(root / "scripts" / "build_mnn.py"),
                 "--platform", "wasm", "-j", str(jobs),
                 "--emsdk-root", str(emsdk_root)],
                cwd=str(root), what="构建 MNN WASM",
            )
            if code != 0 or not mnn_lib.is_file():
                console.warn("MNN WASM 构建失败，task_graph 以 stub 降级（仅影响 MNN 任务）")

        # 1) 构建 libtask_graph.a（多线程：-pthread）
        # -fexceptions：核心库 DAG API 按异常语义报错（dag.cpp throw），
        # app 的 GraphModel 全程 try/catch 消费；wasm 默认禁异常会让 throw
        # 直接 trap（浏览器白屏）。全链路（编译+链接）显式开启。
        # 若 OpenCV WASM 静态库已构建，则核心库也开 OpenCV。
        lib_defines = [
            f"-DCMAKE_TOOLCHAIN_FILE={emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_FLAGS=-pthread -fexceptions",
            "-DCMAKE_C_FLAGS=-pthread",
            "-DCMAKE_EXE_LINKER_FLAGS=-pthread -sUSE_PTHREADS=1 -fexceptions",
        ]
        if (root / "build_wasm" / "opencv" / "install" / "lib" / "cmake" / "opencv4").is_dir():
            console.step("检测到 OpenCV WASM 库，核心库启用 OpenCV")
            lib_defines.append("-DTASK_GRAPH_ENABLE_OPENCV=ON")
        else:
            lib_defines.append("-DTASK_GRAPH_ENABLE_OPENCV=OFF")
        if args.wgpu:
            lib_defines.append("-DTASK_GRAPH_ENABLE_WGPU=ON")

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
                   "-DCMAKE_BUILD_TYPE=Release", f"-DQT_HOST_PATH={qt_host_root}",
                   # Qt 6.6 wasm 交叉编译要求显式 host cmake 包目录
                   f"-DQT_HOST_PATH_CMAKE_DIR={qt_host_root}/lib/cmake",
                   # 与核心库一致：app 侧 try/catch（GraphModel 等）需要异常表
                   "-DCMAKE_CXX_FLAGS=-fexceptions",
                   "-DCMAKE_EXE_LINKER_FLAGS=-fexceptions"]
        if args.wgpu:
            # app 的 EMSCRIPTEN 块按此开关链接 -sUSE_WEBGPU（GpuBootstrap 编入
            # wgpu 路径；浏览器内 init 优雅失败）
            qt_args.append("-DTASK_GRAPH_ENABLE_WGPU=ON")
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

    # 3) 启动 dev server（COOP/COEP）——显式指定端口被占则报错退出；
    # 默认端口被占（常见于上次运行残留的 dev server）自动顺延
    if args.port:
        port = int(args.port)
        if not port_bindable(port):
            console.fail(f"端口 {port} 已被占用（lsof -nP -iTCP:{port} -sTCP:LISTEN "
                         "查看占用进程），换 --port 或结束该进程")
            return 1
    else:
        port = next((p for p in range(8000, 8020) if port_bindable(p)), 0)
        if not port:
            console.fail("端口 8000-8019 均被占用，请用 --port 指定空闲端口")
            return 1
    url = f"http://localhost:{port}/graph_studio.html"
    console.step(f"启动 dev server (COOP+COEP, port {port})")
    print(f"    浏览器访问: {url}")
    if not args.port and port != 8000:
        print(f"    （默认端口 8000 被占用，已顺延到 {port}）")
    print("    Ctrl+C 停止")
    dev_server = Path(__file__).resolve().parent / "wasm_dev_server.py"
    server_env = dict(os.environ, PORT=str(port))
    if not args.no_browser:
        # server 在 socket 就绪后打开默认浏览器（见 wasm_dev_server.py）
        server_env["OPEN_URL"] = url
    return subprocess.run([sys.executable, str(dev_server), str(gs_build)], env=server_env).returncode


if __name__ == "__main__":
    sys.exit(main())