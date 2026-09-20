#!/usr/bin/env python3
"""package_web.py — 把 WASM 版 GraphStudio 打包为可部署的 Web zip。

输入是 scripts/run_graph_studio_wasm.py --build-only 的产物
（app/graph_studio/build_wasm/），打包为 GraphStudio-<version>-web.zip：
  - graph_studio.{html,js,wasm,worker.js} + qtloader.js（不带 .br，
    GitHub Pages 等静态托管不做 brotli 协商，预压缩文件是死重；
    qtlogo.svg 不随包——加载页品牌化为 favicon.svg + CSS spinner，见下）
  - graph_studio.html 注入 coi-serviceworker（见下）后复制为 index.html，
    使 /web/ 目录直接可访问
  - coi-serviceworker.js（vendored 于 app/graph_studio/packaging/web/）
  - 站点图标 favicon.svg/favicon-*.png/apple-touch-icon.png（拷自 docs/static/，
    与博客站一致，注入 <link rel=icon> —— Qt shell 本身不声明任何 favicon）

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
  python scripts/package_web.py --skip-models        # 不随包 face/matting 模型

退出码：0 成功（打印最终 zip 路径）；非 0 表示产物缺失/打包出错。
"""

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import models as gs_models  # noqa: E402

COI_SNIPPET = '<script src="coi-serviceworker.js"></script>'

# 站点图标：与博客站（docs/static/，PaperMod head.html 同一引用集）保持一致，
# Qt 生成的 shell 没有任何 favicon 声明，不注入则 /web/ 标签页是默认空白图标。
FAVICON_FILES = ["favicon.svg", "favicon-16x16.png", "favicon-32x32.png",
                 "apple-touch-icon.png"]
FAVICON_LINKS = (
    '<link rel="icon" href="favicon.svg" type="image/svg+xml">\n'
    '    <link rel="icon" type="image/png" sizes="16x16" href="favicon-16x16.png">\n'
    '    <link rel="icon" type="image/png" sizes="32x32" href="favicon-32x32.png">\n'
    '    <link rel="apple-touch-icon" href="apple-touch-icon.png">'
)

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

# 加载页品牌化：替换 Qt 6.6.3 shell 的 qtlogo.svg 加载页为站点品牌
#（favicon.svg + CSS spinner + "GraphStudio"）。只动 shell 文本，qtlogo.svg
# 随之从 zip 剔除（无引用）。#qtspinner/#qtstatus 结构保留——onExit 会把
# 退出文案写进 #qtstatus 并 showUi(spinner)。
BRAND_STYLE = """<style>
      .gs-brand img { width: 96px; height: 96px; display: block; margin: 0 auto; }
      .gs-spinner { width: 32px; height: 32px; margin: 1em auto;
                    border: 3px solid rgba(59, 130, 246, .25);
                    border-top-color: #3b82f6; border-radius: 50%;
                    animation: gs-spin 1s linear infinite; }
      @keyframes gs-spin { to { transform: rotate(360deg); } }
    </style>"""

BRAND_SPINNER = """<div class="gs-brand"><img src="favicon.svg" alt="GraphStudio"></div>
        <div class="gs-spinner" role="status" aria-label="Loading"></div>"""

QT_LOGO_IMG = '<img src="qtlogo.svg" width="320" height="200" style="display:block"></img>'
QT_LOGO_TITLE = "<strong>Qt for WebAssembly: graph_studio</strong>"
BRAND_TITLE = "<strong>GraphStudio</strong>"


def patch_branding(html: str) -> str:
    """把 Qt 加载页换成站点品牌（幂等；Qt shell 模式不匹配则原样保留）。

    Qt 6.6.3 生成的加载页是 qtlogo.svg + "Qt for WebAssembly: graph_studio"，
    与站点视觉无关。替换为 favicon.svg + CSS spinner；样式随 favicon 注入的
    <head> 块一起进（同一 patch 路径，幂等由 'gs-brand' 标记保证）。
    """
    if "gs-brand" not in html:
        marker = "</style>"
        idx = html.find(marker)
        if idx >= 0:
            pos = idx + len(marker)
            html = html[:pos] + "\n    " + BRAND_STYLE.strip() + html[pos:]
    html = html.replace(QT_LOGO_IMG, BRAND_SPINNER)
    html = html.replace(QT_LOGO_TITLE, BRAND_TITLE)
    return html


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
        html = html[:pos] + "\n  " + FAVICON_LINKS + "\n  " + COI_SNIPPET + "\n  " + BOOT_GUARD + html[pos:]
    if 'onload="init()"' in html:
        html = html.replace('<body onload="init()">', '<body onload="__gsBoot()">')
    return html


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="打包 WASM 版 GraphStudio 为 Web zip")
    ap.add_argument("--version", default="0.1.0", help="版本号（用于 zip 文件名）")
    ap.add_argument("--src-dir", default="", help="wasm 构建产物目录（默认 app/graph_studio/build_wasm）")
    ap.add_argument("--out-dir", default="dist/web", help="输出目录（默认 dist/web）")
    ap.add_argument("--skip-models", action="store_true",
                    help="跳过 face/matting 模型随包（参数留空的任务将报模型未找到）")
    args = ap.parse_args()

    root = repo_root()
    src = Path(args.src_dir) if args.src_dir else root / "app" / "graph_studio" / "build_wasm"
    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    coi = root / "app" / "graph_studio" / "packaging" / "web" / "coi-serviceworker.js"
    favicons = [root / "docs" / "static" / n for n in FAVICON_FILES]
    missing_fav = [str(p.name) for p in favicons if not p.is_file()]
    if missing_fav:
        console.fail(f"站点图标缺失: {missing_fav}（docs/static/）")
        return 1

    assets = ["graph_studio.html", "graph_studio.js", "graph_studio.wasm",
              "graph_studio.worker.js", "qtloader.js"]
    missing = [a for a in assets if not (src / a).is_file()]
    if missing:
        console.fail(f"构建产物缺失: {missing}（先运行 scripts/run_graph_studio_wasm.py --build-only）")
        return 1
    # qtlogo.svg 仍要求存在于构建目录（Qt 生成物的完整性标记），但不随包
    # ——品牌化后 shell 已无引用（详见 patch_branding）。
    if not (src / "qtlogo.svg").is_file():
        console.fail("构建产物缺失: ['qtlogo.svg']（先运行 scripts/run_graph_studio_wasm.py --build-only）")
        return 1
    if not coi.is_file():
        console.fail(f"找不到 coi-serviceworker.js: {coi}")
        return 1

    # 模型随包（wasm 侧 mediapipe 是 stub，只随包 face/matting 的 .mnn；
    # 启动期按 manifest.json fetch 进 MEMFS /models，ModelFinder 按名命中）
    if args.skip_models:
        console.step("跳过模型随包（--skip-models）")
        models_dir = None
    else:
        models_dir = src / "models"
        if not gs_models.stage_web_models(models_dir):
            return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"GraphStudio-{args.version}-web.zip"

    console.step(f"打包 {zip_path.name}")
    html = patch_branding(patch_coi((src / "graph_studio.html").read_text(encoding="utf-8")))
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in assets:
            data = html.encode("utf-8") if name == "graph_studio.html" else (src / name).read_bytes()
            zf.writestr(name, data)
        # index.html = 注入 coi 后的 shell，/web/ 目录直接可访问
        zf.writestr("index.html", html.encode("utf-8"))
        zf.writestr("coi-serviceworker.js", coi.read_bytes())
        # 站点图标（与博客一致；相对路径，zip 自包含部署到任意静态托管也生效）
        for p in favicons:
            zf.write(p, p.name)
        # models/ + manifest.json（启动期 fetch 进 MEMFS /models）
        if models_dir is not None:
            for p in sorted(models_dir.iterdir()):
                if p.is_file():
                    zf.write(p, f"models/{p.name}")

    print(f"    {zip_path} ({human_size(zip_path.stat().st_size)})")
    for name in ["graph_studio.wasm", "graph_studio.js", "graph_studio.html"]:
        f = src / name
        print(f"    {name}: {human_size(f.stat().st_size)}")
    console.ok(f"Web 包完成: {zip_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
