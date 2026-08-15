#!/usr/bin/env python3
"""package_web.py — 把 WASM 版 GraphStudio 打包为可部署的 Web zip。

输入是 scripts/run_graph_studio_wasm.py --build-only 的产物
（app/graph_studio/build_wasm/），打包为 GraphStudio-<version>-web.zip：
  - graph_studio.{html,js,wasm,worker.js} + qtloader.js + qtlogo.svg（不带 .br，
    GitHub Pages 等静态托管不做 brotli 协商，预压缩文件是死重）
  - graph_studio.html 注入 coi-serviceworker（见下）后复制为 index.html，
    使 /web/ 目录直接可访问
  - coi-serviceworker.js（vendored 于 app/graph_studio/packaging/web/）

COOP/COEP：多线程 WASM（SharedArrayBuffer）要求页面 crossOriginIsolated，
即响应需带 Cross-Origin-Opener-Policy/Cross-Origin-Embedder-Policy 头。
GitHub Pages 等静态托管无法自定义响应头，coi-serviceworker 通过 service
worker 给（含导航在内的）响应补这两个头，首访自动刷新一次进入隔离上下文。
zip 因此是自包含的，下载者可在任意 HTTPS 静态托管上原样部署。

用法:
  python scripts/package_web.py                                # 默认 0.1.0
  python scripts/package_web.py --version 0.1.0
  python scripts/package_web.py --src-dir app/graph_studio/build_wasm
  python scripts/package_web.py --out-dir dist/web

退出码：0 成功（打印最终 zip 路径）；非 0 表示产物缺失/打包出错。
"""

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402

COI_SNIPPET = '<script src="coi-serviceworker.js"></script>'

BOOT_GUARD = """<script>
(function () {
    window.__gsBoot = function () {
        if (window.crossOriginIsolated || !('serviceWorker' in navigator)) { init(); return; }
        var retried = false;
        try { retried = sessionStorage.getItem('gs-coi-retried') === '1'; } catch (e) {}
        navigator.serviceWorker.ready.then(function () {
            if (window.crossOriginIsolated) {
                try { sessionStorage.removeItem('gs-coi-retried'); } catch (e) {}
                init();
                return;
            }
            if (!retried) {
                try { sessionStorage.setItem('gs-coi-retried', '1'); } catch (e) {}
                location.reload();
                return;
            }
            init();
        });
    };
})();
</script>"""


def human_size(n: int) -> str:
    for unit in ("B", "K", "M", "G"):
        if n < 1024:
            return f"{n}B" if unit == "B" else f"{n:.1f}{unit}"
        n //= 1024
    return f"{n}G"


def patch_coi(html: str) -> str:
    """注入 coi-serviceworker + 启动守卫，并把 onload 换成守卫入口（幂等）。

    首访竞态：coi 的自动 reload 可能早于 SW activation，Qt loader 会在未隔离
    （无 SharedArrayBuffer）的页面上启动而报错退出。守卫入口先等
    serviceWorker.ready，隔离就绪才 init()；仍未隔离则补一次 reload（该导航
    必经 SW、带 COOP/COEP），sessionStorage 标记防循环。
    """
    if "coi-serviceworker" not in html:
        marker = "<head>"
        idx = html.find(marker)
        pos = idx + len(marker) if idx >= 0 else 0
        html = html[:pos] + "\n  " + COI_SNIPPET + "\n  " + BOOT_GUARD + html[pos:]
    if 'onload="init()"' in html:
        html = html.replace('<body onload="init()">', '<body onload="__gsBoot()">')
    return html


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="打包 WASM 版 GraphStudio 为 Web zip")
    ap.add_argument("--version", default="0.1.0", help="版本号（用于 zip 文件名）")
    ap.add_argument("--src-dir", default="", help="wasm 构建产物目录（默认 app/graph_studio/build_wasm）")
    ap.add_argument("--out-dir", default="dist/web", help="输出目录（默认 dist/web）")
    args = ap.parse_args()

    root = repo_root()
    src = Path(args.src_dir) if args.src_dir else root / "app" / "graph_studio" / "build_wasm"
    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    coi = root / "app" / "graph_studio" / "packaging" / "web" / "coi-serviceworker.js"

    assets = ["graph_studio.html", "graph_studio.js", "graph_studio.wasm",
              "graph_studio.worker.js", "qtloader.js", "qtlogo.svg"]
    missing = [a for a in assets if not (src / a).is_file()]
    if missing:
        console.fail(f"构建产物缺失: {missing}（先运行 scripts/run_graph_studio_wasm.py --build-only）")
        return 1
    if not coi.is_file():
        console.fail(f"找不到 coi-serviceworker.js: {coi}")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"GraphStudio-{args.version}-web.zip"

    console.step(f"打包 {zip_path.name}")
    html = patch_coi((src / "graph_studio.html").read_text(encoding="utf-8"))
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in assets:
            data = html.encode("utf-8") if name == "graph_studio.html" else (src / name).read_bytes()
            zf.writestr(name, data)
        # index.html = 注入 coi 后的 shell，/web/ 目录直接可访问
        zf.writestr("index.html", html.encode("utf-8"))
        zf.writestr("coi-serviceworker.js", coi.read_bytes())

    print(f"    {zip_path} ({human_size(zip_path.stat().st_size)})")
    for name in ["graph_studio.wasm", "graph_studio.js", "graph_studio.html"]:
        f = src / name
        print(f"    {name}: {human_size(f.stat().st_size)}")
    console.ok(f"Web 包完成: {zip_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
