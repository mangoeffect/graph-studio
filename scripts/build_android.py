#!/usr/bin/env python3
"""build_android.py — 用 Android NDK 交叉编译 task_graph + 子模块为静态库（跨平台）。

取代 scripts/build_android.sh。NDK 工具链本身在 macOS/Linux/Windows 都能跑，
所以本脚本三平台通用。

流程:
  1) 用 NDK toolchain 配置 + 构建 libtask_graph.a + 子模块 .a
  2) 合并核心库 + 子模块为 libtask_graph.a（llvm-ar MRI 脚本）
  3) 复制公开头文件到 dist/android/

用法:
  python scripts/build_android.py                       # 构建 arm64-v8a
  python scripts/build_android.py --abi armeabi-v7a     # 指定 ABI
  python scripts/build_android.py --api 24              # Android API level（默认 21）
  python scripts/build_android.py --no-opencv           # 跳过 OpenCV 子模块
  python scripts/build_android.py --also-x86-64         # 同时构建 x86_64（模拟器调试）
  python scripts/build_android.py --clean               # 清空构建目录
  python scripts/build_android.py -j <N>                # 并行编译线程数

环境要求:
  - ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量指向 NDK 根目录
  - CMake 3.16+
  - (可选) scripts/build_opencv_android.py 已执行，产出 build_android/opencv/install/
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import android, console, platform, repo_root, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

OPENCV_INSTALL_REL = "build_android/opencv/install"
SUBMODULE_TARGETS = ["image_filtering", "image_reader"]


def merge_static_libs(build_dir: Path, dist_dir: Path, llvm_ar: Path, opencv_available: bool,
                      root: Path) -> int:
    """用 llvm-ar MRI 脚本合并核心 + 子模块 + OpenCV 静态库为单个 libtask_graph.a。"""
    output = dist_dir / "libtask_graph.a"
    dist_dir.mkdir(parents=True, exist_ok=True)

    libs: List[Path] = [build_dir / "libtask_graph.a"]
    for sub in SUBMODULE_TARGETS:
        lib = build_dir / f"lib{sub}.a"
        if lib.is_file():
            libs.append(lib)

    if opencv_available:
        oc_dir = root / OPENCV_INSTALL_REL / "lib"
        if oc_dir.is_dir():
            # 先收 libopencv_*.a
            for lib in sorted(oc_dir.glob("libopencv_*.a")):
                if lib.is_file():
                    libs.append(lib)
            # 再收非 opencv 的辅助 .a（libcpufeatures / libtegra_hal 等）
            for lib in sorted(oc_dir.glob("lib*.a")):
                if lib.is_file() and not lib.name.startswith("libopencv_"):
                    libs.append(lib)

    console.step(f"合并静态库 -> {output} ({len(libs)} 个库)")
    libs = [l for l in libs if l.is_file()]
    if not libs:
        console.fail(f"找不到任何待合并的 .a（期望 {build_dir / 'libtask_graph.a'}）")
        return 1

    mri = [f"CREATE {output}"]
    for lib in libs:
        mri.append(f"ADDLIB {lib}")
    mri.append("SAVE")
    mri.append("END")
    mri_text = "\n".join(mri) + "\n"

    proc = subprocess.run([str(llvm_ar), "-M"], input=mri_text.encode("utf-8"))
    if proc.returncode != 0:
        console.fail(f"llvm-ar 合并失败 (exit {proc.returncode})")
        return proc.returncode
    return 0


def build_abi(abi: str, root: Path, cmake: CMake, toolchain_file: Path, api_level: str,
              jobs: int, opencv_available: bool, llvm_ar: Path) -> int:
    build_dir = root / f"build_android_{abi}"
    dist_dir = root / "dist" / "android" / abi

    console.step(f"构建 Android {abi} (API {api_level})")

    defines = [
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
        f"-DANDROID_ABI={abi}",
        f"-DANDROID_PLATFORM=android-{api_level}",
        f"-DCMAKE_BUILD_TYPE=Release",
    ]
    if opencv_available:
        defines.append("-DTASK_GRAPH_ENABLE_OPENCV=ON")

    # NDK 工具链强制单配置生成器（Unix Makefiles），即使在 Windows host 上也是单配置。
    code = cmake.configure(root, build_dir, defines=defines, build_type="Release")
    if code != 0:
        return code
    code = cmake.build(build_dir, target="task_graph", jobs=jobs, what=f"构建 task_graph ({abi})")
    if code != 0:
        return code

    # 子模块（Metal 不可用，gpu_image_processing 自动跳过）；失败不致命
    for sub in SUBMODULE_TARGETS:
        if sub in ("image_filtering", "image_reader") and not opencv_available:
            continue
        cmake.build(build_dir, target=sub, jobs=jobs, what=f"构建子模块 {sub}")

    return merge_static_libs(build_dir, dist_dir, llvm_ar, opencv_available, root)


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="用 Android NDK 交叉编译 task_graph + 子模块为静态库")
    ap.add_argument("--abi", default="arm64-v8a", help="目标 ABI（默认 arm64-v8a）")
    ap.add_argument("--api", default="21", help="Android API level（默认 21）")
    ap.add_argument("--no-opencv", action="store_true", help="跳过 OpenCV 子模块")
    ap.add_argument("--also-x86-64", action="store_true", help="同时构建 x86_64（模拟器调试）")
    ap.add_argument("--clean", action="store_true", help="清空构建目录")
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
    llvm_ar = android.find_llvm_ar(ndk)
    if not llvm_ar:
        console.fail("找不到 llvm-ar，请检查 NDK 安装")
        return 1

    cmake_exe = toolchain.find_cmake()
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake 3.16+ or pass --cmake <path>.")
        return 1
    # NDK 强制单配置生成器，显式关掉 multi_config
    cm = CMake(cmake_exe, multi_config=False)

    opencv_available = (not args.no_opencv) and \
        (root / OPENCV_INSTALL_REL / "lib" / "cmake" / "opencv4").is_dir()
    if not args.no_opencv:
        if opencv_available:
            console.step("检测到 OpenCV Android 库，启用 OpenCV")
        else:
            console.step("未检测到 OpenCV Android 库（运行 scripts/build_opencv_android.py 构建），跳过 OpenCV 子模块")

    if args.clean:
        console.step("清理 Android 构建目录")
        shutil.rmtree(root / "build_android", ignore_errors=True)
        shutil.rmtree(root / "dist" / "android", ignore_errors=True)

    code = build_abi(args.abi, root, cm, toolchain_file, args.api, jobs, opencv_available, llvm_ar)
    if code != 0:
        return code

    if args.also_x86_64:
        code = build_abi("x86_64", root, cm, toolchain_file, args.api, jobs, opencv_available, llvm_ar)
        if code != 0:
            return code

    # 复制头文件
    console.step("复制头文件到 dist/android/include/")
    inc_dst = root / "dist" / "android" / "include"
    inc_dst.mkdir(parents=True, exist_ok=True)
    inc_src = root / "include"
    if inc_src.is_dir():
        for item in inc_src.iterdir():
            dst_item = inc_dst / item.name
            if item.is_dir():
                if dst_item.exists():
                    shutil.rmtree(dst_item)
                shutil.copytree(item, dst_item)
            else:
                shutil.copy2(item, dst_item)

    console.ok("构建完成")
    print(f"    dist/android/{args.abi}/libtask_graph.a")
    if args.also_x86_64:
        print("    dist/android/x86_64/libtask_graph.a")
    print("    dist/android/include/")
    return 0


if __name__ == "__main__":
    sys.exit(main())