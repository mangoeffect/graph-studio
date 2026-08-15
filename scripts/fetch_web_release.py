#!/usr/bin/env python3
"""拉取最新的 GraphStudio WASM Web 包（Release 资产）并解压进官网产物，供在线体验。

在 hugo 构建之后运行（website.yml：hugo -> 本脚本 -> upload-pages-artifact）：
    python scripts/fetch_web_release.py

行为：
- 查 Releases API，找最新带 GraphStudio-*-web.zip 资产（release.yml 的 wasm job
  上传，scripts/package_web.py 打包）的 release；
- 下载并解压到 docs/public/web/（hugo 产物根，避开 --minify 改写 Qt shell），
  部署后即 https://<site>/web/；
- 写 docs/data/web.json（present/tag/日期/大小），被 docs/layouts/web.html 消费；
- 无 Web 资产 / API 失败时写 {"present": false} 并退出 0 —— 官网落地页降级为
  空态，绝不阻塞站点构建（与 fetch_releases.py 同一约定）；
- 可选 GITHUB_TOKEN 环境变量透传（CI 传 github.token 规避匿名限流）。

本地联调（不发 API 请求）：
    python scripts/fetch_web_release.py --from-zip dist/web/GraphStudio-x-web.zip \
        --tag v0.1.0-alpha.1 --date 2026-08-15T00:00:00Z
"""

import argparse
import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_REPO = "mangoeffect/graph-studio"
API_BASE = "https://api.github.com/repos"
WEB_ASSET_RE = re.compile(r"^GraphStudio-.*-web\.zip$")


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPO", DEFAULT_REPO),
                        help=f"GitHub 仓库 slug（默认 {DEFAULT_REPO}）")
    parser.add_argument("--per-page", type=int, default=30,
                        help="最多检查多少个 release（默认 30）")
    parser.add_argument("--data-out", type=Path,
                        default=repo_root / "docs" / "data" / "web.json",
                        help="数据 JSON 输出路径（默认 docs/data/web.json，gitignored）")
    parser.add_argument("--web-dir", type=Path,
                        default=repo_root / "docs" / "public" / "web",
                        help="Web 产物解压目录（默认 docs/public/web）")
    # ---- 两阶段模式：website.yml 在 hugo 前跑 --data-only（模板要读数据），
    # hugo 后跑 --unpack-only（产物进 public/web/，避开 minify；该路径只放
    # Qt shell，落地页在 /online/，互不覆盖）。
    parser.add_argument("--data-only", action="store_true",
                        help="只查询 API 写 web.json，不下载解压（hugo 构建前）")
    parser.add_argument("--unpack-only", action="store_true",
                        help="只下载解压到 web-dir，不写 web.json（hugo 构建后）")
    # ---- 本地联调：绕过 API，直接用本地 zip 模拟 present 态 ----
    parser.add_argument("--from-zip", type=Path, default=None,
                        help="（测试）用本地 zip 模拟已发布的 Web 包")
    parser.add_argument("--tag", default="v0-test", help="（配合 --from-zip）模拟的 tag")
    parser.add_argument("--date", default="", help="（配合 --from-zip）模拟的发布时间")
    return parser.parse_args()


def api_get(url: str, token: str):
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "graph-studio-website-fetch",
        **({"Authorization": f"Bearer {token}"} if token else {}),
    })
    with urllib.request.urlopen(request, timeout=60) as resp:
        return json.load(resp)


def api_download(url: str, token: str, dest: Path) -> None:
    request = urllib.request.Request(url, headers={
        "User-Agent": "graph-studio-website-fetch",
        **({"Authorization": f"Bearer {token}"} if token else {}),
    })
    with urllib.request.urlopen(request, timeout=300) as resp, dest.open("wb") as fh:
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            fh.write(chunk)


def find_web_asset(releases: list):
    """返回 (release, asset) 或 None；releases 按新到旧排序。"""
    for release in releases:
        if release.get("draft"):
            continue
        for asset in release.get("assets", []):
            if WEB_ASSET_RE.match(asset.get("name") or ""):
                return release, asset
    return None


def unpack(zip_path: Path, web_dir: Path) -> None:
    web_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(web_dir)


def write_data(path: Path, payload: dict) -> None:
    body = {
        "repo": payload.get("repo", DEFAULT_REPO),
        "fetched_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        **payload,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def empty_payload(repo: str, error: str = "") -> dict:
    return {"repo": repo, "present": False, **({"fetch_error": error} if error else {})}


def main() -> int:
    args = parse_args()
    token = os.environ.get("GITHUB_TOKEN", "")

    if args.from_zip:
        unpack(args.from_zip, args.web_dir)
        write_data(args.data_out, {
            "present": True,
            "tag": args.tag,
            "published_at": args.date or datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "size": args.from_zip.stat().st_size,
            "source": "local",
        })
        print(f"fetch_web_release: 本地模拟 {args.from_zip.name} -> {args.web_dir}")
        return 0

    try:
        releases = api_get(f"{API_BASE}/{args.repo}/releases?per_page={args.per_page}", token)
        found = find_web_asset(releases)
        if not found:
            write_data(args.data_out, empty_payload(args.repo))
            print(f"fetch_web_release: 尚无 Web 资产，落地页降级为空态")
            return 0
        release, asset = found
        if not args.unpack_only:
            write_data(args.data_out, {
                "present": True,
                "tag": release.get("tag_name") or "",
                "name": release.get("name") or "",
                "prerelease": bool(release.get("prerelease")),
                "published_at": release.get("published_at") or "",
                "size": int(asset.get("size") or 0),
                "zip_url": asset.get("browser_download_url") or "",
            })
        if not args.data_only:
            with tempfile.TemporaryDirectory() as tmp:
                zip_path = Path(tmp) / asset["name"]
                api_download(asset["browser_download_url"], token, zip_path)
                unpack(zip_path, args.web_dir)
        print(f"fetch_web_release: {asset['name']} ({release.get('tag_name')}) "
              f"{'data+unpack' if not (args.data_only or args.unpack_only) else ('data-only' if args.data_only else 'unpack-only')}")
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError,
            zipfile.BadZipFile, OSError) as exc:
        # 官网空态兜底：不阻塞部署
        if not args.unpack_only:
            write_data(args.data_out, empty_payload(args.repo, error=str(exc)))
        print(f"fetch_web_release: 警告：拉取/解压 Web 包失败（{exc}），已写入空态数据",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
