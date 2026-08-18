#!/usr/bin/env python3
"""upload_sentry_symbols.py — 上传 GraphStudio 崩溃符号到 Sentry（跨平台）。

取代 scripts/upload_sentry_symbols.sh。用于后台还原 minidump 中的函数名/行号：
  - macOS:  上传 .dSYM（缺失时用 dsymutil 从带 -g 的可执行文件/库现生成）
  - Windows: 上传 .pdb
  - Linux:   上传 ELF 调试信息（补齐原 .sh 缺失的 Linux 分支）

覆盖产物：GraphStudio（app/graph_studio/build）+ task_graph 库 + subnode 插件
（根 build/），这样崩溃发生在共享库/插件里也能符号化。

前置:
  sentry-cli（macOS: brew install getsentry/tools/sentry-cli）
必需环境变量: SENTRY_AUTH_TOKEN
可选: SENTRY_ORG / SENTRY_PROJECT / GS_BUILD_DIR（默认 app/graph_studio/build）

用法:
  python scripts/upload_sentry_symbols.py
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, platform, repo_root, runner  # noqa: E402


def sentry_cli_args() -> List[str]:
    args = []
    if os.environ.get("SENTRY_ORG"):
        args += ["--org", os.environ["SENTRY_ORG"]]
    if os.environ.get("SENTRY_PROJECT"):
        args += ["--project", os.environ["SENTRY_PROJECT"]]
    return args


def collect_macos_candidates(app_build: Path, lib_build: Path) -> List[Path]:
    cands = []
    bin_app = app_build / "graph_studio.app" / "Contents" / "MacOS" / "graph_studio"
    if bin_app.is_file():
        cands.append(bin_app)
    cands.append(lib_build / "libtask_graph.dylib")
    submods = lib_build / "submodules"
    if submods.is_dir():
        for sub in sorted(submods.iterdir()):
            if sub.is_dir():
                for so in sorted(sub.glob("*.dylib")):
                    if so.is_file():
                        cands.append(so)
    # 去重保序
    seen = set()
    uniq = []
    for c in cands:
        if c not in seen and c.is_file():
            seen.add(c)
            uniq.append(c)
    return uniq


def upload_macos(app_build: Path, lib_build: Path) -> int:
    dsymutil = shutil.which("dsymutil")
    candidates = collect_macos_candidates(app_build, lib_build)
    if not candidates:
        console.fail("未找到任何可上传的 Mach-O 二进制")
        return 1

    dsyms: List[Path] = []
    for binary in candidates:
        sym = binary.parent / (binary.name + ".dSYM")
        if not sym.is_dir():
            if not dsymutil:
                console.fail("dsymutil 未找到（需要 Xcode command line tools）")
                return 1
            console.step(f"生成 {sym}")
            if subprocess.run([dsymutil, str(binary), "-o", str(sym)]).returncode != 0:
                console.fail(f"dsymutil 失败: {binary}")
                return 1
        if sym.is_dir():
            dsyms.append(sym)

    if not dsyms:
        console.fail("未找到可上传的符号（构建需带 -g / DEBUG_INFO）")
        return 1

    console.step(f"上传 {len(dsyms)} 个 dSYM 到 Sentry")
    for d in dsyms:
        print(f"    {d}")
    return runner.run(["sentry-cli", "debug-files", "upload", "-t", "dsym"]
                      + sentry_cli_args() + [str(d) for d in dsyms])


def upload_windows(app_build: Path, lib_build: Path) -> int:
    console.step("上传 Windows PDB 到 Sentry")
    code = runner.run(["sentry-cli", "debug-files", "upload", "-t", "pdb"]
                      + sentry_cli_args() + [str(app_build)])
    if code != 0:
        return code
    return runner.run(["sentry-cli", "debug-files", "upload", "-t", "pdb"]
                      + sentry_cli_args() + [str(lib_build)])


def upload_linux(app_build: Path, lib_build: Path) -> int:
    """Linux ELF 调试信息上传（补齐原 .sh 缺失的分支）。"""
    console.step("上传 Linux ELF 调试信息到 Sentry")
    code = runner.run(["sentry-cli", "debug-files", "upload", "-t", "elf"]
                      + sentry_cli_args() + [str(app_build)])
    if code != 0:
        return code
    return runner.run(["sentry-cli", "debug-files", "upload", "-t", "elf"]
                      + sentry_cli_args() + [str(lib_build)])


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="上传 GraphStudio 崩溃符号到 Sentry")
    ap.add_argument("--gs-build-dir", default="",
                    help="GraphStudio 构建目录（默认 app/graph_studio/build）")
    args = ap.parse_args()

    root = repo_root()
    app_build = Path(args.gs_build_dir) if args.gs_build_dir else (root / "app" / "graph_studio" / "build")
    if not app_build.is_absolute():
        app_build = Path.cwd() / app_build
    lib_build = root / "build"

    if not shutil.which("sentry-cli"):
        console.fail("sentry-cli 未安装（macOS: brew install getsentry/tools/sentry-cli）")
        return 1
    if not os.environ.get("SENTRY_AUTH_TOKEN"):
        console.fail("请设置 SENTRY_AUTH_TOKEN（sentry-cli 认证 token）")
        return 1
    if not app_build.is_dir():
        console.fail(f"构建目录不存在: {app_build}（先运行 scripts/run_graph_studio.py）")
        return 1

    if platform.is_macos():
        code = upload_macos(app_build, lib_build)
    elif platform.is_windows():
        code = upload_windows(app_build, lib_build)
    else:
        code = upload_linux(app_build, lib_build)
    if code != 0:
        console.fail(f"上传失败 (exit {code})")
        return code
    console.ok("符号上传完成")
    return 0


if __name__ == "__main__":
    sys.exit(main())