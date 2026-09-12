# GraphStudio macOS 安装态 E2E（AXAPI + CGEvent）

镜像 `dev-docs/e2e-windows-installed.md`（pywinauto 方案）的 macOS 对应物：
对**真实 .app**（dev 构建或 dmg 安装产物）做端到端黑盒——原生菜单、
真实鼠标键盘事件、AX 控件树断言。图文件输入走 **CLI 启动参数**
（`--open <graph.json> [--run]`），不自动化 NSOpenPanel（见下文教训）。

## 选型

| 方案 | 结论 |
|---|---|
| **ctypes 直调 AXAPI/CGEvent（本方案）** | 零第三方依赖；新版 macOS 各 Python 发行版（含系统 python3）均不再预装 pyobjc，PEP 668 又挡 pip 装包 |
| pyobjc / atomacos | 同一套 AXAPI，但依赖装不上（上条）；atomacos 维护一般 |
| XCUITest | 需为 CMake Qt 工程引入 Swift/Xcode test target，脱离仓库 Python 脚本体系 |
| Squish | 商业 license（约 €3k+/席/年），且仓库无此预算 |

AX 断言/AXPress 动作由**目标 app 进程执行**（AXUIElementPerformAction），
基本不触发 TCC；只有 **CGEvent 注入**（画布拖拽、组合键）需要运行方持有
辅助功能权限。

## 图输入通道：CLI `--open/--run`（为什么不做 NSOpenPanel）

app 侧（`entry.cpp` + `MainWindow::OpenGraphAtStartup`）：

- `--open <graph.json>`：启动后（`QTimer::singleShot(0)`，等事件循环/
  日志面板就绪）打开图；失败只走 `loadFromFile` 的日志路径 + stderr
  一行 `[gs] --open failed:`，**无弹窗**。macOS argv 恒 UTF-8，中文路径
  用 `QString::fromUtf8`。
- `--run`：打开成功后立即 `ActionRun()`（= `vm_.execute()`）。

驱动侧（`scenarios.cli_graph_case`）：每张图**冷启动一个干净进程**
（`posix_spawn` + stdout/stderr dup2 到 `app_logs/*.log`）→ 等窗口标题含
文件名（= 打开成功）→ 可选 countsLabel 校验 → 轮询镜像日志等
`Execution finished` → SIGTERM 收尾。批跑期间**零输入注入**，无键盘焦点
竞态；进程隔离还消除了图与图之间的状态残留。

### 历史教训：NSOpenPanel 自动化为什么被放弃

最初 files/run 场景走 File > Open → NSOpenPanel（`Cmd+Shift+G` go-to
sheet 输路径），整条链路全是时序雷，最终放弃：

- 键入必须 `CGEventPostToPid`（pid 定向）——全局 HID tap 受焦点竞态
  影响会丢字符；
- **Return 不提交 go-to sheet**（补全弹层吞键）——「前往」按钮只能真实
  鼠标点（按面板几何推算右上角）；Go 只导航到目录**不选中文件**；
- 面板文件列表懒加载：大目录的行不在 AX 树里，深层 `panel.walk()` 在
  大目录上慢到像挂死（只允许 children 两级浅查按钮）；
- 双击确认受合成点击怪癖影响（`kCGMouseEventClickState`），最终用
  单击选中 + Open 按钮 AXPress。

## 执行完成断言：stderr 镜像文件（为什么不 AX 读 Log Panel）

`MainWindow::onLogMessage` 有 `qInfo() << "[gs]"` 镜像（WASM 进浏览器
console、桌面进 stderr，两端 E2E 的统一断言通道）。驱动把子进程
stdout/stderr dup2 到文件后轮询 `Execution finished: N ok, M failed`。

不能 AX 读 Log 面板的原因：`onExecutionFinished` 会把底栏**切到 Profile
页**，QTabWidget 隐藏页的控件随即从 AX 树消失——执行一结束 Log Panel
就找不到了（`find_by_description` 报"找不到日志面板"，极具迷惑性：
窗口对、countsLabel 同窗口能找到）。stderr 镜像不受 tab 切换影响，
轮询也比 AX roundtrip 快。

## 锚点（MainWindow 为外部自动化显式准备）

`MainWindow.cpp` 注释 "供外部 UI 自动化（E2E）稳定定位"：
- accessibleName：`Task Library` / `Log Panel` / `Output Panel` /
  `Image Results`（**Qt 把 accessibleName 映射到 AXTitle**，个别角色落在
  AXDescription——查找时两者都匹配，见 `find_by_description`）
- `Graph Canvas`：**QGraphicsView 本体不进 AX 树**（plain QWidget 容器
  也被剪），包一层 flat QGroupBox（`canvasHost_`）才暴露为 AXGroup
- countsLabel 文本 `Nodes: N | Edges: M`（AXStaticText 的 AXValue；
  必须是永久控件——`showMessage` 的瞬态 QLabel 不进树）
- 窗口标题 `<file> - Graph Studio`

## Qt → macOS AX 实测事实（ax_driver.py 的设计依据）

- **原生 menubar（QMenuBar → NSMenu）的 AXMenuItem 上 AXPress 返回成功
  但不触发 Qt 动作**——一律真实鼠标点项中心；顶层项的菜单挂在其
  AXChildren（无 AXMenu 属性）。
- 上下文菜单（QMenu）是 **AXWindow** 弹层 + AXMenuItem 子项；子菜单要
  mouse_move 悬停才展开；残留的模态 QMenu 会吞掉后续输入，失败路径要
  ESC 循环清场（`_popup_menu_windows`）。
- 合成双击必须 `CGEventSetIntegerValueField(ev, kCGMouseEventClickState, 2)`，
  否则 AppKit 当两次单击。
- `CFArrayGetCount` 打在非数组 CFType 上是 **NSException 直接杀进程**
  （曾因 AXMenuBar 是单元素而非数组崩溃）——数组展开一律先过
  `CFGetTypeID` 类型闸（`_iter_cf_array`）。
- `AXIsProcessTrusted()` 无副作用可探测；手搓 CFDictionary 调
  `AXIsProcessTrustedWithOptions` 会**段错误**（不要 retry）。

## 场景（scripts/e2e_macos/scenarios.py）

| 场景 | 内容 |
|---|---|
| `core` | 右键画布 → 上下文菜单建 2 节点（Input/opencv_image_read + OpenCV Filter/opencv_blur_filter）→ 端口 CGEvent 拖线（node 宽 140，端口在中心 ±70）→ countsLabel 断言 → Cmd+Z / Cmd+Shift+Z |
| `files` | **全部子模块单测图逐张 `--open --run` 冷启动执行**（覆盖面对齐 e2e_windows/scenarios_files.py）：枚举 `submodules/**/tests/graphs/*.json`（68 张/10 模块，js_error.json 反例除外），图+资产复制到运行目录（writer 输出不污染仓库）→ 标题/计数校验 → 镜像日志断言 0 failed。skip 规则：夹具资产缺失（gpu rgba.png、video_io 合成视频、render shaders）。每张图一个用例 `files/graph:<module>/<name>` |
| `run` | 单图冒烟（生成的 tiny png 图）`--open --run` → `Execution finished: 1 ok, 0 failed` |
| `lifecycle` | Cmd+Q → 进程正常退出（exit 0） |
| `crash` | `--test-crash` 独立进程，预期 SIGSEGV（镜像 verify_crash_reporting 的语义） |

进程管理在入口脚本的 `AppRunner`：`start(*args, log=)`（可选日志重定向）、
`stop()`（SIGTERM → 超时 SIGKILL + waitpid 收僵尸）。批跑用 SIGTERM 而非
Cmd+Q：不产生 crashpad minidump（sentry-native 默认不挂 SIGTERM
handler）、不依赖焦点；优雅退出路径由 lifecycle 场景单独覆盖。批跑前必须
`stop()` 交互实例——两个 GraphStudio 抢焦点会让注入落到错误实例上。

发现/落位逻辑在 `scripts/e2e_macos/graph_cases.py`（Windows 版的差异：
gpu 模块按路径前缀分类——其叶子目录名是 image_processing；macOS 默认跑
全部图，Windows 默认 10 张）。用例级会话助手（status_counts/wait_finished/
state_snapshot）在 `scripts/e2e_macos/session.py`。

GPU allow-fail：CGEvent 注入已要求真实 GUI 会话（Metal 必在），且
GpuBootstrap 的初始化日志早于 VM 日志 sink 注册、镜像里看不到，故不做
Windows 式日志探测；无 GPU 环境设 `TG_E2E_MACOS_ALLOW_GPU_FAIL=1`
把 gpu 图降级为执行完成即通过。

## 运行

```bash
python3 scripts/run_e2e_macos.py                       # dev .app 全场景（files 全部图）
python3 scripts/run_e2e_macos.py --scenario core,run
python3 scripts/run_e2e_macos.py --max-graphs 10       # files 裁剪（read_image/unicode 优先，跨模块轮转）
python3 scripts/run_e2e_macos.py --graphs mediapipe    # files 按路径子串过滤
python3 scripts/run_e2e_macos.py --app /Applications/GraphStudio.app
```

dev .app 需要插件时入口脚本自动设 `TASK_GRAPH_PLUGINS_PATH` /
`GRAPH_STUDIO_MODELS_DIR` / `DYLD_LIBRARY_PATH`（复刻 run_graph_studio.py；
mediapipe 插件对裸 install name 的 libvision.dylib 靠自身 LC_RPATH 解析）。

## 报告（scripts/e2e_macos/report.py，对齐 e2e_windows/report.py）

结果落 `dist/e2e_macos/<时间戳>/`（`--artifacts` 改根目录）：

- `summary.json` — 机器可读：meta + 每用例 status/detail/duration/
  app_state/artifacts/traceback；
- `summary.md` — 人读：meta 表 + 结果汇总 + 用例总表 + 失败明细
  （失败原因/调用栈/失败时应用状态含日志尾部/关联工件）+ 跳过项；
- `submodule_graphs/` — files 场景的图与资产副本（writer 输出也在这里）；
- `app_logs/` — 每张图的子进程 stderr 镜像（完成断言 + 失败诊断）；
- `*_failure.png` — `screencapture -x` 失败现场截图。

退出码 0 = 全部通过；1 = 有失败；2 = 终端无辅助功能权限。

## 已知限制

- 画布端口坐标按 NodeItem 固定几何（宽 140）与默认缩放 1.0 推算；改画布
  默认缩放/节点尺寸需同步 `scenarios.NODE_WIDTH`。
- 上下文菜单依赖插件加载（无插件时任务类型不存在，core 场景建不了节点）。
- `--open/--run` 的启动期行为依赖 `QTimer::singleShot(0)` 时序（事件循环
  起来即开），窗口极慢加载的机器上标题断言有 15s 超时兜底。
- 屏幕录制权限（screencapture 截图）是另一项 TCC，首次会弹窗。
