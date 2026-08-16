# GraphStudio 崩溃收集与上报（Sentry + Crashpad，桌面端）

> 开发者文档。设计随 commit `3134400` 引入，本文件记录当前完整方案（含 CI 条件接入、
> 符号归档、崩溃上下文增强）。WASM / 移动端不在覆盖范围内（见"已知限制"）。

## 方案总览

- **技术栈**：`sentry-native` 0.16.2 + `crashpad` 后端。依赖
  `app/graph_studio/third_party/sentry-native`（含 Chromium crashpad 多层递归子模块），
  由 `scripts/fetch_sentry.py` 按固定 tag 克隆，目录 gitignored。
  缺失时 CMake 照常构建，`CrashReporter.cpp` 编译为 no-op 桩——调用方
  （entry.cpp / GraphViewModel）无需任何 `#ifdef` 分支。
- **生命周期**：`InitCrashReporting()` 在 `main()` 最早期（QApplication 之前）调用，
  `ShutdownCrashReporting()` 退出前调用。DSN 优先级：运行期 `SENTRY_DSN` 环境变量 >
  编译期 `-DGRAPH_STUDIO_SENTRY_DSN`（发布包嵌入，DSN 是公开 client key 可安全进包）。
  无 DSN ⇒ 干净 no-op。
- **crashpad handler 摆放**：POST_BUILD 拷到可执行文件同目录（macOS bundle
  `Contents/MacOS/`、Windows exe 旁、Linux 构建目录由打包脚本带入 AppDir），
  crashpad 按可执行文件相对路径查找并 spawn 它。
- **release 标识**：`task_graph@<version>#<git短哈希>`。version 默认解析根
  project VERSION，发布脚本传完整渠道版本（如 `0.1.0-beta.42`，与 GitHub tag 一致），
  便于在 Sentry 按发布渠道分组。

## 崩溃上下文（崩溃不只是堆栈）

- **日志 → breadcrumb**：sentry init 成功后，CrashReporter 通过框架 logger 的
  `add_log_sink()`（附加观察者，不占用 GUI 面板的主槽 `set_log_sink()`）把
  **WARN 及以上**日志桥接为 sentry breadcrumb（级别映射 warning/error/fatal），
  崩溃事件自动携带最近的告警链。Shutdown 时 `remove_log_sink` 注销。
- **图上下文**：`SetGraphContext(file, node_count, edge_count)` 维护
  `sentry_set_context("graph", ...)`；GraphViewModel 在加载/保存/清空图后同步。
  只上报文件名（不含路径），避免泄漏用户目录结构。
- **执行失败 breadcrumb**：`AddExecutionBreadcrumb(task, message)`；
  `GraphViewModel::onExecutionEvent` 的 `TaskFailed` 分支上报任务名与失败原因。
- 以上均为 no-op 守卫（未 init 直接返回），仅普通执行路径调用（非信号上下文）。

## 本地验证

- **冒烟脚本**：`python scripts/verify_crash_reporting.py`——自动拉取 sentry-native
  （可选）、构建、用指向不可达地址的 dummy DSN 离线初始化、offscreen 运行
  `--test-crash`，断言 sentry 数据库目录生成 `*.dmp`（临时目录重定向，不碰开发者
  本地真实 sentry_db）。不依赖真实 Sentry 项目即可端到端验证「捕获 → 落盘」。
- **手工验证真实上报**：`SENTRY_DSN=<dsn> graph_studio --test-crash`，
  预期 Sentry 出现带符号化堆栈的 crash issue（先上传符号，见下）。

## 符号

- **上传**：`python scripts/upload_sentry_symbols.py`（需 `sentry-cli` +
  `SENTRY_AUTH_TOKEN`，可选 `SENTRY_ORG`/`SENTRY_PROJECT`）。三平台：
  macOS 上传 dSYM（缺失时 dsymutil 现生成）、Windows 上传 PDB、Linux 上传 ELF
  调试信息；覆盖 app + libtask_graph + subnode 插件（崩溃可能发生在任意 SO）。
- **归档兜底**：CI Release 每次都把三平台符号打成 `graphstudio-symbols-*.zip`
  附到 GitHub Release 资产（dSYM / PDB / `objcopy --only-keep-debug` 拆出的
  .debug），即使不用 Sentry 也能离线符号化 minidump。
- 发布构建统一 **RelWithDebInfo**（-O2 仍含 -g），保证符号可用。

## CI 接入（release.yml，条件启用）

配置以下 repo secrets 后自动生效；未配置时构建行为与无 Sentry 完全一致：

| Secret | 作用 | 缺失时行为 |
|---|---|---|
| `SENTRY_DSN` | 编译期嵌入发布包 | 三端传 `--no-sentry`，不构建 sentry-native |
| `SENTRY_AUTH_TOKEN` | sentry-cli 符号上传认证 | 跳过符号上传（符号 zip 仍归档到 Release） |
| `SENTRY_ORG` / `SENTRY_PROJECT` | 可选，sentry-cli 目标 | 走 sentry-cli 默认配置 |

- 发布 tag 在 `meta` job 提前计算，完整渠道版本传给三端打包脚本
  （`--sentry-release` / `-SentryRelease`），Sentry release 与 GitHub tag 一致。
- `sentry-native` 克隆重（递归子模块），按 `fetch_sentry.py` 内容哈希用
  `actions/cache` 缓存。
- 打包脚本：macOS `package_macos.py`、Linux `package_linux.py`
  （`--no-sentry` / `--dsn` / `--sentry-release`，AppDir 自动带入 crashpad_handler）、
  Windows `build_msix.ps1`（`-SkipSentry` / `-SentryDsn` / `-SentryRelease`）。

## 已知限制与后续工作

- **MSVC 下 `raise(SIGSEGV)` 不会产生 minidump**：CRT 默认处理是直接 `_exit(3)`，
  根本没有 SEH 异常，crashpad 捕获不到。因此 `TriggerTestCrash()` 在 Windows 上
  用空指针解引用触发真实 `EXCEPTION_ACCESS_VIOLATION`（volatile 防优化删除），
  POSIX 上仍用 `raise(SIGSEGV)`。这是本地实测发现的行为差异。
- **macOS 沙箱 / App Store 分发**：沙箱内不能 spawn crashpad 子进程 handler，
  届时需切 sentry-native `native`/`inproc` 后端（未做，当前分发渠道是 dmg 不受影响）。
- **Windows 主线程栈溢出**可能捕获不到，需 thread stack-guarantee 变通（未做）。
- **WASM/Web 在线版无崩溃收集**：sentry-native 不支持 wasm。后续可用 emscripten
  `-gseparate-dwarf`/sourcemap + 页面 JS 侧 `onAbort`/`unhandledrejection` 捕获 +
  Sentry JS SDK，是另一条独立链路。
- **非崩溃事件不上报**：`SENTRY_TRANSPORT=none` 下 crash 上传由 crashpad_handler
  完成，但消息类事件（`sentry_capture_event`）不会发送；若要统计任务级失败需切
  curl transport（未做）。
