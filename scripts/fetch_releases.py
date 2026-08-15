#!/usr/bin/env python3
"""拉取 GitHub Releases 数据，供官网（docs/，Hugo）的下载页与更新日志页渲染。

在站点构建前运行（本地 `hugo server` 前或 CI 的 website.yml 中）：
    python scripts/fetch_releases.py

产物 docs/data/releases.json 被以下模板消费：
    docs/layouts/page/downloads.html
    docs/layouts/page/changelog.html

约定：
- 文件被 .gitignore 忽略（构建期生成物），不在仓库中提交；
- API 失败 / 仓库无 release 时写空列表并打印警告、退出码仍为 0 ——
  官网降级为空态文案，绝不阻塞站点构建；
- 可选 GITHUB_TOKEN 环境变量透传（CI 传 github.token 规避匿名限流）。
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
    return {
        "tag_name": tag,
        "name": release.get("name") or "",
        "channel": match.group(1) if match else "",
        "prerelease": bool(release.get("prerelease")),
        "published_at": release.get("published_at") or "",
        "html_url": release.get("html_url") or f"https://github.com/{repo}/releases",
        "body_md": release.get("body") or "",
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
