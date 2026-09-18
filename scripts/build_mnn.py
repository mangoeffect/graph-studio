#!/usr/bin/env python3
"""build_mnn.py — 源码构建 Alibaba MNN 推理引擎静态库，供 task_graph 链接（跨平台）。

产出单一静态库 libMNN.a（Windows: MNN.lib），安装到各平台 install 根（对齐
OpenCV 预编译目录约定，主 CMakeLists 在 -DTASK_GRAPH_ENABLE_MNN=ON 时按平台探测）：
  - macOS/Linux/Windows: build/mnn/install
  - iOS:                 build_ios/mnn/install
  - Android:             build_android/mnn/install/<abi>/（按 ABI 分目录，CMake 按
                         ANDROID_ABI 探测；不做 fat 合并——app 只链单一 ABI）
  - WASM:                build_wasm/mnn/install

为什么外置构建而不是 add_subdirectory/FetchContent：
  MNN 的 CMake 按顶层项目编写——向 CMAKE_CXX_FLAGS 全局注入 -fno-rtti
  -fno-exceptions -O3（会污染宿主 C++20 构建），嵌套 CMakeLists 误用
  CMAKE_SOURCE_DIR（上游 issue #3924），且无导出的 find_package target
  （issue #13）。外置构建下 MNN 是 top-level project，上述问题全部不成立。

用法:
  python scripts/build_mnn.py                        # 本机平台（macos/linux/windows）
  python scripts/build_mnn.py --platform ios         # iOS（arm64, CPU+Metal）
  python scripts/build_mnn.py --platform android     # Android（arm64-v8a [+armeabi-v7a]）
  python scripts/build_mnn.py --platform wasm        # Emscripten（单线程）
  python scripts/build_mnn.py --src /path/to/MNN     # 指定本地 MNN 源码
  python scripts/build_mnn.py --force                # 忽略已有产物强制重建

前置: cmake >= 3.13（Windows 需 Ninja + VS2017+）；Android 需 NDK；
      WASM 需 emsdk（EMSDK_ROOT）；iOS 在 macOS 上构建。
"""

import argparse
import shutil
import sys
from pathlib import Path
from typing import List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import android, console, emsdk, platform, repo_root, runner, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

MNN_VERSION = "3.6.1"
MNN_REPO = "https://github.com/alibaba/MNN.git"
# WASM 工具链钉版（对齐 run_graph_studio_wasm.py / release.yml 的 emsdk 版本）
EMSDK_VERSION = "3.1.46"

# 关闭一切非运行时组件（converter/tools/demo/test 等），只留推理 runtime。
COMMON_DEFINES = [
    "-DMNN_BUILD_SHARED_LIBS=OFF",   # 单一静态库，随 libtask_graph 链入
    "-DMNN_SEP_BUILD=OFF",           # 后端编进 libMNN 而非 dlopen 的分离 .so
    "-DMNN_BUILD_TOOLS=OFF",
    "-DMNN_BUILD_DEMO=OFF",
    "-DMNN_BUILD_TEST=OFF",
    "-DMNN_BUILD_BENCHMARK=OFF",
    "-DMNN_BUILD_CONVERTER=OFF",
    "-DMNN_BUILD_QUANTOOLS=OFF",
    "-DMNN_BUILD_TRAIN=OFF",
    "-DMNN_BUILD_CODEGEN=OFF",
    "-DMNN_EVALUATION=OFF",
    "-DMNN_BUILD_LLM=OFF",
    "-DMNN_BUILD_LLM_OMNI=OFF",
    "-DMNN_BUILD_DIFFUSION=OFF",
    "-DMNN_BUILD_OPENCV=OFF",
    "-DMNN_BUILD_AUDIO=OFF",
    "-DMNN_JNI=OFF",
    "-DMNN_USE_LOGCAT=OFF",
]


def install_root(root: Path, plat: str) -> Path:
    if plat == "ios":
        return root / "build_ios" / "mnn" / "install"
    if plat == "android":
        return root / "build_android" / "mnn" / "install"
    if plat == "wasm":
        return root / "build_wasm" / "mnn" / "install"
    return root / "build" / "mnn" / "install"


def clone_mnn(src: Path, version: str) -> int:
    if (src / "CMakeLists.txt").is_file() and (src / "include" / "MNN").is_dir():
        print(f"==> Using existing MNN source at {src}")
        return 0
    console.step(f"Cloning MNN {version} to {src}")
    src.parent.mkdir(parents=True, exist_ok=True)
    code = runner.run([
        "git", "clone", "--depth", "1", "--branch", version, MNN_REPO, str(src),
    ])
    if code != 0:
        console.fail(f"git clone MNN 失败 (exit {code})；tag 名见 https://github.com/alibaba/MNN/tags")
    return code


def collect_built_libs(build_dir: Path, plat: str, abi: str = "") -> List[Path]:
    """从构建树里找 MNN 静态库产物（不依赖 MNN 自身 install 规则的完整性）。"""
    pats = ["libMNN.a", "MNN.lib"] if plat == "windows" else ["libMNN.a"]
    found: List[Path] = []
    for pat in pats:
        found += list(build_dir.rglob(pat))
    # 排除 3rd_party/scratch 里可能的同名残留
    found = [p for p in found if "3rd_party" not in p.parts]
    if abi and len(found) > 1:
        found = [p for p in found if abi in str(p)] or found
    return sorted(set(found), key=lambda p: len(p.parts))


def copy_headers(src: Path, install_dir: Path) -> None:
    """手动拷贝 include/MNN —— 不依赖 MNN 的 install 规则（各版本差异大）。"""
    inc_src = src / "include" / "MNN"
    inc_dst = install_dir / "include" / "MNN"
    if inc_dst.exists():
        shutil.rmtree(inc_dst)
    inc_dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(inc_src, inc_dst)


def do_android(root: Path, src: Path, cmake_exe: Path, jobs: int, abis: List[str],
               install_dir: Path) -> int:
    ndk = android.find_ndk()
    if not ndk:
        console.fail("找不到 Android NDK。请安装并设 ANDROID_NDK / ANDROID_NDK_HOME。")
        return 1
    toolchain_file = android.ndk_toolchain(ndk)
    if not toolchain_file:
        console.fail(f"NDK 下找不到 android.toolchain.cmake: {ndk}")
        return 1

    android_dir = root / "build_android" / "mnn"
    for i, abi in enumerate(abis):
        arm82 = "ON" if abi == "arm64-v8a" else "OFF"
        build_dir = android_dir / f"build-{abi}"
        abi_install = install_dir / abi
        defines = COMMON_DEFINES + [
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
            f"-DANDROID_ABI={abi}",
            "-DANDROID_PLATFORM=android-24",
            f"-DMNN_ARM82={arm82}",
            "-DMNN_METAL=OFF",
            "-DMNN_OPENCL=OFF",   # 第一阶段 CPU；OpenCL 后端（dlopen）留待后续
        ]
        console.step(f"[Android {abi}] configure/build ({i + 1}/{len(abis)})")
        if build_one(cmake_exe, src, build_dir, defines, jobs) != 0:
            return 1
        libs = collect_built_libs(build_dir, "android", abi)
        if not libs:
            console.fail(f"[Android {abi}] 构建树中未找到 libMNN.a")
            return 1
        # 按 ABI 分目录安装（CMake 侧按 ANDROID_ABI 探测，不做 fat 合并——
        # 实际 app 构建只链接单一 ABI，合并是多余的归档操作）
        (abi_install / "lib").mkdir(parents=True, exist_ok=True)
        shutil.copy2(libs[0], abi_install / "lib" / "libMNN.a")
        copy_headers(src, abi_install)
    return 0


def build_one(cmake_exe: Path, src: Path, build_dir: Path, defines: List[str],
              jobs: int, target: str = "MNN") -> int:
    cm = CMake(cmake_exe, multi_config=False)
    if cm.configure(src, build_dir, defines=defines, build_type="Release") != 0:
        return 1
    if cm.build(build_dir, jobs=jobs, target=target, what=f"构建 MNN (-j {jobs})") != 0:
        return 1
    return 0


def do_wasm(root: Path, src: Path, jobs: int, install_dir: Path,
            emsdk_root_hint: str) -> int:
    emsdk_root = emsdk.find_emsdk_root(emsdk_root_hint or None)
    if not emsdk_root:
        console.fail("找不到 emsdk。请安装并设 EMSDK_ROOT 环境变量。")
        return 1
    # 与 run_graph_studio_wasm.py / release.yml 的工具链钉版一致（Qt wasm 包
    # 只发布到 6.6.3，配套 emsdk 3.1.x）。emscripten 新版（如 homebrew 6.x）
    # 的 libc++ 与 MNN 源不兼容，emcmake 必须取自该 emsdk root 而非 PATH。
    # 3.1.37：emscripten-releases 已清理 ≤3.1.45 的下载（404，
    # `emsdk activate 3.1.37` 在 CI 直接失败），升到同系列 3.1.46
    # （release.yml setup-emsdk 实际安装的版本）。
    if runner.check([sys.executable, str(emsdk_root / "emsdk.py"),
                     "activate", EMSDK_VERSION],
                    what=f"emsdk activate {EMSDK_VERSION}") != 0:
        console.fail(f"emsdk 激活失败（需要 {EMSDK_VERSION}）")
        return 1
    emsdk.activate(emsdk_root)
    emcmake = emsdk_root / "upstream" / "emscripten" / "emcmake"
    if not emcmake.is_file():
        found = emsdk.find_emcmake(emsdk_root)
        emcmake = found if found else emcmake
    if not Path(emcmake).is_file():
        console.fail(f"找不到 emcmake（在 {emsdk_root} 下）")
        return 1
    cmake_exe = toolchain.find_cmake()
    if not cmake_exe:
        console.fail("cmake not found. Install CMake 3.13+.")
        return 1

    build_dir = root / "build_wasm" / "mnn" / "build"
    defines = COMMON_DEFINES + [
        # 官方 WASM 配方：单线程、关线程池、关 SSE（SIMD 后续可选 -msimd128）
        "-DMNN_FORBID_MULTI_THREAD=ON",
        "-DMNN_USE_THREAD_POOL=OFF",
        "-DMNN_USE_SSE=OFF",
        "-DMNN_KLEIDIAI=OFF",
        "-DMNN_SME2=OFF",
        "-DMNN_METAL=OFF",
        # 消费方（libtask_graph.a -> graph_studio/tests）是 -pthread 的
        # shared-memory 模块：wasm-ld 要求链入的每个 .o 都带 atomics/bulk-memory
        # 特性，否则 "--shared-memory is disallowed by Interpreter.cpp.o"。
        # -matomics/-mbulk-memory 只开代码生成特性，不引入线程。
        "-DCMAKE_C_FLAGS=-matomics -mbulk-memory",
        "-DCMAKE_CXX_FLAGS=-matomics -mbulk-memory",
    ]
    # 工具链钉死且旧 CMakeCache 会缓存错误工具链（如 PATH 上其它 emscripten
    # 的 sysroot），缓存不可跨工具链复用——每次清空重建
    if build_dir.exists():
        shutil.rmtree(build_dir, ignore_errors=True)
    # emcmake 是 cmake 的 emscripten 包装器，canonical 用法带 `cmake` 子命令
    console.step("配置 MNN WASM (emcmake cmake)")
    if runner.check([str(emcmake), "cmake", "-S", str(src), "-B", str(build_dir),
                     "-DCMAKE_BUILD_TYPE=Release"] + defines, what="配置 MNN WASM") != 0:
        return 1
    cm = CMake(cmake_exe, multi_config=False)
    if cm.build(build_dir, jobs=jobs, target="MNN", what=f"构建 MNN WASM (-j {jobs})") != 0:
        return 1
    libs = collect_built_libs(build_dir, "wasm")
    if not libs:
        console.fail("WASM 构建树中未找到 libMNN.a")
        return 1
    install_dir.mkdir(parents=True, exist_ok=True)
    (install_dir / "lib").mkdir(parents=True, exist_ok=True)
    shutil.copy2(libs[0], install_dir / "lib" / "libMNN.a")
    copy_headers(src, install_dir)
    return 0


def do_desktop_or_ios(root: Path, src: Path, cmake_exe: Path, jobs: int,
                      plat: str, install_dir: Path) -> int:
    defines = list(COMMON_DEFINES)
    build_dir = (root / "build_ios" / "mnn" / "build") if plat == "ios" \
        else (root / "build" / "mnn" / f"build-{plat}")

    if plat == "macos":
        defines += ["-DMNN_METAL=ON"]
    elif plat == "ios":
        # 对齐官方 package_scripts/ios/buildiOS.sh：静态、CPU+Metal、fp16
        defines += [
            "-DCMAKE_SYSTEM_NAME=iOS",
            "-DCMAKE_OSX_SYSROOT=iphoneos",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
            "-DMNN_METAL=ON",
            "-DMNN_ARM82=ON",
            "-DMNN_USE_THREAD_POOL=OFF",
        ]
    elif plat == "windows":
        # 官方要求 cmake>=3.13 + Ninja + VS2017+（cl.exe 需在 PATH，经 vcvars）
        ninja = toolchain.find_tool("ninja")
        use_ninja = bool(ninja)
        if use_ninja:
            defines = ["-GNinja"] + defines
        else:
            console.warn("PATH 上没有 ninja；使用默认 VS 生成器（官方推荐 Ninja + VS2017+）")

    console.step(f"[{plat}] configure")
    # Windows 上 Ninja 是单配置；VS 生成器是多配置（build 需带 --config Release）
    cm = CMake(cmake_exe, multi_config=(plat == "windows" and not use_ninja))
    if cm.configure(src, build_dir, defines=defines, build_type="Release") != 0:
        return 1
    if cm.build(build_dir, config="Release", jobs=jobs, target="MNN",
                what=f"构建 MNN (-j {jobs})") != 0:
        return 1
    libs = collect_built_libs(build_dir, plat)
    if not libs:
        console.fail(f"[{plat}] 构建树中未找到 MNN 静态库")
        return 1
    install_dir.mkdir(parents=True, exist_ok=True)
    (install_dir / "lib").mkdir(parents=True, exist_ok=True)
    lib_name = "MNN.lib" if plat == "windows" else "libMNN.a"
    shutil.copy2(libs[0], install_dir / "lib" / lib_name)
    copy_headers(src, install_dir)
    return 0


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="源码构建 MNN 静态库供 task_graph 链接")
    ap.add_argument("--platform", default="auto",
                    choices=["auto", "macos", "linux", "windows", "ios", "android", "wasm"],
                    help="目标平台（默认本机桌面平台）")
    ap.add_argument("--version", default=MNN_VERSION, help="MNN git tag")
    ap.add_argument("--src", default="", help="本地 MNN 源码目录（跳过 clone）")
    ap.add_argument("--abis", default="arm64-v8a",
                    help="Android ABI 列表，CSV（默认 arm64-v8a）")
    ap.add_argument("--emsdk-root", default="", help="emsdk 根目录（默认 $EMSDK_ROOT/$EMSDK）")
    ap.add_argument("--force", action="store_true", help="已有产物也强制重建")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()

    plat = args.platform
    if plat == "auto":
        plat = "macos" if platform.is_macos() else ("windows" if platform.is_windows() else "linux")

    install_dir = install_root(root, plat)
    lib_name = "MNN.lib" if plat == "windows" else "libMNN.a"
    # android 按 ABI 分目录安装（install/<abi>/lib/libMNN.a）
    sentinels = ([install_dir / abi / "lib" / lib_name
                  for abi in args.abis.split(",") if abi.strip()]
                 if plat == "android" else [install_dir / "lib" / lib_name])
    if all(s.is_file() for s in sentinels) and not args.force:
        console.ok(f"已存在 {sentinels[0]}，跳过构建（--force 强制重建）")
        return 0

    if args.src:
        src = Path(args.src)
        if not src.is_absolute():
            src = Path.cwd() / src
    else:
        src = root / "build" / "mnn" / "mnn-src"
        if clone_mnn(src, args.version) != 0:
            return 1
    if not (src / "CMakeLists.txt").is_file():
        console.fail(f"MNN 源码不存在: {src}")
        return 1

    if plat == "wasm":
        code = do_wasm(root, src, jobs, install_dir, args.emsdk_root)
    elif plat == "android":
        cmake_exe = toolchain.find_cmake()
        if not cmake_exe:
            console.fail("cmake not found. Install CMake 3.13+.")
            return 1
        code = do_android(root, src, cmake_exe, jobs,
                          [a.strip() for a in args.abis.split(",") if a.strip()],
                          install_dir)
    else:
        cmake_exe = toolchain.find_cmake()
        if not cmake_exe:
            console.fail("cmake not found. Install CMake 3.13+（Windows 另需 Ninja）。")
            return 1
        code = do_desktop_or_ios(root, src, cmake_exe, jobs, plat, install_dir)

    if code != 0:
        return code

    print()
    console.ok(f"MNN {args.version} ({plat}) build complete")
    print(f"Install prefix: {install_dir}")
    for f in sorted((install_dir / "lib").glob("*")):
        print(f"  {f}")
    print()
    print("To enable MNN in task_graph:")
    print("  cmake -S . -B build -DTASK_GRAPH_ENABLE_MNN=ON")
    return 0


if __name__ == "__main__":
    sys.exit(main())
