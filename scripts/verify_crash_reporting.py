#!/usr/bin/env python3
"""verify_crash_reporting.py — 端到端验证 crashpad minidump 落盘（冒烟测试）。

不依赖真实 Sentry 项目：用一个指向不可达地址（127.0.0.1:9）的 dummy DSN
初始化 sentry，offscreen 运行 graph_studio --test-crash 触发 SIGSEGV，
断言 sentry 数据库目录中生成了 minidump（*.dmp）。上传必然失败，dump 留存
在本地 crashpad 数据库（<db>/reports/），足以证明「捕获 → 落盘」链路可用。

崩溃上报目录通过子进程环境变量重定向到临时目录（Windows 覆盖 LOCALAPPDATA，
Unix 覆盖 HOME），不会动到开发者本地真实的 sentry_db。

前置:
  1. sentry-native 已拉取（python scripts/fetch_sentry.py，首次较慢）；
     默认自动拉取，--no-fetch 跳过。
  2. graph_studio 可执行文件已构建（且构建时 sentry-native 已就位，否则
     二进制里根本没有崩溃上报代码）。默认自动构建，--no-build 复用现有产物。

用法:
  python scripts/verify_crash_reporting.py               # 拉取+构建+验证
  python scripts/verify_crash_reporting.py --no-build    # 复用现有产物
  python scripts/verify_crash_reporting.py --keep-output # 保留临时 db 目录便于排查

判定:
  0  = 验证通过（stderr 出现 "[CrashReporter] initialized" 且生成 .dmp）
  1  = 失败（附带原因与修复提示）
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, deps, platform, repo_root  # noqa: E402
from gs import sentry as gs_sentry  # noqa: E402

# 不可达地址：端口 9（discard）几乎必然拒绝连接，crashpad 上传失败后
# dump 保留在本地数据库。DSN 是格式合法的（sentry_init 只校验格式）。
DUMMY_DSN = "http://00000000000000000000000000000000@127.0.0.1:9/1"

DUMP_WAIT_SECONDS = 15


def locate_exe(gs_build: Path, config: str):
    """定位 graph_studio 可执行文件（与 run_graph_studio.py 的规则一致）。"""
    if platform.is_windows():
        cand = gs_build / config / "graph_studio.exe"
    elif platform.is_macos():
        cand = gs_build / "graph_studio.app" / "Contents" / "MacOS" / "graph_studio"
        if not cand.is_file():
            cand = gs_build / "graph_studio"
    else:
        cand = gs_build / "graph_studio"
    return cand if cand.is_file() else None


def build(args) -> int:
    """复用 run_graph_studio.py 的完整构建流程（root 库 + app）。"""
    cmd = [sys.executable, str(repo_root() / "scripts" / "run_graph_studio.py"),
           "--build-only", "--config", args.config]
    if args.jobs:
        cmd += ["-j", str(args.jobs)]
    if args.clean:
        cmd += ["-c"]
    if args.qt:
        cmd += ["--qt", args.qt]
    if args.opencv_dir:
        cmd += ["--opencv-dir", args.opencv_dir]
    if args.cmake:
        cmd += ["--cmake", args.cmake]
    if args.disable_opencv:
        cmd += ["--disable-opencv"]
    return subprocess.run(cmd).returncode


def find_dumps(db_dir: Path):
    return sorted(db_dir.rglob("*.dmp")) if db_dir.is_dir() else []


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="端到端验证 crashpad minidump 落盘")
    ap.add_argument("--no-build", action="store_true", help="复用现有 graph_studio 产物")
    ap.add_argument("--no-fetch", action="store_true", help="不自动拉取 sentry-native")
    ap.add_argument("-c", "--clean", action="store_true", help="清空构建目录后全新构建")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数")
    ap.add_argument("--config", "--build-type", default="Debug",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 Debug）")
    ap.add_argument("--qt", default="", help="Qt6 前缀（传给 run_graph_studio.py）")
    ap.add_argument("--opencv-dir", default="", help="OpenCV 前缀（传给 run_graph_studio.py）")
    ap.add_argument("--disable-opencv", action="store_true", help="关闭 OpenCV 依赖")
    ap.add_argument("--cmake", default="", help="cmake 可执行文件路径")
    ap.add_argument("--dsn", default=DUMMY_DSN, help="验证用 DSN（默认不可达 dummy DSN）")
    ap.add_argument("--keep-output", action="store_true", help="保留临时 sentry db 目录")
    args = ap.parse_args()

    if not (platform.is_windows() or platform.is_macos() or platform.is_linux()):
        console.fail("verify_crash_reporting.py 仅支持桌面平台")
        return 1

    root = repo_root()
    gs_build = root / "app" / "graph_studio" / "build"

    # 1) 确保 sentry-native 就位（必须在 configure 之前，否则构建不含崩溃上报）
    if gs_sentry.ensure_fetched(root, enable=not args.no_fetch) != 0:
        console.fail("拉取 sentry-native 失败，无法验证")
        return 1

    # 2) 构建（或复用产物）
    if not args.no_build:
        console.step("构建 graph_studio（含崩溃上报）")
        if build(args) != 0:
            console.fail("构建失败")
            return 1

    exe = locate_exe(gs_build, args.config)
    if not exe:
        console.fail(f"未找到 graph_studio 产物（config={args.config}），"
                     f"先运行 python scripts/run_graph_studio.py --build-only")
        return 1

    handler = exe.parent / ("crashpad_handler.exe" if platform.is_windows()
                            else "crashpad_handler")
    if not handler.is_file():
        console.warn(f"crashpad_handler 不在可执行文件旁: {handler}（崩溃将无 minidump）")

    # 3) 运行时环境：DLL/库搜索路径 + offscreen + dummy DSN + db 目录重定向。
    #    注意 prepend_env_path 修改的是本进程 os.environ，必须在 copy 出子进程
    #    环境之前完成，否则搜索路径不会传给 graph_studio。
    os.environ["SENTRY_DSN"] = args.dsn
    os.environ["SENTRY_DEBUG"] = "1"
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    lib_env = platform.runtime_lib_env()
    if lib_env:
        platform.prepend_env_path(lib_env, root / "build" / args.config)
        qt_prefix = deps.find_qt(args.qt or None)
        if qt_prefix:
            platform.prepend_env_path(lib_env, qt_prefix / "bin")
        opencv_dir = deps.find_opencv(args.opencv_dir or None)
        if opencv_dir and not args.disable_opencv:
            platform.prepend_env_path(lib_env, opencv_dir / "bin")

    tmp = Path(tempfile.mkdtemp(prefix="gs_crash_verify_"))
    env = os.environ.copy()
    if platform.is_windows():
        env["LOCALAPPDATA"] = str(tmp)
        db_dir = tmp / "GraphStudio" / "sentry_db"
    else:
        env["HOME"] = str(tmp)
        db_dir = tmp / ".graph_studio" / "sentry_db"

    # 4) 触发崩溃：期望非零退出码
    console.step(f"运行 {exe} --test-crash（offscreen，期望崩溃退出）")
    proc = subprocess.run([str(exe), "--test-crash"], env=env,
                          capture_output=True, text=True, timeout=120)
    output = (proc.stdout or "") + (proc.stderr or "")
    print(output.rstrip())

    initialized = "[CrashReporter] initialized" in output
    if not initialized:
        console.fail(f"未看到 '[CrashReporter] initialized' 输出"
                     f"（exit=0x{proc.returncode & 0xFFFFFFFF:X}；"
                     f"二进制可能不含崩溃上报代码或缺少运行时 DLL，重新构建/检查 PATH 试试）")
        return 1
    if proc.returncode == 0:
        console.fail(f"进程正常退出 (code 0)，期望崩溃退出码")
        return 1

    # 5) 断言 minidump 落盘（crashpad handler 写 dump 可能略滞后于进程退出）
    dumps = []
    deadline = time.time() + DUMP_WAIT_SECONDS
    while time.time() < deadline:
        dumps = find_dumps(db_dir)
        if dumps:
            break
        time.sleep(1.0)
    if not dumps:
        console.fail(f"{db_dir} 下未找到 *.dmp（crashpad handler 缺失或数据库未创建）")
        return 1

    for d in dumps:
        console.ok(f"minidump: {d} ({d.stat().st_size} bytes)")
    console.ok("崩溃收集链路验证通过（捕获 → minidump 落盘）")

    if not args.keep_output:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        console.step(f"保留 sentry db 目录: {db_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
