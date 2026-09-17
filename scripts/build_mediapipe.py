#!/usr/bin/env python3
"""build_mediapipe.py — 构建 MediaPipe Tasks C API 库，供 task_graph 链接（跨平台）。

取代 scripts/build_mediapipe_macos.sh。产出：
  - macOS:   lib/libvision.dylib
  - Windows: bin/vision.dll + lib/vision.lib（import lib）
  - 其他:    lib/libmediapipe_vision_c.a（合并静态库）
全部安装到 build/mediapipe/install/（+ include/ 头文件）。
覆盖 MpImage / FaceLandmarker / HandLandmarker / PoseLandmarker / ObjectDetector 等 10 个
vision 任务的 C API。

重要约束:
  - macOS 上 Docker 只能产出 Linux 二进制，无法链接进 macOS 的 task_graph。
    因此 macOS 桌面默认走本机 bazelisk（brew install bazelisk）。
  - --docker 用于产出 Linux 版预编译库（Linux 桌面 / CI / Windows+WSL）。
  - Windows 原生：官方 Bazel 只"实验性"支持 Win32，本脚本封装了实际可用的
    配方（已在 VS2022 + Bazel 7.4.1 + MediaPipe v1.0.0 验证）：
      * MSVC 源码补丁（C3547 / C3857 / C2475，见 patch_windows_msvc）
      * --copt=/Zc:preprocessor --host_copt=/Zc:preprocessor（MSVC 旧预处理器
        展不开 MediaPipe 的 status 宏，上游 PR #6238 同款修复）
      * HERMETIC_PYTHON_VERSION=3.12（3.13 上游不支持）
      * MEDIAPIPE_DISABLE_GPU=1（桌面 GPU 构建官方仅支持 Linux）
      * third_party/opencv_windows.BUILD 自动指向本机 C:\\opencv\\build
        （WORKSPACE 的 windows_opencv 仓库写死了该路径）
    前置：VS2022 Build Tools、C:\\opencv\\build（官方预编译版即可）、JDK 21
    （JAVA_HOME 或 build/mediapipe/tools/jdk/jdk-*/，供 rules_java local_jdk）。

用法:
  python scripts/build_mediapipe.py                         # 默认本机构建
  python scripts/build_mediapipe.py --targets image         # 只构建 MpImage（快速验证链路）
  python scripts/build_mediapipe.py --platform linux        # Linux 版（Linux 本机直接构建；
                                                           #  非 Linux 宿主走 ubuntu:22.04 Docker）
  python scripts/build_mediapipe.py --platform ios          # iOS arm64 静态库 → build_ios/mediapipe/install
  python scripts/build_mediapipe.py --platform android --android-abi arm64-v8a
                                                           # → build_android/mediapipe/install/<abi>/
  python scripts/build_mediapipe.py --docker                # = --platform linux（兼容旧参数）
  python scripts/build_mediapipe.py --version v0.10.35      # 指定 MediaPipe 版本
  python scripts/build_mediapipe.py --src /path/to/mp/src   # 指定本地 MediaPipe 源码
  python scripts/build_mediapipe.py --bazel-user-root F:/_bzl
                                                           # 重定向 bazel 输出基（缓存复用）

前置（本机构建）: brew install bazelisk（macOS）；Windows 自动下载 bazelisk.exe 到
build/mediapipe/tools/。

交叉构建（ios/android/linux-docker）:
  - ios/android 复用同一份 mediapipe-src 源码树（bazel 按配置分目录产出，互不污染），
    产物为全依赖合并静态库 libmediapipe_vision_c.a（cquery 收集 kind(cc_library,
    deps(...)) 的全部 .a 后 libtool / NDK llvm-ar MRI 合并）——官方
    MediaPipeTasksVision.xcframework / AAR 只导出 ObjC/Java API，无 Mp* C API，
    自建是唯一通道（详见 docs/research/mnn-mediapipe-core-integration.md）。
  - android 需要 NDK：env ANDROID_NDK / ANDROID_NDK_HOME，或 macOS 默认路径探测。
  - 非 Linux 宿主上 --platform linux 装到 build/mediapipe/install-linux（避免踩掉
    本机桌面 install；Linux 本机构建仍装 build/mediapipe/install）。
"""

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import io
import urllib.request
from pathlib import Path
from typing import List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, platform, repo_root, runner  # noqa: E402

MP_VERSION = "v1.0.0"
MP_REPO = "https://github.com/google-ai-edge/mediapipe.git"
ZLIB_URL = "http://zlib.net/fossils/zlib-1.2.13.tar.gz"
EIGEN_REPO = "https://gitlab.com/libeigen/eigen.git"
EIGEN_COMMIT = "4c38131a16803130b66266a912029504f2cf23cd"
MP_TARGETS_FULL = ["//mediapipe/tasks/c/vision:libvision.dylib"]
MP_TARGETS_WIN = ["//mediapipe/tasks/c/vision:vision.dll"]
MP_TARGETS_SO = ["//mediapipe/tasks/c/vision:libvision.so"]
MP_TARGETS_MINIMAL = ["//mediapipe/tasks/c/vision/core:image"]
BAZELISK_URL = "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-windows-amd64.exe"
BAZELISK_LINUX_URLS = {
    "x86_64": "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-amd64",
    "arm64": "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-arm64",
    "aarch64": "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-arm64",
}

# Android ABI → mediapipe .bazelrc 配置名
ANDROID_ABI_CONFIG = {
    "armeabi-v7a": "android_arm",
    "arm64-v8a": "android_arm64",
    "x86_64": "android_x86_64",
    "x86": "android_x86",
}


# ---- 文本补丁工具（替代 BSD/GNU sed，跨平台一致）----

def _read(path: Path) -> Optional[str]:
    if not path.is_file():
        return None
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None


def _write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def _replace_once(path: Path, old: str, new: str) -> bool:
    t = _read(path)
    if t is None or old not in t:
        return False
    _write(path, t.replace(old, new, 1))
    return True


def _grep(path: Path, needle: str) -> bool:
    t = _read(path)
    return bool(t and needle in t)


def _regex_sub(path: Path, pattern: str, repl: str, count: int = 1) -> bool:
    t = _read(path)
    if t is None:
        return False
    nt = re.sub(pattern, repl, t, count=count)
    if nt == t:
        return False
    _write(path, nt)
    return True


# ---- zlib override（TARGET_OS_MAC fdopen fix）----

def prepare_zlib_override(mp_build: Path, mp_src: Path) -> Optional[Path]:
    patched = mp_build / "zlib-patched"
    if patched.is_dir():
        return patched
    # 该 override 只为修复 macOS 上 fdopen 重定义；Windows/Linux 用 bazel 自带
    # 的 zlib 即可（且 tar 解压目录名与 zlib-1.2.13-src 的假设不匹配，避免误伤）。
    if not platform.is_macos():
        return None
    zlib_src = mp_build / "zlib-1.2.13-src"
    if not zlib_src.is_dir():
        console.step("Fetching zlib 1.2.13 source")
        mp_build.mkdir(parents=True, exist_ok=True)
        try:
            with urllib.request.urlopen(ZLIB_URL, timeout=120) as resp:
                data = resp.read()
        except Exception as e:
            console.fail(f"下载 zlib 失败: {e}")
            return None
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
            tf.extractall(str(mp_build))
        # 官方 tarball 解压目录是 zlib-1.2.13/，规范化为 zlib-1.2.13-src/
        # （本目录名是既定约定——已在用机器存在该目录时上面整段跳过，
        #   此重命名只发生在全新机器/CI 上；CI 曾因这个目录名假设直接
        #   FileNotFoundError 于 copytree）。
        extracted = mp_build / "zlib-1.2.13"
        if extracted.is_dir():
            extracted.rename(zlib_src)
    shutil.copytree(zlib_src, patched)
    # MACOS||TARGET_OS_MAC → MACOS（修复 macOS 上 fdopen 重定义）
    _replace_once(patched / "zutil.h",
                  "#if defined(MACOS) || defined(TARGET_OS_MAC)", "#if defined(MACOS)")
    shutil.copy2(mp_src / "third_party" / "zlib.BUILD", patched / "BUILD.bazel")
    (patched / "WORKSPACE").touch()
    (patched / "REPO.bazel").touch()
    console.step("Prepared zlib override (TARGET_OS_MAC fdopen fix)")
    return patched


# ---- eigen override（gitlab 403 workaround）----

def prepare_eigen_override(mp_build: Path, mp_src: Path) -> Optional[Path]:
    eigen = mp_build / "eigen-patched"
    if (eigen / "Eigen").is_dir():
        return eigen
    console.step("Preparing eigen override (gitlab 403 workaround)")
    if eigen.exists():
        shutil.rmtree(eigen, ignore_errors=True)
    code = runner.run(["git", "clone", "--depth", "1", EIGEN_REPO, str(eigen)])
    if code != 0:
        console.warn("git clone eigen 失败（将跳过 eigen override）")
        return None
    if eigen.is_dir():
        subprocess.run(["git", "fetch", "--depth", "1", "origin", EIGEN_COMMIT],
                       cwd=str(eigen), check=False)
        subprocess.run(["git", "checkout", EIGEN_COMMIT], cwd=str(eigen), check=False)
    shutil.copy2(mp_src / "third_party" / "eigen.BUILD", eigen / "BUILD.bazel")
    (eigen / "WORKSPACE").touch()
    (eigen / "REPO.bazel").touch()
    return eigen if (eigen / "Eigen").is_dir() else None


# ---- macOS OpenCV5 兼容补丁 ----

def patch_macos_opencv5(mp_src: Path) -> None:
    """macOS 上 Homebrew OpenCV5 的路径 / 模块名兼容（仅 Darwin）。"""
    cellar = Path("/opt/homebrew/Cellar")
    if not cellar.is_dir():
        return
    oc_versions = sorted([d for d in cellar.iterdir() if d.is_dir() and d.name == "opencv"],
                         key=lambda p: p.name)
    if not oc_versions:
        # 列 opencv@5 / opencv@4 等
        oc_versions = sorted([d for d in cellar.iterdir() if d.is_dir() and d.name.startswith("opencv")])
    if not oc_versions:
        return
    oc_ver_dir = oc_versions[0]
    # 取最高版本号子目录
    ver_subs = sorted([d for d in oc_ver_dir.iterdir() if d.is_dir()])
    if not ver_subs:
        return
    ver_name = ver_subs[-1].name

    workspace = mp_src / "WORKSPACE"
    _regex_sub(workspace, r'path = "/usr/local"', f'path = "{cellar}"')
    build = mp_src / "third_party" / "opencv_macos.BUILD"
    _regex_sub(build, r'PREFIX = "[^"]*"', f'PREFIX = "opencv/{ver_name}"')
    _replace_once(build, "include/opencv2/**/*.h*", "include/opencv5/opencv2/**/*.h*")
    _replace_once(build, 'includes = [paths.join(PREFIX, "include/")]',
                  'includes = [paths.join(PREFIX, "include/opencv5")]')
    # OpenCV5 把 calib3d → calib, features2d → features
    _replace_once(build, "libopencv_calib3d.dylib", "libopencv_calib.dylib")
    _replace_once(build, "libopencv_features2d.dylib", "libopencv_features.dylib")
    # OpenCV5 拆出 libopencv_geometry（getPerspectiveTransform 等）。
    # 替换目标必须是完整条目（paths.join(...) 整行）：只匹配文件名会把
    # "lib/libopencv_imgcodecs.dylib" 从中间撕开，产生 unclosed string literal。
    if not _grep(build, "libopencv_geometry.dylib"):
        _replace_once(build, 'paths.join(PREFIX, "lib/libopencv_imgcodecs.dylib")',
                      'paths.join(PREFIX, "lib/libopencv_geometry.dylib"),\n'
                      '            paths.join(PREFIX, "lib/libopencv_imgcodecs.dylib")')


# ---- OpenCV5 API 兼容补丁（C++ 源码）----

def resolve_macos_jdk(mp_build: Path) -> Optional[Path]:
    """macOS 本机 JDK 探测，供 rules_java local_jdk 使用。

    mediapipe v1.0.0（rules_java 7.10.0）坑：无本地 JDK 时 local_jdk 退化为
    bootstrap 模板，引用已被上游移除的 @rules_java//tools/jdk —— analysis 直接
    失败（"BUILD file not found in directory 'tools/jdk'"），与 Bazel 版本无关。
    优先级：env JAVA_HOME > tools/jdk 下载目录 > 系统 JVM > brew LTS > Android Studio JBR。
    """
    if os.environ.get("JAVA_HOME"):
        return Path(os.environ["JAVA_HOME"])
    cands: List[Path] = []
    jdk_root = mp_build / "tools" / "jdk"
    if jdk_root.is_dir():
        cands += sorted(d for d in jdk_root.iterdir() if d.is_dir())
    jvm = Path("/Library/Java/JavaVirtualMachines")
    if jvm.is_dir():
        cands += sorted(p / "Contents" / "Home" for p in jvm.iterdir()
                        if (p / "Contents" / "Home" / "bin" / "java").exists())
    for name in ("openjdk@21", "openjdk@17"):
        p = Path("/opt/homebrew/opt") / name
        if (p / "bin" / "java").exists():
            cands.append(p)
    jbr = Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home")
    if (jbr / "bin" / "java").exists():
        cands.append(jbr)
    return cands[0] if cands else None

def patch_opencv5_api(mp_src: Path) -> None:
    """OpenCV API 兼容（版本自适应）：
    - getPerspectiveTransform 3 参形式仅 OpenCV >= 4（打包的 ios_opencv 是 3.2，只有 2 参）；
    - boxPoints 在 OpenCV 5 被移除（rotated_rect.points(vector&) 且 4 以下签名是裸数组）；
    - geometry/2d.hpp 仅 OpenCV >= 5。
    补丁幂等：检测标记注释，已打过的直接跳过。"""
    f1 = mp_src / "mediapipe" / "calculators" / "tensor" / "image_to_tensor_converter_opencv.cc"
    if f1.is_file():
        if not _grep(f1, "geometry/2d.hpp"):
            _replace_once(
                f1,
                '#include "mediapipe/framework/port/opencv_imgproc_inc.h"',
                '#include "mediapipe/framework/port/opencv_imgproc_inc.h"\n'
                '#if CV_VERSION_MAJOR >= 5\n#include <opencv2/geometry/2d.hpp>\n#endif')
        guarded = ("    cv::Mat src_points;\n"
                   "#if CV_VERSION_MAJOR >= 5\n"
                   "    std::vector<cv::Point2f> rotated_pts;\n"
                   "    rotated_rect.points(rotated_pts);\n"
                   "    src_points = cv::Mat(4, 2, CV_32F);\n"
                   "    for (int i = 0; i < 4; ++i) {\n"
                   "      src_points.at<float>(i, 0) = rotated_pts[i].x;\n"
                   "      src_points.at<float>(i, 1) = rotated_pts[i].y;\n"
                   "    }\n"
                   "#else\n"
                   "    cv::boxPoints(rotated_rect, src_points);\n"
                   "#endif")
        t1 = _read(f1) or ""
        if "#if CV_VERSION_MAJOR >= 5" not in t1:
            if "rotated_rect.points" in t1:
                # 旧版补丁（无守卫的 vector 形式）→ 迁移为守卫版
                old_patched = ("    cv::Mat src_points;\n"
                               "    std::vector<cv::Point2f> rotated_pts;\n"
                               "    rotated_rect.points(rotated_pts);\n"
                               "    src_points = cv::Mat(4, 2, CV_32F);\n"
                               "    for (int i = 0; i < 4; ++i) {\n"
                               "        src_points.at<float>(i, 0) = rotated_pts[i].x;\n"
                               "        src_points.at<float>(i, 1) = rotated_pts[i].y;\n"
                               "    }")
                _replace_once(f1, old_patched, guarded)
            else:
                old = ("    cv::Mat src_points;\n"
                       "    cv::boxPoints(rotated_rect, src_points);")
                _replace_once(f1, old, guarded)
        # 3 参形式仅 >= 4；3.2（ios_opencv 打包）只有 2 参重载
        if not _grep(f1, "CV_VERSION_MAJOR >= 4"):
            _replace_once(f1, "cv::getPerspectiveTransform(src_points, dst_points);",
                          "#if CV_VERSION_MAJOR >= 4\n"
                          "    cv::getPerspectiveTransform(src_points, dst_points, cv::DECOMP_LU);\n"
                          "#else\n"
                          "    cv::getPerspectiveTransform(src_points, dst_points);\n"
                          "#endif")
            _replace_once(f1, "cv::getPerspectiveTransform(src_points, dst_points, cv::DECOMP_LU);",
                          "#if CV_VERSION_MAJOR >= 4\n"
                          "    cv::getPerspectiveTransform(src_points, dst_points, cv::DECOMP_LU);\n"
                          "#else\n"
                          "    cv::getPerspectiveTransform(src_points, dst_points);\n"
                          "#endif")

    f2 = mp_src / "mediapipe" / "calculators" / "image" / "image_transformation_calculator.cc"
    if f2.is_file() and not _grep(f2, "geometry/2d.hpp"):
        _replace_once(
            f2,
            '#include "mediapipe/framework/port/opencv_imgproc_inc.h"',
            '#include "mediapipe/framework/port/opencv_imgproc_inc.h"\n'
            '#if CV_VERSION_MAJOR >= 5\n#include <opencv2/geometry/2d.hpp>\n#endif')


# ---- Windows 原生构建：MSVC 源码补丁 / 环境探测 ----

def patch_windows_msvc(mp_src: Path) -> None:
    """MediaPipe v1.0.0 在 MSVC 下的三处编译修复（均已随 v1.0.0 + Bazel 7.4.1 验证）。

    - api3/calculator_context.h: C3547 —— 可推导模板参数不能跟在 DoNotSpecify
      参数包之后，重排；递归重载在 MSVC 下整体去掉参数包。
    - api3/graph.h: C3857 —— GenericGraph 里的非限定模板 friend 会解析到遗留的
      非模板 mediapipe::SubgraphContext，补前置声明。
    - legacy_calculator_support.cc: C2475 —— MSVC 下 thread_local 静态成员加
      ABSL_CONST_INIT 报错，用宏按编译器开关。
    所有补丁幂等（检测标记注释，已打过的直接跳过）。
    """
    f = mp_src / "mediapipe" / "framework" / "api3" / "calculator_context.h"
    t = _read(f)
    if t and "MSVC C3547 reorder" not in t:
        t = t.replace("template <int&... DoNotSpecify, class Visitor>",
                      "template <class Visitor, int&... DoNotSpecify>")
        t = t.replace("template <typename T, int&... DoNotSpecify, typename F>",
                      "template <typename T, typename F, int&... DoNotSpecify>")
        t = t.replace(
            "template <typename T, typename U, typename... Rest, int&... DoNotSpecify,\n"
            "          typename F>",
            "#if defined(_MSC_VER) && !defined(__clang__)\n"
            "template <typename T, typename U, typename... Rest, typename F>\n"
            "#else\n"
            "template <typename T, typename U, typename... Rest, typename F,\n"
            "          int&... DoNotSpecify>\n"
            "#endif")
        t = t.replace(
            "#ifndef MEDIAPIPE_FRAMEWORK_API3_CALCULATOR_CONTEXT_H_",
            "// MSVC C3547 reorder (patched by build_mediapipe.py): deducible template\n"
            "// parameters must not follow the DoNotSpecify pack on MSVC.\n"
            "#ifndef MEDIAPIPE_FRAMEWORK_API3_CALCULATOR_CONTEXT_H_", 1)
        _write(f, t)
        print("patched api3/calculator_context.h (MSVC C3547)")

    f = mp_src / "mediapipe" / "framework" / "api3" / "graph.h"
    t = _read(f)
    if t and "SubgraphContext (patched by build_mediapipe.py" not in t:
        _replace_once(
            f, "class GenericGraph;\n",
            "class GenericGraph;\n"
            "// Forward declaration (patched by build_mediapipe.py for MSVC): the\n"
            "// unqualified template friend in GenericGraph otherwise resolves to the\n"
            "// legacy non-template mediapipe::SubgraphContext (C3857).\n"
            "template <typename NodeT>\n"
            "class SubgraphContext;\n")
        print("patched api3/graph.h (MSVC C3857)")

    f = mp_src / "mediapipe" / "framework" / "legacy_calculator_support.cc"
    t = _read(f)
    if t and "MP_CONSTINIT" not in t:
        t = t.replace(
            "namespace mediapipe {",
            "namespace mediapipe {\n\n"
            "#ifdef _MSC_VER\n"
            "#define MP_CONSTINIT ABSL_CONST_INIT\n"
            "#else\n"
            "#define MP_CONSTINIT\n"
            "#endif\n", 1)
        t = t.replace("thread_local CalculatorContext*",
                      "MP_CONSTINIT thread_local CalculatorContext*")
        t = t.replace("thread_local CalculatorContract*",
                      "MP_CONSTINIT thread_local CalculatorContract*")
        _write(f, t)
        print("patched legacy_calculator_support.cc (MSVC C2475)")


def patch_windows_opencv(mp_src: Path) -> bool:
    """third_party/opencv_windows.BUILD 指向本机 C:\\opencv\\build。

    WORKSPACE 的 windows_opencv 本地仓库写死了 C:\\opencv\\build 路径；BUILD 模板
    默认按 OpenCV 3.4.10 + vc15 写。这里从实际安装探测 world 版本号与 vc 目录
    （如 4.10.0 + vc16 → opencv_world4100 / x64/vc16）。
    """
    f = mp_src / "third_party" / "opencv_windows.BUILD"
    t = _read(f)
    if not t:
        return False
    if "patched by build_mediapipe.py" in t:
        return True
    opencv_build = Path("C:/opencv/build")
    x64 = opencv_build / "x64"
    if not x64.is_dir():
        console.warn(f"未找到 {opencv_build}（MediaPipe WORKSPACE 的 windows_opencv "
                     f"仓库指向该路径），Windows 构建可能失败")
        return False
    version = ""
    for vc in sorted(d.name for d in x64.iterdir() if d.is_dir()):
        libs = [l for l in (x64 / vc / "lib").glob("opencv_world*.lib")
                if not l.name.endswith("d.lib")]
        if libs:
            version = libs[0].stem.replace("opencv_world", "")
            t = re.sub(r'OPENCV_VERSION = "\d+"',
                       f'OPENCV_VERSION = "{version}"  # patched by build_mediapipe.py', t)
            t = t.replace("x64/vc15/", f"x64/{vc}/")
            _write(f, t)
            print(f"patched opencv_windows.BUILD (world{version}, {vc})")
            return True
    console.warn("C:/opencv/build/x64/*/lib 下未找到 opencv_world*.lib")
    return False


def windows_bazel_env(mp_build: Path) -> dict:
    """Bazel on Windows 需要的环境变量（BAZEL_SH / JAVA_HOME）。

    JAVA_HOME：rules_java 的 local_jdk 工具链要本机 JDK（无 JAVA_HOME 时
    该仓库解析会失败）。优先用 tools/jdk 下已下载的 JDK，其次环境变量。
    """
    env = {}
    bash = shutil.which("bash")
    if not bash:
        git_bash = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git" / "bin" / "bash.exe"
        if git_bash.is_file():
            bash = str(git_bash)
    if bash:
        env["BAZEL_SH"] = str(Path(bash))

    jdk: Optional[Path] = None
    jdk_root = mp_build / "tools" / "jdk"
    if jdk_root.is_dir():
        cands = sorted(d for d in jdk_root.iterdir()
                       if d.is_dir() and (d / "bin" / "java.exe").is_file())
        if cands:
            jdk = cands[-1]
    if jdk is None and os.environ.get("JAVA_HOME"):
        jdk = Path(os.environ["JAVA_HOME"])
    if jdk is None:
        # GitHub windows runner 只保证 JAVA_HOME_<ver>_X64 系列变量
        for var in ("JAVA_HOME_21_X64", "JAVA_HOME_17_X64"):
            if os.environ.get(var) and (Path(os.environ[var]) / "bin" / "java.exe").is_file():
                jdk = Path(os.environ[var])
                break
    path = env.get("PATH", os.environ.get("PATH", ""))
    if jdk:
        env["JAVA_HOME"] = str(jdk)
        path = str(jdk / "bin") + os.pathsep + path
        print(f"JAVA_HOME={jdk}")
    else:
        console.warn("未找到本机 JDK（JAVA_HOME 或 build/mediapipe/tools/jdk），"
                     "rules_java local_jdk 仓库可能解析失败")
    swift_stub = mp_build / "tools" / "swift-stub"
    if swift_stub.is_dir():
        path = str(swift_stub) + os.pathsep + path
    env["PATH"] = path
    return env


def ensure_bazelisk_windows(mp_build: Path) -> Optional[Path]:
    """Windows 下的 bazelisk：PATH 优先，其次 build/mediapipe/tools/bazelisk.exe（按需下载）。"""
    w = shutil.which("bazelisk") or shutil.which("bazel")
    if w:
        return Path(w)
    exe = mp_build / "tools" / "bazelisk.exe"
    if exe.is_file():
        return exe
    exe.parent.mkdir(parents=True, exist_ok=True)
    console.step(f"Downloading bazelisk to {exe}")
    try:
        urllib.request.urlretrieve(BAZELISK_URL, exe)
    except Exception as e:
        console.fail(f"下载 bazelisk 失败: {e}（可手动放到 {exe}）")
        return None
    return exe


# ---- protobuf MSVC 补丁（Windows）----
# protobuf 6.31.1 的 json/internal/untyped_message.{h,cc} 在 UntypedMessage 类体内
# 持有递归 std::variant 的 flat_hash_map 成员——类自身不完整时实例化 variant，
# MSVC（14.36 本机 + 14.44 runner 均触发）直接 C2139/C2079/C2338。修法：MSVC 下
# 经 unique_ptr 持有，把递归 variant 的实例化推迟到类体之外（类体处类型已完整）。
# 交付方式：bazel fetch 物化 external/protobuf~ 后原地打补丁再 build。
# （--override_repository 方案不可行：重建的仓库丢失 Bzlmod repo mapping，
#   protobuf 的 @zlib 依赖断链 → gzip_stream.h 找不到 zlib.h。）

PROTOBUF_MSVC_MARKER = "MSVC recursive-variant defer (patched by build_mediapipe.py)"
PROTOBUF_H = "src/google/protobuf/json/internal/untyped_message.h"
PROTOBUF_CC = "src/google/protobuf/json/internal/untyped_message.cc"
PROTOBUF_IO_BUILD = "src/google/protobuf/io/BUILD.bazel"


def _protobuf_msvc_hunks(eol: str):
    """[(relpath, old, new, count)]，count=0 表示全部替换。行尾随目标文件自适应。"""

    def E(s: str) -> str:
        return s.replace("\n", eol)

    find_count = E("  size_t Count(int32_t field_number) const {\n"
                   "    auto it = fields_.find(field_number);\n"
                   "    if (it == fields_.end()) {")
    find_count_new = E("  size_t Count(int32_t field_number) const {\n"
                       "#ifdef _MSC_VER\n"
                       "    auto it = fields_->find(field_number);\n"
                       "    if (it == fields_->end()) {\n"
                       "#else\n"
                       "    auto it = fields_.find(field_number);\n"
                       "    if (it == fields_.end()) {\n"
                       "#endif")
    find_get = E("  absl::Span<const T> Get(int32_t field_number) const {\n"
                 "    auto it = fields_.find(field_number);\n"
                 "    if (it == fields_.end()) {")
    find_get_new = E("  absl::Span<const T> Get(int32_t field_number) const {\n"
                     "#ifdef _MSC_VER\n"
                     "    auto it = fields_->find(field_number);\n"
                     "    if (it == fields_->end()) {\n"
                     "#else\n"
                     "    auto it = fields_.find(field_number);\n"
                     "    if (it == fields_.end()) {\n"
                     "#endif")
    ctor = E("  explicit UntypedMessage(const ResolverPool::Message* desc) : desc_(desc) {}")
    ctor_new = E("#ifdef _MSC_VER\n"
                 "  explicit UntypedMessage(const ResolverPool::Message* desc)\n"
                 "      : desc_(desc),\n"
                 "        fields_(std::make_unique<absl::flat_hash_map<int32_t, Value>>()) {}\n"
                 "#else\n"
                 "  explicit UntypedMessage(const ResolverPool::Message* desc) : desc_(desc) {}\n"
                 "#endif")
    member = E("  absl::flat_hash_map<int32_t, Value> fields_;")
    member_new = E("#ifdef _MSC_VER\n"
                   f"  // {PROTOBUF_MSVC_MARKER}:\n"
                   "  // hold the map by unique_ptr so the recursive std::variant is\n"
                   "  // instantiated outside the class body (C2079/C2139 otherwise).\n"
                   "  std::unique_ptr<absl::flat_hash_map<int32_t, Value>> fields_;\n"
                   "#else\n"
                   "  absl::flat_hash_map<int32_t, Value> fields_;\n"
                   "#endif")
    cc_old = E("  auto emplace_result = fields_.try_emplace(number, std::forward<T>(value));")
    cc_new = E("#ifdef _MSC_VER\n"
               "  auto emplace_result = fields_->try_emplace(number, std::forward<T>(value));\n"
               "#else\n"
               + cc_old + "\n"
               "#endif")

    # io/BUILD.bazel：MSVC 分支不给 @zlib 依赖，但 gzip_stream.h 无条件
    # #include <zlib.h> 且类成员用 z_stream —— 一律走 zlib 路径（BCR zlib 在
    # MSVC 下可编）。旧解析图里 config_msvc 不匹配、误打误撞走了 default 分支，
    # rules_cc 升级后 config_msvc 命中才暴露。
    copts_old = E('    copts = COPTS + select({\n'
                  '        "//build_defs:config_msvc": [],\n'
                  '        "//conditions:default": ["-DHAVE_ZLIB"],\n'
                  '    }),')
    copts_new = E('    copts = COPTS + ["-DHAVE_ZLIB"],')
    deps_old = E('    ] + select({\n'
                 '        "//build_defs:config_msvc": [],\n'
                 '        "//conditions:default": ["@zlib"],\n'
                 '    }),')
    deps_new = E('    ] + ["@zlib"],')

    return [
        (PROTOBUF_H, find_count, find_count_new, 1),
        (PROTOBUF_H, find_get, find_get_new, 1),
        (PROTOBUF_H, ctor, ctor_new, 1),
        (PROTOBUF_H, member, member_new, 1),
        (PROTOBUF_CC, cc_old, cc_new, 1),
        (PROTOBUF_IO_BUILD, copts_old, copts_new, 0),
        (PROTOBUF_IO_BUILD, deps_old, deps_new, 0),
    ]


def patch_protobuf_msvc(pb_dir: Path) -> bool:
    """对已物化的 protobuf 源码树原地打 MSVC 补丁（逐块幂等）。"""
    header = pb_dir / PROTOBUF_H
    t = _read(header)
    if t is None:
        console.fail(f"未找到 {header}（fetch 未物化？）")
        return False
    eol = "\r\n" if "\r\n" in t else "\n"
    patched = False
    for rel, old, new, count in _protobuf_msvc_hunks(eol):
        f = pb_dir / rel
        s = _read(f) or ""
        if new in s:
            continue  # 该块已打过
        if old not in s:
            console.fail(f"protobuf 补丁锚点未命中（版本变化？）: {rel}: {old[:60]!r}")
            return False
        s = s.replace(old, new) if count == 0 else s.replace(old, new, count)
        _write(f, s)
        patched = True
    if patched:
        print(f"patched protobuf MSVC (recursive-variant defer + zlib always-on): {pb_dir}")
    return True


def prefetch_and_patch_protobuf(bazel_cmd, startup_flags, config_flags, build_flags,
                                override_args, mp_src, env, mp_targets) -> bool:
    """bazel fetch 物化 external/（含 protobuf~），再原地打补丁。

    fetch 与后续 build 的 flags 保持一致（同一 override 集合），避免仓库集
    差异触发 protobuf~ 重新物化、抹掉补丁。
    """
    console.step("bazel fetch（物化 external/ 以便打 protobuf 补丁）")
    # -c opt 必须带上：windows_opencv 的 select 在 fastbuild 下无匹配分支
    fetch_cmd = (bazel_cmd + startup_flags + ["fetch", "-c", "opt"]
                 + config_flags + build_flags + override_args + mp_targets)
    code = subprocess.run(fetch_cmd, cwd=str(mp_src), env=env).returncode
    if code != 0:
        console.warn(f"bazel fetch 失败 (exit {code})，跳过 protobuf 补丁")
        return False
    r = subprocess.run(bazel_cmd + startup_flags + ["info", "output_base"],
                       cwd=str(mp_src), env=env, capture_output=True, text=True, check=False)
    out_base = r.stdout.strip()
    pb_dir = Path(out_base) / "external" / "protobuf~"
    if not pb_dir.is_dir():
        console.warn(f"external/protobuf~ 不存在（{pb_dir}）")
        return False
    return patch_protobuf_msvc(pb_dir)


# ---- rules_swift Windows stub（v1.0.0 Bzlmod 迁移的副作用）----

# v1.0.0 把 Apple rules 迁到 MODULE.bazel 后，rules_swift 的自动配置仓库
# （canonical 名 rules_swift~~non_module_deps~build_bazel_rules_swift_local_config）
# 在 Windows 上因找不到 swiftc.exe 而失败。cc 目标根本不会请求 swift 工具链，
# 用一个"注册了但永不选中"的 stub 仓库 override 掉它。
SWIFT_LOCAL_CONFIG_CANONICAL = "rules_swift~~non_module_deps~build_bazel_rules_swift_local_config"

SWIFT_STUB_BUILD = """# rules_swift local_config stub (generated by build_mediapipe.py).
# Windows cc 构建不会请求 swift 工具链类型；本 toolchain 仅保证
# rules_swift MODULE 注册的 "//:all" 可加载，target_compatible_with
# 指向 ios+arm64 使其永远不会被选中。
package(default_visibility = ["//visibility:public"])

filegroup(name = "swiftc_stub")

toolchain(
    name = "windows-swift-stub",
    toolchain = ":swiftc_stub",
    toolchain_type = "@build_bazel_rules_swift//toolchains:toolchain_type",
    exec_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:ios",
        "@platforms//cpu:arm64",
    ],
)
"""


def prepare_swift_stub(mp_build: Path) -> Optional[Path]:
    stub = mp_build / "tools" / "swift-local-config-stub"
    build = stub / "BUILD"
    if not build.is_file():
        stub.mkdir(parents=True, exist_ok=True)
        _write(build, SWIFT_STUB_BUILD)
        (stub / "REPO.bazel").touch()
        print(f"generated rules_swift local_config stub: {stub}")
    return stub


# ---- 导出符号白名单 + vision BUILD 补丁（C API only，避免 protobuf 符号冲突）----

EXPECTED_SYMBOLS = """_MpErrorFree
_MpFaceDetectorClose
_MpFaceDetectorCloseResult
_MpFaceDetectorCreate
_MpFaceDetectorDetectAsync
_MpFaceDetectorDetectForVideo
_MpFaceDetectorDetectImage
_MpFaceLandmarkerClose
_MpFaceLandmarkerCloseResult
_MpFaceLandmarkerCreate
_MpFaceLandmarkerDetectAsync
_MpFaceLandmarkerDetectForVideo
_MpFaceLandmarkerDetectImage
_MpGestureRecognizerClose
_MpGestureRecognizerCloseResult
_MpGestureRecognizerCreate
_MpGestureRecognizerRecognizeAsync
_MpGestureRecognizerRecognizeForVideo
_MpGestureRecognizerRecognizeImage
_MpHandLandmarkerClose
_MpHandLandmarkerCloseResult
_MpHandLandmarkerCreate
_MpHandLandmarkerDetectAsync
_MpHandLandmarkerDetectForVideo
_MpHandLandmarkerDetectImage
_MpHolisticLandmarkerClose
_MpHolisticLandmarkerCloseResult
_MpHolisticLandmarkerCreate
_MpHolisticLandmarkerDetectAsync
_MpHolisticLandmarkerDetectForVideo
_MpHolisticLandmarkerDetectImage
_MpImageClassifierClose
_MpImageClassifierCloseResult
_MpImageClassifierCreate
_MpImageClassifierClassifyAsync
_MpImageClassifierClassifyForVideo
_MpImageClassifierClassifyImage
_MpImageCreateFromFile
_MpImageCreateFromFloatData
_MpImageCreateFromImageFrame
_MpImageCreateFromUint16Data
_MpImageCreateFromUint8Data
_MpImageDataFloat32
_MpImageDataUint16
_MpImageDataUint8
_MpImageEmbedderClose
_MpImageEmbedderCloseResult
_MpImageEmbedderCosineSimilarity
_MpImageEmbedderCreate
_MpImageEmbedderEmbedAsync
_MpImageEmbedderEmbedForVideo
_MpImageEmbedderEmbedImage
_MpImageFree
_MpImageGetByteDepth
_MpImageGetChannels
_MpImageGetFormat
_MpImageGetHeight
_MpImageGetValueFloat32
_MpImageGetValueUint16
_MpImageGetValueUint8
_MpImageGetWidth
_MpImageGetWidthStep
_MpImageIsAligned
_MpImageIsContiguous
_MpImageIsEmpty
_MpImageSegmenterClose
_MpImageSegmenterCloseResult
_MpImageSegmenterCreate
_MpImageSegmenterGetLabels
_MpImageSegmenterSegmentAsync
_MpImageSegmenterSegmentForVideo
_MpImageSegmenterSegmentImage
_MpImageUsesGpu
_MpObjectDetectorClose
_MpObjectDetectorCloseResult
_MpObjectDetectorCreate
_MpObjectDetectorDetectAsync
_MpObjectDetectorDetectForVideo
_MpObjectDetectorDetectImage
_MpPoseLandmarkerClose
_MpPoseLandmarkerCloseResult
_MpPoseLandmarkerCreate
_MpPoseLandmarkerDetectAsync
_MpPoseLandmarkerDetectForVideo
_MpPoseLandmarkerDetectImage
_MpStringListFree
"""

ALL_VISION_LIBS = [
    "//mediapipe/tasks/c/vision/face_detector:face_detector_c_lib",
    "//mediapipe/tasks/c/vision/face_landmarker:face_landmarker_c_lib",
    "//mediapipe/tasks/c/vision/gesture_recognizer:gesture_recognizer_c_lib",
    "//mediapipe/tasks/c/vision/hand_landmarker:hand_landmarker_c_lib",
    "//mediapipe/tasks/c/vision/holistic_landmarker:holistic_landmarker_c_lib",
    "//mediapipe/tasks/c/vision/image_classifier:image_classifier_c_lib",
    "//mediapipe/tasks/c/vision/image_embedder:image_embedder_c_lib",
    "//mediapipe/tasks/c/vision/image_segmenter:image_segmenter_c_lib",
    "//mediapipe/tasks/c/vision/object_detector:object_detector_c_lib",
    "//mediapipe/tasks/c/vision/pose_landmarker:pose_landmarker_c_lib",
]


def patch_vision_build(mp_src: Path) -> None:
    """修正 VISION_LIBRARIES 为全部 11 个模块的 C API；写导出符号白名单；补 dylib linkopts。"""
    build = mp_src / "mediapipe" / "tasks" / "c" / "vision" / "BUILD"
    if not build.is_file():
        return
    s = _read(build) or ""

    new_lib = "VISION_LIBRARIES = [\n" + ",\n".join(f'    "{l}"' for l in ALL_VISION_LIBS) + ",\n]"
    s = re.sub(r"VISION_LIBRARIES = \[.*?\]", new_lib, s, flags=re.S)

    exp = mp_src / "mediapipe" / "tasks" / "c" / "vision" / "exported_symbols.txt"
    exp.parent.mkdir(parents=True, exist_ok=True)
    existing = set()
    if exp.is_file() and exp.stat().st_size > 0:
        for line in _read(exp).splitlines():
            line = line.strip()
            if line:
                existing.add(line)
    expected = set(l for l in EXPECTED_SYMBOLS.splitlines() if l.strip())
    merged = sorted(existing | expected)
    _write(exp, "".join(l + "\n" for l in merged))
    print(f"merged {len(merged)} exported symbols ({len(expected - existing)} new)")

    # Linux（GNU ld）：linkshared cc_binary 自身无源文件 → 链接期没有任何
    # 未定义引用 → *_c_lib 静态归档一个成员都不会被拉入 → 空 .so（CI ubuntu
    # 实测：libtask_graph.so 全部 Mp* undefined，--as-needed 连 DT_NEEDED 都
    # 丢弃且无 "needed by ... not found" 告警）。macOS dylib 靠 ld64 按
    # -exported_symbols_list 主动拉归档成员、Windows vision.dll 靠 dllexport
    # 拉成员，Linux 侧生成引用全部导出符号的锚点源文件注入 srcs 强制拉入。
    # （EXPECTED_SYMBOLS 为 Mach-O 带下划线格式，ELF 侧剥掉前导下划线。）
    anchor = build.parent / "vision_export_anchor.c"
    anchor_lines = [
        "/* Generated by build_mediapipe.py - GNU ld archive-pull anchor.",
         " * References every exported C API symbol so the linker pulls the",
         " * *_c_lib archive members into libvision.so. Do not edit. */",
    ]
    for raw in sorted(existing | expected):
        name = raw.lstrip("_")
        if not name.startswith("Mp"):
            continue
        anchor_lines.append(f"extern int {name}(void);")
    for raw in sorted(existing | expected):
        name = raw.lstrip("_")
        if not name.startswith("Mp"):
            continue
        # 外部可见的全局指针（不被编译器消除），hidden 不进 .so 导出表
        anchor_lines.append(
            f'__attribute__((visibility("hidden"))) '
            f"void* volatile mp_anchor_{name} = (void*)&{name};")
    anchor.write_text("\n".join(anchor_lines) + "\n", encoding="utf-8")
    if 'name = "libvision.so"' in s and "vision_export_anchor" not in s:
        s = s.replace(
            '    name = "libvision.so",\n',
            '    name = "libvision.so",\n'
            '    srcs = ["vision_export_anchor.c"],\n', 1)

    if "exported_symbols_list" not in s:
        # BUILD 文件里必须用正斜杠：Windows 的原生反斜杠路径会被 bazel 的
        # BUILD 解析器当成非法转义序列（\g/\m → "invalid escape sequence"）。
        export_arg = str(mp_src / "mediapipe" / "tasks" / "c" / "vision"
                         / "exported_symbols.txt").replace("\\", "/")
        s = s.replace(
            '"-Wl,-install_name,libvision.dylib",',
            f'"-Wl,-install_name,libvision.dylib",\n'
            f'        "-Wl,-exported_symbols_list,{export_arg}",')
        if '"data = ["' not in s:
            s = s.replace(
                "    linkshared = True,\n    tags = [",
                '    linkshared = True,\n    data = ["exported_symbols.txt"],\n    tags = [')
    # Windows：上游只带 .so/.dylib 目标，注入 vision.dll（cc_binary 名以 .dll
    # 结尾时 bazel 在 Windows 上产出 vision.dll + vision.dll.if.lib 导入库；
    # 导出走 MP_EXPORT(__declspec(dllexport))，无需版本脚本）。
    if 'name = "vision.dll"' not in s:
        s += (
            "\n"
            "# Windows DLL target injected by build_mediapipe.py (upstream ships .so/.dylib only).\n"
            "# bazel build -c opt --define MEDIAPIPE_DISABLE_GPU=1 \\\n"
            "#   //mediapipe/tasks/c/vision:vision.dll\n"
            "cc_binary(\n"
            "    name = \"vision.dll\",\n"
            "    linkshared = True,\n"
            "    deps = VISION_LIBRARIES,\n"
            ")\n")
    _write(build, s)
    print("patched vision BUILD")


# ---- Android NDK / 交叉构建辅助 ----

def patch_android_ndk_workspace(mp_src: Path, ndk: Path) -> None:
    """WORKSPACE 追加官方 NDK 配方（setup_android_sdk_and_ndk.sh 同款，幂等）。

    v1.0.0 的 WORKSPACE 只 load 了 android_ndk_repository 却标 @unused 从不调用，
    bzlmod 下 NDK 仓库永远不注册 —— --config=android_arm64 直接
    "Unable to find a CC toolchain"。bind android/crosstool 对应 .bazelrc 的
    --crosstool_top=//external:android/crosstool。
    """
    w = mp_src / "WORKSPACE"
    t = _read(w) or ""
    marker = 'android_ndk_repository(name = "androidndk"'
    if marker in t:
        # 已打过但 NDK 路径可能变化（换机器/升版本）——刷新 path
        import re as _re
        t = _re.sub(r'android_ndk_repository\(name = "androidndk", api_level = \d+, '
                    r'path = "[^"]*"\)',
                    f'android_ndk_repository(name = "androidndk", api_level = 24, '
                    f'path = "{ndk}")', t)
        _write(w, t)
        return
    t += ("\n# --- Android NDK (appended by build_mediapipe.py) ---\n"
          f'android_ndk_repository(name = "androidndk", api_level = 24, path = "{ndk}")\n'
          'bind(name = "android/crosstool", actual = "@androidndk//:toolchain")\n'
          'register_toolchains("@androidndk//:all")\n'
          "# NOTE: bazel 7 has no CLI --register_toolchains; WORKSPACE registration\n"
          "# is REQUIRED for --config=android* analysis (rules_cc new-style toolchain\n"
          "# resolution). The docker linux build (sharing this WORKSPACE) neutralizes\n"
          "# it via --override_repository=androidndk=<stub> (empty repo, zero\n"
          "# toolchains) since the NDK path above is host-specific.\n")
    _write(w, t)
    print("patched WORKSPACE (android_ndk_repository + bind android/crosstool)")


def resolve_android_ndk() -> Optional[Path]:
    """NDK 探测：env ANDROID_NDK / ANDROID_NDK_HOME > macOS 默认路径最高版本。"""
    for var in ("ANDROID_NDK", "ANDROID_NDK_HOME"):
        p = os.environ.get(var)
        if p and Path(p).is_dir():
            return Path(p)
    for base in (Path.home() / "Library" / "Android" / "sdk" / "ndk",
                 Path.home() / "Android" / "Sdk" / "ndk"):
        if base.is_dir():
            vers = sorted(d for d in base.iterdir() if d.is_dir())
            if vers:
                return vers[-1]
    return None


def cquery_target_archives(bazel_cmd: List[str], config_flags: List[str],
                           build_flags: List[str], override_args: List[str],
                           mp_src: Path, env: dict,
                           mp_targets: List[str],
                           require_exists: bool = True) -> List[Path]:
    """收集目标平台下 mp_targets 的全部传递 cc_library 归档（.a）。

    kind(cc_library, deps(...)) + --notool_deps：排除 host/exec 配置的工具归档，
    只留目标平台产物（bazel-out/<platform>-opt/bin/.../*.a）。
    require_exists=False 时返回"声明输出"路径（apple 工具链下 .a 并不实际物化，
    仅用于推导 bazel-out 目标平台根目录）。
    """
    expr = " + ".join(f"deps({t})" for t in mp_targets)
    cmd = (bazel_cmd + ["cquery", "-c", "opt", "--notool_deps", "--output=files"]
           + config_flags + build_flags + override_args + [expr])
    r = subprocess.run(cmd, cwd=str(mp_src), env=env,
                       capture_output=True, text=True, check=False)
    if r.returncode != 0:
        console.fail("bazel cquery 收集静态库失败:")
        print(r.stdout[-2000:])
        print(r.stderr[-2000:])
        return []
    seen, archives = set(), []
    for line in r.stdout.splitlines():
        line = line.strip()
        if not line.endswith(".a"):
            continue
        # 相对路径基于 workspace（exec root 与 workspace 对 mediapipe 源码树同构）
        p = Path(line) if Path(line).is_absolute() else mp_src / line
        rp = str(p.resolve())
        if rp in seen:
            continue
        # androidndk 仓库里的 cc_library 是工具链运行库（libc++/libc++abi/
        # libclang_rt.asan/._libsimpleperf_readelf 等），不是目标产物——多数
        # 路径在磁盘上根本不存在（bazel 声明的可选变体），并入只会失败/膨胀
        if "/external/androidndk/" in rp:
            continue
        seen.add(rp)
        archives.append(Path(rp))
    if not require_exists:
        return archives
    # libtool/llvm-ar 对缺失成员只警告甚至静默跳过——合并前自检，缺失即失败
    missing = [a for a in archives if not a.is_file()]
    if missing:
        console.fail(f"{len(missing)}/{len(archives)} 个归档不存在（目标未全部构建?）:")
        for m in missing[:5]:
            print(f"    {m}")
        return []
    return archives


def collect_target_objects(archives_declared: List[Path],
                           suffixes: tuple) -> List[Path]:
    """从 bazel-out 目标平台根收集产物（apple/android crosstool 下 cc_library 的
    传递归档不物化——链接 cc_binary 时按 .lo/.o 对象直链，cquery 的 .a 只是
    声明输出）。out_root 从 cquery 声明路径推导（…/bazel-out/<platform>-opt）。"""
    if not archives_declared:
        return []
    out_root = None
    for p in archives_declared:
        parts = p.parts
        if "bazel-out" in parts:
            idx = parts.index("bazel-out")
            out_root = Path(*parts[:idx + 2])  # .../bazel-out/<platform>-opt
            break
    if out_root is None or not out_root.is_dir():
        console.fail(f"无法定位 bazel-out 目标平台根: {out_root}")
        return []
    objs: List[Path] = []
    for d, _, files in os.walk(out_root):
        if "runfiles" in d:
            continue
        for f in files:
            if f.endswith(suffixes):
                objs.append(Path(d) / f)
    if not objs:
        console.fail(f"{out_root} 下未找到 {suffixes} 产物")
        return []
    return objs


def collect_apple_objects(archives_declared: List[Path], bazel_cmd: List[str],
                          config_flags: List[str], build_flags: List[str],
                          override_args: List[str], mp_src: Path, env: dict,
                          mp_build: Path) -> List[Path]:
    """iOS（apple 工具链）：cc_library 产物是 .lo 单对象（链接 cc_binary 时物化），
    从 bazel-out 目标平台根收集全部 .lo/.a，另并入 ios_opencv 静态 framework。"""
    objs = collect_target_objects(archives_declared, (".lo", ".a"))
    if not objs:
        return []
    # ios_opencv（静态 framework）不在 bazel-out 里，从 external/ 补进来，
    # 否则合并库不自包含（cv:: 符号悬空）
    r = subprocess.run(bazel_cmd + ["info", "output_base"],
                       cwd=str(mp_src), env=env,
                       capture_output=True, text=True, check=False)
    ob = r.stdout.strip()
    if ob:
        fw = Path(ob) / "external" / "ios_opencv" / "opencv2.framework" / "opencv2"
        if fw.is_file():
            # 官方 3.2 ios framework 是 i386/armv7/armv7s/x86_64/arm64 五架构胖子——
            # 只取 arm64 slice 再并入，否则合并库被撑成 fat（其余架构来自 opencv，
            # 链接进 app 时会因多余 slice 报错/膨胀）
            thin = mp_build / "opencv2-ios-arm64.a"
            code = subprocess.run(["lipo", "-thin", "arm64", "-output", str(thin),
                                   str(fw)]).returncode
            if code == 0:
                objs.append(thin)
            else:
                console.warn("lipo -thin arm64 失败，直接并入 fat opencv framework")
                objs.append(fw)
    return objs


def merge_archives_ar(archives: List[Path], out_lib: Path, ar_exe: str) -> bool:
    """llvm-ar / GNU ar MRI 脚本合并（ADDLIB 不展开重打包，保 fat 归档成员）。
    对 iOS 不适用（Mach-O 归档须 libtool），走 merge_archives_libtool。"""
    mri = out_lib.parent / (out_lib.name + ".mri")
    with open(mri, "w", encoding="utf-8") as f:
        f.write(f"CREATE {out_lib}\n")
        for a in archives:
            # ADDLIB 吃归档（嵌套合并）；.o/.lo 对象用 ADDMOD
            verb = "ADDLIB" if a.suffix == ".a" else "ADDMOD"
            f.write(f"{verb} {a}\n")
        f.write("SAVE\n")
    code = subprocess.run([ar_exe, "-M"], stdin=open(str(mri), "r"),
                          capture_output=True).returncode
    mri.unlink(missing_ok=True)
    return code == 0


def merge_archives_libtool(archives: List[Path], out_lib: Path) -> bool:
    """macOS/iOS：libtool -static 直接吃 .a 列表。"""
    libtool = shutil.which("libtool")
    if not libtool:
        return False
    return subprocess.run(
        [libtool, "-static", "-o", str(out_lib)] + [str(a) for a in archives]
    ).returncode == 0


def ensure_bazelisk_linux(mp_build: Path) -> Optional[Path]:
    """Linux 版 bazelisk，供 Docker 容器内使用（按宿主架构选二进制——
    容器跟随宿主原生架构，避免 Apple Silicon 上 amd64 镜像走 qemu）。"""
    exe = mp_build / "tools" / "bazelisk-linux"
    machine = os.uname().machine if hasattr(os, "uname") else "x86_64"
    url = BAZELISK_LINUX_URLS.get(machine)
    if url is None:
        console.fail(f"未知宿主架构 {machine}，无法选择 bazelisk linux 二进制")
        return None
    marker = mp_build / "tools" / "bazelisk-linux.arch"
    if exe.is_file() and _read(marker) == machine:
        return exe
    exe.parent.mkdir(parents=True, exist_ok=True)
    console.step(f"Downloading linux bazelisk ({machine}) to {exe}")
    try:
        urllib.request.urlretrieve(url, exe)
        exe.chmod(0o755)
        _write(marker, machine + "\n")
    except Exception as e:
        console.fail(f"下载 bazelisk-linux 失败: {e}")
        return None
    return exe


def patch_linux_opencv4(mp_src: Path) -> None:
    """third_party/opencv_linux.BUILD 默认按 Ubuntu 18.04 的 OpenCV 3 写，
    4.x 的 include 路径（/usr/include/<triple>/opencv4）全部被注释掉。
    展开为 Ubuntu 22.04 libopencv-dev（4.x）实际布局，幂等。"""
    f = mp_src / "third_party" / "opencv_linux.BUILD"
    s = _read(f)
    if s is None:
        return
    if "patched by build_mediapipe.py" in s:
        return
    new_hdrs = (
        "cc_library(\n"
        '    name = "opencv",\n'
        "    hdrs = glob([\n"
        '        "include/opencv4/opencv2/**/*.h*",\n'
        '        "include/x86_64-linux-gnu/opencv4/opencv2/**/*.h*",\n'
        '        "include/aarch64-linux-gnu/opencv4/opencv2/**/*.h*",\n'
        "    ]),  # patched by build_mediapipe.py (Ubuntu 22.04 libopencv-dev 4.x)\n"
        "    includes = [\n"
        '        "include/opencv4",\n'
        '        "include/x86_64-linux-gnu/opencv4",\n'
        '        "include/aarch64-linux-gnu/opencv4",\n'
        "    ],\n")
    # 替换整个 cc_library 头部（到 includes 段结束），linkopts 之后的保持原样
    marker_end = "    linkopts = ["
    idx = s.find(marker_end)
    start = s.find("cc_library(")
    if start == -1 or idx == -1 or idx < start:
        return
    s = s[:start] + new_hdrs + s[idx:]
    _write(f, s)
    print("patched opencv_linux.BUILD (Ubuntu 22.04 / OpenCV 4.x include paths)")


def ensure_androidndk_stub(mp_build: Path) -> Optional[Path]:
    """docker linux 构建用的 androidndk 空 stub（WORKSPACE 全局注册的中和器）。

    WORKSPACE 的 register_toolchains("@androidndk//:all") 是 android 分析必需，
    但会强制所有平台 fetch NDK 仓库（宿主专属路径在容器内不存在）。
    --override_repository=androidndk=<本 stub> 把仓库替换为空（零 toolchain），
    注册落空、linux 构建照常。
    """
    stub = mp_build / "tools" / "androidndk-stub"
    build = stub / "BUILD"
    if not build.is_file():
        stub.mkdir(parents=True, exist_ok=True)
        _write(build, "# androidndk stub (generated by build_mediapipe.py)\n"
                      "exports_files([\"BUILD\"])\n")
        (stub / "REPO.bazel").touch()
    return stub


def run_docker_linux_build(mp_src: Path, mp_build: Path, jobs: int,
                           eigen_patched: Optional[Path]) -> int:
    """ubuntu:22.04 容器内构建 Linux libvision.so（宿主可以是任意平台）。

    镜像内没有 JDK/bazelisk：apt 装 openjdk-17，bazelisk 用宿主下载的 linux 版
    （经 mp_build 挂载进容器）。eigen override 同样挂载，路径改写为容器内路径。
    """
    r = subprocess.run(["docker", "info"], capture_output=True)
    if r.returncode != 0:
        console.fail("Docker 未运行")
        return 1
    bazelisk = ensure_bazelisk_linux(mp_build)
    if bazelisk is None:
        return 1
    ndk_stub = ensure_androidndk_stub(mp_build)
    inner_script = "\n".join([
        "set -e",
        "export DEBIAN_FRONTEND=noninteractive",
        # root 装 JDK（容器默认按宿主 uid 运行时 apt 无法写系统目录），
        # 再 setpriv 降回宿主 uid 跑 bazel（产物属主与宿主一致）。
        "apt-get update -qq",
        # ubuntu-toolchain-r PPA 的 gcc-13：v1.0.0 的 api3 用了 class NTTP +
        # deleted copy-ctor（C++20 structural），gcc-11/12 与 clang-14(+gcc11/12
        # libstdc++ 头) 全都编不过；保持 22.04 基底（glibc 2.35 = CI runner）
        # 只升编译器。
        "apt-get install -y -qq --no-install-recommends software-properties-common gnupg >/dev/null",
        "add-apt-repository -y ppa:ubuntu-toolchain-r/test >/dev/null",
        "apt-get update -qq",
        # libopencv-dev：linux_opencv 仓库指向 /usr 的系统 OpenCV
        # （third_party/opencv_linux.BUILD 链接 libopencv_*.so，头文件走 /usr/include）
        "apt-get install -y -qq --no-install-recommends openjdk-17-jdk-headless util-linux build-essential python3 g++-13 libopencv-dev >/dev/null",
        "export JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java))))",
        "chown -R ${TG_UID}:${TG_GID} /work-build/tools",
        "chown -R ${TG_UID}:${TG_GID} /tmp/bazel-root",
        "install -d -m 700 -o ${TG_UID} -g ${TG_GID} /tmp/bazel-home",
        "chmod +x /work-build/tools/bazelisk-linux",
        # bazel 输出基放容器内部 FS（/tmp）：osxfs/virtiofs 挂载上 rules_python
        # 的 rctx.delete("share/terminfo") 确定性失败（unlinkat: Directory not
        # empty）。产物回拷在 root 段执行（setpriv 降权 uid 对 osxfs 挂载点
        # 无写权限，实测 "cp: Permission denied"；root 经 Docker Desktop 的
        # uid 映射可写宿主目录），拷完 chown 回宿主 uid。
        "setpriv --reuid=${TG_UID} --regid=${TG_GID} --clear-groups "
        "env HOME=/tmp/bazel-home USER=bazel JAVA_HOME=${JAVA_HOME} "
        "HERMETIC_PYTHON_VERSION=3.12 "
        "bash -c "
        "'/work-build/tools/bazelisk-linux --output_user_root=/tmp/bazel-root "
        "build -c opt --jobs %d "
        "--define=MEDIAPIPE_DISABLE_GPU=1 "
        # XNNPACK AVX 扩展禁用同原生 Linux 路径：容器 22.04 的 gas 2.38 汇编
        # 不了 gcc-13 编出的 vpdpbssd 等指令（vpdpbssd 需 gas 2.42）。
        "--define=xnn_enable_avxvnni=false "
        "--define=xnn_enable_avxvnniint8=false "
        "--define=xnn_enable_avx256vnni=false "
        "--define=xnn_enable_avx256vnnigfni=false "
        "--define=xnn_enable_avx512vnni=false "
        "--define=xnn_enable_avx512vnnigfni=false "
        "--define=xnn_enable_avx512amx=false "
        "--define=xnn_enable_avx512bf16=false "
        "--define=xnn_enable_avx512fp16=false "
        "--override_repository=androidndk=/work-build/tools/androidndk-stub "
        "--repo_env=CC=/usr/bin/gcc-13 --repo_env=CXX=/usr/bin/g++-13 %s "
        "//mediapipe/tasks/c/vision:libvision.so' && "
        "found=$(find /tmp/bazel-root -type f -name libvision.so -not -path \"*runfiles*\" | head -1) && "
        "[ -n \"$found\" ] && rm -f /work-build/libvision.so && "
        "cp -v \"$found\" /work-build/libvision.so && "
        "(chown ${TG_UID}:${TG_GID} /work-build/libvision.so || true)"
        % (jobs,
           ("--override_repository=eigen=/work-build/eigen-patched"
            if eigen_patched else "")),
    ])
    cmd = (["docker", "run", "--rm",
            # root 运行（apt 需要），容器内再 setpriv 降回宿主 uid；
            # named volume 持久化 bazel 输出基（容器 FS 随 --rm 丢弃）
            "-v", "taskgraph-mediapipe-bazel:/tmp/bazel-root",
            "-e", f"TG_UID={os.getuid() if hasattr(os, 'getuid') else 0}",
            "-e", f"TG_GID={os.getgid() if hasattr(os, 'getgid') else 0}",
            "-v", f"{mp_src}:/work",
            "-v", f"{mp_build}:/work-build",
            "-w", "/work",
            "--entrypoint", "bash",
            "ubuntu:22.04", "-c", inner_script])
    return subprocess.run(cmd).returncode


# ---- bazel 命令解析 ----

def _rm_rf(path: Path) -> None:
    """bazel 产物带只读属性，rmtree 前先清掉（否则 Windows 下静默残留）。"""
    def _onerror(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    if path.exists():
        shutil.rmtree(path, onerror=_onerror)


def _copy_writable(src: Path, dst: Path) -> None:
    shutil.copy2(src, dst)
    os.chmod(dst, stat.S_IWRITE)


def resolve_bazel_cmd(use_docker: bool, mp_src: Path, mp_build: Path):
    """返回 (bazel_cmd_list, is_docker)。失败返回 (None, False)。"""
    if use_docker:
        console.step("Docker 模式（产出 Linux 二进制，仅供 Linux 桌面/CI）")
        r = subprocess.run(["docker", "info"], capture_output=True)
        if r.returncode != 0:
            console.fail("Docker 未运行")
            return None, False
        return (["docker", "run", "--rm", "-i",
                 "--user", f"{os.getuid() if hasattr(os, 'getuid') else 0}:"
                           f"{os.getgid() if hasattr(os, 'getgid') else 0}",
                 "-v", f"{mp_src}:/work",
                 "-v", f"{mp_build}/bazel-cache:/root/.cache/bazel",
                 "-w", "/work",
                 "ghcr.io/bazelbuild/bazelisk:latest"], True)
    if platform.is_windows():
        exe = ensure_bazelisk_windows(mp_build)
        if exe is None:
            return None, False
        console.step(f"本机构建模式: {exe}")
        return ([str(exe)], False)
    if shutil.which("bazelisk"):
        console.step("本机构建模式: bazelisk")
        return (["bazelisk"], False)
    if shutil.which("bazel"):
        console.step("本机构建模式: bazel")
        return (["bazel"], False)
    console.fail("未找到 bazel/bazelisk。macOS 请: brew install bazelisk")
    return None, False


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="构建 MediaPipe Tasks C API 静态库")
    ap.add_argument("--version", default=MP_VERSION, help=f"MediaPipe 版本 tag（默认 {MP_VERSION}）")
    ap.add_argument("--src", default="", help="本地 MediaPipe 源码目录")
    ap.add_argument("--targets", default="all",
                    help="'image' 仅 MpImage / 'all' 全部 / 逗号分隔自定义")
    ap.add_argument("--platform", default="host",
                    choices=["host", "linux", "ios", "android"],
                    help="目标平台（host=本机；linux 在非 Linux 宿主走 Docker）")
    ap.add_argument("--android-abi", default="arm64-v8a",
                    choices=sorted(ANDROID_ABI_CONFIG),
                    help="Android ABI（默认 arm64-v8a）")
    ap.add_argument("--docker", action="store_true",
                    help="等价 --platform linux（兼容旧参数）")
    ap.add_argument("--install-dir", default="",
                    help="覆盖安装前缀（默认按平台: build{,_ios,_android}/mediapipe/install）")
    ap.add_argument("--bazel-user-root", default="",
                    help="重定向 bazel 输出基目录（如 F:/_bzl），用于复用本机缓存")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()
    mp_build = root / "build" / "mediapipe"

    if args.docker and args.platform == "host":
        args.platform = "linux"

    # install root：host=build/mediapipe/install；ios/android 走各自平台根；
    # 非 Linux 宿主的 --platform linux（Docker 产 Linux 库）装 install-linux，
    # 避免踩掉本机桌面 install（同 MNN 的"桌面 install 不编码宿主平台"教训）。
    if args.install_dir:
        mp_install = Path(args.install_dir)
    elif args.platform == "ios":
        mp_install = root / "build_ios" / "mediapipe" / "install"
    elif args.platform == "android":
        mp_install = (root / "build_android" / "mediapipe" / "install"
                      / args.android_abi)
    elif args.platform == "linux" and not platform.is_linux():
        mp_install = mp_build / "install-linux"
    else:
        mp_install = mp_build / "install"

    # targets
    # ios/android：构建 dylib/.so cc_binary（目标平台配置）——bazel 只在链接时
    # 物化传递依赖的归档/对象（apple 工具链产物是 .lo 单对象，非 .a），构建
    # 完成后从 bazel-out 收集合并；产物库才是真正的交付物。
    if args.targets == "image":
        mp_targets = MP_TARGETS_MINIMAL
    elif args.platform == "ios":
        mp_targets = MP_TARGETS_FULL
    elif args.platform == "android":
        mp_targets = MP_TARGETS_SO
    elif args.platform == "linux":
        mp_targets = MP_TARGETS_SO
    elif args.targets in ("all", ""):
        if platform.is_windows():
            mp_targets = MP_TARGETS_WIN
        elif platform.is_linux():
            mp_targets = MP_TARGETS_SO  # Linux 本机构建 libvision.so
        else:
            mp_targets = MP_TARGETS_FULL
    else:
        mp_targets = [t.strip() for t in args.targets.split(",") if t.strip()]

    # src
    if args.src:
        mp_src = Path(args.src)
        if not mp_src.is_absolute():
            mp_src = Path.cwd() / mp_src
    else:
        # sentinel 记录 clone 时的版本：复用前比对，不符则重新克隆。
        # （历史事故：默认版本升到 v1.0.0 后，现存 v0.10.35 旧树被静默复用，
        #   产出的无前缀头文件与既有 Mp* dylib 混装，mediapipe_vision 编译失败。）
        mp_src = mp_build / "mediapipe-src"
        ver_file = mp_src / ".mp-build-version"
        reuse = (mp_src / ".git").is_dir()
        if reuse:
            have = (_read(ver_file) or "").strip()
            if have != args.version:
                console.warn(f"现有源码树 {mp_src} 版本不符"
                             f"（记录 {have or '未知'}，请求 {args.version}），重新克隆")
                _rm_rf(mp_src)
                reuse = False
        if not reuse:
            console.step(f"Cloning MediaPipe {args.version} to {mp_src}")
            mp_build.mkdir(parents=True, exist_ok=True)
            code = runner.run(["git", "clone", "--depth", "1", "--branch", args.version, MP_REPO, str(mp_src)])
            if code != 0:
                console.fail(f"git clone MediaPipe 失败 (exit {code})")
                return code
            _write(ver_file, args.version + "\n")
        else:
            print(f"==> Using existing MediaPipe source at {mp_src} ({args.version})")

    if not (mp_src / "WORKSPACE").is_file() and not (mp_src / "MODULE.bazel").is_file():
        console.fail(f"MediaPipe 源码不存在或非 Bazel 项目: {mp_src}")
        return 1

    # Linux on 非 Linux 宿主：Docker 构建（容器内自带 JDK + linux bazelisk）
    docker_done = False
    if args.platform == "linux" and not platform.is_linux():
        eigen_patched = prepare_eigen_override(mp_build, mp_src)
        patch_linux_opencv4(mp_src)
        # vision BUILD 补丁（锚点 srcs + 导出清单）对 Docker 内的 .so 构建
        # 同样必需：容器里跑的就是这份挂载源码，未补丁时产出的同样是空壳 .so
        patch_vision_build(mp_src)
        code = run_docker_linux_build(mp_src, mp_build, jobs, eigen_patched)
        if code != 0:
            console.fail(f"Docker Linux 构建失败 (exit {code})")
            return code
        bazel_cmd, is_docker, docker_done = None, True, True
    else:
        bazel_cmd, is_docker = resolve_bazel_cmd(False, mp_src, mp_build)
        if bazel_cmd is None:
            return 1

    console.step(f"Building targets ({jobs} jobs):")
    for t in mp_targets:
        print(f"    {t}")

    override_args: List[str] = []
    env = dict(os.environ, HERMETIC_PYTHON_VERSION="3.12")
    if docker_done:
        pass  # 构建已在容器内完成（产物 mp_build/libvision.so），直接进收集
    else:
        # 本机：zlib + eigen override + 平台补丁（macOS OpenCV5 / Windows MSVC）
        zlib_patched = prepare_zlib_override(mp_build, mp_src)
        if zlib_patched:
            override_args += ["--override_repository=zlib=" + str(zlib_patched)]
        eigen_patched = prepare_eigen_override(mp_build, mp_src)
        if eigen_patched:
            override_args += ["--override_repository=eigen=" + str(eigen_patched)]
        if platform.is_macos():
            patch_macos_opencv5(mp_src)
        patch_opencv5_api(mp_src)
        if platform.is_linux():
            # Linux 本机构建（CI ubuntu）：系统 OpenCV 是 4.x 布局
            patch_linux_opencv4(mp_src)
        if platform.is_windows():
            patch_windows_msvc(mp_src)
            patch_windows_opencv(mp_src)
            swift_stub = prepare_swift_stub(mp_build)
            if swift_stub:
                override_args.append(
                    f"--override_repository={SWIFT_LOCAL_CONFIG_CANONICAL}=" + str(swift_stub))
        # 补丁覆盖全部共享库目标：dylib/dll（exported_symbols_list / vision.dll
        # 注入）+ so（锚点 srcs——GNU ld 归档拉入，Linux host/CI 原生路径；
        # 此前漏掉 SO targets，CI 上 BUILD 从未打补丁 → 空 .so 复发）
        patched_targets = MP_TARGETS_FULL + MP_TARGETS_WIN + MP_TARGETS_SO
        if any(t in patched_targets for t in mp_targets):
            patch_vision_build(mp_src)

        build_flags = ["--define=MEDIAPIPE_DISABLE_GPU=1"]
        if args.platform == "ios":
            config_flags = ["--config=ios_arm64"]
        elif args.platform == "android":
            config_flags = [
                "--config=" + ANDROID_ABI_CONFIG[args.android_abi],
            ]
            # Android 与桌面不同：不能 MEDIAPIPE_DISABLE_GPU——gl_context.h 有
            # 未守卫的 GlVersion/GlTextureInfo 调用点，gpu_buffer_format.h 里
            # 这些类型被 #if !MEDIAPIPE_DISABLE_GPU 剥掉后直接编译错误。NDK 自带
            # EGL/GLES 头，上游 AAR 也是编入 GL 的；GPU delegate 由我们引擎层的
            # TASK_GRAPH_MEDIAPIPE_CPU_ONLY 在运行时拦截。
            build_flags = [f for f in build_flags
                           if f != "--define=MEDIAPIPE_DISABLE_GPU=1"]
        elif platform.is_macos():
            config_flags = ["--config=macos"]
        else:
            config_flags = []
        if platform.is_linux() and args.platform in ("host", "linux"):
            # XNNPACK：ubuntu-22.04 的 binutils（gas 2.38）汇编不了 gcc-13 编出的
            # AVXVNNIINT8/AMX 等 x86 新指令（vpdpbssd，gas 2.42 才支持；CI 首跑
            # 实测 avxvnniint8_prod_microkernels 全批 assembler error）。与下方
            # Windows 配方同款 define 全量关掉——只影响 x86 CPU microkernel 选择，
            # arm64 目标与功能无损。
            build_flags += [
                "--define=xnn_enable_avxvnni=false",
                "--define=xnn_enable_avxvnniint8=false",
                "--define=xnn_enable_avx256vnni=false",
                "--define=xnn_enable_avx256vnnigfni=false",
                "--define=xnn_enable_avx512vnni=false",
                "--define=xnn_enable_avx512vnnigfni=false",
                "--define=xnn_enable_avx512amx=false",
                "--define=xnn_enable_avx512bf16=false",
                "--define=xnn_enable_avx512fp16=false",
            ]
        env = dict(os.environ, HERMETIC_PYTHON_VERSION="3.12")
        if args.platform == "android":
            ndk = resolve_android_ndk()
            if ndk is None:
                console.fail("未找到 Android NDK（设 ANDROID_NDK/ANDROID_NDK_HOME，"
                             "或安装到 ~/Library/Android/sdk/ndk/）")
                return 1
            env["ANDROID_NDK_HOME"] = str(ndk)
            env["ANDROID_NDK"] = str(ndk)
            console.ok(f"ANDROID_NDK_HOME={ndk}")
            patch_android_ndk_workspace(mp_src, ndk)
        if platform.is_macos():
            # mediapipe v1.0.0 坑：无本地 JDK 时 local_jdk 走 bootstrap 模板并引用
            # 已移除的 @rules_java//tools/jdk，analysis 直接失败（详见 resolve_macos_jdk）。
            jdk = resolve_macos_jdk(mp_build)
            if jdk:
                env["JAVA_HOME"] = str(jdk)
                console.ok(f"JAVA_HOME={jdk}")
            else:
                console.warn("未找到本机 JDK，rules_java local_jdk 可能解析失败"
                             "（设 JAVA_HOME，或放 JDK 到 build/mediapipe/tools/jdk/）")
        elif platform.is_linux() and not env.get("JAVA_HOME"):
            # 同一 JDK 坑在 Linux 成立；GitHub runner 只保证 JAVA_HOME_<ver>_X64 系列
            for var in ("JAVA_HOME_21_X64", "JAVA_HOME_17_X64"):
                if os.environ.get(var):
                    env["JAVA_HOME"] = os.environ[var]
                    console.ok(f"JAVA_HOME={env['JAVA_HOME']}")
                    break
        if platform.is_windows():
            # Windows 原生配方（与实际成功构建逐项对齐，见 bazel server 日志）：
            build_flags += [
                # XNNPACK：关掉 MSVC 下编译不过 / 本机未必支持的 AVX 扩展
                "--define=xnn_enable_avxvnni=false",
                "--define=xnn_enable_avxvnniint8=false",
                "--define=xnn_enable_avx256vnni=false",
                "--define=xnn_enable_avx256vnnigfni=false",
                "--define=xnn_enable_avx512vnni=false",
                "--define=xnn_enable_avx512vnnigfni=false",
                "--define=xnn_enable_avx512amx=false",
                "--define=xnn_enable_avx512bf16=false",
                "--define=xnn_enable_avx512fp16=false",
                # protobuf 在 MSVC 下需要显式放行
                "--define=protobuf_allow_msvc=true",
                # MSVC 旧预处理器展不开 MediaPipe 的 status 宏（上游 PR #6238 同款）
                "--copt=/Zc:preprocessor", "--host_copt=/Zc:preprocessor",
                # 源码含 UTF-8 字符；XNNPACK/pthreadpool 的 C 代码需要 C11 atomics
                "--copt=/utf-8", "--host_copt=/utf-8",
                "--conlyopt=/std:c11", "--host_conlyopt=/std:c11",
                "--conlyopt=/experimental:c11atomics", "--host_conlyopt=/experimental:c11atomics",
                # 依赖 tarball（如 TensorFlow ~500MB）从 github.com 匿名下载，
                # 会被限流 429；bazel 仓库级下载重试（CI 实测需要）。
                "--experimental_repository_downloader_retries=5",
            ]
            env.update(windows_bazel_env(mp_build))
            if args.bazel_user_root:
                build_flags.append(f"--repository_cache={args.bazel_user_root}/repository_cache")
        # --output_user_root 是启动选项，必须位于子命令（build/info）之前
        startup_flags = [f"--output_user_root={args.bazel_user_root}"] if args.bazel_user_root else []
        if platform.is_windows():
            # protobuf MSVC 补丁依赖 fetch 物化的 external/（flags 与 build 完全
            # 一致，避免仓库集差异触发 protobuf~ 重新物化抹掉补丁）
            if not prefetch_and_patch_protobuf(bazel_cmd, startup_flags, config_flags,
                                               build_flags, override_args, mp_src,
                                               env, mp_targets):
                return 1
        cmd = bazel_cmd + startup_flags + ["build", "-c", "opt", "--jobs", str(jobs)] + config_flags + build_flags + override_args + mp_targets
        code = subprocess.run(cmd, cwd=str(mp_src), env=env).returncode
        if code != 0:
            console.fail(f"bazel build 失败 (exit {code})")
            return code

    # ---- 收集产物 ----
    console.step("Collecting build outputs")
    bazel_bin_host = ""
    if is_docker:
        bazel_bin_host = str(mp_src / "bazel-out")
        console.warn(f"Docker 模式产物在 {bazel_bin_host}（Linux 二进制，仅供 Linux 桌面/CI）")
    else:
        info_cmd = list(bazel_cmd)
        if args.bazel_user_root:
            info_cmd.append(f"--output_user_root={args.bazel_user_root}")
        info_cmd.append("info")
        info_cmd.append("-c")
        info_cmd.append("opt")
        info_cmd.append("bazel-bin")
        r = subprocess.run(info_cmd, cwd=str(mp_src),
                           env=env, capture_output=True, text=True, check=False)
        bazel_bin = r.stdout.strip()
        if bazel_bin:
            bazel_bin_host = str(Path(bazel_bin))

    if mp_install.exists():
        _rm_rf(mp_install)
    (mp_install / "lib").mkdir(parents=True, exist_ok=True)
    (mp_install / "include").mkdir(parents=True, exist_ok=True)

    # 交叉构建（ios/android）：收集目标平台产物合并成单库
    if args.platform in ("ios", "android"):
        console.step("Collecting + merging target-platform static libraries")
        if args.platform == "ios":
            declared = cquery_target_archives(bazel_cmd, config_flags, build_flags,
                                              override_args, mp_src, env, mp_targets,
                                              require_exists=False)
            archives = collect_apple_objects(declared, bazel_cmd, config_flags,
                                             build_flags, override_args, mp_src, env,
                                             mp_build)
        else:
            # android crosstool 同样对象直链（_objs/*.o），传递 .a 不物化
            declared = cquery_target_archives(bazel_cmd, config_flags, build_flags,
                                              override_args, mp_src, env, mp_targets,
                                              require_exists=False)
            archives = collect_target_objects(declared, (".o", ".lo", ".a"))
        if not archives:
            return 1
        print(f"    {len(archives)} archives")
        merged_lib = mp_install / "lib" / "libmediapipe_vision_c.a"
        if args.platform == "ios":
            ok = merge_archives_libtool(archives, merged_lib)
            if not ok:
                console.fail("libtool 合并失败")
                return 1
            # strip 调试段（bazel apple opt 产物仍带 -g 调试信息，合并后 GB 级；
            # strip -S 只去调试符号，不影响链接语义与全局符号表）
            console.step("Stripping debug symbols")
            subprocess.run(["strip", "-S", str(merged_lib)], check=False)
        else:
            ndk = resolve_android_ndk()
            ar_candidates = sorted((ndk / "toolchains" / "llvm" / "prebuilt")
                                   .glob("*/bin/llvm-ar")) if ndk else []
            if not ar_candidates:
                console.fail("NDK 里未找到 llvm-ar")
                return 1
            if not merge_archives_ar(archives, merged_lib, str(ar_candidates[0])):
                console.fail(f"llvm-ar MRI 合并失败: {ar_candidates[0]}")
                return 1
        print(f"    -> {merged_lib}")
        _install_headers(mp_src, mp_install)
        _print_done(mp_install, merged_lib)
        return 0

    # Linux（本机或 Docker）：libvision.so
    is_linux_target = (args.platform == "linux"
                       or (args.platform == "host" and platform.is_linux()))
    if is_linux_target:
        so_found = ""
        # Docker：容器内构建完拷回 mp_build/libvision.so（见 run_docker_linux_build）
        if is_docker:
            cand = mp_build / "libvision.so"
            if cand.is_file():
                so_found = str(cand)
        else:
            for d, _, files in os.walk(bazel_bin_host or ""):
                if "runfiles" not in d and "libvision.so" in files:
                    so_found = str(Path(d) / "libvision.so")
                    break
        if not so_found:
            console.fail(f"未找到 libvision.so（docker={is_docker}, "
                         f"bazel_bin={bazel_bin_host}）")
            return 1
        console.step("Found libvision.so (完整 MediaPipe framework, 含全部 C API)")
        shutil.copy2(so_found, mp_install / "lib" / "libvision.so")
        print(f"    -> {mp_install}/lib/libvision.so")
        _install_headers(mp_src, mp_install)
        _print_done(mp_install, mp_install / "lib" / "libvision.so")
        return 0

    # 优先找 libvision.dylib（完整 framework）
    dylib_found = ""
    if not is_docker and bazel_bin_host:
        for d, _, files in os.walk(bazel_bin_host):
            if "opt/bin" in d.replace("\\", "/") and "runfiles" not in d:
                if "libvision.dylib" in files:
                    dylib_found = str(Path(d) / "libvision.dylib")
                    break

    if dylib_found:
        console.step("Found libvision.dylib (完整 MediaPipe framework, 含全部 C API)")
        shutil.copy2(dylib_found, mp_install / "lib" / "libvision.dylib")
        print(f"    -> {mp_install}/lib/libvision.dylib")
        _install_headers(mp_src, mp_install)
        _print_done(mp_install, mp_install / "lib" / "libvision.dylib")
        return 0

    # Windows: vision.dll + 导入库（vision.dll.if.lib → vision.lib）
    if platform.is_windows() and not is_docker and bazel_bin_host:
        vision_dir = Path(bazel_bin_host) / "mediapipe" / "tasks" / "c" / "vision"
        dll = vision_dir / "vision.dll"
        implib = vision_dir / "vision.dll.if.lib"
        if dll.is_file() and implib.is_file():
            console.step("Found vision.dll (完整 MediaPipe framework, 含全部 C API)")
            (mp_install / "bin").mkdir(parents=True, exist_ok=True)
            _copy_writable(dll, mp_install / "bin" / "vision.dll")
            _copy_writable(implib, mp_install / "lib" / "vision.lib")
            print(f"    -> {mp_install}/bin/vision.dll")
            print(f"    -> {mp_install}/lib/vision.lib")
            _install_headers(mp_src, mp_install)
            _print_done(mp_install, mp_install / "bin" / "vision.dll")
            return 0
        console.fail(f"未找到 {dll} / {implib}")
        return 1

    # 合并静态库
    console.step("Merging static libraries")
    found_as: List[str] = []
    if not is_docker and bazel_bin_host:
        cquery = bazel_cmd + ["cquery", "-c", "opt"] + override_args + ["--output=files"] + mp_targets
        r = subprocess.run(cquery, cwd=str(mp_src), env=env, capture_output=True, text=True, check=False)
        for line in r.stdout.splitlines():
            line = line.strip()
            if line.endswith(".a"):
                found_as.append(line)
    if not found_as and bazel_bin_host:
        for d, _, files in os.walk(bazel_bin_host):
            for f in files:
                if f.startswith("lib") and f.endswith(".a"):
                    found_as.append(str(Path(d) / f))

    if not found_as:
        console.fail(f"未在 {bazel_bin_host} 下找到任何 .a 文件")
        return 1

    merged_lib = mp_install / "lib" / "libmediapipe_vision_c.a"
    merge_tmp = mp_build / "merge-tmp"
    if merge_tmp.exists():
        shutil.rmtree(merge_tmp, ignore_errors=True)
    merge_tmp.mkdir(parents=True, exist_ok=True)
    for a in found_as:
        ap = Path(a)
        if not ap.is_absolute():
            ap = mp_src / a
        if ap.is_file():
            subprocess.run(["ar", "x", str(ap)], cwd=str(merge_tmp), check=False)
    objs = sorted(merge_tmp.glob("*.o"))
    if platform.is_macos():
        libtool = shutil.which("libtool")
        if libtool:
            code = subprocess.run([libtool, "-static", "-o", str(merged_lib)] + [str(o) for o in objs]).returncode
        else:
            code = 1
        if code != 0:
            code = subprocess.run(["ar", "rcs", str(merged_lib)] + [str(o) for o in objs]).returncode
    else:
        code = subprocess.run(["ar", "rcs", str(merged_lib)] + [str(o) for o in objs]).returncode
    shutil.rmtree(merge_tmp, ignore_errors=True)
    if code != 0:
        console.fail(f"静态库合并失败 (exit {code})")
        return code
    print(f"    -> {merged_lib}")

    _install_headers(mp_src, mp_install)
    _print_done(mp_install, merged_lib)
    return 0


def _install_headers(mp_src: Path, mp_install: Path) -> None:
    console.step("Installing headers")
    dst = mp_install / "include" / "mediapipe" / "tasks" / "c"
    dst.mkdir(parents=True, exist_ok=True)
    src_c = mp_src / "mediapipe" / "tasks" / "c"
    if src_c.is_dir():
        for item in src_c.iterdir():
            d = dst / item.name
            if item.is_dir():
                if d.exists():
                    shutil.rmtree(d)
                shutil.copytree(item, d)
            else:
                shutil.copy2(item, d)


def _print_done(mp_install: Path, lib: Path) -> None:
    print()
    console.ok("MediaPipe C API build complete")
    print(f"Install prefix: {mp_install}")
    print(f"Library: {lib}")
    print(f"Headers: {mp_install}/include/mediapipe/tasks/c/")
    print()
    print("重新配置 task_graph 以链接 MediaPipe：")
    print("  cmake -S . -B build && cmake --build build -j")


if __name__ == "__main__":
    sys.exit(main())