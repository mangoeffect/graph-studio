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
import re
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from gs import models as gs_models  # noqa: E402

COI_SNIPPET = '<script src="coi-serviceworker.js"></script>'

# coi 的 doReload 覆盖：必须在 coi 脚本之前注入（coi 经 `...window.coi`
# 合并配置）。coi 的 notcontrolling/updatefound reload 可能在 wasm 预取
# 完成前抢跑（首访快网实测 71ms 即 reload，34MB 预取中止作废）——统一让
# 所有 reload 路径（coi 的 doReload + 启动守卫的 reload）等预取落定，
# reload 后的 fetch 命中 HTTP 缓存。__gsPrefetchDone 自身有 20s 停滞 /
# 180s 总限，promise 必定落定——这里不再另设较短的超时（30s 会误杀
# 慢链路上正常的 55-90s 预取）。
# 注意 coepdegrade 是 head 期同步调用，此时 PREFETCH 尚未执行，
# __gsPrefetchDone 还是 undefined——立即 reload 是正确的（该分支的
# 目的是尽快切到 require-corp，不该先下载 wasm）。
COI_RELOAD_WAIT = """<script>
(function () {
    window.coi = window.coi || {};
    window.coi.doReload = function () {
        var fired = false;
        var go = function () {
            if (fired) return;
            fired = true;
            location.reload();
        };
        var pre = window.__gsPrefetchDone;
        if (pre && pre.then) pre.then(go, go);
        else go();
    };
})();
</script>"""

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

# 启动守卫：所有分支都必须有上限与用户可见的失败态。
#  - 只有 crossOriginIsolated === true 才允许 init()——多线程 wasm 没有
#    SharedArrayBuffer 必然失败，旧实现"重试后仍未隔离也 init()"只会把
#    错误藏进 console，页面永远停在 spinner；
#  - serviceWorker.ready 按规范不会 reject 且可能永不落定（存储被禁、
#    第三方 iframe、扩展拦截、旧浏览器无 crossOriginIsolated API）——
#    10s 超时后按"未就绪"处理，最多再 reload 一次；
#  - 任何失败都进 gsBootError()：写 .gs-status/#qtstatus，并给"重试 /
#    清除缓存并重试"两个入口（后者可救回被旧 wasm/glue 污染的浏览器）。
BOOT_GUARD = """<script>
(function () {
    var VERSION = '__GS_VERSION__';
    window.__gsShell = { version: VERSION, state: 'boot', isolated: false };

    function shellState(state) {
        if (window.__gsShell) window.__gsShell.state = state;
    }

    window.gsBootError = function (message) {
        shellState('error');
        var status = document.querySelector('.gs-status');
        var qt = document.querySelector('#qtstatus');
        var host = document.querySelector('#qtspinner center') || document.body;
        if (status) {
            status.style.color = '#dc2626';
            status.textContent = 'Unable to start GraphStudio';
        }
        if (qt) qt.textContent = '';
        if (document.getElementById('gs-error-detail')) return;
        var box = document.createElement('div');
        box.id = 'gs-error-detail';
        box.style.cssText = 'margin-top:12px;font:13px/1.5 system-ui,' +
            '-apple-system,sans-serif;color:#475569;max-width:560px';
        var detail = document.createElement('div');
        detail.textContent = message;
        detail.style.cssText = 'margin-bottom:10px';
        var retry = document.createElement('a');
        retry.href = '#';
        retry.textContent = 'Retry';
        retry.style.cssText = 'margin-right:16px;color:#2563eb;text-decoration:underline';
        retry.onclick = function (e) { e.preventDefault(); location.reload(); };
        var clear = document.createElement('a');
        clear.href = '#';
        clear.textContent = 'Clear cache & retry';
        clear.style.cssText = 'color:#2563eb;text-decoration:underline';
        clear.onclick = function (e) {
            e.preventDefault();
            clear.textContent = 'Clearing...';
            var jobs = [];
            try {
                if (window.caches && caches.keys) {
                    jobs.push(caches.keys().then(function (keys) {
                        return Promise.all(keys.filter(function (k) {
                            return k.indexOf('gs-web-') === 0 || k.indexOf('gs-models-') === 0;
                        }).map(function (k) { return caches.delete(k); }));
                    }));
                }
            } catch (err) {}
            try {
                if (navigator.serviceWorker && navigator.serviceWorker.getRegistrations) {
                    jobs.push(navigator.serviceWorker.getRegistrations().then(function (regs) {
                        return Promise.all(regs.map(function (r) { return r.unregister(); }));
                    }));
                }
            } catch (err) {}
            var go = function () { location.reload(); };
            if (jobs.length) Promise.all(jobs).then(go, go); else go();
        };
        box.appendChild(detail);
        box.appendChild(retry);
        box.appendChild(clear);
        host.appendChild(box);
        if (window.console) console.error('[gs] boot error:', message);
    };

    function initIsolated() {
        try { sessionStorage.removeItem('gs-coi-retried'); } catch (e) {}
        if (window.__gsShell) window.__gsShell.isolated = true;
        shellState('init');
        // 60s 仍未 onLoaded：至少告诉用户不是"无响应"，并给出 reload 路径。
        setTimeout(function () {
            if (!window.__gsLoaded && window.__gsShell &&
                window.__gsShell.state !== 'error') {
                var el = document.querySelector('.gs-status');
                if (el) el.textContent =
                    'Still loading... if this persists, reload the page.';
            }
        }, 60000);
        init();
    }

    window.__gsBoot = function () {
        if (window.crossOriginIsolated === true) { initIsolated(); return; }
        if (typeof window.crossOriginIsolated === 'undefined') {
            gsBootError('This browser does not support cross-origin isolation ' +
                '(SharedArrayBuffer). Please use desktop Chrome/Edge 92+ or Safari 15.2+.');
            return;
        }
        if (!window.isSecureContext) {
            gsBootError('This page must be served over HTTPS; the multithreaded ' +
                'WebAssembly build cannot start on plain HTTP.');
            return;
        }
        if (!('serviceWorker' in navigator) || !navigator.serviceWorker) {
            gsBootError('Service Worker is unavailable in this browser, so the ' +
                'multithreaded build cannot start. Please use desktop Chrome or Edge over HTTPS.');
            return;
        }
        var retried = false;
        try { retried = sessionStorage.getItem('gs-coi-retried') === '1'; } catch (e) {}
        var ready;
        try { ready = navigator.serviceWorker.ready; }
        catch (e) { ready = Promise.reject(e); }
        var timer = new Promise(function (resolve) {
            setTimeout(function () { resolve('timeout'); }, 10000);
        });
        Promise.race([
            ready.then(function () { return 'ready'; }, function () { return 'error'; }),
            timer
        ]).then(function (state) {
            // crossOriginIsolated 是文档级布尔值，同文档内不会变；这里只
            // 作防御性检查，真正的隔离来自下一次导航（SW 改写响应头）。
            if (window.crossOriginIsolated === true) { initIsolated(); return; }
            if (!retried) {
                var go = function () {
                    try { sessionStorage.setItem('gs-coi-retried', '1'); } catch (e) {}
                    location.reload();
                };
                var pre = window.__gsPrefetchDone;
                if (pre && pre.then) pre.then(go, go);
                else go();
                return;
            }
            gsBootError(state === 'timeout'
                ? 'The service worker did not activate within 10 seconds. Site data ' +
                  'or cookies may be blocked for this site. Allow them and retry, ' +
                  'or use "Clear cache & retry".'
                : 'The service worker could not enable cross-origin isolation. ' +
                  'Try "Clear cache & retry", or allow site data/cookies for this site.');
        });
    };
})();
</script>"""

# 首访（未隔离）导航即预取 wasm，字节进度写 .gs-status：
#  - 下载与 coi SW 注册/激活/reload 往返全程重叠，进度从 t=0 可见；
#  - 资产 URL 带 ?v=<version>（打包期烧入），HTTP 缓存与 SW Cache Storage
#    都按构建版本隔离，旧 wasm/glue 不可能被新版本命中；
#  - 进度分子是解压字节、分母是 gzip content-length，带 Content-Encoding
#    时只显示已下载 MB（否则会出现 "31.1 MB / 10.1 MB" 的假卡死观感）；
#  - 预取必须有边界：20s 无新数据判停滞、180s 总上限，abort 后照常
#    resolve __gsPrefetchDone——后续导航自己再拉，绝不让 reload 无限期等待；
#  - 已隔离的导航（二次导航/回访）只剩编译+启动，不重复预取。
# body 用 reader 循环丢弃（不驻留 32MB），响应完整过线即进 HTTP 缓存。
PREFETCH_SNIPPET = """<script>
(function () {
    var VERSION = '__GS_VERSION__';
    function setStatus(text) {
        var el = document.querySelector('.gs-status');
        if (el) el.textContent = text;
    }
    function fmtMB(n) { return (n / 1048576).toFixed(1) + ' MB'; }
    function shellState(state) {
        if (window.__gsShell) window.__gsShell.state = state;
    }
    window.__gsPrefetchDone = Promise.resolve();
    if (window.crossOriginIsolated) {
        // .gs-status 在 body 里，head 期 setStatus 是 no-op——等 DOM 就绪
        window.addEventListener('DOMContentLoaded', function () {
            setStatus('Starting\\u2026');
        });
        // 回访时主动触发 SW 更新检查（coi 在已隔离导航提前 return 不再
        // register，浏览器默认 24h 才查一次——SW 缓存时代会拖慢新版触达）。
        // 新 SW 经 skipWaiting+claim 接管后 controllerchange 重载进新版。
        if (navigator.serviceWorker && navigator.serviceWorker.controller) {
            navigator.serviceWorker.register('coi-serviceworker.js')
                .then(function (reg) {
                    reg.addEventListener('updatefound', function () {
                        navigator.serviceWorker.addEventListener(
                            'controllerchange', function () { location.reload(); });
                    });
                }).catch(function () {});
        }
        return;
    }
    window.__gsPrefetchDone = new Promise(function (resolve) {
        var settled = false;
        var idle = null;
        var hard = null;
        var abort = ('AbortController' in window) ? new AbortController() : null;
        var finish = function (state) {
            if (settled) return;
            settled = true;
            clearTimeout(idle);
            clearTimeout(hard);
            shellState('prefetch:' + state);
            resolve();
        };
        var armIdle = function () {
            clearTimeout(idle);
            idle = setTimeout(function () {
                if (abort) { try { abort.abort(); } catch (e) {} }
                finish('stalled');
            }, 20000);
        };
        hard = setTimeout(function () {
            if (abort) { try { abort.abort(); } catch (e) {} }
            finish('timeout');
        }, 180000);
        var opts = abort ? { signal: abort.signal } : {};
        // 先武装 idle：连响应头都等不到（连接挂起）也必须在 20s 内 abort，
        // 不能只靠 180s 总限。
        armIdle();
        fetch('graph_studio.wasm?v=' + VERSION, opts).then(function (resp) {
            if (!resp.ok || !resp.body || !resp.body.getReader) {
                return resp.arrayBuffer().then(function () { finish('buffered'); },
                                               function () { finish('error'); });
            }
            var total = 0;
            var len = parseInt(resp.headers.get('content-length') || '0', 10);
            var encoded = !!resp.headers.get('content-encoding');
            var reader = resp.body.getReader();
            armIdle();
            var pump = function () {
                return reader.read().then(function (chunk) {
                    if (chunk.done) { finish('done'); return; }
                    total += chunk.value.length;
                    armIdle();
                    setStatus(len > 0 && !encoded
                        ? 'Loading ' + fmtMB(total) + ' / ' + fmtMB(len)
                        : 'Loading ' + fmtMB(total));
                    return pump();
                });
            };
            pump().catch(function () { finish('error'); });
        }).catch(function () { finish('error'); });
    });
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
      .gs-status { text-align: center; margin-top: .8em; color: #64748b;
                   font: 13px/1.4 system-ui, -apple-system, sans-serif;
                   min-height: 1.4em; }
      @keyframes gs-spin { to { transform: rotate(360deg); } }
    </style>"""

BRAND_SPINNER = """<div class="gs-brand"><img src="favicon.svg" alt="GraphStudio"></div>
        <div class="gs-spinner" role="status" aria-label="Loading"></div>
        <div class="gs-status">Loading\u2026</div>"""

QT_LOGO_IMG = '<img src="qtlogo.svg" width="320" height="200" style="display:block"></img>'
QT_LOGO_TITLE = "<strong>Qt for WebAssembly: graph_studio</strong>"
BRAND_TITLE = "<strong>GraphStudio</strong>"


class PackageError(RuntimeError):
    """打包期约定被破坏（Qt/Emscripten 模板变化）——必须失败，不能静默降级。"""


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

    # 成功/失败钩子：onLoaded 标记启动完成（配合 60s watchdog），init() 的
    # catch 把 wasm 实例化失败送到 gsBootError —— 否则 Qt 只 console.error，
    # 页面无限 spinner（SW 缓存串版时正是这条路径）。
    old_loaded = "onLoaded: () => showUi(screen),"
    new_loaded = ('onLoaded: () => { window.__gsLoaded = true; '
                  'window.__gsShell.state = "loaded"; showUi(screen); },')
    if new_loaded not in html:
        if old_loaded not in html:
            raise PackageError("Qt shell 模板变化：找不到 onLoaded 钩子（无法标记启动完成）")
        html = html.replace(old_loaded, new_loaded)

    old_catch = ("            } catch (e) {\n"
                 "                console.error(e);\n"
                 "                console.error(e.stack);\n"
                 "            }")
    new_catch = ("            } catch (e) {\n"
                 "                console.error(e);\n"
                 "                console.error(e.stack);\n"
                 "                if (window.gsBootError) gsBootError(\n"
                 "                    'WebAssembly failed to start: ' +\n"
                 "                    (e && e.message ? e.message : e));\n"
                 "            }")
    if "WebAssembly failed to start: " not in html:
        if old_catch not in html:
            raise PackageError("Qt shell 模板变化：找不到 init() catch 块（无法上报启动失败）")
        html = html.replace(old_catch, new_catch)
    return html


def patch_version(html: str, version: str) -> str:
    """把构建版本烧进 shell：资产 URL 加 ?v=，并替换 __GS_VERSION__。

    固定文件名 + GitHub Pages max-age=600 + SW cache-first 会让旧 wasm/glue
    跨版本串用；URL 带版本后 HTTP 缓存与 Cache Storage 的键都按构建隔离。
    coi-serviceworker.js 自身不带 query（SW 更新检查依赖稳定 URL）。
    """
    if not re.fullmatch(r"[A-Za-z0-9._-]+", version or ""):
        raise PackageError(f"非法版本号（只允许字母数字 . _ -）: {version!r}")
    pairs = [
        ('<script src="graph_studio.js"></script>',
         f'<script src="graph_studio.js?v={version}"></script>'),
        ('<script type="text/javascript" src="qtloader.js"></script>',
         f'<script type="text/javascript" src="qtloader.js?v={version}"></script>'),
    ]
    for old, new in pairs:
        if new in html:
            continue  # 幂等：已烧过同一版本
        if html.count(old) != 1:
            raise PackageError(f"Qt shell 模板变化：资产引用 {old!r} 命中 "
                               f"{html.count(old)} 次（期望 1）")
        html = html.replace(old, new)
    if "__GS_VERSION__" not in html:
        raise PackageError("壳层模板缺少 __GS_VERSION__ 占位符")
    html = html.replace("__GS_VERSION__", version)
    if "__GS_VERSION__" in html:
        raise PackageError("__GS_VERSION__ 占位符未替换干净")
    return html


def version_glue(data: bytes, version: str) -> bytes:
    """给 Emscripten glue 里的 wasm/worker 文件名加 ?v=<version>。

    Emscripten 3.1.46 的固定形态（打包期断言，模板变化必须显式失败）：
      wasmBinaryFile="graph_studio.wasm"
      locateFile("graph_studio.worker.js")
    locateFile 只做 scriptDirectory 拼接，query 不影响目录解析。
    """
    q = version.encode("utf-8")
    pairs = [
        (b'wasmBinaryFile="graph_studio.wasm"',
         b'wasmBinaryFile="graph_studio.wasm?v=' + q + b'"'),
        (b'locateFile("graph_studio.worker.js")',
         b'locateFile("graph_studio.worker.js?v=' + q + b'")'),
    ]
    for old, new in pairs:
        if data.count(old) != 1:
            raise PackageError(f"Emscripten glue 模板变化：{old!r} 命中 "
                               f"{data.count(old)} 次（期望 1）")
        data = data.replace(old, new)
    return data


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
        html = html[:pos] + "\n  " + FAVICON_LINKS + "\n  " + COI_RELOAD_WAIT \
            + "\n  " + COI_SNIPPET + "\n  " + BOOT_GUARD + "\n  " \
            + PREFETCH_SNIPPET + html[pos:]
    if 'onload="init()"' in html:
        html = html.replace('<body onload="init()">', '<body onload="__gsBoot()">')
    if 'onload="__gsBoot()"' not in html:
        raise PackageError(
            "Qt shell 模板变化：找不到 <body onload=\"init()\">，启动守卫未接线")
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
    try:
        html = patch_version(
            patch_branding(patch_coi(
                (src / "graph_studio.html").read_text(encoding="utf-8"))),
            args.version)
        glue = version_glue((src / "graph_studio.js").read_bytes(), args.version)
    except PackageError as e:
        console.fail(str(e))
        return 1
    # SW 版本键烧入：仓库态源文件保持 "gs-web-dev"，zip 内替换为本次
    # 构建版本——SW 字节随版本变化触发浏览器更新流程（coi 的
    # updatefound reload），activate 清理旧 gs-web-* 缓存。
    coi_bytes = coi.read_bytes().replace(
        b'"gs-web-dev"', f'"gs-web-{args.version}"'.encode("utf-8"))
    if b'"gs-web-dev"' in coi_bytes:
        console.fail("coi-serviceworker.js 版本键替换失败（源文件缺少 \"gs-web-dev\"）")
        return 1
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in assets:
            if name == "graph_studio.html":
                data = html.encode("utf-8")
            elif name == "graph_studio.js":
                data = glue
            else:
                data = (src / name).read_bytes()
            zf.writestr(name, data)
        # index.html = 注入 coi 后的 shell，/web/ 目录直接可访问
        zf.writestr("index.html", html.encode("utf-8"))
        zf.writestr("coi-serviceworker.js", coi_bytes)
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
