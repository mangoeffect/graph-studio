#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""wasm_load_profile.py — GraphStudio WASM 加载耗时分相剖析（测量基建）。

对本地构建树 / 发布 zip / 线上 URL 用真实有头 Chrome（CDP 驱动）做加载剖析：
- 冷（全新 profile：SW 注册 + coi 双载 + 空缓存）× 热（同浏览器再开 tab）
- 分相采集：HTML 首字节、coi reload 间隙、wasm 下载（过线字节/编码/缓存
  命中）、WebAssembly 编译+实例化、Qt 启动到 canvas 就绪
- 页面侧经 Page.addScriptToEvaluateOnNewDocument 注入探针（monkey-patch
  fetch / WebAssembly.compile / instantiate），线上未改造站点同样可用；
  探针时刻经 performance.timeOrigin 换算成 epoch，与 CDP wallTime、
  驱动侧 time.time() 对齐成单一时间轴

三条时间轴的对齐原理：
- CDP 事件 timestamp 是浏览器单调钟（秒）；requestWillBeSent 自带
  wallTime（epoch 秒），用两者的中位差把全部 CDP 时刻映射到 epoch
- 探针 performance.now() + performance.timeOrigin → epoch
- 驱动侧 time.time() 与 Chrome wallTime 同机同钟

用法:
  python scripts/wasm_load_profile.py                      # dev 构建树
  python scripts/wasm_load_profile.py --from-zip dist/web/GraphStudio-*-web.zip
  python scripts/wasm_load_profile.py --url https://studio.mangoeffect.net/web/
  python scripts/wasm_load_profile.py --encoding gzip --throttle 20m --runs 3
  可选: --modes cold,warm --cache-evict-warm（每轮热载前清 HTTP 缓存，
        分离 SW 缓存与浏览器缓存两条回访路径）

输出: dist/wasm_load_profile/<stamp>/summary.{json,md} + 每轮 raw json
"""

import argparse
import functools
import gzip as gzip_mod
import http.server
import json
import os
import shutil
import statistics
import sys
import tempfile
import threading
import time
import zipfile
from pathlib import Path
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402
from e2e_wasm.cdp_browser import CdpBrowser  # noqa: E402
from run_ui_tests_wasm import find_chrome  # noqa: E402

READINESS_EXPR = (
    "!!(document.querySelector('#qt-shadow-container')"
    " && document.querySelector('#qt-shadow-container').shadowRoot"
    " && document.querySelector('#qt-shadow-container').shadowRoot"
    ".querySelector('canvas'))"
)

# 页面探针：在任何页面脚本之前注入（add_init_script）。
# 只记时刻不读 body（消费 body 会破坏调用方；字节数由 CDP Network 域给）。
#  rejection 分支刻意不 rethrow——我们的旁路链吞掉异常避免 unhandledrejection
# 噪音，原始 promise 的语义由调用方自行处理。
LOAD_PROBE_JS = r"""
(function () {
  if (window.__gsLoadProbe) return;
  var P = window.__gsLoadProbe = {
    docStart: performance.now(),
    timeOrigin: performance.timeOrigin,
    href: location.href,
    isolated: window.crossOriginIsolated === true,
    cores: navigator.hardwareConcurrency || 0,
    net: (navigator.connection && navigator.connection.effectiveType) || "",
    fetches: {},
    wasm: {}
  };
  function rec(store, key, start) {
    var r = store[key] = store[key] || {};
    if (r.start === undefined) r.start = start;
    return r;
  }
  var origFetch = window.fetch;
  window.fetch = function (input, init) {
    var url = typeof input === "string" ? input
            : (input && input.url) || String(input);
    var promise = origFetch.apply(this, arguments);
    if (/\.(wasm|mnn)([?#]|$)/i.test(url)) {
      var key = url.split("/").pop().split("?")[0];
      var r = rec(P.fetches, key, performance.now());
      promise.then(function (resp) {
        r.headersAt = performance.now();
        r.status = resp.status;
        r.contentType = resp.headers.get("content-type");
        r.contentEncoding = resp.headers.get("content-encoding");
        r.contentLength = resp.headers.get("content-length");
      }, function (err) {
        r.errorAt = performance.now();
        r.error = String(err);
      });
    }
    return promise;
  };
  function wrap(target, name, key) {
    var orig = target[name];
    if (typeof orig !== "function") return;
    target[name] = function () {
      var r = rec(P.wasm, key, performance.now());
      var promise = orig.apply(this, arguments);
      Promise.resolve(promise).then(function (v) {
        r.end = performance.now();
        return v;
      }, function (e) {
        r.end = performance.now();
        r.error = String(e);
      });
      return promise;
    };
  }
  wrap(WebAssembly, "compile", "compile");
  wrap(WebAssembly, "instantiate", "instantiate");
  wrap(WebAssembly, "instantiateStreaming", "instantiateStreaming");
})();
"""

THROTTLE_PRESETS = {
    # (latency_ms, down_bps, up_bps)
    "fast3g": (150, 200_000, 100_000),
    "slow3g": (400, 51_200, 51_200),
}


def parse_throttle(spec: str):
    if not spec or spec == "none":
        return None
    if spec in THROTTLE_PRESETS:
        return THROTTLE_PRESETS[spec]
    if spec.endswith("m"):  # "20m" = 20 Mbps
        mbps = float(spec[:-1])
        return (40, int(mbps * 125_000), int(mbps * 125_000))
    raise ValueError(f"无法解析限速档: {spec}（可用 none|fast3g|slow3g|<N>m）")


class ProfileHandler(http.server.SimpleHTTPRequestHandler):
    """剖析用静态 server：COI 头可关（zip 模式镜像 Pages 无 COI、由 coi SW
    提供），传输编码可选 identity/gzip/brotli（镜像 Pages gzip / CF brotli）。"""

    coi_headers = True
    encoding_mode = "identity"
    _gz_cache: dict = {}

    def end_headers(self):
        if self.coi_headers:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        if self.encoding_mode == "identity":
            return super().do_GET()
        path = self.translate_path(self.path.split("?", 1)[0])
        if os.path.isfile(path):
            if self.encoding_mode == "brotli" and os.path.isfile(path + ".br"):
                return self._send_bytes(
                    open(path + ".br", "rb").read(), self.guess_type(path), "br")
            if self.encoding_mode == "gzip":
                blob = ProfileHandler._gz_cache.get(path)
                if blob is None:
                    with open(path, "rb") as f:
                        blob = gzip_mod.compress(f.read(), 6)
                    ProfileHandler._gz_cache[path] = blob
                return self._send_bytes(blob, self.guess_type(path), "gzip")
        return super().do_GET()

    def _send_bytes(self, body: bytes, ctype: str, encoding: str):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Encoding", encoding)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def prepare_content(args, root: Path) -> Path:
    if args.from_zip:
        out = Path(tempfile.mkdtemp(prefix="gs_profile_zip_"))
        with zipfile.ZipFile(args.from_zip) as zf:
            for member in zf.namelist():
                target = (out / member).resolve()
                if not str(target).startswith(str(out.resolve()) + os.sep):
                    raise ValueError(f"zip 内非法路径: {member}")
            zf.extractall(out)
        return out
    gs_build = root / "app" / "graph_studio" / "build_wasm"
    if not (gs_build / "graph_studio.html").is_file():
        console.fail("未找到 WASM 构建产物，先跑 "
                     "scripts/run_graph_studio_wasm.py --build-only")
        raise SystemExit(1)
    return gs_build


def probe_page_url(raw: str) -> str:
    raw = raw.strip()
    if raw.lower().endswith(".html"):
        return raw
    return raw.rstrip("/") + "/"


def _asset_name(url: str) -> str:
    if url.startswith(("data:", "blob:")):
        return url.split(",", 1)[0][:24] + "…"
    base = url.split("?", 1)[0].rstrip("/").rsplit("/", 1)[-1] or "(dir)"
    return base if len(base) <= 48 else base[:45] + "…"


def _median_wall_offset(records: list) -> float:
    diffs = [r["wall_req"] - r["t_req"] for r in records
             if r.get("wall_req") and r.get("t_req")]
    return statistics.median(diffs) if diffs else 0.0


def analyze_load(label: str, t_nav: float, t_ready: float, tab,
                 probe: dict | None) -> dict:
    """把一次加载的原始采集整理成分相结构（时刻全部 epoch 秒）。"""
    records = [dict(r) for r in tab.network]
    offset = _median_wall_offset(records)
    for r in records:
        r["wall_req"] = r["wall_req"] or (r["t_req"] + offset if r["t_req"] else 0)
        for tk in ("t_headers", "t_fin"):
            if r.get(tk):
                r[tk + "_wall"] = r[tk] + offset
    # coi reload 间隙：Page.frameNavigated 不一定带 timestamp（可选字段，
    # 实测此 Chrome 不带），改用 Document 请求的 wallTime——每次主帧导航
    # 恰好产生一条 Document 请求
    doc_reqs = sorted(r["wall_req"] for r in records
                      if r.get("rtype") == "Document")
    coi_reload = len(doc_reqs) > 1
    coi_gap = round(doc_reqs[1] - doc_reqs[0], 3) if coi_reload else None

    assets = []
    for r in records:
        if r.get("canceled"):
            continue  # 预取后被取消的请求不进表（json 里有原始记录）
        entry = {
            "name": _asset_name(r["url"]), "url": r["url"],
            "status": r.get("status") or (r.get("errorText") or ""),
            "wireBytes": r.get("encodedDataLength"),
            "encoding": r.get("contentEncoding", ""),
            "cache": ("disk" if r.get("fromDiskCache")
                      else "sw" if r.get("fromServiceWorker") else ""),
            "protocol": r.get("protocol", ""),
            "reqAt": round(r["wall_req"], 3),
        }
        if r.get("t_headers_wall") and r.get("t_fin_wall"):
            entry["ttfbS"] = round(r["t_headers_wall"] - r["wall_req"], 3)
            entry["durS"] = round(r["t_fin_wall"] - r["wall_req"], 3)
        assets.append(entry)

    wasm_recs = [r for r in records if r["url"].split("?")[0].endswith(".wasm")]
    wasm = {}
    if wasm_recs:
        w = wasm_recs[-1]
        via_sw = bool(w.get("fromServiceWorker"))
        wasm = {
            "reqAt": round(w["wall_req"], 3),
            # coi SW 代理（passthrough）时字节发生在 SW 会话，页面级
            # encodedDataLength 恒 0——报 None，过线字节另行用 curl 测
            "wireViaSW": via_sw,
            "wireMB": None if via_sw else
            round((w.get("encodedDataLength") or 0) / 1e6, 2),
            "encoding": (w.get("contentEncoding") or "(identity)")
            if not via_sw else "(sw)",
            "cache": ("disk" if w.get("fromDiskCache")
                      else "sw" if via_sw else ""),
            "protocol": w.get("protocol", ""),
        }
        if w.get("t_headers_wall") and w.get("t_fin_wall"):
            wasm["ttfbS"] = round(w["t_headers_wall"] - w["wall_req"], 3)
            wasm["downloadS"] = round(w["t_fin_wall"] - w["wall_req"], 3)
            wasm["finAt"] = round(w["t_fin_wall"], 3)

    inst_path = None
    if probe:
        pw = probe.get("wasm", {})
        for key in ("instantiateStreaming", "instantiate", "compile"):
            r = pw.get(key)
            if r and r.get("end") is not None and not r.get("error"):
                inst_path = key
                origin_s = probe["timeOrigin"] / 1000.0
                wasm["instStartAt"] = round(origin_s + r["start"] / 1000.0, 3)
                wasm["instEndAt"] = round(origin_s + r["end"] / 1000.0, 3)
                wasm["instDurS"] = round((r["end"] - r["start"]) / 1000.0, 3)
                break
        # 流式路径下编译与下载重叠；instantiate 结束是"可运行"里程碑
    qt_startup = None
    if wasm.get("instEndAt"):
        qt_startup = round(t_ready - wasm["instEndAt"], 3)
    elif wasm.get("finAt"):
        qt_startup = round(t_ready - wasm["finAt"], 3)

    shell_recs = [r for r in records if r.get("rtype") == "Document"]
    html_ttfb = None
    if shell_recs:
        s = shell_recs[0]
        if s.get("t_headers_wall"):
            html_ttfb = round(s["t_headers_wall"] - s["wall_req"], 3)

    return {
        "label": label,
        "ok": True,
        "totalS": round(t_ready - t_nav, 3),
        "consoleTail": tab.console_text()[-20000:],
        "coiReload": coi_reload,
        "coiGapS": coi_gap,
        "navCount": len(doc_reqs),
        "htmlTtfbS": html_ttfb,
        "wasm": wasm,
        "qtStartupS": qt_startup,
        "assets": assets,
        "probe": probe,
    }


def profile_load(browser: CdpBrowser, url: str, throttle=None,
                 timeout: float = 180.0, label: str = "",
                 cache_evict: bool = False) -> dict:
    """一次加载测量：返回 analyze 结果或 fail 结构。"""
    tab = browser.new_tab("about:blank")
    try:
        tab.enable_network()
        tab.add_init_script(LOAD_PROBE_JS)
        if throttle:
            tab.emulate_network(*throttle)
        if cache_evict:
            tab.clear_browser_cache()
        t_nav = time.time()
        tab.navigate(url, timeout=timeout)
        tab.wait_expr(READINESS_EXPR, timeout=timeout, poll=0.1)
        t_ready = time.time()
        try:
            probe = tab.evaluate(
                "Object.assign(window.__gsLoadProbe || {}, {"
                " modelState: window.__gsModelState || null,"
                " modelsManifest: !!window.__gsModelsManifest,"
                " swController: !!(navigator.serviceWorker"
                " && navigator.serviceWorker.controller)})")
        except RuntimeError:
            probe = None
        try:
            tab.wait_network_idle(quiet=0.5, timeout=5.0)
        except TimeoutError:
            pass  # 后台模型 fetch 未停也无碍——关键路径已经量到
        return analyze_load(label, t_nav, t_ready, tab, probe)
    except (TimeoutError, RuntimeError, OSError) as e:
        console.fail(f"  [{label}] 加载未就绪/失败: {type(e).__name__}: {e}")
        return {"label": label, "ok": False,
                "error": f"{type(e).__name__}: {e}",
                "consoleTail": tab.console_text()[-2000:]}
    finally:
        browser.close_tab(tab)


def _med(vals: list) -> float | None:
    vals = [v for v in vals if v is not None]
    return round(statistics.median(vals), 3) if vals else None


def aggregate(runs: list) -> dict:
    ok = [r for r in runs if r.get("ok")]
    if not ok:
        return {}
    return {
        "runs": len(ok),
        "totalS": _med([r["totalS"] for r in ok]),
        "coiGapS": _med([r.get("coiGapS") for r in ok]),
        "wasmDownloadS": _med([r.get("wasm", {}).get("downloadS") for r in ok]),
        "wasmWireMB": _med([r.get("wasm", {}).get("wireMB") for r in ok]),
        "wasmInstS": _med([r.get("wasm", {}).get("instDurS") for r in ok]),
        "qtStartupS": _med([r.get("qtStartupS") for r in ok]),
        "htmlTtfbS": _med([r.get("htmlTtfbS") for r in ok]),
    }


def write_report(out_dir: Path, meta: dict, runs: list, aggs: dict):
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps({"meta": meta, "aggregates": aggs, "runs": runs},
                   ensure_ascii=False, indent=1, default=str))

    lines = [f"# GraphStudio WASM 加载剖析 {meta['stamp']}", ""]
    lines += [f"- 目标: `{meta['target']}`",
              f"- 模式: {meta['modes']}，每档 {meta['runsWanted']} 轮",
              f"- 传输编码: {meta['encoding']}，限速: {meta['throttle']}",
              f"- Chrome: {meta.get('chrome', '')}", ""]
    lines += ["## 分相汇总（中位数，秒）", "",
              "| 模式 | 轮数 | 总耗时 | coi双载间隙 | HTML首字节 | "
              "wasm下载 | wasm过线MB | 编码 | 编译+实例化 | Qt启动 |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for mode, agg in aggs.items():
        if not agg:
            lines.append(f"| {mode} | 0 | (全部失败) | | | | | | | |")
            continue
        enc = next((r.get("wasm", {}).get("encoding", "")
                    for r in runs if r.get("label", "").startswith(mode)
                    and r.get("ok")), "")
        lines.append(
            f"| {mode} | {agg['runs']} | {agg['totalS']} | "
            f"{agg['coiGapS'] or '—'} | {agg['htmlTtfbS'] or '—'} | "
            f"{agg['wasmDownloadS'] or '—'} | {agg['wasmWireMB'] or '—'} "
            f"| {enc} | {agg['wasmInstS'] or '—'} | "
            f"{agg['qtStartupS'] or '—'} |")
    lines += ["", "## 冷载代表性资产表（首轮）", "",
              "| 资产 | 状态 | 过线字节 | 编码 | 缓存 | 协议 | TTFB(s) | "
              "全程(s) |", "|---|---|---|---|---|---|---|---|"]
    rep = next((r for r in runs if r.get("ok")
                and r.get("label", "").startswith("cold")), None)
    if rep:
        for a in rep["assets"]:
            lines.append(
                f"| {a['name']} | {a.get('status', '')} | "
                f"{(a.get('wireBytes') or 0)} | {a.get('encoding') or '—'} | "
                f"{a.get('cache') or '—'} | {a.get('protocol') or '—'} | "
                f"{a.get('ttfbS', '—')} | {a.get('durS', '—')} |")
    lines += ["", "## 自动发现", ""]
    findings = derive_findings(runs)
    lines += [f"- {f}" for f in findings] or ["- （无）"]
    (out_dir / "summary.md").write_text("\n".join(lines) + "\n")
    return findings


def derive_findings(runs: list) -> list:
    out = []
    ok = [r for r in runs if r.get("ok")]
    cold = [r for r in ok if r["label"].startswith("cold")]
    warm = [r for r in ok if r["label"].startswith("warm")]
    if cold:
        w = [r.get("wasm", {}) for r in cold]
        enc = {x.get("encoding") for x in w if x}
        wire = [x.get("wireMB") for x in w if x and x.get("wireMB") is not None]
        if enc and "(identity)" in enc and wire and max(wire) > 20:
            out.append(f"wasm 明文（identity）传输 {max(wire)}MB——"
                       "过线字节未被压缩，是首访下载耗时的直接放大器")
        if any(x.get("encoding") == "(sw)" for x in w):
            out.append("wasm 经 SW 代理传输（coi passthrough）——页面级字节"
                       "不可见，过线字节以 curl/服务端口径为准")
        if any(x.get("coiReload") for x in cold):
            gaps = [x.get("coiGapS") for x in cold if x.get("coiGapS") is not None]
            out.append(f"coi 双载发生（首访固有），reload 间隙中位数 "
                       f"{_med(gaps)}s")
        else:
            out.append("coi 双载未发生（server 直接给 COI 头或 SW 已激活）")
        inst = [x.get("instDurS") for x in w if x]
        if inst:
            out.append(f"WebAssembly 编译+实例化（含流式下载段）中位数 "
                       f"{_med(inst)}s")
    if cold and warm:
        warm = [r for r in warm if not r["label"].endswith("_install")]
        cw = [r.get("wasm", {}).get("wireMB") for r in cold if r.get("wasm")]
        ww = [r.get("wasm", {}).get("wireMB") for r in warm if r.get("wasm")]
        wt = _med([r.get("totalS") for r in warm])
        ct = _med([r.get("totalS") for r in cold])
        ci = [r.get("wasm", {}).get("instDurS") for r in cold if r.get("wasm")]
        wi = [r.get("wasm", {}).get("instDurS") for r in warm if r.get("wasm")]
        if ww is not None and cw is not None and statistics.median(ww) < 0.1:
            extra = ""
            if wi and ci:
                extra = f"（编译 {statistics.median(wi)}s vs 冷载 "
                f"{statistics.median(ci)}s——V8 wasm code cache）"
            out.append(f"回访 wasm 过线≈0（HTTP 缓存/SW 命中）：总耗时 "
                       f"{wt}s vs 冷载 {ct}s{extra}")
        elif ww is not None:
            out.append(f"回访仍需过线 {statistics.median(ww):.1f}MB"
                       "（缓存未命中/已过期）")
    return out


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio WASM 加载分相剖析")
    ap.add_argument("--from-zip", default="", help="发布 zip")
    ap.add_argument("--url", default="", help="线上 URL（http(s)）")
    ap.add_argument("--chrome", default=os.environ.get("GS_CHROME", ""))
    ap.add_argument("--port", type=int, default=8210)
    ap.add_argument("--cdp-port", type=int, default=9341)
    ap.add_argument("--runs", type=int, default=3, help="每档重复轮数")
    ap.add_argument("--modes", default="cold,warm", help="cold,warm 组合")
    ap.add_argument("--encoding", default="identity",
                    choices=["identity", "gzip", "brotli"],
                    help="本地 server 传输编码（--url 模式忽略）")
    ap.add_argument("--coi-headers", default="auto",
                    choices=["auto", "on", "off"],
                    help="本地 server 是否直接给 COI 头（auto: 构建树 on / "
                         "zip off 镜像 Pages 由 coi SW 提供）")
    ap.add_argument("--throttle", default="none",
                    help="CDP 限速档: none|fast3g|slow3g|<N>m（N=Mbps）")
    ap.add_argument("--cache-evict-warm", action="store_true",
                    help="热载每轮前清 HTTP 缓存（保留 SW/Cache Storage）")
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--artifacts", default="", help="输出根目录")
    args = ap.parse_args()
    if sum(bool(x) for x in (args.from_zip, args.url)) > 1:
        console.fail("--from-zip 与 --url 互斥")
        return 1

    root = repo_root()
    stamp = time.strftime("%Y%m%d-%H%M%S")
    out_root = Path(args.artifacts) if args.artifacts \
        else root / "dist" / "wasm_load_profile"
    out_dir = out_root / stamp

    throttle = None
    if args.throttle != "none":
        throttle = parse_throttle(args.throttle)

    chrome_path = find_chrome(args.chrome)
    if chrome_path is None:
        return 1

    # 内容源：--url 线上；否则本地 server（构建树或解包 zip）
    server = None
    zip_tmp: Path | None = None
    if args.url:
        page_url = probe_page_url(args.url)
        content_desc = page_url
    else:
        content_dir = prepare_content(args, root)
        if args.from_zip:
            zip_tmp = content_dir
        coi = args.coi_headers
        if coi == "auto":
            # zip 布局带 coi-serviceworker：关 COI 头镜像 Pages 真实首访
            coi = "off" if args.from_zip else "on"
        handler_cls = type("BoundProfileHandler", (ProfileHandler,),
                           {"coi_headers": coi == "on",
                            "encoding_mode": args.encoding})
        server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", args.port),
            functools.partial(handler_cls, directory=str(content_dir)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        page = "index.html" if (content_dir / "index.html").is_file() \
            else "graph_studio.html"
        page_url = f"http://localhost:{args.port}/{page}"
        content_desc = (f"{args.from_zip or '构建树'} @ {page_url}"
                        f"（COI头={coi}, 编码={args.encoding}）")
        console.ok(f"剖析 server: {content_desc}")

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    meta = {"stamp": stamp, "target": content_desc, "modes": ",".join(modes),
            "runsWanted": args.runs, "encoding": args.encoding
            if not args.url else "(线上)",
            "throttle": args.throttle, "chrome": chrome_path.name}
    console.step(f"剖析目标: {content_desc}；限速 {args.throttle}；"
                 f"每档 {args.runs} 轮")

    runs: list = []
    aggs: dict = {}
    try:
        for mode in modes:
            if mode == "cold":
                for i in range(args.runs):
                    browser = CdpBrowser(chrome_path, cdp_port=args.cdp_port)
                    browser.start()
                    try:
                        r = profile_load(browser, page_url, throttle,
                                         args.timeout,
                                         label=f"cold{i + 1}")
                    finally:
                        browser.stop()
                    runs.append(r)
                    if r.get("ok"):
                        console.ok(f"  cold{i + 1}: 总 {r['totalS']}s，"
                                   f"wasm {r['wasm'].get('wireMB')}MB/"
                                   f"{r['wasm'].get('downloadS')}s，"
                                   f"编译+实例化 {r['wasm'].get('instDurS')}s")
            elif mode == "warm":
                browser = CdpBrowser(chrome_path, cdp_port=args.cdp_port)
                browser.start()
                try:
                    # 第一轮同时是 SW 安装/预热，单列 install 不计入 warm 聚合
                    install = profile_load(browser, page_url, throttle,
                                           args.timeout, label="warm_install")
                    runs.append(install)
                    for i in range(args.runs):
                        r = profile_load(browser, page_url, throttle,
                                         args.timeout,
                                         label=f"warm{i + 1}",
                                         cache_evict=args.cache_evict_warm)
                        runs.append(r)
                        if r.get("ok"):
                            console.ok(
                                f"  warm{i + 1}: 总 {r['totalS']}s，wasm "
                                f"{r['wasm'].get('wireMB')}MB，编译+实例化 "
                                f"{r['wasm'].get('instDurS')}s")
                finally:
                    browser.stop()
            else:
                console.fail(f"未知模式: {mode}")
                return 1
    finally:
        if server:
            server.shutdown()
        if zip_tmp is not None:
            shutil.rmtree(zip_tmp, ignore_errors=True)

    for mode in modes:
        # warm_install 是 SW/缓存安装轮，不算 warm 聚合（cold 同理只匹配 coldN）
        sel = [r for r in runs
               if r["label"] == mode or
               (r["label"].startswith(mode)
                and r["label"][len(mode):len(mode) + 1].isdigit())
               and r.get("ok")]
        aggs[mode] = aggregate(sel)

    findings = write_report(out_dir, meta, runs, aggs)
    console.step(f"报告: {out_dir / 'summary.md'}")
    for f in findings:
        console.ok(f"发现: {f}")
    ok_count = sum(1 for r in runs if r.get("ok"))
    console.ok(f"{ok_count}/{len(runs)} 轮成功")
    return 0 if ok_count == len(runs) else 2


if __name__ == "__main__":
    sys.exit(main())
