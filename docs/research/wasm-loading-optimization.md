# WASM GraphStudio 加载体验调研（logo 优化 + 加载提速）

调研对象：https://studio.mangoeffect.net/web/（release.yml wasm job →
package_web.py 打包 → website.yml 解包进 docs/public/web/）。
产物构成：Qt 6.6.3 wasm_multithread 静态构建单文件 `graph_studio.wasm`
（Release，33.5MB）+ `graph_studio.js`（350KB）+ `qtloader.js` + Qt 生成 shell。

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
   事件循环起来，桌面 ~1-3s、手机更久。进度文案切到 "Starting..." 即可。

## 二、加载速度：瓶颈定量与各杠杆可行性

线上实测（curl，GitHub Pages 边缘）：

| 项 | 值 |
|---|---|
| graph_studio.wasm 原始 | 33.5MB |
| Pages 动态 gzip 传输 | **10.9MB**（`content-encoding: gzip`，实测下载字节数） |
| 同文件 brotli -11（构建产物 .br） | 7.3MB |
| graph_studio.js | 350KB → gzip 80KB |
| cache-control | **max-age=600**（Pages 对一切资产只给 10 分钟） |

首访时序：导航 → coi SW 注册 + 自动 reload（一次额外往返）→ 下载 10.9MB
gzip wasm → 编译实例化（33MB 模块，移动端数秒）→ Qt UI。模型（face/matting
.mnn）由 entry.cpp 启动期后台 fetch（`__gsModelsReady` 不阻塞 UI），**不是**
首屏瓶颈。

### 杠杆清单（按性价比排序）

1. **SW 缓存 wasm（收益最大、完全自主可控）**：`cache-control: max-age=600`
   意味着 10 分钟后重访要重新下载 10.9MB。我们已有 coi-serviceworker 拦截
   全部请求（含 wasm），扩展为对 `graph_studio.{wasm,js}` 做 cache-first
   （Cache Storage + 版本键 = zip 内文件 etag/hash），重访近零下载。这是
   对回访体验影响最大的一项。
2. **下载进度条**（不提速但体感）：见上，shell 侧 `qt.module` 预取。10.9MB
   静止 vs 有字节进度，体感差异巨大。
3. **Brotli 传输（-33% 首访字节，但 Pages 做不到）**：GitHub Pages 不做
   brotli 协商（.br 文件是死重，打包时已剔除）。可行的旁路：
   - 域名在 Cloudflare（当前 DNS-only，因为代理会挡 Let's Encrypt HTTP-01
     续期）。若切橙云，需接受：ACME HTTP-01 续期每 ~90 天会被代理挡一次，
     得临时切回灰云续期（或改 DNS 验证）。收益：CF 对 wasm 自动 brotli +
   边缘缓存，10.9MB → ~7.3MB，且 max-age 不再是 10 分钟。
   - 或 wasm 单独放支持 brotli 协商的静态托管（jsDelivr 不行，需源站），
     shell 里绝对 URL 指过去——但跨源 + coi（credentialless）下需 CORP，
     且自包含部署语义破坏。**不建议**。
4. **减小 wasm 本体（工程量大、收益中）**：33.5MB = Qt Widgets 静态全量 +
   OpenCV + MNN(6.2MB) + QuickJS + 应用。Release 已是 -O2；进一步：
   - `-sASYNCIFY` 为 wgpu spin_until 保留（去 WebGPU 才能去，另增运行时
     优化空间，见 wasm 章节注释，属大手术）；
   - exceptions（-fexceptions）被 QuickJS/绑定路径依赖，不可去；
   - LTO（-flto）需 Qt/OpenCV/MNN 全链路 LTO 重编，Qt wasm 官方包非 LTO，
     不可行；
   - MNN 若可拆为按需 fetch（首次用到 face/matting 才拉 .a 对应代码）——
     静态链接做不到函数级懒加载，只能整体拆 task_graph_core + 可选任务的
     双模块 + dynamicLibrary，wasm 侧 emscripten 支持但 Qt 静态构建改造大。
5. **首访双导航（coi reload）**：固有一次 reload 往返，coi 机制本身无法
   避免（Pages 无自定义头）。可做的只是确保 boot guard 不再多 reload（已修）。

### 建议落地顺序

1. shell 品牌化 spinner + 字节进度（package_web.py 注入，无构建改动）；
2. coi SW 扩展 cache-first（重访零下载）；
3. 评估 Cloudflare 橙云 + DNS-01 验证切换（首访 -33% + 长缓存）；
4. （远期）wasm 瘦身：拆 MNN/可选 subnode 为动态模块。
