# task_graph Studio - Implementation Plan

## Phase 0: WASM 可行性研究（暂停）

> 注：根据用户需求，优先实现 macOS 和 Windows 平台，WASM 相关任务暂时搁置。

### [ ] Task P0.1: 搭建 Qt for WebAssembly 开发环境
- **Priority**: high
- **Depends On**: None
- **Description**: 
  - 安装 Qt for WebAssembly
  - 配置环境变量
  - 验证基本编译功能
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `programmatic` TR-P0.1.1: 执行 `qmake --version` 返回有效版本号
  - `programmatic` TR-P0.1.2: 成功编译一个简单的 Qt6 程序到 WASM

### [ ] Task P0.2: 编译 task_graph 到 WASM
- **Priority**: high
- **Depends On**: Task P0.1
- **Description**: 
  - 配置 task_graph 的 WASM 编译选项
  - 解决编译错误
  - 验证 task_graph 功能在 WASM 中正常工作
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `programmatic` TR-P0.2.1: task_graph 编译成功，生成 WASM 库
  - `programmatic` TR-P0.2.2: DAG 创建、任务添加等功能测试通过

### [-] Task P0.3: 创建最小 Qt6 WASM 应用（暂停）
### [-] Task P0.4: 输出可行性报告（暂停）

---

## Phase 1: 基础框架搭建（2-3 周）

### [ ] Task 1.1: 搭建 Qt6 开发环境（Windows/macOS）
- **Priority**: high
- **Depends On**: None
- **Description**: 
  - 安装 Qt6（Qt Online Installer 或 Homebrew）
  - 配置 Qt6 CMake 集成
  - 创建基础项目结构
- **Acceptance Criteria Addressed**: AC-13, AC-14
- **Test Requirements**:
  - `human-judgment` TR-1.1.1: Qt6 安装完成，qmake 可正常运行
  - `programmatic` TR-1.1.2: CMake 配置可正确找到 Qt6 库

### [ ] Task 1.2: 集成 task_graph 库
- **Priority**: high
- **Depends On**: Task 1.1
- **Description**: 
  - 配置 task_graph 的 include 和 lib 路径
  - 验证链接正确性
  - 创建简单的 DAG 实例测试
- **Acceptance Criteria Addressed**: AC-8, AC-9
- **Test Requirements**:
  - `programmatic` TR-1.2.1: 成功编译并链接 task_graph 库
  - `programmatic` TR-1.2.2: 成功创建 DAG 实例并添加任务

### [x] Task 1.3: 实现主窗口布局（QMainWindow + Qt Widgets）
- **Priority**: high
- **Depends On**: Task 1.1
- **Description**: 
  - 创建 MainWindow 类，继承自 QMainWindow
  - 实现工具栏（QToolBar）
  - 实现侧边栏（QDockWidget）
  - 实现画布区域（QGraphicsView）
  - 实现状态栏（QStatusBar）
- **Acceptance Criteria Addressed**: AC-1, AC-6, AC-12
- **Test Requirements**:
  - `human-judgment` TR-1.3.1: 主窗口显示工具栏、侧边栏（任务库）、画布区域、状态栏
  - `human-judgment` TR-1.3.2: 布局在不同窗口尺寸下自适应

### [x] Task 1.4: 实现 GraphViewModel 基础功能
- **Priority**: high
- **Depends On**: Task 1.2
- **Description**: 
  - 创建 GraphViewModel 类，继承自 QObject
  - 实现节点和边的管理接口
  - 实现 Q_PROPERTY 属性和信号
- **Acceptance Criteria Addressed**: AC-1, AC-2, AC-4, AC-5
- **Test Requirements**:
  - `programmatic` TR-1.4.1: 添加/删除节点功能测试通过
  - `programmatic` TR-1.4.2: 添加/删除边功能测试通过
  - `programmatic` TR-1.4.3: 信号正确触发变更通知

---

## Phase 2: 核心编辑功能（3-4 周）

### [x] Task 2.1: 实现 QGraphicsScene/QGraphicsView 画布
- **Priority**: high
- **Depends On**: Task 1.3
- **Description**: 
  - 创建 GraphScene 类，继承自 QGraphicsScene
  - 实现网格背景绘制
  - 实现鼠标事件处理（缩放、平移）
- **Acceptance Criteria Addressed**: AC-6
- **Test Requirements**:
  - `human-judgment` TR-2.1.1: 画布显示网格背景
  - `human-judgment` TR-2.1.2: 鼠标滚轮缩放正常工作
  - `human-judgment` TR-2.1.3: 拖拽空白区域平移正常工作

### [x] Task 2.2: 实现 NodeItem 节点图形项
- **Priority**: high
- **Depends On**: Task 2.1, Task 1.4
- **Description**: 
  - 创建 NodeItem 类，继承自 QGraphicsItem
  - 实现节点绘制（背景、标题、类型、端口）
  - 实现节点选中和拖拽功能
- **Acceptance Criteria Addressed**: AC-1, AC-2, AC-3
- **Test Requirements**:
  - `human-judgment` TR-2.2.1: 节点正确渲染，显示任务名称和类型
  - `human-judgment` TR-2.2.2: 点击节点选中，显示选中状态
  - `human-judgment` TR-2.2.3: 拖拽节点可移动位置

### [x] Task 2.3: 实现 EdgeItem 连线图形项
- **Priority**: high
- **Depends On**: Task 2.2
- **Description**: 
  - 创建 EdgeItem 类，继承自 QGraphicsItem
  - 实现贝塞尔曲线绘制
  - 实现连线选中和删除功能
- **Acceptance Criteria Addressed**: AC-4, AC-5
- **Test Requirements**:
  - `human-judgment` TR-2.3.1: 连线显示为平滑的贝塞尔曲线
  - `human-judgment` TR-2.3.2: 点击连线选中，显示选中状态
  - `human-judgment` TR-2.3.3: 按 Delete 键成功删除连线

### [x] Task 2.4: 实现任务节点管理（添加/删除/编辑）
- **Priority**: high
- **Depends On**: Task 2.2, Task 1.4
- **Description**: 
  - 实现从任务库拖拽任务到画布
  - 实现节点删除功能（Delete 键/右键菜单）
  - 实现节点双击编辑
- **Acceptance Criteria Addressed**: AC-1, AC-2, AC-3
- **Test Requirements**:
  - `human-judgment` TR-2.4.1: 从任务库拖拽任务到画布成功创建节点
  - `human-judgment` TR-2.4.2: 选中节点按 Delete 键成功删除
  - `human-judgment` TR-2.4.3: 双击节点弹出编辑对话框

### [x] Task 2.5: 实现依赖连线编辑（拖拽创建）
- **Priority**: high
- **Depends On**: Task 2.3, Task 1.4
- **Description**: 
  - 实现从节点端口拖拽创建连线
  - 实现连线命中测试
  - 更新 ViewModel 中的边数据
- **Acceptance Criteria Addressed**: AC-4, AC-5
- **Test Requirements**:
  - `human-judgment` TR-2.5.1: 从节点输出端口拖拽到输入端口成功创建连线
  - `human-judgment` TR-2.5.2: 连线自动更新位置跟随节点移动

### [x] Task 2.6: 实现 JSON 导入导出
- **Priority**: high
- **Depends On**: Task 1.2, Task 1.4
- **Description**: 
  - 实现 DAG 到 JSON 的导出
  - 实现从 JSON 导入 DAG
  - 集成文件对话框（QFileDialog）
- **Acceptance Criteria Addressed**: AC-8, AC-9
- **Test Requirements**:
  - `programmatic` TR-2.6.1: 导出的 JSON 文件可被 DAGSerializer 正确解析
  - `programmatic` TR-2.6.2: 导入 JSON 文件后，DAG 结构与原始一致
  - `human-judgment` TR-2.6.3: 文件保存/打开对话框正常工作

---

## Phase 3: 交互优化（2-3 周）

### [x] Task 3.1: 实现画布缩放/平移
- **Priority**: medium
- **Depends On**: Task 2.1
- **Description**: 
  - 实现鼠标滚轮缩放（围绕鼠标位置）
  - 实现拖拽空白区域平移
  - 实现缩放范围限制
- **Acceptance Criteria Addressed**: AC-6
- **Test Requirements**:
  - `human-judgment` TR-3.1.1: 鼠标滚轮缩放画布，节点大小相应变化
  - `human-judgment` TR-3.1.2: 拖拽空白区域平移画布，节点位置相应变化
  - `human-judgment` TR-3.1.3: 缩放围绕鼠标位置进行

### [x] Task 3.2: 实现撤销/重做功能
- **Priority**: medium
- **Depends On**: Task 2.4, Task 2.5
- **Description**: 
  - 实现 CommandStack 命令栈（继承自 QObject）
  - 实现 AddTaskCommand、RemoveTaskCommand、AddEdgeCommand、RemoveEdgeCommand
  - 绑定 Ctrl+Z/Ctrl+Y 快捷键
- **Acceptance Criteria Addressed**: AC-11
- **Test Requirements**:
  - `human-judgment` TR-3.2.1: 执行添加节点后，按 Ctrl+Z 撤销，节点消失
  - `human-judgment` TR-3.2.2: 撤销后按 Ctrl+Y 重做，节点重新出现
  - `programmatic` TR-3.2.3: 连续多次撤销/重做操作正确

### [x] Task 3.3: 实现自动布局算法
- **Priority**: medium
- **Depends On**: Task 1.4
- **Description**: 
  - 实现基于层级的 DAG 布局算法
  - 计算节点位置和连线路径
  - 更新 ViewModel 中的节点位置
- **Acceptance Criteria Addressed**: AC-7
- **Test Requirements**:
  - `human-judgment` TR-3.3.1: 点击自动布局按钮后，节点排列成清晰的层级结构
  - `human-judgment` TR-3.3.2: 连线不交叉，布局美观

### [x] Task 3.4: 实现属性面板
- **Priority**: medium
- **Depends On**: Task 1.3, Task 2.4
- **Description**: 
  - 创建属性面板布局（QDockWidget）
  - 实现属性与 ViewModel 的数据绑定（Qt Property System）
  - 支持修改任务配置参数
- **Acceptance Criteria Addressed**: AC-3, AC-12
- **Test Requirements**:
  - `human-judgment` TR-3.4.1: 选中节点后，属性面板显示该节点的配置参数
  - `human-judgment` TR-3.4.2: 修改属性面板中的参数，节点配置实时更新

---

## Phase 4: WASM 支持（3-4 周）

### [ ] Task 4.1: 搭建 Qt for WebAssembly 编译环境
- **Priority**: high
- **Depends On**: Phase 0（如果继续）
- **Description**: 
  - 安装 Qt for WebAssembly 工具链
  - 配置 Emscripten 环境
  - 验证编译功能
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `programmatic` TR-4.1.1: Qt for WebAssembly 编译成功
  - `human-judgment` TR-4.1.2: 记录环境配置步骤

### [ ] Task 4.2: 编译 task_graph 到 WASM
- **Priority**: high
- **Depends On**: Task 4.1
- **Description**: 
  - 配置 task_graph 的 WASM 编译选项
  - 解决编译错误
  - 验证 task_graph 功能在 WASM 中正常工作
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `programmatic` TR-4.2.1: task_graph 编译成功
  - `programmatic` TR-4.2.2: DAG 创建、任务添加、执行等功能测试通过

### [ ] Task 4.3: 编译 Qt6 应用到 WASM
- **Priority**: high
- **Depends On**: Task 4.1, Task 4.2
- **Description**: 
  - 配置 Qt6 WASM CMake 选项
  - 解决编译错误
  - 生成 HTML/JS/WASM 文件
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `programmatic` TR-4.3.1: Qt6 应用编译成功
  - `human-judgment` TR-4.3.2: 生成 HTML/JS/WASM 文件

### [ ] Task 4.4: 测试 WASM 端完整功能
- **Priority**: high
- **Depends On**: Task 4.3
- **Description**: 
  - 在浏览器中测试所有编辑功能
  - 验证 JSON 导入导出
  - 测试执行预览
- **Acceptance Criteria Addressed**: AC-15
- **Test Requirements**:
  - `human-judgment` TR-4.4.1: 所有编辑功能（添加/删除节点、连线）正常工作
  - `human-judgment` TR-4.4.2: JSON 导入导出正常工作
  - `human-judgment` TR-4.4.3: 执行预览正常工作

---

## Phase 5: 测试与优化（2 周）

### [ ] Task 5.1: 单元测试
- **Priority**: medium
- **Depends On**: Task 1.4, Task 3.2
- **Description**: 
  - 编写 GraphViewModel 单元测试（Qt Test）
  - 编写 CommandStack 单元测试（Qt Test）
  - 编写 JSON 序列化单元测试（Catch2）
- **Acceptance Criteria Addressed**: AC-8, AC-9, AC-11
- **Test Requirements**:
  - `programmatic` TR-5.1.1: GraphViewModel 测试用例全部通过
  - `programmatic` TR-5.1.2: CommandStack 测试用例全部通过
  - `programmatic` TR-5.1.3: JSON 序列化测试用例全部通过

### [ ] Task 5.2: 集成测试
- **Priority**: medium
- **Depends On**: Task 2.4, Task 2.5, Task 2.6
- **Description**: 
  - 编写完整编辑流程的集成测试
  - 编写执行流程的集成测试
  - 编写撤销/重做流程的集成测试
- **Acceptance Criteria Addressed**: AC-1, AC-2, AC-4, AC-5, AC-8, AC-9, AC-11
- **Test Requirements**:
  - `programmatic` TR-5.2.1: 完整编辑流程测试通过（创建节点→添加连线→导出→导入验证）
  - `programmatic` TR-5.2.2: 执行流程测试通过（创建图→执行→验证状态更新）
  - `programmatic` TR-5.2.3: 撤销/重做流程测试通过

### [ ] Task 5.3: WASM 性能优化
- **Priority**: medium
- **Depends On**: Task 4.4
- **Description**: 
  - 优化内存使用
  - 优化渲染性能（QGraphicsScene 优化）
  - 优化加载时间
- **Acceptance Criteria Addressed**: NFR-1, NFR-4, NFR-5
- **Test Requirements**:
  - `programmatic` TR-5.3.1: 内存占用 ≤ 512MB
  - `programmatic` TR-5.3.2: 加载时间 ≤ 5 秒（首次加载）
  - `programmatic` TR-5.3.3: 100+ 节点编辑帧率 ≥ 30fps

### [ ] Task 5.4: 跨平台兼容性测试
- **Priority**: medium
- **Depends On**: Task 1.1, Task 4.4
- **Description**: 
  - 在 Windows 上测试所有功能
  - 在 macOS 上测试所有功能
  - 在主流浏览器中测试 WASM 版本
- **Acceptance Criteria Addressed**: AC-13, AC-14, AC-15, NFR-2
- **Test Requirements**:
  - `human-judgment` TR-5.4.1: Windows 平台所有功能正常工作
  - `human-judgment` TR-5.4.2: macOS 平台所有功能正常工作
  - `human-judgment` TR-5.4.3: WASM 版本在 Chrome/Firefox/Safari/Edge 中正常工作