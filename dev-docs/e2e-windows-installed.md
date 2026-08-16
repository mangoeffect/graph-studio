# GraphStudio Windows 安装态 E2E 自动化测试

> 开发者文档。记录「用户拿到 MSIX 安装包 → 安装 → 像真人一样使用 → 卸载」链路的
> 自动化测试方案：选型结论、harness 架构、场景与断言通道、运行方式、已知限制与
> CI 接入路径。现有进程内 Qt Test（offscreen、`scripts/run_ui_tests.py`）不覆盖
> 本层——它们验证控件行为，本层验证**发布产物在真实 Windows 会话里的表现**。

## 目标与非目标

**目标**

- 测的是**安装后的产品**：从 `scripts/build_msix.ps1` 产出的签名 MSIX（或指定路径
  的现成包）经 `Add-AppxPackage` 安装，从 `shell:AppsFolder` 启动——与用户路径一致。
- **真实输入**：外部进程注入真实鼠标/键盘事件（非 Qt 合成事件、非 offscreen），
  应用窗口真实渲染在交互式桌面上。
- **黑盒断言**：只通过 UIA 可见文本（状态栏/标题/树/下拉）、磁盘工件（graph.json、
  `*.dmp`）与进程退出码判定结果，不依赖任何进程内钩子。
- 本地优先：`python scripts/run_e2e_windows.py` 一条命令在开发机上完成
  构建→安装→场景→卸载→报告。

**非目标（现状不覆盖，列为后续项）**

- WASM / macOS / Linux 安装态测试（本文只管 Windows）。
- 性能/压力测试、多语言系统矩阵（UI 本身英文硬编码，但原生文件对话框会本地化，
  见「已知限制」）。
- 视频流（stream mode）场景：夹具需要视频文件，待第二批加入。

## 选型结论

| 方案 | 结论 | 理由 |
|---|---|---|
| **pywinauto（UIA 后端）** | **采用** | 纯 Python 与仓库脚本体系一致（唯一 pip 依赖 `pywinauto`+`pillow`）；0.6.9（2025-01）支持 Win11；UIA 元素定位 + 真实输入注入一体；免费开源 |
| WinAppDriver + Appium | 不采 | 底层 WinAppDriver 最后版本 1.2.1（2020-11）后停更，社区公认弃维护；还需额外服务进程 |
| FlaUI (.NET) | 不采 | 活跃维护、API 现代，但给 C++/Python 仓库引入 C# 工具链，维护面变大 |
| Squish for Qt | 不采 | 对 Qt 信号槽级断言最强，但商业授权成本高；黑盒文本通道已够用 |

执行环境结论：**GitHub 托管 Windows runner 作业运行在 Session 0 非交互桌面**
（[runner-images #7227](https://github.com/actions/runner-images/issues/7227) 黑屏/UIA
找不到窗口），真实输入保真度不可靠。因此本地交互式会话为主，CI 见文末
「CI 接入路径」。

## 架构

```
scripts/run_e2e_windows.py        # 入口：构建（可选）→ 安装 → 场景 → 卸载 → 报告
scripts/e2e_windows/
  msix.py                         # 包管理适配：信任证书 / Add-AppxPackage / 查询 / 卸载
  app.py                          # AppSession：启动/附加、UIA 查询、真实输入、画布几何
  fixtures.py                     # 自包含夹具：graph.json + 内嵌生成的小 PNG
  report.py                       # 运行目录 / 失败截图 / UIA 树转储 / 汇总
  scenarios_core.py               # 核心编辑+执行流
  scenarios_files.py              # 文件打开 / 拖拽打开 / 保存往返
  scenarios_crash.py              # 安装态崩溃上报（--test-crash）
  scenarios_lifecycle.py          # 覆盖安装升级 / 卸载清理
  requirements.txt                # pywinauto>=0.6.9, pillow
```

运行产物在 `dist/e2e/<时间戳>/`（`dist/` 已 gitignore）：夹具、保存的图、失败截图、
UIA 树转储、`summary.json`。

### 关键机制

- **安装与信任**：签名证书导入 `Cert:\LocalMachine\TrustedPeople`（需管理员）。
  证书优先取包旁导出的 `<Publisher>.cer`（`build_msix.ps1` 自动导出），缺失则用
  `Get-AuthenticodeSignature` 从包签名提取。无管理员权限时降级
  `Add-AppxPackage -Register dist\msix\staging\AppxManifest.xml`（免签名免管理员，
  二进制与清单相同但跳过签名/包完整性验证，报告中标注 register 模式）。
- **启动**：交互场景用 `explorer.exe shell:AppsFolder\<PFN>!GraphStudio`（等价开始菜单
  启动；应用无 AppExecutionAlias，不能裸命令行启动）。需要传进程环境变量的场景
  （崩溃上报的 `SENTRY_DSN`）直接执行 `<InstallLocation>\graph_studio.exe`——
  full-trust 打包应用可直接运行，包身份（含 MSIX 文件系统虚拟化）随进程令牌生效。
- **画布几何**（源码常量，`NodeItem.cpp:13-16`）：节点 140×70、坐标系以节点**中心**
  为原点（`onTaskAdded` 直接 `setPos(x,y)`）、单端口节点输入/输出端口在中心
  `(-70,0)`/`(+70,0)`、命中半径 14px。右键菜单在右键点创建节点
  （`GraphScene::contextMenuEvent` → `nodeCreateRequested(name, scenePos)`），因此
  **右键屏幕坐标 = 节点中心屏幕坐标**（视图变换不变时），端口坐标 = 中心 ±70px。
  两个节点放在画布中心左右各 ~180px（场景矩形远小于视口，无滚动条、变换保持
  恒等），端口连线用真实鼠标分步拖拽（`GraphScene` 要求 press/move/release 且
  move ≥1 步）。
- **文件拖拽**：真实 Explorer OLE 拖放（详见下文「文件拖拽」节；Qt6 忽略
  WM_DROPFILES，合成消息无效——实测结论）。
- **原生文件对话框**：File>Open / Save As 弹出的是系统 IFileDialog（类 `#32770`）。
  自动化方式：定位文件名 Edit → 真实键盘输入完整路径 → 回车确认。**不点击任何
  按钮**——按钮文本随系统语言变化（zh-CN 为「打开/保存」），输路径+回车是
  locale 无关的。
- **DPI**：harness 启动时 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`，
  所有坐标使用物理像素；附加窗口后把窗口规整到固定几何（1440×900，屏幕内）。

### 断言通道（多通道冗余，任一可读即可判）

| 通道 | 例子 | 说明 |
|---|---|---|
| 状态栏文本 | `Nodes: 2  \|  Edges: 1`、`Zoom: 100%` | `statusBar_->showMessage` + 永久 zoomLabel（已加 accessibleName） |
| 窗口标题 | `blur_graph.json - Graph Studio` | `UpdateWindowTitle` |
| 任务库 UIA 树项 | `opencv_image_read` 等 | 证明安装态从 `PlugIns\` 成功加载插件 |
| 结果下拉条目 | `blur (opencv_gaussian_blur_filter)` | `RebuildResultSelector` 格式 `nodeId (type)` |
| Run/Stop 按钮使能 | 执行期间 Run 禁用 / Stop 启用 | 异步执行的同步原语 |
| 磁盘工件 | 保存的 graph.json（stdlib json 校验）、`*.dmp` | |
| 进程退出码 / stderr | `[CrashReporter] initialized` | console 子系统 exe，stdout 可见 |

日志面板（logPanel QPlainTextEdit）的 UIA 文本可读性在试点中验证；可用则作为
「Execution finished: N ok, M failed」的主断言，否则以上表为冗余组合。

### 应用侧可测试性改进（已随本方案落地）

外部 UIA 依赖控件可辨识。给关键控件补了 `setObjectName` + `setAccessibleName`
（`MainWindow.cpp`）：`taskLibrary`/`Task Library`、`graphView`/`Graph Canvas`、
`logPanel`/`Log Panel`、`outputPanel`/`Output Panel`、`resultSelector`/`Image Results`、
`zoomLabel`。纯标识用途，无行为变化；注意勿用样式表已引用的 `ImageResultLabel` 名。

## 插件用例注册表（core 场景数据驱动）

core 场景的交互代码是统一驱动器（建节点→按标签设参数→端口连线→Run→断言），
插件用例全部是数据：`scripts/e2e_windows/plugin_cases.py` 的 `CASES` 列表。
**新增一个插件子类的用例 = 加一条 `PluginCase`**（分类、类型、参数表、连线、
期望断言），零交互代码。占位符：`$ASSET`（夹具图片）、`$JS`（内嵌 JS 脚本）、
`$OUT/xxx`（输出目录前缀）。

首批 8 用例覆盖全部分类（参数控件类型覆盖 int SpinBox / float DoubleSpinBox /
string / file 行）：input_filter、filter_chain、geometry_resize、color_cvt、
color_grade_bcs、gpu_grayscale（无 GPU 后端自动 skip）、scripting_js、
output_write（断言执行产物落盘）；末尾统一附 `core/save_roundtrip`（Save As
原生对话框 → 落盘 JSON 与画布往返校验）。

## 子模块夹具发现规则（files 场景）

- 枚举 `submodules/**/tests/graphs/*.json`（排除故意失败的 `js_error.json`）；
- 相对路径参数按图目录的 1~3 级祖先探测（夹具布局：`<tests>/graphs/*.json` +
  `<tests>/{data,models,scripts}/…`；写出型任务（`*_write`/`video_writer`）的
  路径参数视为输出不要求预存在）；资产缺失 → skip 并注明（video_io 的
  `synth.mp4` 由测试驱动生成，属预期跳过）；
- 图与被引用文件复制到运行目录（扁平布局）再打开——输出写进副本不污染仓库；
- 默认纳入 10 个图（`--max-graphs` 调整，`--graphs` 按路径过滤），代表性小图
  优先 + 跨模块轮转补足，保证每个子模块至少一图；
- mediapipe 图（安装包内为 stub 构建）与无 GPU 时的 gpu 图按 allow-fail 处理
  （执行完成即通过，附注实际 ok/failed）。

## 文件拖拽：真实 Explorer OLE 拖放

Qt6 窗口注册为 OLE drop target，**忽略 WM_DROPFILES**（实测验证）——合成
`WM_DROPFILES` 无效。拖拽场景走最高保真路径：`explorer.exe /select,<file>`
打开真实资源管理器 → UIA 定位文件项（注意 Explorer 默认隐藏扩展名，按 stem
匹配）→ 真实鼠标慢拖到画布（OLE 协商）→ 断言标题/计数；只清理本次新开的
Explorer 窗口（按 HWND 差集，不动用户已有窗口）。

## 报告结构

每条用例（`core/<case>`、`files/open:<graph>`…）独立记录；失败自动采集
截图 + UIA 树转储 + 应用状态快照（标题 / Nodes-Edges / 结果下拉 / Run 按钮态 /
日志尾 30 行）+ 完整 traceback。`finish()` 产出：

- `summary.json` — 机器可读全量结果；
- `summary.md` — 人读：元信息表 → 用例总表（状态/耗时/说明）→ **失败明细**
  （失败原因 + 调用栈 + 失败时应用状态 + 日志尾 + 关联工件清单）→ 跳过项及原因。

## E2E 实测发现并已修复的应用缺陷（2026-08-16）

1. **js_script 节点 GUI 不可配置**：`paramSpecs_` 只在 `on_init()`（实例化）
   填充，GUI 属性面板查询注册表原型拿到空列表 → Parameters 面板永远为空，
  用户无法在界面给 js_script 设置 script_path（先有鸡后有蛋）。修复：
   `JsTask::param_specs()` 在 specs 为空时静态返回 script_path。
2. **另存后窗口标题不更新**：`ActionSaveAs` 设置 `currentFilePath_` 后未调
   `UpdateWindowTitle()`（与 Open 行为不一致）。修复：补调用。

## 场景清单

| 场景 | 步骤要点 | 关键断言 |
|---|---|---|
| **core** 核心编辑+执行流 | 安装→shell 启动→右键菜单建 `opencv_image_read`/`opencv_gaussian_blur_filter` 两节点→端口拖拽连线→选节点设 `file_path` 参数→工具栏 Run→Save As 原生对话框 | 任务库含插件类型；状态栏 Nodes/Edges 递增；属性面板 ID=`opencv_image_read_1`；Run 完成后结果下拉含 blur 条目；保存的 JSON 含 2 任务 1 边与 file_path |
| **files** 文件打开/拖拽打开 | File>Open 原生对话框逐个打开子模块图（默认 10 个跨模块均衡）→Run；真实 Explorer 拖拽打开 3 个代表图（含中文资产名） | 标题/Nodes/Edges；Run 后结果下拉有条目（证明 `assets/test.png` 相对路径被正确解析） |
| **crash** 安装态崩溃上报 | 直启安装态 exe `--test-crash` + 不可达 dummy DSN | 退出码非 0；stderr 含 `[CrashReporter] initialized`；两处候选 `sentry_db`（真实 `%LOCALAPPDATA%\GraphStudio\` 与 MSIX 虚拟化 `%LOCALAPPDATA%\Packages\<PFN>\LocalCache\Local\GraphStudio\`）任一出现 `*.dmp` |
| **lifecycle** 升级/卸载清理 | （崩溃场景留下包内工件后）`Add-AppxPackage` 覆盖安装更高版本→启动验证→`Remove-AppxPackage` | 升级后工件保留、应用可启动；卸载后 `Get-AppxPackage` 为空、安装目录与 `Packages\<PFN>` LocalCache 消失 |

夹具由 `fixtures.py` 自包含生成（16×16 纯色 PNG 用 zlib/struct 现场构造，不依赖
仓库 submodule 布局），graph.json 使用与 `submodules/**/tests/graphs/` 相同的
version 2.0 格式（`from`/`from_port`/`to`/`to_port`）。

## 运行方式

```text
# 完整流程（构建签名包 → 安装 → 全场景 → 卸载；需交互式桌面会话）
python scripts/run_e2e_windows.py

# 测现成包 / 过滤场景 / 保留安装与现场
python scripts/run_e2e_windows.py --msix dist\msix\graph_studio-0.1.0.0_x64.msix
python scripts/run_e2e_windows.py --only core,files
python scripts/run_e2e_windows.py --keep-installed --keep-output

# 无管理员权限（跳过签名信任，走 staging Register 模式）
python scripts/run_e2e_windows.py --register

# 升级场景指定两个版本（缺省时自动用 -SkipBuild 重打包一个更高版本号）
python scripts/run_e2e_windows.py --old-msix a.msix --new-msix b.msix
```

依赖：`pip install -r scripts/e2e_windows/requirements.txt`（pywinauto、pillow；
harness 启动时检测并给出提示）。

## 已知限制与排障

- **会话要求**：真实输入需要已连接的交互式桌面。本地控制台会话即可；**RDP 断开
  会使桌面消失导致输入失效**——通过 RDP 跑 E2E 时保持会话连接（或用 `tscon`
  重定向到 console）。
- **管理员权限**：`LocalMachine\TrustedPeople` 导入需要管理员。非提权 shell 会
  自动降级 register 模式（或显式 `--register`）；提权运行则走真实安装路径。
- **comtypes 缓存**（pywinauto 常见故障）：UIA 调用爆 `COMError`/`AttributeError`
  时先清缓存：`clear_comtypes_cache.exe remove` 或删除
  `%TEMP%\comtypes_cache`（见 pywinauto [#1125](https://github.com/pywinauto/pywinauto/issues/1125)）。
- **异步等待**：一律 `wait_until` 轮询（默认 10-60s 超时），信号用按钮使能态/
  状态栏文本/下拉条目，不做固定 sleep。
- **原生对话框**：只用「输入完整路径+回车」，规避本地化按钮；保存一律用新文件名
  规避覆盖确认框（该框按钮也是本地化的）。
- **GPU 缺失（VM）**：`InitGpuBackend` fail-open 仅 WARN；图像结果经 Qt RHI 走
  D3D11，无 GPU 的 Windows VM 也能渲染。Vulkan 后端缺席不影响 opencv 类节点。
- **防火墙/SmartScreen**：自签名包首次运行如遇 SmartScreen 提示——安装路径经
  `Add-AppxPackage`（信任后）不会出现；register 模式同。若手动下载 release 包
  安装则属于用户教育范畴，不在自动化内。

## CI 接入路径

**现状（预留）**：`.github/workflows/e2e-windows.yml` 仅 `workflow_dispatch` 手动
触发，验证「构建→（register 模式）→ 场景→报告上传」链路在 CI 环境的可运行性。

**托管 runner 限制**：GitHub 托管 Windows runner 作业在 Session 0 非交互桌面，
真实鼠标/键盘与窗口渲染不可靠（黑屏、UIA 找不到窗口，runner-images #7227）。
若必须在托管 runner 上跑，只能覆盖「无输入注入」的子集（崩溃上报、文件工件
断言），UI 场景应跳过。

**推荐 CI 形态**：自托管 Windows VM runner——自动登录到交互式会话后以
`WindowsService` 之外的方式（`config.cmd --run as service` 换成开机启动交互进程）
运行 runner；workflow 里提权安装证书 + `Add-AppxPackage`，跑全量场景，失败时
上传 `dist/e2e/**` 截图与 UIA 转储。发布流程（`release.yml`）后续可在 `package`
job 后追加 `needs: [package]` 的 `e2e` job，把发布 MSIX 的冒烟（安装→启动→
核心场景→崩溃上报）作为发版门禁。

## 后续项（不在本批）

- 视频流场景（`video_reader → blur → video_writer`，夹具需小视频文件）。
- 发布包真实安装的 CI 门禁（依赖签名 secrets：`MSIX_CERT_PFX`/`MSIX_CERT_PASSWORD`
  ——`build_msix.ps1` 已支持，`release.yml` 尚未切换）。
- 任务库拖拽建节点作为一等场景（当前右键菜单为主路径；拖拽受 OLE 时序影响较大）。
- 日志面板若 UIA 不可读：给 logPanel 增加 `QAccessibleValueInterface`（应用侧小改）。
