# Cloudflare 橙云切换运维手册（studio.mangoeffect.net /web/ 提速）

配套调研：`docs/research/wasm-loading-optimization.md`（2026-10-11 实测基线）。
本手册是 **P4 落地清单**——dash 操作需要人执行，每步带验证命令与回退路径。

## 背景与预期收益

| 项 | 灰云现状（GitHub Pages/fastly SIN） | 橙云预期 |
|---|---|---|
| 本机线路吞吐（2026-10-11 实测） | ~190KB/s | ~7.2MB/s（speed.cloudflare.com 同刻对照） |
| wasm 传输编码 | gzip 11.1MB | brotli ~7.4MB（CF 对 `application/wasm` 自动压缩，上线实证） |
| 首载（本线路） | 60-90s | 预计 <5s |
| Cache-Control | max-age=600 | 可经 Transform Rules 上调 |
| coi 双载（1.2s） | 固有 | 可经 Response Header Transform 直接给 COOP/COEP 消除 |

已知代价：橙云代理挡 GitHub Pages 的 Let's Encrypt HTTP-01 续期（2026-09
切域名时实测）。缓解见 §证书续期。

## 前置确认

```bash
# Pages 侧当前状态（留存回退快照）
gh api repos/mangoeffect/graph-studio/pages
# 期望: cname=studio.mangoeffect.net, https_enforced=true,
#       pending_domain_unverified_at: null
```

DNS：Cloudflare zone `mangoeffect.net`，`studio` CNAME → `mangoeffect.github.io`
当前**灰云（DNS only）**。

## 切换步骤

1. **回退快照**：留存上面的 pages API 输出；本文件即回退手册。
2. **DNS 切橙云**：dash → DNS → `studio` 记录切 Proxied。
3. **SSL/TLS 模式**：确认 Full（非 strict）——CF→GitHub 走 https 源站即可。
4. **缓存规则**：`.wasm` 在 CF 默认可缓存扩展名清单内；如 `cf-cache-status`
   显示 MISS/DYNAMIC，再加 Cache Rule：`studio.mangoeffect.net/web/*` →
   Cache Eligible。
5. **Response Header Transform**（消 coi 双载，加分项）：
   匹配 `hostname eq studio.mangoeffect.net and uri path starts with /web/`，
   Set response header：
   - `Cross-Origin-Opener-Policy: same-origin`
   - `Cross-Origin-Embedder-Policy: require-corp`

   兼容性：页面已隔离时 coi-serviceworker 自动 no-op（保留作灰云回退）；
   已装旧 SW 的老用户由 SW 补头，行为不变。
6. **Cache-Control 策略（重要陷阱）**：**不要**对 wasm/js 设
   `max-age=31536000, immutable`——文件名固定（`graph_studio.wasm`），
   immutable 会把旧版锁死在浏览器一年，发新版只能靠用户清缓存。第一阶段
   维持源站 `max-age=600`（或上调至 1h），版本切换交给 SW 版本键链路
   （coi-serviceworker 的 `gs-web-<version>` cache + 回访 register() 主动
   触发更新检查 + controllerchange reload，见 package_web.py 注入脚本）。
   若后续把产物文件名改成带版本/hash（`graph_studio-<ver>.wasm` +
   index 引用改写），才可安全 immutable——那是第二阶段。

## 验证清单（切换后逐项跑）

```bash
# 1. brotli + CF 缓存
curl -sI -H 'Accept-Encoding: br' \
  https://studio.mangoeffect.net/web/graph_studio.wasm \
  | grep -iE 'content-encoding|cf-cache-status|content-length|cache-control'
# 期望: content-encoding: br、content-length ≈ 7.4MB、cf-cache-status HIT/MISS

# 2. COI 头（Transform 生效）
curl -sI https://studio.mangoeffect.net/web/ \
  | grep -iE 'cross-origin-(opener|embedder)'
# 期望: 两头都在 → 浏览器首访无双载

# 3. 分相实测（对比基线 dist/wasm_load_profile/20261011-151315）
python3 scripts/wasm_load_profile.py \
  --url https://studio.mangoeffect.net/web/ --modes cold,warm --runs 2
```

## 证书续期（每 ~90 天）

GitHub Pages 的 LE HTTP-01 续期在橙云下会被代理挡。CF Full（非 strict）
不校验源站证书链/有效期，源站证书过期用户侧无感，但避免 GitHub 侧行为
漂移（Pages 设置页可能标记域名证书异常）：

1. 监控：dash → SSL/TLS → Edge Certificates 看**边缘**证书（CF 管，自动
   续，无忧）；源站状态在 `gh api repos/mangoeffect/graph-studio/pages`
   的证书字段 + GitHub Pages 设置页观察。
2. 续期窗口（源站证书剩 ~2 周时）：`studio` 记录临时切灰云 30 分钟 →
   GitHub 续期（可在 Pages 设置页重存一次 custom domain 触发）→ 切回
   橙云。灰云窗口内站点直连 Pages，一切照旧。
3. 若忘了窗口：站点大概率仍可用（CF Full 兜底），按 2 补一次即可。

## 回退

`studio` DNS 记录切回灰云（DNS only）即完整回到现状：COI 由
coi-serviceworker 恢复、无 Transform 头、fastly 直接服务。无需任何代码
或 GitHub 侧改动。

## 风险与未决

- CF 对 `application/wasm` 的 brotli 行为以 §验证清单 1 实证为准；若不压
  （仅 gzip），首访也有吞吐收益，但 -33% 字节落空——可考虑 Worker 透传
  预压缩 .br（后续项）。
- `/online/`、博客页（Hugo 静态）同样走 CF：PaperMod 资源均可缓存，无
  特殊处理；注意 `fetch_web_release.py` 的 GitHub API 调用不经 CF，无关。
- Pages 域名验证（账号级 TXT）此前已确认不需要；橙云不改这一点。
