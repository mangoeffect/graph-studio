# task_graph Studio - Product Requirement Document

## Overview
- **Summary**: 基于 Qt6 框架构建的 task_graph DAG 可视化编辑器，支持 macOS/Windows/WASM 三平台，采用 MVVM 开发范式，提供任务节点管理、依赖连线编辑、DAG 可视化、JSON 导入导出等核心功能。
- **Purpose**: 为 task_graph 框架提供直观的可视化编辑界面，降低 DAG 构建和调试的复杂度，支持跨平台使用。
- **Target Users**: task_graph 框架开发者、数据管道工程师、需要构建和调试 DAG 任务的技术人员。

## Goals
- 构建基于 Qt6 的 DAG 可视化编辑器，支持任务节点的添加、删除和编辑
- 实现依赖连线的拖拽创建和管理
- 提供画布缩放、平移和自动布局功能
- 支持 JSON 格式的 DAG 导入导出，与 DAGSerializer 互操作
- 实现任务执行预览和状态追踪
- 支持撤销/重做操作
- 支持 macOS/Windows/WASM 三平台

## Non-Goals (Out of Scope)
- 实时协作编辑功能
- 任务执行结果的高级分析和可视化
- 第三方插件市场
- 移动端支持
- 打印功能

## Background & Context
- task_graph 框架提供了 DAG 调度、执行和插件系统，但缺少可视化编辑工具
- Qt6 是一个成熟的 C++ 跨平台 UI 框架，支持 QGraphicsScene/QGraphicsView 用于节点编辑器
- Qt for WebAssembly 提供了将 Qt 应用编译为 WASM 的能力
- QGraphicsScene/QGraphicsView 是行业标准的节点编辑器解决方案（Unreal Blueprints、Blender、Qt Designer）

## Functional Requirements
- **FR-1**: 用户可以从任务库中拖拽任务节点到画布
- **FR-2**: 用户可以删除选中的任务节点
- **FR-3**: 用户可以编辑任务节点的配置参数
- **FR-4**: 用户可以通过拖拽创建任务间的依赖连线
- **FR-5**: 用户可以删除选中的依赖连线
- **FR-6**: 用户可以缩放和平移画布
- **FR-7**: 用户可以执行自动布局算法排列节点
- **FR-8**: 用户可以导出 DAG 为 JSON 格式
- **FR-9**: 用户可以从 JSON 文件导入 DAG
- **FR-10**: 用户可以预览任务执行顺序和状态
- **FR-11**: 用户可以撤销/重做操作
- **FR-12**: 用户可以查看和编辑选中节点的属性

## Non-Functional Requirements
- **NFR-1**: 支持 100+ 节点的流畅编辑（帧率 ≥ 30fps）
- **NFR-2**: 跨平台一致性，三平台 UI 表现一致
- **NFR-3**: 线程安全，executor 异步执行时 UI 安全更新
- **NFR-4**: WASM 版本加载时间 ≤ 5 秒（首次加载）
- **NFR-5**: 内存占用 ≤ 512MB

## Constraints
- **Technical**: C++20, Qt6 框架, CMake 构建系统
- **Business**: 优先实现核心功能，次要功能后续迭代
- **Dependencies**: task_graph 框架、Qt6 库

## Assumptions
- Qt6 可以通过 Qt for WebAssembly 编译到 WASM
- Qt6 的 QGraphicsScene/QGraphicsView 在 WASM 平台上性能满足需求
- task_graph 框架可以编译到 WASM
- 用户具备基本的 DAG 概念理解

## Acceptance Criteria

### AC-1: 任务节点添加
- **Given**: 编辑器已打开，任务库面板可见
- **When**: 用户从任务库拖拽一个任务类型到画布
- **Then**: 画布上出现一个新的任务节点，包含任务名称和类型标识
- **Verification**: `human-judgment`
- **Notes**: 节点应出现在鼠标释放位置附近

### AC-2: 任务节点删除
- **Given**: 画布上存在至少一个任务节点
- **When**: 用户选中节点并按下 Delete 键或点击删除按钮
- **Then**: 节点及其相关连线从画布上消失
- **Verification**: `human-judgment`

### AC-3: 任务节点编辑
- **Given**: 画布上存在一个任务节点
- **When**: 用户双击节点或选中后点击编辑按钮
- **Then**: 属性面板显示该节点的配置参数，用户可以修改并保存
- **Verification**: `human-judgment`

### AC-4: 依赖连线创建
- **Given**: 画布上存在至少两个任务节点
- **When**: 用户从一个节点的输出端口拖拽到另一个节点的输入端口
- **Then**: 画布上出现一条贝塞尔曲线连接两个节点，表示依赖关系
- **Verification**: `human-judgment`

### AC-5: 依赖连线删除
- **Given**: 画布上存在一条依赖连线
- **When**: 用户点击选中连线并按下 Delete 键
- **Then**: 连线从画布上消失
- **Verification**: `human-judgment`

### AC-6: 画布缩放和平移
- **Given**: 画布上存在多个节点
- **When**: 用户使用鼠标滚轮缩放，或拖拽空白区域平移
- **Then**: 画布内容相应缩放或平移，节点保持正确的相对位置
- **Verification**: `human-judgment`

### AC-7: 自动布局
- **Given**: 画布上存在多个节点和连线
- **When**: 用户点击自动布局按钮
- **Then**: 节点重新排列成清晰的层级结构，连线不交叉
- **Verification**: `human-judgment`

### AC-8: JSON 导出
- **Given**: 画布上存在至少一个节点
- **When**: 用户点击导出按钮
- **Then**: 弹出文件保存对话框，生成的 JSON 文件包含完整的 DAG 结构
- **Verification**: `programmatic` - 验证导出的 JSON 可被 DAGSerializer 正确解析

### AC-9: JSON 导入
- **Given**: 用户选择一个有效的 DAG JSON 文件
- **When**: 用户点击导入按钮
- **Then**: 画布上显示 JSON 文件中定义的所有节点和连线
- **Verification**: `programmatic` - 验证导入后的 DAG 与原始 JSON 结构一致

### AC-10: 执行预览
- **Given**: 画布上存在一个有效的 DAG
- **When**: 用户点击执行按钮
- **Then**: 节点按执行顺序依次高亮显示执行状态（等待→执行中→完成）
- **Verification**: `human-judgment`

### AC-11: 撤销/重做
- **Given**: 用户执行了至少一个编辑操作
- **When**: 用户按下 Ctrl+Z（撤销）或 Ctrl+Y（重做）
- **Then**: 画布状态回退到上一步或前进到下一步
- **Verification**: `human-judgment`

### AC-12: 属性面板
- **Given**: 用户选中了一个任务节点
- **When**: 属性面板可见
- **Then**: 属性面板显示该节点的所有配置参数，用户可以修改并实时预览
- **Verification**: `human-judgment`

### AC-13: Windows 平台构建
- **Given**: 开发环境已配置好 Qt6 和 CMake
- **When**: 执行 Windows 构建命令
- **Then**: 生成可执行文件，运行后显示完整的编辑器界面
- **Verification**: `human-judgment`

### AC-14: macOS 平台构建
- **Given**: 开发环境已配置好 Qt6 和 CMake
- **When**: 执行 macOS 构建命令
- **Then**: 生成应用程序包，运行后显示完整的编辑器界面
- **Verification**: `human-judgment`

### AC-15: WASM 平台构建
- **Given**: Qt for WebAssembly 环境已配置好
- **When**: 执行 WASM 构建命令
- **Then**: 生成 .html/.js/.wasm 文件，在浏览器中打开后显示完整的编辑器界面
- **Verification**: `human-judgment`

## Open Questions
- [ ] Qt6 for WebAssembly 在复杂场景下的性能表现（需 Phase 0 验证）
- [ ] QGraphicsScene 在 WASM 平台上的渲染性能（需 Phase 0 验证）
- [ ] WASM 版本的文件系统访问限制