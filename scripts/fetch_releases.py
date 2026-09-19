#!/usr/bin/env python3
"""拉取 GitHub Releases 数据，供官网（docs/，Hugo）的下载页与更新日志页渲染。

在站点构建前运行（本地 `hugo server` 前或 CI 的 website.yml 中）：
    python scripts/fetch_releases.py

产物 docs/data/releases.json 被以下模板消费：
    docs/layouts/downloads.html
    docs/layouts/changelog.html

约定：
- 文件被 .gitignore 忽略（构建期生成物），不在仓库中提交；
- API 失败 / 仓库无 release 时写空列表并打印警告、退出码仍为 0 ——
  官网降级为空态文案，绝不阻塞站点构建；
- 可选 GITHUB_TOKEN 环境变量透传（CI 传 github.token 规避匿名限流）；
- 双语 release body：body 内以 <!-- zh --> / <!-- en --> 标记分段（GitHub 渲染
  时注释不可见），切分为 body_md_zh / body_md_en 两个字段供 changelog 页按语言
  取用；无标记的历史 body 两字段均为整份原文，模板端回退展示。
"""

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_REPO = "mangoeffect/graph-studio"
API_BASE = "https://api.github.com/repos"
CHANNEL_RE = re.compile(r"-(alpha|beta|hotfix|stable)\.\d+$")
# 双语分段标记（release.yml 的 body 模板产出；GitHub markdown 渲染不显示注释）
LANG_MARK_RE = re.compile(r"<!--\s*(zh|en)\s*-->", re.IGNORECASE)
# generate_release_notes 追加在 body 末尾的自动段（语言中立）：PR 列表 / Full Changelog 链接
AUTO_TAIL_RES = (
    re.compile(r"(?m)^## What.s Changed\s*$"),
    re.compile(r"(?m)^\*\*Full Changelog\*\*:"),
)


def split_release_body(body: str) -> tuple:
    """把双语 release body 切成 (zh, en) 两段。

    - <!-- zh --> / <!-- en --> 标记后的内容归属对应语言段，第一个标记前的
      散文字并入首个语言段（不静默丢弃）；
    - 自动段先摘出、重新附到每个非空语言段末尾——它固定追加在整个 body 之后，
      不摘会全部落进最后一个语言段（另一语言的页面丢内容）；
    - 无标记（历史 / 手写单语 body）：两段都返回整份原文（模板按语言取不到
      时回退 body_md，行为与切分前一致）；
    - 某语言段缺失时保持为空串，让模板回退，不用另一语言或自动段填充。
    """
    tail = ""
    for pattern in AUTO_TAIL_RES:
        match = pattern.search(body)
        if match:
            tail = body[match.start():].strip()
            body = body[:match.start()].rstrip()
            break

    sections = {"zh": "", "en": ""}
    marks = list(LANG_MARK_RE.finditer(body))
    if marks:
        head = body[:marks[0].start()].strip()
        if head:
            sections[marks[0].group(1).lower()] = head
        for i, mark in enumerate(marks):
            start = mark.end()
            end = marks[i + 1].start() if i + 1 < len(marks) else len(body)
            text = body[start:end].strip()
            key = mark.group(1).lower()
            sections[key] = f"{sections[key]}\n\n{text}" if sections[key] else text
    else:
        sections["zh"] = sections["en"] = body.strip()

    if tail:
        for key, text in sections.items():
            if text:
                sections[key] = f"{text}\n\n{tail}"
    return sections["zh"], sections["en"]


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPO", DEFAULT_REPO),
                        help=f"GitHub 仓库 slug（默认 {DEFAULT_REPO}）")
    parser.add_argument("--out", type=Path,
                        default=repo_root / "docs" / "data" / "releases.json",
                        help="输出 JSON 路径（默认 <repo>/docs/data/releases.json）")
    parser.add_argument("--per-page", type=int, default=30,
                        help="最多拉取多少个 release（默认 30）")
    return parser.parse_args()


def fetch_releases(repo: str, token: str, per_page: int):
    url = f"{API_BASE}/{repo}/releases?per_page={per_page}"
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "graph-studio-website-fetch",
        **({"Authorization": f"Bearer {token}"} if token else {}),
    })
    with urllib.request.urlopen(request, timeout=30) as resp:
        return json.load(resp)


def slim(release: dict, repo: str) -> dict:
    tag = release.get("tag_name") or ""
    match = CHANNEL_RE.search(tag)
    body = release.get("body") or ""
    body_zh, body_en = split_release_body(body)
    return {
        "tag_name": tag,
        "name": release.get("name") or "",
        "channel": match.group(1) if match else "",
        "prerelease": bool(release.get("prerelease")),
        "published_at": release.get("published_at") or "",
        "html_url": release.get("html_url") or f"https://github.com/{repo}/releases",
        "body_md": body,
        "body_md_zh": body_zh,
        "body_md_en": body_en,
        "assets": [
            {
                "name": asset.get("name") or "",
                "size": int(asset.get("size") or 0),
                "url": asset.get("browser_download_url") or "",
            }
            for asset in release.get("assets", [])
        ],
    }


def write_payload(path: Path, repo: str, releases=None, error: str = "") -> None:
    payload = {
        "repo": repo,
        "html_url": f"https://github.com/{repo}/releases",
        "fetched_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        **({"fetch_error": error} if error else {}),
        "releases": releases or [],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def main() -> int:
    args = parse_args()
    token = os.environ.get("GITHUB_TOKEN", "")
    try:
        raw = fetch_releases(args.repo, token, args.per_page)
        releases = [slim(item, args.repo) for item in raw if not item.get("draft")]
        write_payload(args.out, args.repo, releases)
        print(f"fetch_releases: {len(releases)} release(s) -> {args.out}")
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        # 官网空态兜底：不阻塞 hugo 构建
        write_payload(args.out, args.repo, error=str(exc))
        print(f"fetch_releases: 警告：GitHub API 拉取失败（{exc}），已写入空数据", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
