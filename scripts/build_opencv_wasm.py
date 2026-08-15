#!/usr/bin/env python3
"""build_opencv_wasm.py — 用 emscripten 编译 OpenCV 静态库（WASM 多线程）（跨平台）。

取代 scripts/build_opencv_wasm.sh。产出 libopencv_{core,imgproc,imgcodecs}.a
安装到 build_wasm/opencv/install/，供 task_graph WASM build 链接。

用法:
  python scripts/build_opencv_wasm.py                          # 自动 clone OpenCV 4.x
  python scripts/build_opencv_wasm.py /path/to/opencv/src      # 指定本地 OpenCV 源码
  python scripts/build_opencv_wasm.py --modules "core,imgproc" # 指定模块（默认 core+imgproc+imgcodecs）

前置: 安装 emsdk 并设 EMSDK_ROOT 环境变量（或让 emcmake 已在 PATH 上）。
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, emsdk, platform, repo_root, runner, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

OPENCV_REPO = "https://github.com/opencv/opencv.git"
OPENCV_BRANCH = "4.x"


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="用 emscripten 编译 OpenCV 静态库（WASM 多线程）")
    ap.add_argument("opencv_src", nargs="?", default="", help="本地 OpenCV 源码目录（不指定则自动 clone 4.x）")
    ap.add_argument("--modules", default="core,imgproc,imgcodecs", help="编译的 OpenCV 模块（CSV）")
    ap.add_argument("--emsdk-root", default="", help="emsdk 根目录（默认 $EMSDK_ROOT/$EMSDK）")
    ap.add_argument("--clean", action="store_true", help="先清空构建目录")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()

    emsdk_root = emsdk.find_emsdk_root(args.emsdk_root or None)
    if not emsdk_root:
        console.fail("找不到 emsdk。请安装 emsdk 并设 EMSDK_ROOT 环境变量。")
        return 1
    emsdk.activate(emsdk_root)
    emcmake = emsdk.find_emcmake(emsdk_root)
    if not emcmake:
        console.fail(f"找不到 emcmake（在 {emsdk_root} 下）")
        return 1
    cmake_exe = toolchain.find_cmake()
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake 3.16+.")
        return 1

    build_dir = root / "build_wasm" / "opencv"
    install_dir = build_dir / "install"

    src = Path(args.opencv_src) if args.opencv_src else (build_dir / "opencv-src")
    if not args.opencv_src:
        if not src.is_dir():
            console.step(f"Cloning OpenCV {OPENCV_BRANCH} source to {src}")
            src.parent.mkdir(parents=True, exist_ok=True)
            code = runner.run([
                "git", "clone", "--depth", "1", "--branch", OPENCV_BRANCH, OPENCV_REPO, str(src),
            ])
            if code != 0:
                console.fail(f"git clone OpenCV 失败 (exit {code})")
                return code
        else:
            print(f"==> Using existing OpenCV source at {src}")

    if not (src / "CMakeLists.txt").is_file():
        console.fail(f"OpenCV source not found at {src}")
        return 1

    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir, ignore_errors=True)

    defines = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=OFF",
        f"-DBUILD_LIST={args.modules}",
        "-DBUILD_TESTS=OFF",
        "-DBUILD_PERF_TESTS=OFF",
        "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_DOCS=OFF",
        "-DBUILD_opencv_apps=OFF",
        "-DBUILD_opencv_gapi=OFF",
        "-DWITH_ADE=OFF",
        "-DWITH_FFMPEG=OFF",
        "-DWITH_GTK=OFF",
        "-DWITH_V4L=OFF",
        "-DWITH_OPENCL=OFF",
        "-DWITH_PROTOBUF=OFF",
        "-DWITH_TBB=OFF",
        "-DWITH_OPENMP=OFF",
        "-DWITH_IPP=OFF",
        "-DWITH_LAPACK=OFF",
        "-DWITH_ITT=OFF",
        "-DWITH_EIGEN=OFF",
        "-DENABLE_PIC=OFF",
        "-DCV_ENABLE_INTRINSICS=OFF",
        "-DCPU_BASELINE=",
        "-DCPU_DISPATCH=",
        "-DCMAKE_C_FLAGS=-pthread",
        "-DCMAKE_CXX_FLAGS=-pthread",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
    ]
    # emcmake 是 cmake 的 emscripten 包装器，canonical 用法是显式带 `cmake`
    # 子命令（新版本 emcmake 对裸 flags 会把首个参数当可执行文件）：
    # emcmake cmake -S ... -B ...
    console.step(f"Configuring OpenCV WASM (modules: {args.modules})")
    code = runner.check([str(emcmake), "cmake", "-S", str(src), "-B", str(build_dir)] + defines, what="配置 OpenCV WASM")
    if code != 0:
        return code

    # WASM 是单配置 Makefiles（emcmake 强制）
    cm = CMake(cmake_exe, multi_config=False)
    code = cm.build(build_dir, jobs=jobs, what=f"Building OpenCV WASM (-j {jobs})")
    if code != 0:
        return code
    code = cm.install(build_dir)
    if code != 0:
        return code

    print()
    console.ok("OpenCV WASM build complete")
    print(f"Install prefix: {install_dir}")
    print("Libraries:")
    lib_dir = install_dir / "lib"
    if lib_dir.is_dir():
        libs = sorted(lib_dir.glob("*.a"))
        for lib in libs:
            print(f"  {lib}")
        if not libs:
            print("  (no .a files found)")
    print()
    print("To enable OpenCV in task_graph WASM build:")
    print("  cmake ... -DTASK_GRAPH_ENABLE_OPENCV=ON")
    print(f"  (OpenCV_DIR will be auto-detected from {install_dir}/lib/cmake/opencv4)")
    return 0


if __name__ == "__main__":
    sys.exit(main())