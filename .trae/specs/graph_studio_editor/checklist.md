# task_graph Studio - Verification Checklist

## Phase 0: WASM 可行性研究
- [ ] Checkpoint P0.1: Qt for WebAssembly 环境配置完成
- [ ] Checkpoint P0.2: task_graph 成功编译到 WASM
- [ ] Checkpoint P0.3: 最小 Qt6 WASM 应用创建完成，QGraphicsScene 渲染验证通过
- [ ] Checkpoint P0.4: 输出完整的 WASM 可行性报告，明确是否继续

## Phase 1: 基础框架搭建
- [ ] Checkpoint 1.1: Qt6 开发环境搭建完成（Windows/macOS）
- [ ] Checkpoint 1.2: task_graph 库集成完成，可创建 DAG 实例
- [ ] Checkpoint 1.3: 主窗口布局实现完成，包含工具栏、侧边栏、画布、状态栏
- [ ] Checkpoint 1.4: GraphViewModel 基础功能实现完成，支持节点和边的管理

## Phase 2: 核心编辑功能
- [ ] Checkpoint 2.1: QGraphicsScene/QGraphicsView 画布实现完成
- [ ] Checkpoint 2.2: NodeItem 节点图形项实现完成
- [ ] Checkpoint 2.3: EdgeItem 连线图形项实现完成
- [ ] Checkpoint 2.4: 任务节点管理功能完成，支持添加/删除/编辑
- [ ] Checkpoint 2.5: 依赖连线编辑功能完成，支持拖拽创建和删除
- [ ] Checkpoint 2.6: JSON 导入导出功能完成，与 DAGSerializer 互操作

## Phase 3: 交互优化
- [ ] Checkpoint 3.1: 画布缩放/平移功能完成，支持鼠标滚轮和拖拽
- [ ] Checkpoint 3.2: 撤销/重做功能完成，支持 Ctrl+Z/Ctrl+Y 快捷键
- [ ] Checkpoint 3.3: 自动布局算法完成，节点排列成清晰的层级结构
- [ ] Checkpoint 3.4: 属性面板功能完成，支持查看和编辑节点配置

## Phase 4: WASM 支持
- [ ] Checkpoint 4.1: Qt for WebAssembly 编译环境搭建完成
- [ ] Checkpoint 4.2: task_graph 编译到 WASM 完成，功能正常
- [ ] Checkpoint 4.3: Qt6 应用编译到 WASM 完成
- [ ] Checkpoint 4.4: WASM 端所有功能测试通过

## Phase 5: 测试与优化
- [ ] Checkpoint 5.1: 单元测试完成，所有测试用例通过
- [ ] Checkpoint 5.2: 集成测试完成，所有测试用例通过
- [ ] Checkpoint 5.3: WASM 性能优化完成，满足性能要求
- [ ] Checkpoint 5.4: 跨平台兼容性测试完成，三平台功能正常

## 功能完整性验证
- [ ] Checkpoint F1: 任务节点添加功能正常
- [ ] Checkpoint F2: 任务节点删除功能正常
- [ ] Checkpoint F3: 任务节点编辑功能正常
- [ ] Checkpoint F4: 依赖连线创建功能正常
- [ ] Checkpoint F5: 依赖连线删除功能正常
- [ ] Checkpoint F6: 画布缩放和平移功能正常
- [ ] Checkpoint F7: 自动布局功能正常
- [ ] Checkpoint F8: JSON 导出功能正常，可被 DAGSerializer 解析
- [ ] Checkpoint F9: JSON 导入功能正常，结构与原始一致
- [ ] Checkpoint F10: 执行预览功能正常，节点状态正确更新
- [ ] Checkpoint F11: 撤销/重做功能正常
- [ ] Checkpoint F12: 属性面板功能正常

## 非功能需求验证
- [ ] Checkpoint NFR1: 100+ 节点编辑帧率 ≥ 30fps
- [ ] Checkpoint NFR2: 三平台 UI 表现一致
- [ ] Checkpoint NFR3: executor 异步执行时 UI 安全更新
- [ ] Checkpoint NFR4: WASM 版本加载时间 ≤ 5 秒（首次加载）
- [ ] Checkpoint NFR5: 内存占用 ≤ 512MB

## 平台验证
- [ ] Checkpoint PLAT1: Windows 平台构建成功，运行正常
- [ ] Checkpoint PLAT2: macOS 平台构建成功，运行正常
- [ ] Checkpoint PLAT3: WASM 平台构建成功，浏览器中运行正常