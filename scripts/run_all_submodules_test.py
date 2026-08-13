#!/usr/bin/env python3
"""run_all_submodules_test.py — 自动收集并运行 task_graph 全部子模块测试（跨平台）。

子模块测试由每个 leaf 子模块的 CMakeLists.txt 通过 add_test 注册进根 build/ 的
ctest 面板（gpu 仅 macOS/Metal、js_task 非 MSVC、mediapipe 需预编译库+模型否则软跳过）。
本脚本读取 subnode.json 枚举已声明子模块，运行 ctest -N 拿到实际注册的测试，再按
"子模块名 → 测试名正则" 表筛选出属于子模块的测试并一次性运行。

流程:
  1) 解析 subnode.json，核对每个子模块目录是否在磁盘上（缺失提示 git submodule update --init）
  2) 配置 + 构建根构建树（TASK_GRAPH_BUILD_SUBMODULES=ON），把所有子模块测试二进制编进 build/
  3) ctest -N 列出实际注册的测试，按子模块正则筛选出"子模块测试"子集
  4) ctest -R '<合并正则>' 一次性运行全部子模块测试

用法:
  python scripts/run_all_submodules_test.py              # 构建 + 收集 + 运行全部子模块测试
  python scripts/run_all_submodules_test.py --no-build   # 复用现有 build/，仅收集 + 运行
  python scripts/run_all_submodules_test.py -c           # 清空 build/ 后全新构建再运行
  python scripts/run_all_submodules_test.py -j <N>       # 并行编译线程数（默认 CPU 核数）
  python scripts/run_all_submodules_test.py --config <C> # 构建配置（默认 Debug；VS 多配置传 -C）
  python scripts/run_all_submodules_test.py -S image_filtering                       # 只跑某个子模块
  python scripts/run_all_submodules_test.py -S image_reader -S mediapipe_vision      # 跑多个子模块
  python scripts/run_all_submodules_test.py --download-models                        # 先下载 mediapipe 模型再跑
  python scripts/run_all_submodules_test.py --disable-opencv   # 关闭 OpenCV（多数子模块测试会消失）
  python scripts/run_all_submodules_test.py -v            # ctest 详细输出
  python scripts/run_all_submodules_test.py -b <dir>      # 指定构建目录（默认 build）
  python scripts/run_all_submodules_test.py --cmake <path># 指定 cmake 可执行文件
  python scripts/run_all_submodules_test.py --opencv-dir <dir>  # OpenCV 前缀（默认自动探测）

退出码：0 全部通过（或因门控没有可运行测试）；非 0 表示构建出错或存在测试失败。
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root, runner, toolchain  # noqa: E402
from gs.cmake import CMake  # noqa: E402

# subnode.json 的子模块 name → 该子模块测试名的正则（同时用于 ctest -R 与按子模块分组）。
# 仅使用 CMake 正则方言支持的原子（字面量 + .*），保证 ctest -R 与 Python re 都能解析；
# ^...$ 锚定在 main() 里统一加上。新增子模块必须在此登记一条 name→正则，否则其测试不会被
# 收集（main() 里 active 只取 declared ∩ SUBMODULE_TESTS）；沿用 test_<name>..._graph 命名
# 时正则就是字面量，命名特殊（如 gpu_image_processing → test_gpu_image_graph）需写实际测试名。
SUBMODULE_TESTS = {
    "image_reader": r"test_image_reader.*_graph",
    "image_writer": r"test_image_writer_graph",
    "image_color": r"test_image_color_graph",
    "image_color_grading": r"test_image_color_grading_graph",
    "image_filtering": r"test_image_filtering_graph",
    "image_geometry": r"test_image_geometry_graph",
    "gpu_image_processing": r"test_gpu_image_graph",
    "js_task": r"test_js_script_graph",
    "mediapipe_vision": r"test_mediapipe_.*",
}


def load_submodules(root: Path):
    cfg = root / "subnode.json"
    if not cfg.is_file():
        console.fail(f"找不到 {cfg}（请在仓库根目录运行本脚本）")
        return None
    try:
        data = json.loads(cfg.read_text(encoding="utf-8"))
    except Exception as e:
        console.fail(f"解析 subnode.json 失败: {e}")
        return None
    out = []
    for entry in data.get("submodules", []):
        if entry.get("type") != "local":
            continue
        url = entry.get("url", "")
        out.append({
            "name": entry.get("name", ""),
            "path": (root / url).resolve() if url else None,
        })
    return out


def list_ctest_tests(ctest: Path, build_dir: Path, config: str, multi_config: bool):
    args = [str(ctest), "-N"]
    if multi_config and config:
        args += ["-C", config]
    try:
        proc = subprocess.run(args, cwd=str(build_dir), capture_output=True, text=True)
    except OSError as e:
        console.fail(f"无法启动 ctest: {e}")
        return []
    if proc.returncode != 0:
        console.warn(f"ctest -N 返回非零 ({proc.returncode})，可能读不到测试列表")
    return re.findall(r"Test\s+#\d+:\s*(\S+)", proc.stdout)


def download_models(root: Path) -> None:
    script = root / "scripts" / "download_mediapipe_models.py"
    if not script.is_file():
        console.warn(f"未找到 {script.name}，跳过模型下载")
        return
    console.step("下载 MediaPipe 模型")
    if runner.check([sys.executable, str(script)], what="下载 MediaPipe 模型") != 0:
        console.warn("模型下载失败（mediapipe 测试将软跳过）")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="自动收集并运行 task_graph 全部子模块测试")
    ap.add_argument("-b", "--build-dir", default="build", help="构建目录（默认 build）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数（默认 CPU 核数）")
    ap.add_argument("-c", "--clean", action="store_true", help="先清空构建目录再全新构建")
    ap.add_argument("--no-build", action="store_true", help="跳过配置/构建，直接收集 + 运行")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    ap.add_argument("--enable-opencv", dest="opencv", action="store_true", default=True,
                    help="打开 TASK_GRAPH_ENABLE_OPENCV（默认开）")
    ap.add_argument("--disable-opencv", dest="opencv", action="store_false",
                    help="关闭 TASK_GRAPH_ENABLE_OPENCV（多数子模块测试会消失）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 安装前缀（默认自动探测）")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("-S", "--submodule", action="append", default=[], metavar="NAME",
                    help="只运行指定子模块的测试（subnode.json 中的 name，可重复）")
    ap.add_argument("--download-models", action="store_true",
                    help="运行前先执行 scripts/download_mediapipe_models.py 下载 MediaPipe 模型")
    ap.add_argument("-v", "--verbose", action="store_true", help="ctest 详细输出")
    args = ap.parse_args()

    root = repo_root()
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = root / build_dir
    jobs = args.jobs or platform.cpu_count()

    # ---- 子模块清单 ----
    submodules = load_submodules(root)
    if submodules is None:
        return 1
    declared = {s["name"]: s for s in submodules}
    missing_dirs = [s["name"] for s in submodules if not (s["path"] and s["path"].is_dir())]
    if missing_dirs:
        console.warn(f"以下子模块目录缺失: {', '.join(missing_dirs)}")
        console.warn("若刚 clone，请在仓库根运行: git submodule update --init --recursive")

    uncovered = [n for n in declared if n not in SUBMODULE_TESTS]
    if uncovered:
        console.warn(f"subnode.json 中有子模块未在脚本正则表中登记: {', '.join(uncovered)}")

    unknown = [n for n in args.submodule if n not in declared]
    if unknown:
        console.fail(f"未知子模块: {', '.join(unknown)}（可选: {', '.join(sorted(declared))}）")
        return 1
    active = args.submodule or [n for n in declared if n in SUBMODULE_TESTS]

    # ---- 工具链 ----
    cmake_exe = toolchain.find_cmake(args.cmake, build_dir=build_dir)
    if not cmake_exe or not cmake_exe.is_file():
        console.fail("cmake not found. Install CMake or pass --cmake <path>.")
        return 1
    ctest_exe = toolchain.find_ctest(cmake_exe) or Path("ctest")
    console.step(f"cmake: {cmake_exe}")
    cm = CMake(cmake_exe)

    # ---- 模型下载（可选）----
    if args.download_models:
        download_models(root)

    # ---- 清理 ----
    if args.clean and build_dir.exists():
        console.step(f"清理构建目录 {build_dir}")
        shutil.rmtree(build_dir, ignore_errors=True)

    # ---- 构建（默认；--no-build 跳过）----
    if not args.no_build:
        defines = [
            f"-DTASK_GRAPH_ENABLE_OPENCV={'ON' if args.opencv else 'OFF'}",
            "-DTASK_GRAPH_BUILD_SUBMODULES=ON",
        ]
        opencv_dir = deps.find_opencv(args.opencv_dir) if args.opencv else None
        if args.opencv and opencv_dir and (opencv_dir / "lib").is_dir():
            defines.append(f"-DOpenCV_DIR={opencv_dir / 'lib'}")
        if cm.configure(root, build_dir, defines=defines, build_type=args.config) != 0:
            return 1
        if cm.build(build_dir, config=args.config, jobs=jobs) != 0:
            return 1
    elif not (build_dir / "CTestTestfile.cmake").is_file():
        console.fail(f"构建目录 {build_dir} 未配置或不存在，且指定了 --no-build")
        return 1

    # ---- 收集：ctest -N ----
    registered = list_ctest_tests(ctest_exe, build_dir, args.config, cm.multi_config)
    console.step(f"ctest 注册测试总数: {len(registered)}")

    groups = {}
    for name in active:
        pat = SUBMODULE_TESTS.get(name)
        if pat is None:
            continue
        rx = re.compile(pat)
        groups[name] = [t for t in registered if rx.fullmatch(t)]

    total = sum(len(h) for h in groups.values())
    print()
    console.step(f"子模块测试收集结果（共 {total} 个）")
    for name in active:
        hits = groups.get(name, [])
        sub = declared.get(name)
        on_disk = bool(sub and sub["path"] and sub["path"].is_dir())
        if hits:
            print(f"    [{name}] {len(hits)} 个: {', '.join(hits)}")
        elif not on_disk:
            print(f"    [{name}] 0 个（目录缺失）")
        else:
            print(f"    [{name}] 0 个（平台/依赖门控未注册）")

    patterns = [SUBMODULE_TESTS[n] for n in active if n in SUBMODULE_TESTS]
    if not patterns:
        console.warn("没有可运行的子模块测试正则")
        return 0
    if total == 0:
        console.warn("未收集到任何子模块测试（可能被平台/依赖门控，或 OpenCV 已关闭）")
        return 0

    # ---- 运行时库搜索路径（Windows: build\<Config> 与 OpenCV bin；Unix 通常依赖 RPATH）----
    platform.prepend_env_path(platform.runtime_lib_env() or "", build_dir / args.config)
    if args.opencv:
        od = deps.find_opencv(args.opencv_dir) or deps.find_opencv()
        if od:
            platform.prepend_env_path(platform.runtime_lib_env() or "", od / "bin")

    # ---- 运行 ----
    combined = "^(" + "|".join(patterns) + ")$"
    status = cm.ctest(ctest_exe, build_dir, config=args.config,
                      filter=combined, verbose=args.verbose)
    print()
    if status == 0:
        console.ok(f"全部子模块测试通过（{total} 个）")
    else:
        console.fail(f"存在子模块测试失败 (exit {status})")
    return status


if __name__ == "__main__":
    sys.exit(main())
