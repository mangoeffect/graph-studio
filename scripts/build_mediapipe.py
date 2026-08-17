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
  python scripts/build_mediapipe.py --docker                # 用 Docker 构建（产 Linux 版）
  python scripts/build_mediapipe.py --version v0.10.35      # 指定 MediaPipe 版本
  python scripts/build_mediapipe.py --src /path/to/mp/src   # 指定本地 MediaPipe 源码
  python scripts/build_mediapipe.py --bazel-user-root F:/_bzl
                                                           # 重定向 bazel 输出基（缓存复用）

前置（本机构建）: brew install bazelisk（macOS）；Windows 自动下载 bazelisk.exe 到
build/mediapipe/tools/。
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
MP_TARGETS_MINIMAL = ["//mediapipe/tasks/c/vision/core:image"]
BAZELISK_URL = "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-windows-amd64.exe"


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
    # OpenCV5 拆出 libopencv_geometry（getPerspectiveTransform 等）
    if not _grep(build, "libopencv_geometry.dylib"):
        _replace_once(build, "libopencv_imgcodecs.dylib",
                      'libopencv_geometry.dylib\n            paths.join(PREFIX, "lib/libopencv_imgcodecs.dylib")')


# ---- OpenCV5 API 兼容补丁（C++ 源码）----

def patch_opencv5_api(mp_src: Path) -> None:
    """OpenCV5 API 兼容：getPerspectiveTransform 移到 geometry/2d.hpp；boxPoints 移除。"""
    f1 = mp_src / "mediapipe" / "calculators" / "tensor" / "image_to_tensor_converter_opencv.cc"
    if f1.is_file():
        if not _grep(f1, "geometry/2d.hpp"):
            _replace_once(
                f1,
                '#include "mediapipe/framework/port/opencv_imgproc_inc.h"',
                '#include "mediapipe/framework/port/opencv_imgproc_inc.h"\n'
                '#if CV_VERSION_MAJOR >= 5\n#include <opencv2/geometry/2d.hpp>\n#endif')
        _replace_once(f1, "cv::getPerspectiveTransform(src_points, dst_points);",
                      "cv::getPerspectiveTransform(src_points, dst_points, cv::DECOMP_LU);")
        if not _grep(f1, "rotated_rect.points"):
            old = ("    cv::Mat src_points;\n"
                   "    cv::boxPoints(rotated_rect, src_points);")
            new = ("    cv::Mat src_points;\n"
                   "    std::vector<cv::Point2f> rotated_pts;\n"
                   "    rotated_rect.points(rotated_pts);\n"
                   "    src_points = cv::Mat(4, 2, CV_32F);\n"
                   "    for (int i = 0; i < 4; ++i) {\n"
                   "        src_points.at<float>(i, 0) = rotated_pts[i].x;\n"
                   "        src_points.at<float>(i, 1) = rotated_pts[i].y;\n"
                   "    }")
            _replace_once(f1, old, new)

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

    if "exported_symbols_list" not in s:
        s = s.replace(
            '"-Wl,-install_name,libvision.dylib",',
            f'"-Wl,-install_name,libvision.dylib",\n'
            f'        "-Wl,-exported_symbols_list,{mp_src}/mediapipe/tasks/c/vision/exported_symbols.txt",')
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
    ap.add_argument("--docker", action="store_true",
                    help="用 Docker 构建（产 Linux 版，供 Linux 桌面/CI/WSL）")
    ap.add_argument("--bazel-user-root", default="",
                    help="重定向 bazel 输出基目录（如 F:/_bzl），用于复用本机缓存")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    args = ap.parse_args()

    root = repo_root()
    jobs = args.jobs or platform.cpu_count()
    mp_build = root / "build" / "mediapipe"
    mp_install = mp_build / "install"

    # targets
    if args.targets == "image":
        mp_targets = MP_TARGETS_MINIMAL
    elif args.targets in ("all", ""):
        mp_targets = MP_TARGETS_WIN if platform.is_windows() else MP_TARGETS_FULL
    else:
        mp_targets = [t.strip() for t in args.targets.split(",") if t.strip()]

    # src
    if args.src:
        mp_src = Path(args.src)
        if not mp_src.is_absolute():
            mp_src = Path.cwd() / mp_src
    else:
        mp_src = mp_build / "mediapipe-src"
        if not (mp_src / ".git").is_dir():
            console.step(f"Cloning MediaPipe {args.version} to {mp_src}")
            mp_build.mkdir(parents=True, exist_ok=True)
            code = runner.run(["git", "clone", "--depth", "1", "--branch", args.version, MP_REPO, str(mp_src)])
            if code != 0:
                console.fail(f"git clone MediaPipe 失败 (exit {code})")
                return code
        else:
            print(f"==> Using existing MediaPipe source at {mp_src}")

    if not (mp_src / "WORKSPACE").is_file() and not (mp_src / "MODULE.bazel").is_file():
        console.fail(f"MediaPipe 源码不存在或非 Bazel 项目: {mp_src}")
        return 1

    bazel_cmd, is_docker = resolve_bazel_cmd(args.docker, mp_src, mp_build)
    if bazel_cmd is None:
        return 1

    console.step(f"Building targets ({jobs} jobs):")
    for t in mp_targets:
        print(f"    {t}")

    override_args: List[str] = []
    if is_docker:
        env = dict(os.environ, HERMETIC_PYTHON_VERSION="3.12")
        cmd = bazel_cmd + ["build", "-c", "opt", "--jobs", str(jobs)] + mp_targets
        code = subprocess.run(cmd, env=env).returncode
        if code != 0:
            console.fail(f"bazel build 失败 (exit {code})")
            return code
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
        if platform.is_windows():
            patch_windows_msvc(mp_src)
            patch_windows_opencv(mp_src)
            swift_stub = prepare_swift_stub(mp_build)
            if swift_stub:
                override_args.append(
                    f"--override_repository={SWIFT_LOCAL_CONFIG_CANONICAL}=" + str(swift_stub))
        full_targets = MP_TARGETS_FULL + MP_TARGETS_WIN
        if any(t in full_targets for t in mp_targets):
            patch_vision_build(mp_src)

        build_flags = ["--define=MEDIAPIPE_DISABLE_GPU=1"]
        config_flags = ["--config=macos"] if platform.is_macos() else []
        env = dict(os.environ, HERMETIC_PYTHON_VERSION="3.12")
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
            ]
            env.update(windows_bazel_env(mp_build))
            if args.bazel_user_root:
                build_flags.append(f"--repository_cache={args.bazel_user_root}/repository_cache")
        # --output_user_root 是启动选项，必须位于子命令（build/info）之前
        startup_flags = [f"--output_user_root={args.bazel_user_root}"] if args.bazel_user_root else []
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