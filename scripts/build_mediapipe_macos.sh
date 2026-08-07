#!/usr/bin/env bash
#
# build_mediapipe_macos.sh - 构建 MediaPipe Tasks C API 静态库，供 task_graph 链接。
#
# 产出 libmediapipe_vision_c.a + 头文件，安装到 build/mediapipe/install/。
# 覆盖 MpImage / FaceLandmarker / HandLandmarker / PoseLandmarker / ObjectDetector 的 C API。
#
# 用法：
#   scripts/build_mediapipe_macos.sh                         # 默认本机 bazelisk 构建
#   scripts/build_mediapipe_macos.sh --targets image         # 只构建 MpImage（快速验证链路）
#   scripts/build_mediapipe_macos.sh --docker                # 用 Docker 构建（产 Linux 版）
#   scripts/build_mediapipe_macos.sh --version v0.10.35      # 指定 MediaPipe 版本
#   scripts/build_mediapipe_macos.sh --src /path/to/mp/src   # 指定本地 MediaPipe 源码
#
# 重要约束：
#   macOS 上 Docker 只能产出 Linux 二进制，无法链接进 macOS 的 task_graph。
#   因此 macOS 桌面默认走本机 bazelisk（brew install bazelisk）。
#   --docker 仅用于产出 Linux 版预编译库（Linux 桌面 / CI）。
#
# 前置（本机构建）：brew install bazelisk

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MP_VERSION="v1.0.0"
MP_SRC=""
USE_DOCKER=false
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

MP_TARGETS_FULL=(
    "//mediapipe/tasks/c/vision:libvision.dylib"
)
MP_TARGETS_MINIMAL=(
    "//mediapipe/tasks/c/vision/core:image"
)
MP_TARGETS=("${MP_TARGETS_FULL[@]}")

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)  MP_VERSION="$2"; shift 2 ;;
        --src)      MP_SRC="$2"; shift 2 ;;
        --targets)
            case "$2" in
                image)    MP_TARGETS=("${MP_TARGETS_MINIMAL[@]}") ;;
                all|"")   MP_TARGETS=("${MP_TARGETS_FULL[@]}") ;;
                *)        IFS=',' read -r -a MP_TARGETS <<< "$2" ;;
            esac
            shift 2 ;;
        --docker)   USE_DOCKER=true; shift ;;
        --jobs|-j)  JOBS="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
done

MP_BUILD_DIR="${ROOT_DIR}/build/mediapipe"
MP_INSTALL_DIR="${MP_BUILD_DIR}/install"

if [[ -z "${MP_SRC}" ]]; then
    MP_SRC="${MP_BUILD_DIR}/mediapipe-src"
    if [[ ! -d "${MP_SRC}/.git" ]]; then
        echo "==> Cloning MediaPipe ${MP_VERSION} to ${MP_SRC}"
        mkdir -p "${MP_BUILD_DIR}"
        git clone --depth 1 --branch "${MP_VERSION}" \
            https://github.com/google-ai-edge/mediapipe.git "${MP_SRC}"
    else
        echo "==> Using existing MediaPipe source at ${MP_SRC}"
    fi
fi

if [[ ! -f "${MP_SRC}/WORKSPACE" && ! -f "${MP_SRC}/MODULE.bazel" ]]; then
    echo "ERROR: MediaPipe 源码不存在或非 Bazel 项目: ${MP_SRC}" >&2
    exit 1
fi

BAZEL_CMD=""
if [[ "${USE_DOCKER}" == "true" ]]; then
    echo "==> Docker 模式（产出 Linux 二进制，仅供 Linux 桌面/CI）"
    if ! docker info >/dev/null 2>&1; then
        echo "ERROR: Docker 未运行" >&2; exit 1
    fi
    BAZEL_CMD=(docker run --rm -i --user "$(id -u):$(id -g)" \
        -v "${MP_SRC}:/work" -v "${MP_BUILD_DIR}/bazel-cache:/root/.cache/bazel" \
        -w /work \
        ghcr.io/bazelbuild/bazelisk:latest)
else
    if command -v bazelisk >/dev/null 2>&1; then
        BAZEL_CMD=(bazelisk)
    elif command -v bazel >/dev/null 2>&1; then
        BAZEL_CMD=(bazel)
    else
        echo "ERROR: 未找到 bazel/bazelisk。macOS 请: brew install bazelisk" >&2
        exit 1
    fi
    echo "==> 本机构建模式: ${BAZEL_CMD[0]}"
fi

echo "==> Building targets (${JOBS} jobs):"
printf '    %s\n' "${MP_TARGETS[@]}"

if [[ "${USE_DOCKER}" == "true" ]]; then
    docker run --rm -i --user "$(id -u):$(id -g)" \
        -v "${MP_SRC}:/work" -v "${MP_BUILD_DIR}/bazel-cache:/root/.cache/bazel" \
        -w /work -e HERMETIC_PYTHON_VERSION=3.12 \
        ghcr.io/bazelbuild/bazelisk:latest \
        build -c opt --jobs "${JOBS}" "${MP_TARGETS[@]}"
else
    MP_OVERRIDE_ARGS=()
    ZLIB_PATCHED="${MP_BUILD_DIR}/zlib-patched"
    if [[ ! -d "${ZLIB_PATCHED}" ]]; then
        ZLIB_SRC="${MP_BUILD_DIR}/zlib-1.2.13-src"
        if [[ ! -d "${ZLIB_SRC}" ]]; then
            echo "==> Fetching zlib 1.2.13 source"
            mkdir -p "${MP_BUILD_DIR}"
            curl -sfL http://zlib.net/fossils/zlib-1.2.13.tar.gz | tar xz -C "${MP_BUILD_DIR}"
            mv "${MP_BUILD_DIR}/zlib-1.2.13" "${ZLIB_SRC}"
        fi
        cp -R "${ZLIB_SRC}" "${ZLIB_PATCHED}"
        sed -i '' 's/#if defined(MACOS) || defined(TARGET_OS_MAC)/#if defined(MACOS)/' \
            "${ZLIB_PATCHED}/zutil.h" 2>/dev/null || \
            sed -i 's/#if defined(MACOS) || defined(TARGET_OS_MAC)/#if defined(MACOS)/' \
            "${ZLIB_PATCHED}/zutil.h"
        cp "${MP_SRC}/third_party/zlib.BUILD" "${ZLIB_PATCHED}/BUILD.bazel"
        touch "${ZLIB_PATCHED}/WORKSPACE" "${ZLIB_PATCHED}/REPO.bazel"
        echo "==> Prepared zlib override (TARGET_OS_MAC fdopen fix)"
    fi
    MP_OVERRIDE_ARGS=(--override_repository=zlib="${ZLIB_PATCHED}")

    EIGEN_PATCHED="${MP_BUILD_DIR}/eigen-patched"
    if [[ ! -d "${EIGEN_PATCHED}/Eigen" ]]; then
        echo "==> Preparing eigen override (gitlab 403 workaround)"
        rm -rf "${EIGEN_PATCHED}"
        git clone --depth 1 https://gitlab.com/libeigen/eigen.git "${EIGEN_PATCHED}" 2>/dev/null
        if [[ -d "${EIGEN_PATCHED}/.git" ]]; then
            (cd "${EIGEN_PATCHED}" && git fetch --depth 1 origin 4c38131a16803130b66266a912029504f2cf23cd 2>/dev/null && git checkout 4c38131a16803130b66266a912029504f2cf23cd 2>/dev/null)
        fi
        cp "${MP_SRC}/third_party/eigen.BUILD" "${EIGEN_PATCHED}/BUILD.bazel" 2>/dev/null || true
        touch "${EIGEN_PATCHED}/WORKSPACE" "${EIGEN_PATCHED}/REPO.bazel"
    fi
    [[ -d "${EIGEN_PATCHED}/Eigen" ]] && MP_OVERRIDE_ARGS+=(--override_repository=eigen="${EIGEN_PATCHED}")

    if [[ "$(uname)" == "Darwin" ]]; then
        OCV_CELLAR="/opt/homebrew/Cellar"
        OCV_VER="$(ls -d ${OCV_CELLAR}/opencv/*/ 2>/dev/null | head -1)"
        if [[ -n "${OCV_VER}" ]]; then
            OCV_VER_NAME="$(basename "${OCV_VER}")"
            sed -i '' "s|path = \"/usr/local\"|path = \"${OCV_CELLAR}\"|" "${MP_SRC}/WORKSPACE" 2>/dev/null || true
            sed -i '' "s|PREFIX = \"[^\"]*\"|PREFIX = \"opencv/${OCV_VER_NAME}\"|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
            sed -i '' "s|include/opencv2/\*\*/\*.h\*|include/opencv5/opencv2/**/*.h*|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
            sed -i '' "s|includes = \[paths.join(PREFIX, \"include/\")\]|includes = [paths.join(PREFIX, \"include/opencv5\")]|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
            sed -i '' "s|libopencv_calib3d.dylib|libopencv_calib.dylib|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
            sed -i '' "s|libopencv_features2d.dylib|libopencv_features.dylib|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
            grep -q "libopencv_geometry.dylib" "${MP_SRC}/third_party/opencv_macos.BUILD" || \
                sed -i '' "s|libopencv_imgcodecs.dylib|libopencv_geometry.dylib\n            paths.join(PREFIX, \"lib/libopencv_imgcodecs.dylib\")|" "${MP_SRC}/third_party/opencv_macos.BUILD" 2>/dev/null || true
        fi
    fi

    # OpenCV5 API 兼容:getPerspectiveTransform 移到 geometry/2d.hpp;boxPoints 移除
    if [[ -f "${MP_SRC}/mediapipe/calculators/tensor/image_to_tensor_converter_opencv.cc" ]]; then
        sed -i '' 's|#include "mediapipe/framework/port/opencv_imgproc_inc.h"|#include "mediapipe/framework/port/opencv_imgproc_inc.h"\n#if CV_VERSION_MAJOR >= 5\n#include <opencv2/geometry/2d.hpp>\n#endif|' \
            "${MP_SRC}/mediapipe/calculators/tensor/image_to_tensor_converter_opencv.cc" 2>/dev/null || true
        sed -i '' 's|cv::getPerspectiveTransform(src_points, dst_points);|cv::getPerspectiveTransform(src_points, dst_points, cv::DECOMP_LU);|' \
            "${MP_SRC}/mediapipe/calculators/tensor/image_to_tensor_converter_opencv.cc" 2>/dev/null || true
        grep -q "rotated_rect.points" "${MP_SRC}/mediapipe/calculators/tensor/image_to_tensor_converter_opencv.cc" || \
            python3 - "${MP_SRC}/mediapipe/calculators/tensor/image_to_tensor_converter_opencv.cc" << 'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "    cv::Mat src_points;\n    cv::boxPoints(rotated_rect, src_points);"
new = ("    cv::Mat src_points;\n"
       "    std::vector<cv::Point2f> rotated_pts;\n"
       "    rotated_rect.points(rotated_pts);\n"
       "    src_points = cv::Mat(4, 2, CV_32F);\n"
       "    for (int i = 0; i < 4; ++i) {\n"
       "        src_points.at<float>(i, 0) = rotated_pts[i].x;\n"
       "        src_points.at<float>(i, 1) = rotated_pts[i].y;\n"
       "    }")
if old in s:
    open(p, 'w').write(s.replace(old, new))
    print("patched boxPoints")
PYEOF
    fi

    # OpenCV5 API 兼容:image_transformation_calculator 的 getRotationMatrix2D 移到
    # geometry/2d.hpp(holistic/gesture 等新 C API 才拉入该 calculator)。与上方
    # getPerspectiveTransform 同一模式:CV5 下显式 include geometry/2d.hpp。
    if [[ -f "${MP_SRC}/mediapipe/calculators/image/image_transformation_calculator.cc" ]]; then
        grep -q "geometry/2d.hpp" "${MP_SRC}/mediapipe/calculators/image/image_transformation_calculator.cc" || \
            sed -i '' 's|#include "mediapipe/framework/port/opencv_imgproc_inc.h"|#include "mediapipe/framework/port/opencv_imgproc_inc.h"\n#if CV_VERSION_MAJOR >= 5\n#include <opencv2/geometry/2d.hpp>\n#endif|' \
            "${MP_SRC}/mediapipe/calculators/image/image_transformation_calculator.cc"
    fi

    # 构建 libvision.dylib 时只导出 C API 符号,避免 protobuf 符号冲突
    if [[ "${MP_TARGETS_FULL[0]}" == *"libvision.dylib"* ]] && [[ -f "${MP_SRC}/mediapipe/tasks/c/vision/BUILD" ]]; then
        python3 - "${MP_SRC}" << 'PYEOF'
import sys, os
src = sys.argv[1]
build = os.path.join(src, "mediapipe/tasks/c/vision/BUILD")
s = open(build).read()
# 修正 VISION_LIBRARIES 为全部 11 个模块的 C API(把一次性全量重建,避免反复 bazel)
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
new_lib = "VISION_LIBRARIES = [\n" + ",\n".join(
    '    "{}"'.format(l) for l in ALL_VISION_LIBS) + ",\n]"
import re
s = re.sub(r'VISION_LIBRARIES = \[.*?\]', new_lib, s, flags=re.S)

# 导出符号白名单:静态列表,只导出 C API,避免 protobuf 符号冲突。
# 幂等合并:每次把「已有的符号」与「期望的新符号」并集写回,保证已有 checkout
# 也会被更新(而不是只在文件不存在/为空时写入)。
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
exp = os.path.join(src, "mediapipe/tasks/c/vision/exported_symbols.txt")
os.makedirs(os.path.dirname(exp), exist_ok=True)
existing = set()
if os.path.exists(exp) and os.path.getsize(exp) > 0:
    for line in open(exp):
        line = line.strip()
        if line:
            existing.add(line)
expected = set(l for l in EXPECTED_SYMBOLS.splitlines() if l.strip())
merged = sorted(existing | expected)
open(exp, 'w').write("".join(l + "\n" for l in merged))
print("merged %d exported symbols (%d new)" % (len(merged), len(expected - existing)))
# dylib linkopts
if 'exported_symbols_list' not in s:
    s = s.replace('"-Wl,-install_name,libvision.dylib",',
                  '"-Wl,-install_name,libvision.dylib",\n        "-Wl,-exported_symbols_list,%s/mediapipe/tasks/c/vision/exported_symbols.txt",' % src)
    if '"data = ["' not in s:
        s = s.replace('    linkshared = True,\n    tags = [', '    linkshared = True,\n    data = ["exported_symbols.txt"],\n    tags = [')
open(build, 'w').write(s)
print("patched vision BUILD")
PYEOF
    fi

    MP_BUILD_FLAGS=(--define=MEDIAPIPE_DISABLE_GPU=1)

    (cd "${MP_SRC}" && HERMETIC_PYTHON_VERSION=3.12 \
        "${BAZEL_CMD[@]}" build -c opt --jobs "${JOBS}" --config=macos \
        "${MP_BUILD_FLAGS[@]}" "${MP_OVERRIDE_ARGS[@]}" "${MP_TARGETS[@]}")
fi

echo "==> Collecting build outputs"
BAZEL_BIN_HOST=""
if [[ "${USE_DOCKER}" == "true" ]]; then
    BAZEL_BIN_HOST="${MP_SRC}/bazel-out"
    echo "WARN: Docker 模式产物在 ${BAZEL_BIN_HOST}（Linux 二进制，仅供 Linux 桌面/CI）" >&2
else
    BAZEL_BIN="$(cd "${MP_SRC}" && HERMETIC_PYTHON_VERSION=3.12 \
        "${BAZEL_CMD[@]}" info bazel-bin 2>/dev/null)"
    BAZEL_BIN_HOST="$(dirname "$(dirname "${BAZEL_BIN}")")"
fi

rm -rf "${MP_INSTALL_DIR}"
mkdir -p "${MP_INSTALL_DIR}/lib" "${MP_INSTALL_DIR}/include"

DYLIB_FOUND=""
if [[ "${USE_DOCKER}" == "false" ]]; then
    DYLIB_FOUND="$(find "${BAZEL_BIN_HOST}" -name "libvision.dylib" 2>/dev/null | grep "opt/bin" | grep -v runfiles | head -1)"
fi

if [[ -n "${DYLIB_FOUND}" ]]; then
    echo "==> Found libvision.dylib (完整 MediaPipe framework, 含全部 C API)"
    cp "${DYLIB_FOUND}" "${MP_INSTALL_DIR}/lib/libvision.dylib"
    echo "    -> ${MP_INSTALL_DIR}/lib/libvision.dylib ($(du -h "${MP_INSTALL_DIR}/lib/libvision.dylib" | cut -f1))"
    echo "==> Installing headers"
    mkdir -p "${MP_INSTALL_DIR}/include/mediapipe/tasks/c"
    if [[ -d "${MP_SRC}/mediapipe/tasks/c" ]]; then
        cp -R "${MP_SRC}/mediapipe/tasks/c/." "${MP_INSTALL_DIR}/include/mediapipe/tasks/c/"
    fi
    echo ""
    echo "=== MediaPipe C API build complete ==="
    echo "Install prefix: ${MP_INSTALL_DIR}"
    echo "Library: ${MP_INSTALL_DIR}/lib/libvision.dylib"
    echo "Headers: ${MP_INSTALL_DIR}/include/mediapipe/tasks/c/"
    echo ""
    echo "重新配置 task_graph 以链接 MediaPipe："
    echo "  cmake -S . -B build && cmake --build build -j"
    exit 0
fi

echo "==> Merging static libraries"
FOUND_AS=()
if [[ "${USE_DOCKER}" == "false" ]]; then
    while IFS= read -r f; do
        [[ "${f}" == *.a ]] && FOUND_AS+=("${f}")
    done < <(cd "${MP_SRC}" && HERMETIC_PYTHON_VERSION=3.12 \
        "${BAZEL_CMD[@]}" cquery -c opt --override_repository=zlib="${MP_BUILD_DIR}/zlib-patched" \
        --output=files "${MP_TARGETS[@]}" 2>/dev/null)
fi
if [[ ${#FOUND_AS[@]} -eq 0 ]]; then
    while IFS= read -r -d '' a; do
        FOUND_AS+=("$a")
    done < <(find "${BAZEL_BIN_HOST}" -name 'lib*.a' -print0 2>/dev/null)
fi

if [[ ${#FOUND_AS[@]} -eq 0 ]]; then
    echo "ERROR: 未在 ${BAZEL_BIN_HOST} 下找到任何 .a 文件" >&2
    exit 1
fi

MERGED_LIB="${MP_INSTALL_DIR}/lib/libmediapipe_vision_c.a"
MERGE_TMP="${MP_BUILD_DIR}/merge-tmp"
rm -rf "${MERGE_TMP}"
mkdir -p "${MERGE_TMP}"
for a in "${FOUND_AS[@]}"; do
    abs_a="${a}"
    [[ "${abs_a}" != /* ]] && abs_a="${MP_SRC}/${abs_a}"
    (cd "${MERGE_TMP}" && ar x "${abs_a}")
done
if [[ "$(uname)" == "Darwin" ]]; then
    libtool -static -o "${MERGED_LIB}" "${MERGE_TMP}"/*.o 2>/dev/null || \
        ar rcs "${MERGED_LIB}" "${MERGE_TMP}"/*.o
else
    ar rcs "${MERGED_LIB}" "${MERGE_TMP}"/*.o
fi
rm -rf "${MERGE_TMP}"
echo "    -> ${MERGED_LIB} ($(du -h "${MERGED_LIB}" | cut -f1))"

echo "==> Installing headers"
mkdir -p "${MP_INSTALL_DIR}/include/mediapipe/tasks/c"
if [[ -d "${MP_SRC}/mediapipe/tasks/c" ]]; then
    cp -R "${MP_SRC}/mediapipe/tasks/c/." "${MP_INSTALL_DIR}/include/mediapipe/tasks/c/"
fi
if [[ -d "${MP_SRC}/mediapipe/tasks/c/core" ]]; then
    mkdir -p "${MP_INSTALL_DIR}/include/mediapipe/tasks/c/core"
    cp -R "${MP_SRC}/mediapipe/tasks/c/core/." "${MP_INSTALL_DIR}/include/mediapipe/tasks/c/core/" 2>/dev/null || true
fi

echo ""
echo "=== MediaPipe C API build complete ==="
echo "Install prefix: ${MP_INSTALL_DIR}"
echo "Library: ${MERGED_LIB}"
echo "Headers: ${MP_INSTALL_DIR}/include/mediapipe/tasks/c/"
echo ""
echo "重新配置 task_graph 以链接 MediaPipe："
echo "  cmake -S . -B build && cmake --build build -j"
