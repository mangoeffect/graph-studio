#!/usr/bin/env python3
"""GraphStudio 节点手册一致性校验。

预期任务清单 = subnode.json 各模块 tasks + 核心库内置 io_input/io_output
（gpu_compute 任务当前未注册，不在清单内）。与官网 docs/content/{zh,en}/blog/
下的页面目录双向比对：

  - app 有而文档缺        → missing（strict 模式失败）
  - 文档有（带 node-guide 标签）而 app 无 → orphan（strict 模式失败）
  - zh/en 单边存在        → unpaired（strict 模式失败）
  - 双边齐                → ok

用法（仓库根裸文件名运行）：
    python3 scripts/check_node_docs.py           # 报告模式（exit 0）
    python3 scripts/check_node_docs.py --strict  # 缺失即失败（exit 1）
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BLOG = {"zh": REPO / "docs" / "content" / "zh" / "blog",
        "en": REPO / "docs" / "content" / "en" / "blog"}
NODE_GUIDE_TAG = '"node-guide"'
CORE_TASKS = ("io_input", "io_output")  # 核心库直接注册的图边界任务


def expected_tasks():
    sub = json.loads((REPO / "subnode.json").read_text())
    tasks = set(CORE_TASKS)
    for m in sub["submodules"]:
        tasks.update(m.get("tasks", []))
    return tasks


def doc_dirs(lang):
    root = BLOG[lang]
    if not root.is_dir():
        return set()
    return {p.parent.name for p in root.glob("*/index.md")}


def is_node_guide_page(lang, slug):
    f = BLOG[lang] / slug / "index.md"
    return f.is_file() and NODE_GUIDE_TAG in f.read_text()


def main():
    strict = "--strict" in sys.argv
    expected = expected_tasks()
    zh, en = doc_dirs("zh"), doc_dirs("en")

    missing = {lang: sorted(expected - dirs) for lang, dirs in (("zh", zh), ("en", en))}
    orphans = {lang: sorted(d for d in (dirs - expected) if is_node_guide_page(lang, d))
               for lang, dirs in (("zh", zh), ("en", en))}
    unpaired = sorted((expected & zh) ^ (expected & en)) if True else []
    ok = sorted((expected & zh) & (expected & en))

    print(f"expected task types: {len(expected)}")
    print(f"paired pages (zh+en): {len(ok)}")
    problems = 0
    for lang in ("zh", "en"):
        if missing[lang]:
            problems += len(missing[lang])
            print(f"MISSING[{lang}]: {', '.join(missing[lang])}")
        if orphans[lang]:
            problems += len(orphans[lang])
            print(f"ORPHAN[{lang}] (node-guide page for unknown task): {', '.join(orphans[lang])}")
    if unpaired:
        problems += len(unpaired)
        print(f"UNPAIRED (only one language): {', '.join(unpaired)}")

    if problems:
        print(f"RESULT: {problems} problem(s)")
        if strict:
            return 1
    else:
        print("RESULT: all green")
    return 0


if __name__ == "__main__":
    sys.exit(main())
