# GraphStudio macOS 安装态 E2E（AXAPI + CGEvent）

镜像 `dev-docs/e2e-windows-installed.md`（pywinauto 方案）的 macOS 对应物：
对**真实 .app**（dev 构建或 dmg 安装产物）做端到端黑盒——原生菜单、
NSOpenPanel、真实鼠标键盘事件、AX 控件树断言。

## 选型

| 方案 | 结论 |
|---|---|
| **ctypes 直调 AXAPI/CGEvent（本方案）** | 零第三方依赖；新版 macOS 各 Python 发行版（含系统 python3）均不再预装 pyobjc，PEP 668 又挡 pip 装包 |
| pyobjc / atomacos | 同一套 AXAPI，但依赖装不上（上条）；atomacos 维护一般 |
| XCUITest | 需为 CMake Qt 工程引入 Swift/Xcode test target，脱离仓库 Python 脚本体系 |
| Squish | 商业 license（约 €3k+/席/年），且仓库无此预算 |

AX 断言/AXPress 动作由**目标 app 进程执行**（AXUIElementPerformAction），
基本不触发 TCC；只有 **CGEvent 注入**（画布拖拽、组合键、面板输入）需要
运行方持有辅助功能权限。

## 权限（一次性）

系统设置 -> 隐私与安全性 -> 辅助功能 -> 勾选运行本脚本的宿主
（Terminal/iTerm/VSCode…）。未授权时入口脚本会触发系统弹窗引导并以
退出码 2 终止。

## 锚点（MainWindow 为外部自动化显式准备）

`MainWindow.cpp` 注释 "供外部 UI 自动化（E2E）稳定定位"：
- accessibleName（Qt → AXDescription）：`Task Library` / `Graph Canvas` /
  `Log Panel` / `Output Panel` / `Image Results`
- countsLabel 文本 `Nodes: N | Edges: M`（AXStaticText 的 AXValue）
- 窗口标题 `<file> - Graph Studio`
- 菜单为原生 NSMenu（AXMenuBar / AXMenuItem，AXPress 可触发）

## 场景（scripts/e2e_macos/scenarios.py）

| 场景 | 内容 |
|---|---|
| `core` | 右键画布 → 上下文菜单建 2 节点（opencv_image_read / opencv_image_filtering）→ 端口 CGEvent 拖线（node 宽 140，端口在中心 ±70）→ countsLabel 断言 → Cmd+Z / Cmd+Shift+Z |
| `files` | File > Open（原生菜单 AXPress）→ NSOpenPanel 里 Cmd+Shift+G 输路径 → Return×2 → 窗口标题断言 |
| `run` | 打开 e2e_graph.json（opencv_image_read 读绝对路径 png）→ Cmd+R → Log Panel AXValue 等 "Execution finished: 1 ok, 0 failed" |
| `lifecycle` | Cmd+Q → 进程正常退出（exit 0） |
| `crash` | `--test-crash` 独立进程，预期 SIGSEGV（镜像 verify_crash_reporting 的语义） |

## 运行

```bash
python3 scripts/run_e2e_macos.py                       # dev .app 全场景
python3 scripts/run_e2e_macos.py --scenario core,run
python3 scripts/run_e2e_macos.py --app /Applications/GraphStudio.app
```

dev .app 需要插件时入口脚本自动设 `TASK_GRAPH_PLUGINS_PATH` /
`GRAPH_STUDIO_MODELS_DIR` / `DYLD_LIBRARY_PATH`（复刻 run_graph_studio.py）。
失败场景用 `screencapture -x` 留现场截图到 `--artifacts`（默认
/tmp/gs_e2e_macos）。

## 已知限制

- 画布端口坐标按 NodeItem 固定几何（宽 140）与默认缩放 1.0 推算；改画布
  默认缩放/节点尺寸需同步 `scenarios.NODE_WIDTH`。
- 上下文菜单依赖插件加载（无插件时任务类型不存在，core 场景建不了节点）。
- NSOpenPanel 的 Go-to-Folder 交互依赖系统面板行为，macOS 大版本升级后
  需回归一次 files 场景。
- 屏幕录制权限（screencapture 截图）是另一项 TCC，首次会弹窗。
