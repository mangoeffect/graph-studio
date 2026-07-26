# task_graph 桌面端编辑器实现方案

## 一、概述

本文档详细描述基于 **Qt6** 框架构建 task_graph DAG 可视化编辑器的实现方案，严格遵循 **MVVM** 开发范式，优先支持 **macOS/Windows/WASM** 三个平台。

---

## 二、需求分析

### 2.1 核心功能

| 功能模块 | 描述 | 优先级 |
|---|---|---|
| 任务节点管理 | 添加/删除/编辑任务节点，配置参数 | P0 |
| 依赖连线编辑 | 拖拽创建任务间依赖关系 | P0 |
| DAG 可视化 | 画布展示节点和连线，支持缩放/平移 | P0 |
| JSON 导入导出 | 与 DAGSerializer 互操作 | P0 |
| 执行预览 | 预览任务执行顺序和依赖关系 | P1 |
| 属性面板 | 编辑选中节点的配置参数 | P1 |
| 撤销/重做 | 支持操作回退 | P2 |
| 插件管理 | 加载/卸载 task_graph 插件 | P2 |

### 2.2 非功能需求

| 需求 | 描述 |
|---|---|
| 跨平台 | 支持 macOS/Windows/WASM |
| 性能 | 支持 100+ 节点的流畅编辑 |
| 可扩展性 | 插件化架构，易于添加新任务类型 |
| 线程安全 | executor 异步执行时 UI 安全更新 |

---

## 三、技术选型

### 3.1 UI 框架：Qt6

**选择理由**：
- 成熟稳定的 C++ 跨平台 UI 框架，支持 macOS/Windows/Linux/WASM
- QGraphicsScene/QGraphicsView 是行业标准的节点编辑器解决方案
- 完善的信号槽机制，天然支持 MVVM 模式
- Qt for WebAssembly 官方支持，编译流程成熟
- 丰富的控件库和布局系统
- 活跃的社区和完善的文档

**Qt6 核心组件**：
| 组件 | 用途 |
|---|---|
| QGraphicsScene | 场景管理，包含所有图形项 |
| QGraphicsView | 视图控件，显示场景内容 |
| QGraphicsItem | 图形项基类，用于节点和连线 |
| QPainter | 2D 绘图引擎，绘制节点和曲线 |
| QPropertyAnimation | 属性动画，支持平滑过渡 |
| QDockWidget | 停靠面板，用于侧边栏和属性面板 |
| QToolBar | 工具栏，包含操作按钮 |
| QStatusBar | 状态栏，显示状态信息 |

### 3.2 WASM 编译：Qt for WebAssembly

**选择理由**：
- Qt 官方提供的 WASM 编译工具链
- 自动处理 Emscripten 配置
- 支持大部分 Qt Widgets 和 QML 功能
- 提供 Qt WebAssembly 运行时库
- 支持 OpenGL ES 2.0/3.0 渲染

---

## 四、架构设计

### 4.1 MVVM 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                        View Layer                          │
│  (Qt Widgets + QGraphicsScene/QGraphicsView)               │
│  ┌──────────────┬──────────────┬──────────────┐            │
│  │ TaskListPanel│ GraphCanvas  │PropertyPanel │            │
│  │ (任务列表)   │ (DAG画布)    │ (属性编辑器) │            │
│  └──────────────┴──────────────┴──────────────┘            │
│  自定义图形项: NodeItem, EdgeItem                          │
└──────────────────────┬──────────────────────────────────────┘
                       │ Signals/Slots (Qt)
┌──────────────────────▼──────────────────────────────────────┐
│                     ViewModel Layer                         │
│  (QObject + Q_PROPERTY)                                    │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ GraphViewModel  ←→  TaskNodeViewModel  ←→  EdgeVM    │   │
│  │   (DAG状态管理)     (单个任务节点)      (依赖边)     │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │ EditorCommands (操作命令模式，支持撤销/重做)          │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────────┘
                       │ Model API
┌──────────────────────▼──────────────────────────────────────┐
│                         Model Layer                         │
│  (task_graph Framework)                                    │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ DAG / Task / IPluginTask / TaskConfig / Profiler     │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 三平台统一架构

#### 平台差异对比

| 平台 | 运行环境 | 渲染后端 | 窗口系统 |
|---|---|---|---|
| **Windows** | Native C++ | OpenGL/Direct3D | Win32 |
| **macOS** | Native C++ | OpenGL/Metal | Cocoa |
| **WASM** | C++ → WASM | WebGL2 | Browser |

**核心设计理念**：三平台共享**完全相同的代码库**，仅通过 Qt 的平台抽象层实现底层差异隔离。

#### WASM 架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Browser Environment                          │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                    WebAssembly Module                          │  │
│  │  ┌──────────────────────────────────────────────────────────┐  │  │
│  │  │                  Qt6 Full Stack                           │  │  │
│  │  │  ┌────────────────────────────────────────────────────┐  │  │  │
│  │  │  │  View Layer (QGraphicsScene/QGraphicsView/Widgets) │  │  │  │
│  │  │  │  ViewModel Layer (QObject + Q_PROPERTY)            │  │  │  │
│  │  │  │  Model Layer (DAG + Executor + PluginRegistry)     │  │  │  │
│  │  │  └────────────────────────────────────────────────────┘  │  │  │
│  │  │  QPainter → OpenGL ES → WebGL2 Renderer                │  │  │
│  │  └──────────────────────────────────────────────────────────┘  │  │
│  │                              │                                  │  │
│  │                              │ Emscripten Runtime                │  │
│  │                              ▼                                  │  │
│  │  ┌──────────────────────────────────────────────────────────┐  │  │
│  │  │              Browser Host Environment                    │  │  │
│  │  │  - HTML Canvas Element                                   │  │  │
│  │  │  - WebGL2 Context                                        │  │  │
│  │  │  - Input Events (mouse/keyboard/wheel)                   │  │  │
│  │  │  - requestAnimationFrame                                 │  │  │
│  │  └──────────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 五、核心模块设计

### 5.1 目录结构

```
app/graph_studio/
├── CMakeLists.txt                    # 主构建配置
├── include/
│   ├── viewmodel/
│   │   ├── graph_view_model.hpp      # DAG 状态管理
│   │   ├── task_node_view_model.hpp  # 任务节点 VM
│   │   ├── edge_view_model.hpp       # 依赖边 VM
│   │   └── command_stack.hpp         # 命令栈（撤销/重做）
│   ├── view/
│   │   ├── main_window.hpp           # 主窗口接口
│   │   ├── graph_scene.hpp           # 自定义图形场景
│   │   ├── node_item.hpp             # 节点图形项
│   │   ├── edge_item.hpp             # 连线图形项
│   │   └── task_list_panel.hpp       # 任务列表面板
│   ├── model/
│   │   └── editor_model.hpp          # 编辑器模型接口
│   └── platform/
│       └── wasm/
│           └── wasm_main.hpp         # WASM 入口
├── src/
│   ├── viewmodel/
│   │   ├── graph_view_model.cpp
│   │   ├── task_node_view_model.cpp
│   │   ├── edge_view_model.cpp
│   │   └── command_stack.cpp
│   ├── view/
│   │   ├── main_window.cpp           # 主窗口实现
│   │   ├── graph_scene.cpp           # 场景实现
│   │   ├── node_item.cpp             # 节点实现
│   │   ├── edge_item.cpp             # 连线实现
│   │   └── task_list_panel.cpp       # 任务列表实现
│   ├── model/
│   │   └── editor_model.cpp          # 编辑器模型实现
│   └── platform/
│       ├── windows/
│       │   └── gui_main.cpp          # Windows 入口
│       ├── macos/
│       │   └── gui_main.cpp          # macOS 入口
│       └── wasm/
│           └── wasm_main.cpp         # WASM 入口实现
└── wasm/
    ├── index.html                    # 浏览器入口页面
```

### 5.2 ViewModel 接口设计

#### 5.2.1 GraphViewModel

```cpp
class GraphViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isExecuting READ isExecuting NOTIFY isExecutingChanged)
    Q_PROPERTY(double zoomLevel READ zoomLevel NOTIFY zoomLevelChanged)

public:
    explicit GraphViewModel(task_graph::DAG& dag, QObject* parent = nullptr);
    
    // 任务节点管理
    Q_INVOKABLE void addTask(const QString& taskType, const task_graph::TaskConfig& config = {});
    Q_INVOKABLE void removeTask(const QString& taskId);
    Q_INVOKABLE void updateTask(const QString& taskId, const task_graph::TaskConfig& config);
    
    // 依赖管理
    Q_INVOKABLE void addEdge(const QString& fromId, const QString& toId);
    Q_INVOKABLE void removeEdge(const QString& fromId, const QString& toId);
    
    // 画布操作
    Q_INVOKABLE void layoutNodes();
    Q_INVOKABLE void zoom(double factor);
    Q_INVOKABLE void pan(double dx, double dy);
    
    // 序列化
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE void fromJson(const QString& jsonStr);
    
    // 执行控制
    Q_INVOKABLE void execute();
    Q_INVOKABLE void stop();
    
    // 属性访问
    bool isExecuting() const;
    double zoomLevel() const;
    
    // 数据绑定属性
    QList<TaskNodeViewModel*> nodes() const;
    QList<EdgeViewModel*> edges() const;
    
    // 命令支持
    void pushCommand(std::unique_ptr<EditorCommand> command);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;
    
signals:
    void isExecutingChanged();
    void zoomLevelChanged();
    void graphChanged();
    void taskCompleted(const QString& taskId);
    void errorOccurred(const QString& error);
    
private:
    task_graph::DAG& dag_;
    QList<TaskNodeViewModel*> nodes_;
    QList<EdgeViewModel*> edges_;
    bool isExecuting_ = false;
    double zoomLevel_ = 1.0;
    CommandStack commandStack_;
    
    void onTaskCompleted(const QString& taskId);
};
```

#### 5.2.2 TaskNodeViewModel

```cpp
class TaskNodeViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString taskId READ taskId CONSTANT)
    Q_PROPERTY(QString taskType READ taskType CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QRectF bounds READ bounds WRITE setBounds NOTIFY boundsChanged)
    Q_PROPERTY(task_graph::TaskStatus status READ status WRITE setStatus NOTIFY statusChanged)

public:
    explicit TaskNodeViewModel(const QString& taskId, const task_graph::TaskConfig& config,
                               QObject* parent = nullptr);
    
    QString taskId() const;
    QString taskType() const;
    QString displayName() const;
    QRectF bounds() const;
    task_graph::TaskStatus status() const;
    
    void setBounds(const QRectF& bounds);
    void setStatus(task_graph::TaskStatus status);
    
    QList<QString> inputPorts() const;
    QList<QString> outputPorts() const;
    
signals:
    void boundsChanged();
    void statusChanged();
    
private:
    QString taskId_;
    QString taskType_;
    QRectF bounds_;
    task_graph::TaskStatus status_;
};
```

#### 5.2.3 EdgeViewModel

```cpp
class EdgeViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fromId READ fromId CONSTANT)
    Q_PROPERTY(QString toId READ toId CONSTANT)

public:
    explicit EdgeViewModel(const QString& fromId, const QString& toId, QObject* parent = nullptr);
    
    QString fromId() const;
    QString toId() const;
    
    QPointF startPoint() const;
    QPointF endPoint() const;
    
private:
    QString fromId_;
    QString toId_;
};
```

### 5.3 画布编辑器实现

#### 5.3.1 GraphScene

```cpp
class GraphScene : public QGraphicsScene
{
    Q_OBJECT
    
public:
    explicit GraphScene(GraphViewModel& vm, QObject* parent = nullptr);
    
    // 节点操作
    void addNode(NodeItem* node);
    void removeNode(NodeItem* node);
    void selectNode(NodeItem* node);
    
    // 连线操作
    void addEdge(EdgeItem* edge);
    void removeEdge(EdgeItem* edge);
    
    // 画布操作
    void zoom(double factor, const QPointF& center);
    void pan(double dx, double dy);
    void layoutNodes();
    
    // 坐标转换
    QPointF screenToCanvas(const QPoint& screenPoint) const;
    
signals:
    void nodeSelected(NodeItem* node);
    void edgeSelected(EdgeItem* edge);
    void nothingSelected();
    void connectionStarted(NodeItem* fromNode, int portIndex);
    void connectionCompleted(NodeItem* toNode, int portIndex);
    
protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;
    
private:
    // 绘制方法
    void drawGrid(QPainter* painter, const QRectF& rect);
    
    // 命中测试
    NodeItem* hitTestNode(const QPointF& scenePoint) const;
    EdgeItem* hitTestEdge(const QPointF& scenePoint) const;
    
    // 交互状态
    GraphViewModel& vm_;
    bool isDragging_ = false;
    NodeItem* draggedNode_ = nullptr;
    QPointF dragStart_;
    bool isConnecting_ = false;
    QPointF connectionStart_;
    NodeItem* connectionFromNode_ = nullptr;
    int connectionFromPort_ = -1;
    QGraphicsLineItem* tempLine_ = nullptr;
    
    // 视图状态
    double scale_ = 1.0;
    double offsetX_ = 0.0;
    double offsetY_ = 0.0;
};
```

#### 5.3.2 NodeItem

```cpp
class NodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };
    
    NodeItem(TaskNodeViewModel* vm, QGraphicsItem* parent = nullptr);
    
    int type() const override { return Type; }
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    TaskNodeViewModel* viewModel() const;
    
    // 端口管理
    QPointF inputPortPosition(int index = 0) const;
    QPointF outputPortPosition(int index = 0) const;
    
    // 选中状态
    void setSelected(bool selected);
    bool isSelected() const;
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    
private:
    TaskNodeViewModel* vm_;
    bool selected_ = false;
    bool hovered_ = false;
    
    // 尺寸常量
    static constexpr qreal NODE_WIDTH = 180;
    static constexpr qreal NODE_HEIGHT = 120;
    static constexpr qreal PORT_SIZE = 12;
};
```

#### 5.3.3 EdgeItem

```cpp
class EdgeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };
    
    EdgeItem(EdgeViewModel* vm, NodeItem* fromNode, NodeItem* toNode,
             QGraphicsItem* parent = nullptr);
    
    int type() const override { return Type; }
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    EdgeViewModel* viewModel() const;
    
    void updatePosition();
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    
private:
    EdgeViewModel* vm_;
    NodeItem* fromNode_;
    NodeItem* toNode_;
    QPainterPath path_;
    
    QPainterPath createBezierPath(const QPointF& start, const QPointF& end) const;
};
```

### 5.4 操作命令模式

#### 5.4.1 EditorCommand 基类

```cpp
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString description() const = 0;
};
```

#### 5.4.2 具体命令

```cpp
class AddTaskCommand : public EditorCommand
{
public:
    AddTaskCommand(GraphViewModel& vm, const QString& type, 
                   const task_graph::TaskConfig& config, const QPointF& position);
    void execute() override;
    void undo() override;
    QString description() const override { return "Add Task"; }
    
private:
    GraphViewModel& vm_;
    QString type_;
    task_graph::TaskConfig config_;
    QPointF position_;
    QString createdId_;
};

class RemoveTaskCommand : public EditorCommand
{
public:
    RemoveTaskCommand(GraphViewModel& vm, const QString& taskId);
    void execute() override;
    void undo() override;
    QString description() const override { return "Remove Task"; }
    
private:
    GraphViewModel& vm_;
    QString taskId_;
    TaskNodeViewModel* savedNode_;
    QList<EdgeViewModel*> savedEdges_;
};

class AddEdgeCommand : public EditorCommand
{
public:
    AddEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& toId);
    void execute() override;
    void undo() override;
    QString description() const override { return "Add Connection"; }
    
private:
    GraphViewModel& vm_;
    QString fromId_;
    QString toId_;
};

class RemoveEdgeCommand : public EditorCommand
{
public:
    RemoveEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& toId);
    void execute() override;
    void undo() override;
    QString description() const override { return "Remove Connection"; }
    
private:
    GraphViewModel& vm_;
    QString fromId_;
    QString toId_;
};
```

#### 5.4.3 CommandStack

```cpp
class CommandStack : public QObject
{
    Q_OBJECT
    
public:
    void push(std::unique_ptr<EditorCommand> cmd);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    
signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    
private:
    QList<std::unique_ptr<EditorCommand>> undoStack_;
    QList<std::unique_ptr<EditorCommand>> redoStack_;
    
    void updateCanExecute();
};
```

---

## 六、WASM 编译方案

### 6.1 环境准备

#### 6.1.1 Qt for WebAssembly 安装

```bash
# 通过 Qt Online Installer 安装
# 或通过 Homebrew（macOS）
brew install qt@6

# 确保 Qt WebAssembly 工具链可用
qmake --version
```

#### 6.1.2 工具链版本

| 工具 | 版本要求 |
|---|---|
| Qt | 6.6+ |
| Emscripten | 3.1.50+（Qt 内置） |
| CMake | 3.20+ |
| Node.js | 18+（开发服务器） |

### 6.2 CMake 配置

#### 6.2.1 主 CMakeLists.txt（原生平台）

```cmake
cmake_minimum_required(VERSION 3.20)
project(graph_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 依赖
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# task_graph 依赖
include_directories(${TASK_GRAPH_ROOT}/include)
link_directories(${TASK_GRAPH_ROOT}/lib)

# 源文件
set(PROJECT_SOURCE_FILES
    src/viewmodel/graph_view_model.cpp
    src/viewmodel/task_node_view_model.cpp
    src/viewmodel/edge_view_model.cpp
    src/viewmodel/command_stack.cpp
    src/view/main_window.cpp
    src/view/graph_scene.cpp
    src/view/node_item.cpp
    src/view/edge_item.cpp
    src/view/task_list_panel.cpp
    src/model/editor_model.cpp
    src/platform/windows/gui_main.cpp
    src/platform/macos/gui_main.cpp
)

# 头文件
set(PROJECT_HEADER_FILES
    include/viewmodel/graph_view_model.hpp
    include/viewmodel/task_node_view_model.hpp
    include/viewmodel/edge_view_model.hpp
    include/viewmodel/command_stack.hpp
    include/view/main_window.hpp
    include/view/graph_scene.hpp
    include/view/node_item.hpp
    include/view/edge_item.hpp
    include/view/task_list_panel.hpp
    include/model/editor_model.hpp
)

# 生成可执行文件
add_executable(graph_studio
    ${PROJECT_SOURCE_FILES}
    ${PROJECT_HEADER_FILES}
)

# 链接库
target_link_libraries(graph_studio
    Qt6::Widgets
    task_graph
)

# macOS 应用程序包配置
if(APPLE)
    set_target_properties(graph_studio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in
    )
endif()
```

#### 6.2.2 WASM CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(graph_studio_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 WebAssembly 工具链
set(CMAKE_TOOLCHAIN_FILE "${QT_DIR}/lib/cmake/Qt6/qt.toolchain.cmake")
set(QT_WASM_EMSCRIPTEN_PATH "/path/to/emsdk")

# Qt6 依赖
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# task_graph 依赖（WASM 版本）
include_directories(${TASK_GRAPH_ROOT}/include)
link_directories(${TASK_GRAPH_ROOT}/lib/wasm)

# 源文件（仅 WASM 相关）
set(PROJECT_SOURCE_FILES
    src/viewmodel/graph_view_model.cpp
    src/viewmodel/task_node_view_model.cpp
    src/viewmodel/edge_view_model.cpp
    src/viewmodel/command_stack.cpp
    src/view/main_window.cpp
    src/view/graph_scene.cpp
    src/view/node_item.cpp
    src/view/edge_item.cpp
    src/view/task_list_panel.cpp
    src/model/editor_model.cpp
    src/platform/wasm/wasm_main.cpp
)

# 编译可执行文件
add_executable(graph_studio
    ${PROJECT_SOURCE_FILES}
)

# 链接库
target_link_libraries(graph_studio
    Qt6::Widgets
    task_graph
)

# WASM 特定配置
set_target_properties(graph_studio PROPERTIES
    QT_WASM_EXPORTED_FUNCTIONS '["_main", "_qt_main"]'
    QT_WASM_EXTRA_ARGS '--bindings'
)
```

### 6.3 WASM 部署约束

#### 6.3.1 HTTP Server 配置

开发服务器配置示例（Python）：

```python
# serve.py
import http.server
import socketserver

PORT = 8080

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
    print(f"Serving at http://localhost:{PORT}")
    httpd.serve_forever()
```

#### 6.3.2 浏览器兼容性

| 浏览器 | 最低版本 | 说明 |
|---|---|---|
| Chrome | 70+ | 完整支持 |
| Firefox | 60+ | 完整支持 |
| Safari | 14+ | 需要启用 SharedArrayBuffer |
| Edge | 79+ | 完整支持 |

### 6.4 WASM 入口文件

#### 6.4.1 wasm_main.cpp

```cpp
#include "platform/wasm/wasm_main.hpp"
#include "view/main_window.hpp"
#include "viewmodel/graph_view_model.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    
    // 创建 DAG 和 ViewModel
    task_graph::DAG dag;
    GraphViewModel vm(dag);
    
    // 创建主窗口
    MainWindow window(&vm);
    window.show();
    
    return app.exec();
}
```

### 6.5 浏览器入口 HTML

#### 6.5.1 index.html

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>task_graph Studio</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        html, body {
            width: 100%;
            height: 100%;
            overflow: hidden;
            background: #f5f5f5;
        }
        
        #canvas {
            display: block;
            width: 100%;
            height: 100%;
            cursor: default;
        }
        
        .loading {
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            font-size: 16px;
            color: #666;
        }
    </style>
</head>
<body>
    <div class="loading" id="loading">Loading task_graph Studio...</div>
    <script type="text/javascript">
        // Qt WASM 运行时会自动加载
        var module = {
            onRuntimeInitialized: function() {
                document.getElementById('loading').style.display = 'none';
            }
        };
    </script>
    <script src="graph_studio.js"></script>
</body>
</html>
```

---

## 七、线程安全方案

### 7.1 架构设计

```
┌──────────────────────┐     ┌──────────────────────┐
│   Executor Threads   │     │      UI Thread       │
│  (task_graph)        │     │   (Qt Event Loop)    │
│                      │     │                      │
└──────────┬───────────┘     └──────────┬───────────┘
           │                            │
           │ 任务完成回调                 │ 状态更新
           ▼                            ▼
┌────────────────────────────────────────────────┐
│           ThreadSafeEventQueue                 │
│  ┌──────────────────────────────────────────┐  │
│  │  QMutex + QWaitCondition                 │  │
│  │  Queue<Event>                            │  │
│  │  QTimer (UI Dispatcher)                  │  │
│  └──────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

### 7.2 ThreadSafeEventQueue 实现

```cpp
template<typename Event>
class ThreadSafeEventQueue
{
public:
    void push(Event&& event)
    {
        QMutexLocker locker(&mutex_);
        queue_.push(std::forward<Event>(event));
        cv_.wakeOne();
    }
    
    bool tryPop(Event& event)
    {
        QMutexLocker locker(&mutex_);
        if (queue_.empty()) return false;
        event = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    void waitAndProcess(std::function<void(Event)> handler)
    {
        QMutexLocker locker(&mutex_);
        cv_.wait(&mutex_, [this] { return !queue_.empty(); });
        
        while (!queue_.empty()) {
            Event event = std::move(queue_.front());
            queue_.pop();
            locker.unlock();
            handler(std::move(event));
            locker.relock();
        }
    }
    
    bool empty() const
    {
        QMutexLocker locker(&mutex_);
        return queue_.empty();
    }
    
private:
    std::queue<Event> queue_;
    mutable QMutex mutex_;
    QWaitCondition cv_;
};

// Event 类型定义
struct GraphEvent
{
    enum class Type { TaskCompleted, GraphChanged, Error, ExecutionStarted, ExecutionStopped };
    
    Type type;
    QString taskId;
    QString errorMessage;
};
```

### 7.3 ViewModel 集成

```cpp
void GraphViewModel::onTaskCompleted(const QString& taskId)
{
    eventQueue_.push(GraphEvent{
        .type = GraphEvent::Type::TaskCompleted,
        .taskId = taskId
    });
}

void GraphViewModel::processEvents()
{
    GraphEvent event;
    while (eventQueue_.tryPop(event)) {
        switch (event.type) {
            case GraphEvent::Type::TaskCompleted:
                auto node = findNode(event.taskId);
                if (node) {
                    node->setStatus(task_graph::TaskStatus::Completed);
                }
                emit taskCompleted(event.taskId);
                break;
            case GraphEvent::Type::GraphChanged:
                emit graphChanged();
                break;
            case GraphEvent::Type::Error:
                emit errorOccurred(event.errorMessage);
                break;
        }
    }
}
```

---

## 八、构建流程

### 8.1 原生平台构建

```bash
# Windows
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.6.0\msvc2019_64"
cmake --build . --config Release

# macOS
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/usr/local/opt/qt@6" -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 8.2 WASM 平台构建

```bash
# 设置 Qt WebAssembly 环境
export QT_DIR=/path/to/qt/6.6.0/wasm_32
export PATH=$QT_DIR/bin:$PATH

# 创建构建目录
mkdir build_wasm && cd build_wasm

# 配置 CMake
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$QT_DIR/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release

# 编译（生成 .html, .js, .wasm 文件）
make -j$(nproc)

# 启动开发服务器
python3 serve.py
```

### 8.3 CI/CD 配置

```yaml
# .github/workflows/build.yml
name: Build

on: [push, pull_request]

jobs:
  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Setup Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.6.0'
          host: 'windows'
          target: 'desktop'
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -G "Visual Studio 17 2022" -A x64
          cmake --build . --config Release

  macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - name: Setup Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.6.0'
          host: 'macos'
          target: 'desktop'
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release
          make -j$(nproc)

  wasm:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Setup Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.6.0'
          host: 'linux'
          target: 'wasm'
      - name: Build WASM
        run: |
          mkdir build_wasm && cd build_wasm
          cmake .. -DCMAKE_TOOLCHAIN_FILE=${QT_DIR}/lib/cmake/Qt6/qt.toolchain.cmake
          make -j$(nproc)
      - name: Upload WASM Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: wasm-build
          path: build_wasm/*.html
          path: build_wasm/*.js
          path: build_wasm/*.wasm
```

---

## 九、测试策略

### 9.1 单元测试

| 模块 | 测试内容 | 测试框架 |
|---|---|---|
| GraphViewModel | 节点增删、边增删、序列化 | Qt Test |
| CommandStack | 撤销/重做、命令栈状态 | Qt Test |
| ThreadSafeEventQueue | 线程安全、并发访问 | Qt Test |
| DAGSerializer | JSON 导入导出 | Catch2 |

### 9.2 集成测试

| 测试场景 | 描述 |
|---|---|
| 完整编辑流程 | 创建节点 → 添加连线 → 导出 JSON → 导入验证 |
| 执行流程 | 创建图 → 执行 → 验证状态更新 |
| 撤销重做 | 执行多个操作 → 撤销 → 重做 → 验证状态 |

### 9.3 WASM 测试

| 测试类型 | 描述 | 工具 |
|---|---|---|
| 功能测试 | 浏览器中验证核心功能 | Playwright |
| 性能测试 | 大图渲染性能、交互响应时间 | Lighthouse |
| 兼容性测试 | 多浏览器/设备兼容性 | BrowserStack |

---

## 十、性能优化

### 10.1 渲染优化

| 优化项 | 方案 |
|---|---|
| 脏区域渲染 | QGraphicsScene 自动处理 |
| 虚拟列表 | 节点过多时使用 QGraphicsItemGroup 分批渲染 |
| 批量绘制 | 使用 QGraphicsScene::items() 批量操作 |
| 禁用不必要的刷新 | 合理使用 update() 和 invalidate() |

### 10.2 数据优化

| 优化项 | 方案 |
|---|---|
| 增量更新 | 仅传递变化数据 |
| 延迟加载 | 按需加载插件和配置 |

### 10.3 线程优化

| 优化项 | 方案 |
|---|---|
| 后台布局 | 复杂布局计算在后台线程执行 |
| 批量事件 | 合并短时间内的多个事件 |

---

## 十一、风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|---|---|---|---|
| **Qt for WebAssembly 性能** | 中 | 高 | 使用 QGraphicsScene 的优化特性，实现脏区域渲染，限制节点数量 |
| Qt6 版本兼容性 | 低 | 中 | 使用 LTS 版本（6.6+），锁定依赖版本 |
| 线程安全问题 | 中 | 高 | 使用 ThreadSafeEventQueue + Qt 信号槽机制 |
| WASM 文件大小 | 中 | 中 | 启用 Qt 模块裁剪，使用 -O3 优化 |
| 字体渲染兼容性 | 低 | 中 | 使用 Qt 内置字体或 Web 字体 |

---

## 十二、实施计划

### Phase 0：WASM 可行性研究（1-2 周）

**目的**：在正式投入开发前，验证 Qt6 + task_graph 能否在 WebAssembly 下编译和运行。

| 任务 | 描述 |
|---|---|
| T0.1 | 搭建 Qt for WebAssembly 开发环境 |
| T0.2 | 尝试编译 task_graph 到 WASM |
| T0.3 | 创建最小 Qt6 WASM 应用，验证 QGraphicsScene 渲染 |
| T0.4 | 输出可行性报告，决定是否继续 WASM 方案 |

### 阶段一：基础框架搭建（2-3 周）

| 任务 | 描述 |
|---|---|
| T1.1 | 搭建 Qt6 开发环境（Windows/macOS） |
| T1.2 | 集成 task_graph 库 |
| T1.3 | 实现主窗口布局（QMainWindow + QToolBar + QDockWidget） |
| T1.4 | 实现 GraphViewModel 基础功能 |

### 阶段二：核心编辑功能（3-4 周）

| 任务 | 描述 |
|---|---|
| T2.1 | 实现 QGraphicsScene/QGraphicsView 画布 |
| T2.2 | 实现 NodeItem 节点图形项 |
| T2.3 | 实现 EdgeItem 连线图形项 |
| T2.4 | 实现任务节点管理（添加/删除/编辑） |
| T2.5 | 实现依赖连线编辑（拖拽创建） |
| T2.6 | 实现 JSON 导入导出 |

### 阶段三：交互优化（2-3 周）

| 任务 | 描述 |
|---|---|
| T3.1 | 实现画布缩放/平移 |
| T3.2 | 实现撤销/重做功能 |
| T3.3 | 实现自动布局算法 |
| T3.4 | 实现属性面板 |

### 阶段四：WASM 支持（3-4 周）

| 任务 | 描述 |
|---|---|
| T4.1 | 搭建 Qt for WebAssembly 编译环境 |
| T4.2 | 编译 task_graph 到 WASM（验证） |
| T4.3 | 编译 Qt6 应用到 WASM |
| T4.4 | 测试 WASM 端完整功能 |

### 阶段五：测试与优化（2 周）

| 任务 | 描述 |
|---|---|
| T5.1 | 单元测试 |
| T5.2 | 集成测试 |
| T5.3 | WASM 性能优化 |
| T5.4 | 跨平台兼容性测试 |

---

## 十三、代码规范

### 13.1 命名规范

| 类型 | 规范 | 示例 |
|---|---|---|
| 类名 | PascalCase | GraphViewModel |
| 方法名 | camelCase | addTask |
| 属性名 | camelCase | taskId |
| 变量名 | camelCase | taskId |
| 常量名 | UPPER_CASE_UNDERSCORE | MAX_NODE_COUNT |
| 文件命名 | snake_case | graph_view_model.hpp |

### 13.2 编码规范

- 使用 C++20 标准
- 头文件保护：`#pragma once`
- 智能指针优先：`std::unique_ptr` / `std::shared_ptr`
- 避免裸 `new` / `delete`
- 使用 `const` 引用传递大对象
- 异常安全：使用 RAII
- Qt 类继承需添加 `Q_OBJECT` 宏

### 13.3 注释规范

- 类/方法：Doxygen 风格注释
- 复杂逻辑：行内注释说明
- 公共 API：完整的参数和返回值说明

---

## 十四、附录

### A. Qt6 主窗口布局示例

```cpp
class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    MainWindow(GraphViewModel* vm, QWidget* parent = nullptr);
    
private:
    void setupUI();
    void setupToolbar();
    void setupSidebar();
    void setupCanvas();
    void setupStatusBar();
    
    GraphViewModel* vm_;
    GraphScene* scene_;
    QGraphicsView* view_;
    QToolBar* toolbar_;
    QDockWidget* sidebar_;
    QStatusBar* statusBar_;
};
```

### B. 依赖关系图

```
graph_studio
├── Qt6 (UI Framework)
│   ├── Widgets (QMainWindow/QToolBar/QDockWidget)
│   ├── Graphics (QGraphicsScene/QGraphicsView/QGraphicsItem)
│   └── Core (QObject/Q_PROPERTY/Signals/Slots)
├── task_graph (Core Framework)
│   ├── DAG (Graph Structure)
│   ├── Executor (Task Scheduler)
│   ├── PluginAPI (Plugin System)
│   └── Profiler (Performance Profiling)
└── Qt for WebAssembly
    ├── Emscripten (Compiler)
    └── WebGL2 Renderer
```

---

*文档版本: 2.0*  
*最后更新: 2026-07-26*  
*适用项目: task_graph Studio*  
*框架: Qt6*