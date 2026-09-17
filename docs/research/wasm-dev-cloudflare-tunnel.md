# WASM GraphStudio 临时域名调试方案（cloudflared quick tunnel）

日期：2026-09-16。实测环境：macOS、cloudflared 2026.8.3（homebrew）。

## 背景

`scripts/run_graph_studio_wasm.py` 构建多线程 WASM GraphStudio 后，用
`scripts/wasm_dev_server.py` 起本地 dev server（默认端口 8000，占用则顺延 8000–8019，
`--port` 显式指定时被占即报错）。server 关键行为：

- 绑定 `("", PORT)`（全接口）——隧道可从 localhost 转发；
- 每个响应都带 `Cross-Origin-Opener-Policy: same-origin` +
  `Cross-Origin-Embedder-Policy: require-corp`（SharedArrayBuffer / cross-origin
  isolation 必需）；
- 支持 `.br`/`.gz` 预压缩旁路文件（构建脚本会 brotli 预压缩 wasm/js/html）。

结论：**无需改任何代码**，quick tunnel 直接叠加在现有 dev server 上即可。

## 方案（临时域名，零配置零账号）

```bash
# 终端 1：构建 + 起 server（隧道场景建议 --no-browser，本地浏览器走 tunnel URL 调试）
python3 scripts/run_graph_studio_wasm.py --no-browser

# 终端 2：起临时隧道（注意 --config，见下方坑）
: > /tmp/empty.yml
cloudflared tunnel --config /tmp/empty.yml --url http://localhost:8000
```

cloudflared 会打印一个随机的 `https://<words>.trycloudflare.com` 临时域名，
浏览器直接访问 `https://<...>.trycloudflare.com/graph_studio.html`。
不需要 Cloudflare 账号/DNS/证书；进程 Ctrl+C 后域名即刻失效；每次重启域名随机。

## 实测验证（2026-09-16，本机）

- `curl -I` 经隧道：`HTTP/2 200`，`cross-origin-opener-policy: same-origin`、
  `cross-origin-embedder-policy: require-corp` 头完整穿透 →
  浏览器 `crossOriginIsolated === true`，SharedArrayBuffer 可用（HTTPS + 真实
  COOP/COEP 头，比 coi-serviceworker 方案更干净——dev shell 未注入 coi）。
- 连续 6 次请求全部 200，稳定。
- 注意：真机 GUI Chrome 验证（不要用 Electron 内嵌浏览器/headless，二者都不支持
  cross-origin isolation，见 AGENTS.md 既有结论）。

## 坑（本机实测踩到）

1. **`~/.cloudflared/config.yml` 会污染 quick tunnel**（关键坑）：本机已有 DSH 的
   命名隧道配置（`tunnel: <id>` + ingress `dsh.mangoeffect.net → 127.0.0.1:3080`，
   兜底 `http_status:404`）。cloudflared 的 `tunnel --url` 会合并该文件，config 里的
   ingress 规则覆盖 `--url`，导致 trycloudflare 域名命不中任何规则 → 边缘恒 404
   （debug 日志可见 `ingressRule=1 originService=http_status:404`）。
   **解法：显式传 `--config /tmp/empty.yml`（空文件）绕过。**
2. 本机网络对 Cloudflare 隧道边缘的 QUIC(7844)/部分 TCP 不通（precheck
   `hard_fail`），cloudflared 自动降级 http2 协议即可工作；4 条边缘连接通常只注册
   1 条（NRT），实测不影响使用。
3. 单线程 `http.server`（TCPServer）足够单客户端调试；多人并发加载大 wasm 可能
   排队，属预期。

## 与备选方案对比

- **ngrok**：需账号 + token，免费版有告警页；trycloudflare 无这些负担。
- **coi-serviceworker（生产 web 包方案）**：纯静态、无服务器，但首次加载有 SW
  注册/重载时序问题；tunnel 方案由服务端直接下发真实 COOP/COEP 头，无该时序问题，
  更适合调试。
- **局域网直连 `http://<lan-ip>:8000`**：同为 secure-context 例外，但仅限同网段，
  手机跨网/远程协作用 tunnel 更方便。
