# WASM GraphStudio 加载体验调研（logo 优化 + 加载提速）

调研对象：https://studio.mangoeffect.net/web/（release.yml wasm job →
package_web.py 打包 → website.yml 解包进 docs/public/web/）。
产物构成：Qt 6.6.3 wasm_multithread 静态构建单文件 `graph_studio.wasm`
（Release + wgpu/ASYNCIFY，34.3MB）+ `graph_studio.js`（351KB）+ `qtloader.js`
+ Qt 生成 shell（package_web.py 注入 coi/启动守卫/品牌 spinner）。

## 一、加载 logo：现状与可行方案

### 现状

Qt 6.6.3 生成的 shell（`graph_studio.html`）里加载页是一个 `<figure
id="qtspinner">`：`<img src="qtlogo.svg" width="320" height="200">`（Qt 官方
绿色 logo）+ 一行 "Loading..."（`#qtstatus`，之后被 onExit 的退出文案复用）。
`qtloader.js` 的 `QtLoader` 兼容 API 有 showLoader 钩子，但我们的 shell 用的是
新 `qtLoad()` API——**没有加载期回调**，status 从 "Loading..." 到 UI 出现之间
全程静止（onLoaded 前没有任何状态输出）。

### 结论：完全可以去掉/替换，且不需要重编 wasm

shell 是纯文本产物，`package_web.py` 已经在打包期注入 coi/启动守卫/favicon，
同一位置继续改 shell 即可，Release 侧零改动。三个层次：

1. **去掉 qtlogo.svg**（最低成本）：把 `<img src="qtlogo.svg">` 替换为品牌
   元素（`favicon.svg` 已随包）+ CSS spinner；qtlogo.svg 可从 zip 的 assets
   里剔除（替换后无引用）。注意 `onExit` 路径会把退出文案写进 `#qtstatus`
   并 showUi(spinner)——spinner 容器本身要保留，只换内容。
2. **真实下载进度**（qtloader 原生支持）：`qtLoad()` 支持 `qt.module`
   （`Promise<WebAssembly.Module>`），存在时它会设置 `instantiateWasm` 用
   预编译模块实例化（qtloader.js L110-122）。因此 shell 可以自己用
   `fetch` + `ReadableStream` 拉 wasm，按 `content-length` 累计字节显示
   "x.y / 10.9 MB (zz%)"，`WebAssembly.compile` 后把 promise 传给
   `qt: { module }`。纯 shell 改动，wasm/js 产物不动。
   - 陷阱：GitHub Pages 对 wasm 动态 gzip 无 content-length，进度条要么按
     已知字节数（构建期烧入）要么只显示已下载 MB。
3. **编译/启动期提示**：字节到 100% 后是 `WebAssembly.instantiate` + Qt
   事件循环起来，桌面 ~0.3s（实测，见下）、手机更久。进度文案切到
   "Starting..." 即可。

## 二、耗时全景实测（2026-10-11 基线）

测量工具：`scripts/wasm_load_profile.py`（本轮新建）——真实有头 Chrome + CDP
（Network 域逐资产过线字节/时延 + `Page.addScriptToEvaluateOnNewDocument`
注入探针 monkey-patch fetch/`WebAssembly.compile`/`instantiateStreaming`，
时刻经 `performance.timeOrigin` 与 CDP wallTime 对齐成单一 epoch 时间轴）。
模式：cold=全新浏览器 profile（SW 注册 + coi 双载 + 空缓存）；
warm=同浏览器再开 tab。报告落 `dist/wasm_load_profile/<stamp>/`。

**环境注记（对数字影响极大）**：测量点同一时刻的对照吞吐——
GitHub Pages（fastly SIN 边缘）**~190KB/s**，Cloudflare 边缘 **7.2MB/s**
（speed.cloudflare.com 25MB 实测）。即：本机网络很快，慢的是"到 fastly
新加坡边缘"的线路（中文用户群的典型形态）。下列线上数字应按"~190KB/s
线路"理解；20Mbps 线路的首访形态见"本地 gzip+限速"行。

### 实测汇总

| 场景 | 总耗时 | coi双载 | wasm 过线 | wasm 下载 | 编译+实例化¹ | Qt 启动 |
|---|---|---|---|---|---|---|
| 本地回环 identity（冷×3） | **0.54-0.57s**² | —（无 coi） | 34.32MB | 0.15s | 0.13-0.16s | ~0.32s |
| 本地回环（热×3，disk cache） | **0.28-0.52s** | — | ≈0 | 0.03s | **0.06s**³ | 0.16-0.24s |
| 本地 zip 形态（冷，coi SW 双载） | 0.39-0.65s | **0.072s**（回环） | (sw)⁴ | 0.08s | ~0.1s | — |
| 本地 gzip + 20Mbps 限速（冷×2） | **5.1-6.4s** | — | **10.91MB** | 4.6-5.9s | （与下载重叠） | ~0.3s |
| **线上**（本机 ~190KB/s，冷×2） | **63.7 / 83.8s** | **1.2s** | (sw)⁴ | **61.7 / 81.7s** | （与下载重叠） | — |
| 线上（热，缓存落定后） | **0.53s** | — | ≈0 | — | 0.14s | — |

¹ `instantiateStreaming` 全程时长，流式编译与下载重叠；下载完成后的纯编译
增量在桌面 <0.1s（见冷/热对比）。
² 会话内首个 Chrome 实例另有 ~1.5s 级系统冷噪（二进制分页/GPU 初始化），
不属应用本身。
³ 热载编译 0.06s vs 冷载 0.13-0.16s——**V8 wasm code cache 正常生效**。
⁴ coi SW passthrough 代理下页面级 `encodedDataLength` 恒 0，过线字节以
curl 为准。

### 传输层（curl 实测，与浏览器互证）

| 项 | 值 |
|---|---|
| wasm（34.3MB）gzip 过线 | **11,098,318B（11.1MB）**，`application/wasm`（流式编译合法）、HTTP/2、fastly SIN |
| brotli 协商 | **不支持**（`Accept-Encoding: br` 时 identity 34.3MB 兜底）；自压 brotli -11 = 7.4MB |
| graph_studio.js | 81KB gzip 过线（裸 351KB，未 minify） |
| cache-control | `max-age=600` + 弱 etag（10 分钟后回访需再验证） |
| 吞吐对照 | fastly SIN ~190KB/s vs Cloudflare 边缘 7.2MB/s（同机同时刻） |

### 改变了优先级的发现

1. **生产首访瓶颈是"到 fastly 的吞吐"，不是字节数本身**：同一时刻 CF 边缘
   7.2MB/s，fastly SIN 190KB/s——11.1MB gzip 在此线路就是 60s+。任何只减
   字节的手段（brotli -33%、瘦身 -10-20%）在低吞吐线路上都是线性改善，
   而**换 CDN 路径（Cloudflare 橙云）是数量级改善**（吞吐 + brotli + 长缓存
   三合一）。P4 从"可选项"升级为**首访第一杠杆**。
2. **启动期模型全量预取是"线上旧二进制"行为，main 已修复待发布**：实测
   （本地构建树与线上一致）启动后即并行下载全部 19MB 模型——本机 09-26
   构建树与线上部署版均早于 dfa4401（09-28"模型按需延迟加载 + Cache
   Storage 持久缓存"：启动期只拉 manifest，模型由 ModelFinder 按需经
   `__gsModelLoad` 下载，cache 命中本地回填、miss 直接跳过不走网络）。
   它是线上实测 warm_install 240s 超时 / warm1 108s 的直接肇因（19MB
   @190KB/s ≈ 100s 抢带宽）。**下一个 release 上线即自动消除**，无需
   额外改动；release 前的本地剖析也应以重建后的构建树为准。
3. **桌面端编译不是瓶颈**（冷 0.13-0.16s / code cache 后 0.06s）；瘦身的
   收益主要落在低吞吐线路的过线字节与移动端。
4. **V8 code cache 与 HTTP 缓存均正常工作**；max-age=600 过期后的回访靠
   SW cache-first 兜底（未实现，见杠杆 3）。
5. **coi 双载间隙线上实测 1.2s**（首访固有）；若走 CF，Response Header
   Transform 直接给 COOP/COEP 可整体消除（coi SW 保留为灰云回退）。

## 三、杠杆清单（按 2026-10-11 实测重排）

1. **Cloudflare 橙云（首访第一杠杆）**：域名已在 CF（当前灰云）。切橙云收益：
   ① 吞吐（本线路实测 7.2MB/s vs 190KB/s）；② wasm brotli（11.1→7.4MB，
   -33%）；③ Response Header Transform 摆脱 `max-age=600`（长缓存）；
   ④ 加分项：Transform Rules 直接加 COOP/COEP → coi 双载消失。代价：橙云代理
   挡 GitHub Pages 的 Let's Encrypt HTTP-01 续期（既有实测）→ 接受每 ~90 天
   临时灰云续期（监控证书有效期），或届时评估 DNS-01。CF 对
   `application/wasm` 的 brotli/缓存行为上线前 staging 实证。
   **操作手册：`dev-docs/cloudflare-orange-runbook.md`**（步骤/验证命令/
   回退/证书续期窗口；dash 操作需人工执行）。
2. **模型按需加载（已完成于 main，随下个 release 生效）**：dfa4401——启动期
   只拉 manifest（数百字节），模型由 ModelFinder 按需下载 + Cache Storage
   持久缓存。线上部署版（旧二进制）仍会启动期全量预取 19MB，是慢线路
   首访后约 100s 带宽抢占的元凶；发布即愈。
3. **SW cache-first + 进度条 + coi 窗口预取（纯壳层）**：
   - coi-serviceworker（已 vendored，MIT）扩展 cache-first：版本键=构建
     版本（打包期烧入），activate 清旧缓存；wasm/js/worker/qtloader
     cache-first，index 自身 network-first（防旧壳配新 wasm）。解决
     max-age=600 过期后的回访再下载（实测热缓存 0.5s，冷缓存 60s+ 的差）。
   - 字节进度条：见第一节层次 2。
   - coi 窗口预取：第一导航即开始 fetch wasm（落 HTTP 磁盘缓存，reload 后
     qtloader 命中）——把 11MB 下载与 SW 注册/reload 的 1.2s 重叠掉；需实测
     二次加载 transferSize≈0 才保留。
4. **构建侧瘦身（2026-10-11 实验结论，MNN 保留）**：34.3MB = Qt 静态全量
   + ASYNCIFY 全默认插桩 + MNN 5.8MB + OpenCV 8.3MB+3rdparty（jpeg/png/
   zlib 与 Qt bundled 重复）+ 核心 3.85MB（QuickJS 831KB）+ 13 子模块
   whole-archive 3.4MB。逐项实测：
   - **ASYNCIFY 插桩是最大单项，但当前工具链下不可收窄**：
     `-sASYNCIFY_IGNORE_INDIRECT=1` 实验 34.3MB→**21.1MB（-13.2MB，brotli
     7.0→5.0M）**——插桩膨胀全部来自 binaryen 对"间接调用可能触达 sleep"
     的保守扩散（executor 的虚 execute() 派发把所有任务+OpenCV+MNN 全部
     拖进插桩集）。但干净全量 E2E 判定 **boot 即挂**（启动期 GpuBootstrap
     的 wgpu spin_until 跨未插桩间接帧，asyncify 展开断裂）——unsound
     实锤，不可用。工具链三重限制：IGNORE_INDIRECT 崩、ASYNCIFY_REMOVE
     对间接传播零效果（binaryen 3.1.46 的间接扩展不看 remove 表，^锚定/
     前缀.* 两轮实测字节级不变）、ASYNCIFY_ADVISE 在 3.1.46 只是 verbose
     透传产不出 ONLY 白名单。**升级 emsdk（>3.1.4x）后值得重试**——
     binaryen 后续版本对 asyncify 传播分析有改进，且 Qt wasm 止步 6.6.3
     的约束只钉 Qt 侧，未来换 emscripten 6.x + `--use-port=emdawnwebgpu`
     路线时一并重评。
   - **裁 OpenCV 编解码**：`build_opencv_wasm.py` 关 TIFF/WEBP/OPENJPEG
     （测试夹具与产品链路只用 png/jpg/ppm；ppm 是 imgcodecs 内建）。
   - **--closure=1 否决**：emcc 3.1.46 的 closure=1 实际以 ADVANCED_
     OPTIMIZATIONS 运行，与 Qt glue/EM_ASM 编译失败；且 js gzip 后仅
     80KB，收益边际。
   - **-sEVAL_CTORS=1 否决**：emcc 明确报错 "EVAL_CTORS is not compatible
     with asyncify yet"。
   - **-Os 否决**：优化等级被根 CMakeLists:656 与各子模块尾置 -O2 钉死，
     收益上限 ~0.7MB 且要改 5 个仓库 13 处，性价比过低。
   - **-sENVIRONMENT=web,worker 保留**（裁 js 里 node 分支）。
   - 不可做：LTO（Qt 官方 wasm 包非 LTO）；exceptions（QuickJS/绑定
   依赖）；MNN 拆动态模块属大手术。
5. **首访双导航（coi reload）**：固有一次 reload 往返（线上实测 1.2s）。
   Pages 形态下无法避免；CF 形态下可用 Response Header Transform 根治。
   boot guard 已确保只 reload 一次。

### 建议落地顺序

1. ~~模型回填门控~~（已随 dfa4401 落在 main，下个 release 生效）+
   壳层进度条（已落地，见下）；
2. coi SW 扩展 cache-first（已落地，见下）；
3. Cloudflare 橙云评估与切换（**手册就绪：dev-docs/cloudflare-orange-runbook.md**，
   吞吐/字节/缓存三合一，dash 操作需人工）；
4. 构建侧瘦身（当日已尽：ENVIRONMENT + OpenCV 裁剪落地；asyncify/
   closure/EVAL_CTORS/-Os 全部否决，见杠杆 4）；
5. 每步用 `scripts/wasm_load_profile.py` 前后对比（线上/本地 gzip+限速两
   口径）。

## 四、当日落地结果（2026-10-11，before → after）

| 项 | before | after | 说明 |
|---|---|---|---|
| wasm 本体 | 34.32MB | **32.64MB**（-4.9%） | OpenCV 裁剪（.a 侧 -1.83MB） |
| gzip 过线（20Mbps 口径） | 10.91MB / 4.6-5.9s | **10.39MB / 4.3-5.5s** | 同源同比 -5% |
| brotli（dev 侧参考） | 7.47MB | 7.10MB | |
| 首访体感 | 静止 spinner（10.9MB 无反馈） | **字节进度条**（Loading x.x MB） | 壳层注入，等预取落定才 reload |
| 首访下载与 coi 重叠 | 串行（reload 后才开始拉 wasm） | **重叠**（第一导航即预取，reload 命中 HTTP 缓存） | coi 的 doReload 被 window.coi 覆盖等待预取 |
| 回访（>10min 过期后） | 重新过线 10.9MB | **SW Cache Storage 直接回放（≈0 下载）** | 版本键=构建版本，activate 清旧缓存 |
| 回访（缓存落定） | 0.5s | 0.45s | 不变（本就命中） |
| 部署新版触达 | — | 回访主动 register() 触发 SW 更新检查 + controllerchange 重载 | coi 在已隔离导航不 register 的 24h 盲区补上 |
| 全量 E2E | — | 103 pass（boot/core/files 92/models 3/twice…） | models 场景既有落位 bug 顺带修复 |

本地 E2E 与剖析全绿；**线上生效需下一次 release 构建**（wasm 瘦身 +
壳层/SW 改动都走打包链）。预期线上收益 = 上表字节数 × 线路吞吐系数，
叠加 dfa4401（模型按需）后首访后的 19MB 带宽抢占消失；CF 橙云再叠加
吞吐 + brotli + 长 Cache-Control（最大单项，待人工执行手册）。
