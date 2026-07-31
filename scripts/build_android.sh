#!/usr/bin/env bash
#
# build_android.sh - 用 Android NDK 交叉编译 task_graph + 子模块为静态库。
#
# 流程：
#   1) 用 NDK toolchain 配置 + 构建 libtask_graph.a + 子模块 .a
#   2) 合并核心库 + 子模块为 libtask_graph_full.a（llvm-ar MRI 脚本）
#   3) 复制公开头文件到 dist/android/
#
# 用法:
#   scripts/build_android.sh                       # 构建 arm64-v8a
#   scripts/build_android.sh --abi armeabi-v7a     # 指定 ABI
#   scripts/build_android.sh --api 24              # Android API level（默认 21）
#   scripts/build_android.sh --no-opencv           # 跳过 OpenCV 子模块
#   scripts/build_android.sh --also-x86_64         # 同时构建 x86_64（模拟器调试）
#   scripts/build_android.sh --clean               # 清空构建目录
#   scripts/build_android.sh -j <N>                # 并行编译线程数
#
# 环境要求：
#   - ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量指向 NDK 根目录
#   - CMake 3.16+
#   - (可选) scripts/build_opencv_android.sh 已执行，产出 build_android/opencv/install/
#
# 退出码：0 成功，非 0 失败。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ABI="arm64-v8a"
API_LEVEL="21"
JOBS=""
CLEAN=0
NO_OPENCV=0
ALSO_X86_64=0

if [[ -t 1 ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_BOLD=""; C_RESET=""
fi

usage() {
    sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --abi)          ABI="$2"; shift 2 ;;
        --api)          API_LEVEL="$2"; shift 2 ;;
        --no-opencv)    NO_OPENCV=1; shift ;;
        --also-x86_64)  ALSO_X86_64=1; shift ;;
        -j|--jobs)      JOBS="$2"; shift 2 ;;
        --clean)        CLEAN=1; shift ;;
        -h|--help)      usage 0 ;;
        *) echo "${C_RED}未知参数: $1${C_RESET}" >&2; usage 1 ;;
    esac
done

if [[ -z "${JOBS}" ]]; then
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4)
fi

cd "${ROOT_DIR}"

# ============================================================
# 环境检查
# ============================================================
ANDROID_NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}"
if [[ -z "${ANDROID_NDK}" ]]; then
    echo "${C_RED}找不到 Android NDK。请设置 ANDROID_NDK 或 ANDROID_NDK_HOME 环境变量${C_RESET}" >&2
    exit 1
fi
if [[ ! -f "${ANDROID_NDK}/build/cmake/android.toolchain.cmake" ]]; then
    echo "${C_RED}NDK toolchain 文件不存在：${ANDROID_NDK}/build/cmake/android.toolchain.cmake${C_RESET}" >&2
    exit 1
fi

TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"

# 查找 NDK 的 llvm-ar（用于合并静态库）
LLVM_AR=$(find "${ANDROID_NDK}/toolchains/llvm/prebuilt" -name llvm-ar -type f 2>/dev/null | head -1)
if [[ -z "${LLVM_AR}" ]]; then
    echo "${C_RED}找不到 llvm-ar，请检查 NDK 安装${C_RESET}" >&2
    exit 1
fi

# OpenCV 可用性检查
OPENCV_FLAG=""
OPENCV_AVAILABLE=0
if [[ "${NO_OPENCV}" -eq 0 ]]; then
    if [[ -d "${ROOT_DIR}/build_android/opencv/install/lib/cmake/opencv4" ]]; then
        OPENCV_FLAG="-DTASK_GRAPH_ENABLE_OPENCV=ON"
        OPENCV_AVAILABLE=1
        echo "${C_BOLD}==> 检测到 OpenCV Android 库，启用 OpenCV${C_RESET}"
    else
        echo "${C_BOLD}==> 未检测到 OpenCV Android 库（运行 scripts/build_opencv_android.sh 构建），跳过 OpenCV 子模块${C_RESET}"
    fi
fi

# 清理
if [[ "${CLEAN}" -eq 1 ]]; then
    echo "${C_BOLD}==> 清理 Android 构建目录${C_RESET}"
    rm -rf "${ROOT_DIR}/build_android" "${ROOT_DIR}/dist/android"
fi

# ============================================================
# 构建函数
# ============================================================
build_abi() {
    local abi="$1"
    local build_dir="${ROOT_DIR}/build_android_${abi}"
    local dist_dir="${ROOT_DIR}/dist/android/${abi}"

    echo "${C_BOLD}==> 构建 Android ${abi} (API ${API_LEVEL})${C_RESET}"

    cmake -S "${ROOT_DIR}" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DANDROID_ABI="${abi}" \
        -DANDROID_PLATFORM="android-${API_LEVEL}" \
        -DCMAKE_BUILD_TYPE=Release \
        ${OPENCV_FLAG} \
        >/dev/null

    cmake --build "${build_dir}" --target task_graph -j "${JOBS}"

    # 构建子模块（Metal 不可用，gpu_image_processing 自动跳过）
    local submodules=(task1 task2 task_processor)
    [[ "${OPENCV_AVAILABLE}" -eq 1 ]] && submodules+=(image_filtering image_reader)

    for sub in "${submodules[@]}"; do
        cmake --build "${build_dir}" --target "${sub}" -j "${JOBS}" 2>/dev/null || true
    done

    # 合并静态库
    merge_static_libs "${build_dir}" "${dist_dir}"
}

# ============================================================
# 合并静态库：核心 + 子模块 + OpenCV -> libtask_graph.a
# 使用 llvm-ar MRI 脚本合并 .a 文件
# ============================================================
merge_static_libs() {
    local build_dir="$1"
    local dist_dir="$2"
    local output="${dist_dir}/libtask_graph.a"

    mkdir -p "${dist_dir}"

    local libs=("${build_dir}/libtask_graph.a")
    for sub in task1 task2 task_processor image_filtering image_reader; do
        local lib="${build_dir}/lib${sub}.a"
        [[ -f "${lib}" ]] && libs+=("${lib}")
    done

    # OpenCV 静态库
    if [[ "${OPENCV_AVAILABLE}" -eq 1 ]]; then
        local oc_dir="${ROOT_DIR}/build_android/opencv/install/lib"
        for oc_lib in "${oc_dir}"/libopencv_*.a; do
            [[ -f "${oc_lib}" ]] && libs+=("${oc_lib}")
        done
        # Android NDK 的 libcpufeatures / libtegra_hal 等（OpenCV 可能依赖）
        for oc_lib in "${oc_dir}"/lib*.a; do
            [[ -f "${oc_lib}" ]] || continue
            local basename=$(basename "${oc_lib}")
            [[ "${basename}" == libopencv_* ]] && continue
            libs+=("${oc_lib}")
        done
    fi

    echo "${C_BOLD}==> 合并静态库 -> ${output} (${#libs[@]} 个库)${C_RESET}"

    # 生成 MRI 脚本
    local mri_script="${dist_dir}/merge.mri"
    echo "CREATE ${output}" > "${mri_script}"
    for lib in "${libs[@]}"; do
        echo "ADDLIB ${lib}" >> "${mri_script}"
    done
    echo "SAVE" >> "${mri_script}"
    echo "END" >> "${mri_script}"

    "${LLVM_AR}" -M < "${mri_script}"
    rm -f "${mri_script}"
}

# ============================================================
# 执行构建
# ============================================================
build_abi "${ABI}"

if [[ "${ALSO_X86_64}" -eq 1 ]]; then
    build_abi "x86_64"
fi

# ============================================================
# 复制头文件
# ============================================================
echo "${C_BOLD}==> 复制头文件到 dist/android/include/${C_RESET}"
mkdir -p "${ROOT_DIR}/dist/android/include"
cp -R "${ROOT_DIR}/include/"* "${ROOT_DIR}/dist/android/include/"

echo "${C_GREEN}${C_BOLD}==> 构建完成${C_RESET}"
echo "    dist/android/${ABI}/libtask_graph.a"
[[ "${ALSO_X86_64}" -eq 1 ]] && echo "    dist/android/x86_64/libtask_graph.a"
echo "    dist/android/include/"
