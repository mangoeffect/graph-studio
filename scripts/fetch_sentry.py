#!/usr/bin/env python3
"""fetch_sentry.py — 拉取 sentry-native（含递归 git 子模块 crashpad）到
app/graph_studio/third_party/sentry-native，供 GraphStudio 崩溃上报使用（跨平台）。

取代 scripts/fetch_sentry.sh。sentry-native 的 crashpad backend 依赖 Chromium
crashpad，仓库内含多层 git 子模块，CMake 的 FetchContent 无法可靠递归拉取，
因此用固定版本脚本克隆（结果目录被 gitignore）。

用法:
  python scripts/fetch_sentry.py              # 拉取固定版本 0.16.2
  python scripts/fetch_sentry.py -f           # 删除已有目录后重新克隆

环境变量:
  SENTRY_NATIVE_REF      覆盖版本/tag（默认 0.16.2）
  SENTRY_NATIVE_VERSION  同上（向后兼容，优先级低于 SENTRY_NATIVE_REF）
"""

import argparse
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root, runner  # noqa: E402

DEST_REL = "app/graph_studio/third_party/sentry-native"
REPO = "https://github.com/getsentry/sentry-native.git"
DEFAULT_REF = "0.16.2"


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="拉取 sentry-native（含递归子模块）")
    ap.add_argument("-f", "--force", action="store_true", help="删除已有目录后重新克隆")
    args = ap.parse_args()

    ref = os.environ.get("SENTRY_NATIVE_REF") or os.environ.get("SENTRY_NATIVE_VERSION") or DEFAULT_REF
    dest = repo_root() / DEST_REL

    if dest.is_dir() and not args.force:
        print(f"sentry-native 已存在于 {dest}，跳过（用 -f 强制重拉）")
        return 0

    if dest.is_dir():
        shutil.rmtree(dest, ignore_errors=True)
    dest.parent.mkdir(parents=True, exist_ok=True)

    console.step(f"克隆 sentry-native@{ref}（递归子模块，首次较慢）")
    code = runner.run([
        "git", "clone", "--depth", "1", "--branch", ref, "--recursive", REPO, str(dest),
    ])
    if code != 0:
        console.fail(f"git clone 失败 (exit {code})")
        return code
    console.ok(f"sentry-native@{ref} 已就绪: {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())