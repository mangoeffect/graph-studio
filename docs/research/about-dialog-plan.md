# GraphStudio Help 菜单 About 信息汇总方案（调研）

> **状态：方案 B 已实施**（2026-09）：CMake 元信息宏已提通用、Help 菜单已拆三项、
> `AboutDialog` 已落地（含 Copy build info），`test_gui` 新增 `testAboutDialogBuildInfo`，
> offscreen UI 测试通过。方案 C（应用元数据/`setApplicationVersion`）未实施。

日期：2026-09 · 范围：`app/graph_studio` 桌面端（含 WASM 同源路径）

## 1. 现状

- **Help 菜单已存在**：`app/graph_studio/src/view/MainWindow.cpp:451`（`CreateMenuBar()`），
  仅一个 `About` action，弹 `QMessageBox::about`，内容是**硬编码纯文本**，
  且把"操作说明（Controls）"和 About 混在同一个弹窗里。
- **无版本信息**：弹窗里没有版本号、git hash、构建类型、Qt 版本、任务/插件统计。
- **未设置应用级元数据**：没搜到 `QApplication::setApplicationVersion /
  setOrganizationName / setApplicationName` 调用（影响 About 的 Qt 惯例取数，
  也影响 macOS 自动迁移到应用菜单的命名）。

## 2. 现成可复用的数据源（重要：都在）

| 信息 | 来源 | 位置 |
|---|---|---|
| 版本号 | `GRAPH_STUDIO_SENTRY_VERSION` 编译宏（默认解析根 `project()` VERSION，发布可覆盖） | `app/graph_studio/CMakeLists.txt:171-184` |
| git 短哈希 | `GRAPH_STUDIO_SENTRY_BUILD_HASH`（execute_process git rev-parse） | 同上 :185-192 |
| 构建环境 | `GRAPH_STUDIO_SENTRY_ENVIRONMENT`（production/development） | 同上 :193-202 |
| release 字符串拼装 | `CrashReporter.cpp::get_release()`（`task_graph@<ver>#<hash>`） | `app/graph_studio/src/CrashReporter.cpp:100-111` |
| 已注册任务列表/数量 | `PluginRegistry::instance().available_tasks()` | `src/PluginBootstrap.cpp:96-100`（WASM 路径已在用） |
| 已加载插件名列表 | `PluginBootstrap` 的 `PluginLoadResult.loaded`（QStringList） | `src/PluginBootstrap.cpp:85+` |
| 崩溃上报开关 | `#ifdef GRAPH_STUDIO_HAS_SENTRY` | CMakeLists :103 |
| Qt / 系统 / 架构 | `qVersion()`、`QSysInfo::prettyProductName()`、`QSysInfo::buildCpuArchitecture()` | 运行时，零成本 |
| 官网 / 仓库 | `https://studio.mangoeffect.net/`、`github.com/mangoeffect/graph-studio` | 已知 |

**关键坑**：`GRAPH_STUDIO_SENTRY_VERSION / BUILD_HASH / ENVIRONMENT` 三个宏
**只在 `if(GRAPH_STUDIO_HAS_SENTRY)` 块内定义**（CMakeLists :225-227）。
本地没跑 `scripts/fetch_sentry.sh` 时整个块被跳过——About 若直接引用这些宏会编译失败。
需把"版本 + git hash + 环境探测"这段 CMake **提升到 Sentry 块之外**（可改名为
`GRAPH_STUDIO_VERSION / GRAPH_STUDIO_GIT_HASH` 通用宏，Sentry 侧引用同一变量），
About 才能脱离 Sentry 依赖工作。Sentry 宏名是否保留兼容：`CrashReporter.cpp`
可以改引新宏，也可让 CMake 同时输出两套宏，二选一，推荐直接迁移并改引用。

## 3. 方案对比

### 方案 A：增强现有 QMessageBox（最小改动）
保持 `QMessageBox::about`，把文本扩成：应用名+版本+hash+环境+Qt 版本+任务数+官网链接。
- 优点：~30 行改动，无新文件；WASM 零风险。
- 缺点：纯文本排版简陋；链接需 `setTextFormat(Qt::RichText)` 才可点；
  "Controls 操作说明"仍和 About 混在一起。

### 方案 B：拆分 + 独立 AboutDialog（推荐）
1. **拆项**：Help 菜单改为 `About Graph Studio`、`Controls / Shortcuts`、（可选）`About Qt`。
   - 操作说明移到 `QMessageBox::information`（或后续做成 What's This / 快捷键表）。
   - `helpMenu->addAction(helpMenu->addAction("About Qt")...)` 用 `QMessageBox::aboutQt(this)`，
     Qt 原生入口，顺带展示 Qt 许可信息。
2. **新建 `app/graph_studio/src/view/AboutDialog.cpp/.hpp`**（或先做 MainWindow 内的
   `showAbout()` 私有函数）：`QDialog` + `QLabel(richText)` + logo（可复用
   `docs/static/img/og-image.png` 系图标，拷入 app 资源）+ 信息表：
   - Version：`GRAPH_STUDIO_VERSION (GRAPH_STUDIO_GIT_HASH, env)`
   - Qt：`qVersion()`；OS：`QSysInfo::prettyProductName()`；Arch
   - Tasks：`PluginRegistry::instance().available_tasks().size()`
   - Plugins：`PluginLoadResult.loaded.join(", ")`（需 MainWindow 已持有或经
     ViewModel 暴露——PluginBootstrap 结果目前只在启动日志消费，需加一条传递路径）
   - Crash reporting：`#ifdef GRAPH_STUDIO_HAS_SENTRY → enabled/disabled`
   - 链接行：官网 + GitHub（`QLabel::setOpenExternalLinks(true)`；WASM 上点外链
     会新开标签，行为正确）
   - 底部加 `Copy build info` 按钮（`QClipboard`），方便用户报障时贴环境——
     与 Sentry 用户反馈场景互补。
- 优点：结构化、可测试（offscreen UI 测试可断言 label 文本含版本号）、
  符合仓库"GraphStudio 桌面编辑器"的产品化方向。
- 成本：新文件 + CMake 源列表 + PluginBootstrap 结果传递（一个小接口改动）。

### 方案 C：Qt 应用元数据 + 平台惯例（与 B 叠加的收尾项）
- `main()`（或 entry.cpp 构建 QApplication 后）调用
  `setApplicationName("Graph Studio") / setApplicationVersion(GRAPH_STUDIO_VERSION) /
  setOrganizationName("mangoeffect")`。
- About action 设 `QAction::setMenuRole(QAction::AboutRole)`：macOS 上自动从
  Help 菜单迁移到**应用菜单**（平台惯例），Windows/Linux 仍在 Help 下——
  单行代码获得平台原生行为。
- 附带收益：`QMessageBox::about` 默认用 applicationName 做窗口标题；
  WASM 端 applicationName 也影响浏览器存储键名，统一有长期价值。

## 4. 建议落地顺序

1. CMake：版本/git-hash/环境宏提出 Sentry 块，改通用名（Sentry 侧同步改引用）。
2. MainWindow：Help 菜单拆 `About Graph Studio` / `Controls` / `About Qt`；
   About action 设 `AboutRole`。
3. AboutDialog（方案 B 结构），首版可先不含插件列表（数据传递可后补），
   必含：版本、hash、环境、Qt、OS/arch、任务数、崩溃上报状态、官网/仓库链接、
   Copy build info。
4. UI 测试：`scripts/run_ui_tests.sh`（offscreen）加用例：触发 About，
   断言版本号与编译宏一致。
5. `main()` 设置应用元数据（方案 C）。

## 5. 风险与注意

- **宏依赖**：见 §2 关键坑——不先做第 1 步，方案 B/C 均无法在无 Sentry 环境编译。
- **WASM**：QDialog/QClipboard 均可用（剪贴板经 JS bridge），无平台分支需求；
  保持单一代码路径（仓库既有约定）。
- **本地化**：现有菜单均为英文硬编码，About 首版保持英文一致，不单独引 i18n。
