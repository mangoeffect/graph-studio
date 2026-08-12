#!/usr/bin/env python3
"""build_opencv_android.py — 用 Android NDK 交叉编译 OpenCV 静态库（跨平台）。

取代 scripts/build_opencv_android.sh。产出 libopencv_{core,imgproc,imgcodecs}.a
安装到 build_android/opencv/install/，供 task_graph Android build 链接。

用法:
  python scripts/build_opencv_android.py                          # 自动 clone OpenCV 4.x
  python scripts/build_opencv_android.py /path/to/opencv/src      # 指定本地 OpenCV 源码
  python scripts/build_opencv_android.py --modules "core,imgproc" # 指定模块
  python scripts/build_opencv_android.py --abi armeabi-v7a        # 指定 ABI（默认 arm64-v8a）
  python scripts/build_opencv_android.py --api 24                 # Android API level（默认 21）

前置：设置 ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import android, console, platform, repo_root, runner, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

OPENCV_REPO = "https://github.com/opencv/opencv.git"
OPENCV_BRANCH = "4.x"


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="用 Android NDK 交叉编译 OpenCV 静态库")
    ap.add_argument("opencv_src", nargs="?", default="", help="本地 OpenCV 源码目录（不指定则自动 clone 4.x）")
    ap.add_argument("--modules", default="core,imgproc,imgcodecs", help="编译的 OpenCV 模块（CSV）")
    ap.add_argument("--abi", default="arm64-v8a", help="目标 ABI（默认 arm64-v8a）")
    ap.add_argument("--api", default="21", help="Android API level（默认 21）")
    ap.add_argument("--clean", action="store_true", help="先清空构建目录")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()

    ndk = android.find_ndk()
    if not ndk:
        console.fail("找不到 Android NDK。请设置 ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量")
        return 1
    toolchain_file = android.ndk_toolchain(ndk)
    if not toolchain_file:
        console.fail(f"NDK toolchain 文件不存在: {ndk}/build/cmake/android.toolchain.cmake")
        return 1

    build_dir = root / "build_android" / "opencv"
    install_dir = build_dir / "install"

    # 源码：显式 > 自动 clone
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

    cmake_exe = toolchain.find_cmake()
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake 3.16+.")
        return 1
    # NDK 强制单配置生成器，显式关掉 multi_config
    cm = CMake(cmake_exe, multi_config=False)

    defines = [
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
        f"-DANDROID_ABI={args.abi}",
        f"-DANDROID_PLATFORM=android-{args.api}",
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
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
    ]
    console.step(f"Configuring OpenCV Android ({args.abi}, API {args.api}, modules: {args.modules})")
    code = cm.configure(src, build_dir, defines=defines, build_type="Release")
    if code != 0:
        return code

    code = cm.build(build_dir, jobs=jobs, what=f"Building OpenCV Android (-j {jobs})")
    if code != 0:
        return code

    code = cm.install(build_dir)
    if code != 0:
        return code

    print()
    console.ok("OpenCV Android build complete")
    print(f"Install prefix: {install_dir}")
    print("Libraries:")
    if install_dir.is_dir():
        libs = sorted((install_dir / "lib").glob("*.a")) if (install_dir / "lib").is_dir() else []
        for lib in libs:
            print(f"  {lib}")
        if not libs:
            print("  (no .a files found)")
    print()
    print("To enable OpenCV in task_graph Android build:")
    print("  scripts/build_android.py  (会自动检测 build_android/opencv/install/)")
    return 0


if __name__ == "__main__":
    sys.exit(main())