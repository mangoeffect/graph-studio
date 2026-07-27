#include "view/MainWindow.h"
#include "view/GraphScene.h"
#include "view/GraphView.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include "viewmodel/GraphViewModel.h"

#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QPainter>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QShortcut>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

using namespace graph_studio;

static QString edgeKey(const QString& from, const QString& to)
{
    return from + "->" + to;
}

MainWindow::MainWindow(GraphViewModel& vm, QWidget* parent)
    : QMainWindow(parent), vm_(vm), commandStack_(this)
{
    setWindowTitle("Graph Studio");
    resize(1600, 1000);
    setMinimumSize(1200, 700);

    ApplyDarkTheme();
    InitializeLayout();
    ConnectSignals();

    // Seed a demo graph through the ViewModel
    vm_.addTask("file_input", -300, -50, "file_input");
    vm_.addTask("opencv_blur_filter", -50, -50);
    vm_.addTask("opencv_sobel_filter", 200, -100);
    vm_.addTask("display", 450, -50);
    vm_.addTask("save_image", 200, 100);
    vm_.addEdge("file_input", "opencv_blur_filter");
    vm_.addEdge("opencv_blur_filter", "opencv_sobel_filter");
    vm_.addEdge("opencv_sobel_filter", "display");
    vm_.addEdge("opencv_blur_filter", "save_image");

    graphicsView_->centerOn(0, 0);
    UpdateStatusBar();
    UpdateUndoRedoActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::ApplyDarkTheme()
{
    setStyleSheet(R"(
        QMainWindow { background-color: #1e1e1e; }
        QWidget { color: #d4d4d4; background-color: #1e1e1e; font-size: 13px; }

        QMenuBar { background-color: #2d2d30; color: #d4d4d4; border-bottom: 1px solid #3c3c3c; padding: 2px; }
        QMenuBar::item { padding: 4px 12px; background: transparent; }
        QMenuBar::item:selected { background-color: #3c3c3c; }
        QMenu { background-color: #2d2d30; border: 1px solid #3c3c3c; }
        QMenu::item { padding: 6px 24px; }
        QMenu::item:selected { background-color: #094771; }

        QToolBar { background-color: #2d2d30; border: none; border-bottom: 1px solid #3c3c3c; padding: 3px; spacing: 3px; }
        QToolBar::separator { background-color: #3c3c3c; width: 1px; margin: 4px 6px; }

        QStatusBar { background-color: #007acc; color: #ffffff; border-top: 1px solid #3c3c3c; }
        QStatusBar::item { border: none; }

        QSplitter::handle { background-color: #3c3c3c; }
        QSplitter::handle:horizontal { width: 2px; }
        QSplitter::handle:vertical { height: 2px; }

        QGroupBox {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 10px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
        }

        QListWidget {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            outline: none;
        }
        QListWidget::item { padding: 6px 10px; border-bottom: 1px solid #2d2d30; }
        QListWidget::item:selected { background-color: #094771; color: #ffffff; }
        QListWidget::item:hover { background-color: #2a2d2e; }

        QPlainTextEdit {
            background-color: #1e1e1e;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            font-family: "Menlo", "Consolas", monospace;
            font-size: 12px;
        }

        QGraphicsView {
            background-color: #1a1a1a;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
        }

        QLabel { background-color: transparent; }
        QLabel#ImageResultLabel {
            background-color: #1a1a1a;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            qproperty-alignment: AlignCenter;
            color: #666666;
        }

        QLineEdit, QSpinBox, QComboBox, QDoubleSpinBox {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            border-radius: 3px;
            padding: 3px 6px;
            color: #d4d4d4;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #007acc; }
        QLineEdit:read-only { color: #999999; }

        QPushButton {
            background-color: #0e639c;
            color: #ffffff;
            border: none;
            padding: 5px 16px;
            border-radius: 3px;
        }
        QPushButton:hover { background-color: #1177bb; }
        QPushButton:pressed { background-color: #094771; }

        QScrollBar:vertical { background-color: #1e1e1e; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background-color: #424242; border-radius: 6px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background-color: #4f4f4f; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QScrollBar:horizontal { background-color: #1e1e1e; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background-color: #424242; border-radius: 6px; min-width: 30px; }
        QScrollBar::handle:horizontal:hover { background-color: #4f4f4f; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        QScrollArea { border: none; background-color: transparent; }
        QToolTip { background-color: #2d2d30; color: #d4d4d4; border: 1px solid #3c3c3c; }
    )");
}

void MainWindow::InitializeLayout()
{
    CreateMenuBar();
    CreateToolbar();

    mainSplitter_ = new QSplitter(Qt::Vertical, this);

    topSplitter_ = new QSplitter(Qt::Horizontal, mainSplitter_);
    topSplitter_->addWidget(CreateTaskPanel());

    CreateCanvas();
    topSplitter_->addWidget(graphicsView_);

    topSplitter_->addWidget(CreateImageResultPanel());

    bottomSplitter_ = new QSplitter(Qt::Horizontal, mainSplitter_);
    bottomSplitter_->addWidget(CreateNodePropertyPanel());
    bottomSplitter_->addWidget(CreateLogPanel());
    bottomSplitter_->addWidget(CreateOutputPanel());

    mainSplitter_->addWidget(topSplitter_);
    mainSplitter_->addWidget(bottomSplitter_);

    mainSplitter_->setStretchFactor(0, 3);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setSizes({700, 250});

    topSplitter_->setStretchFactor(0, 1);
    topSplitter_->setStretchFactor(1, 4);
    topSplitter_->setStretchFactor(2, 2);
    topSplitter_->setSizes({200, 800, 400});

    bottomSplitter_->setStretchFactor(0, 1);
    bottomSplitter_->setStretchFactor(1, 2);
    bottomSplitter_->setStretchFactor(2, 1);
    bottomSplitter_->setSizes({250, 500, 250});

    setCentralWidget(mainSplitter_);

    CreateStatusBar();
}

void MainWindow::ConnectSignals()
{
    // ViewModel → MainWindow
    connect(&vm_, &GraphViewModel::taskAdded, this, &MainWindow::onTaskAdded);
    connect(&vm_, &GraphViewModel::taskRemoved, this, &MainWindow::onTaskRemoved);
    connect(&vm_, &GraphViewModel::edgeAdded, this, &MainWindow::onEdgeAdded);
    connect(&vm_, &GraphViewModel::edgeRemoved, this, &MainWindow::onEdgeRemoved);
    connect(&vm_, &GraphViewModel::nodeMoved, this, &MainWindow::onNodeMovedVm);
    connect(&vm_, &GraphViewModel::graphReset, this, &MainWindow::onGraphReset);
    connect(&vm_, &GraphViewModel::logMessage, this, &MainWindow::onLogMessage);
    connect(&vm_, &GraphViewModel::selectionChanged, this, &MainWindow::onSelectionChangedVm);
    connect(&vm_, &GraphViewModel::nodeParamsChanged, this, [this](const QString& id){
        // 当前选中节点的参数被改（含 undo/redo、外部调用）后，同步刷新参数控件
        if (!id.isEmpty() && id == vm_.selectedNodeId()) {
            RebuildParamWidgets(id);
        }
    });
    connect(&vm_, &GraphViewModel::taskCountChanged, this, &MainWindow::UpdateStatusBar);
    connect(&vm_, &GraphViewModel::edgeCountChanged, this, &MainWindow::UpdateStatusBar);

    // Scene → MainWindow
    connect(scene_, &GraphScene::edgeCreationRequested, this, &MainWindow::onEdgeCreationRequested);
    connect(scene_, &GraphScene::nodeMoved, this, &MainWindow::onNodeMovedScene);
    connect(scene_, &GraphScene::nodeDoubleClicked, this, &MainWindow::onNodeDoubleClicked);
    connect(scene_, &GraphScene::nodeCreateRequested, this, &MainWindow::CreateNodeAt);
    connect(scene_, &QGraphicsScene::selectionChanged, this, &MainWindow::onSceneSelectionChanged);

    // GraphView → MainWindow
    connect(graphicsView_, &GraphView::taskDropped, this, [this](const QString& taskType, const QPointF& pos) {
        CreateNodeAt(taskType, pos);
    });
    connect(graphicsView_, &GraphView::zoomChanged, this, [this](qreal factor) {
        if (zoomLabel_)
            zoomLabel_->setText(QString("Zoom: %1%").arg(int(factor * 100)));
    });

    // Task list double-click → add at view center
    connect(taskList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
            return;
        QPointF center = graphicsView_->mapToScene(graphicsView_->viewport()->rect().center());
        CreateNodeAt(item->text(), center);
    });

    // Command stack
    connect(&commandStack_, &CommandStack::canUndoChanged, this, &MainWindow::UpdateUndoRedoActions);
    connect(&commandStack_, &CommandStack::canRedoChanged, this, &MainWindow::UpdateUndoRedoActions);
}

void MainWindow::CreateMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    connect(fileMenu->addAction("New"), &QAction::triggered, this, &MainWindow::ActionNew);
    connect(fileMenu->addAction("Open..."), &QAction::triggered, this, &MainWindow::ActionOpen);
    connect(fileMenu->addAction("Save"), &QAction::triggered, this, &MainWindow::ActionSave);
    connect(fileMenu->addAction("Save As..."), &QAction::triggered, this, &MainWindow::ActionSaveAs);
    fileMenu->addSeparator();
    connect(fileMenu->addAction("Exit"), &QAction::triggered, this, &QMainWindow::close);

    auto* editMenu = menuBar()->addMenu("&Edit");
    undoAction_ = editMenu->addAction("Undo");
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = editMenu->addAction("Redo");
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::ActionUndo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::ActionRedo);
    editMenu->addSeparator();
    auto* deleteAction = editMenu->addAction("Delete");
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::DeleteSelected);

    auto* viewMenu = menuBar()->addMenu("&View");
    connect(viewMenu->addAction("Zoom In"), &QAction::triggered, this, &MainWindow::ActionZoomIn);
    connect(viewMenu->addAction("Zoom Out"), &QAction::triggered, this, &MainWindow::ActionZoomOut);
    connect(viewMenu->addAction("Fit to View"), &QAction::triggered, this, &MainWindow::ActionFitToView);
    viewMenu->addSeparator();
    connect(viewMenu->addAction("Auto Layout"), &QAction::triggered, this, &MainWindow::ActionAutoLayout);

    auto* helpMenu = menuBar()->addMenu("&Help");
    connect(helpMenu->addAction("About"), &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Graph Studio",
            "Graph Studio\nA DAG visual editor for task_graph.\n\n"
            "Controls:\n"
            "  Ctrl+Scroll - Zoom\n"
            "  Middle-drag - Pan\n"
            "  Drag task from left panel - Create node\n"
            "  Drag output port to input port - Create edge\n"
            "  Delete - Remove selected\n");
    });
}

void MainWindow::CreateToolbar()
{
    toolbar_ = new QToolBar("Main Toolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(20, 20));

    auto* btnNew = toolbar_->addAction("New");
    connect(btnNew, &QAction::triggered, this, &MainWindow::ActionNew);
    auto* btnOpen = toolbar_->addAction("Open");
    connect(btnOpen, &QAction::triggered, this, &MainWindow::ActionOpen);
    auto* btnSave = toolbar_->addAction("Save");
    connect(btnSave, &QAction::triggered, this, &MainWindow::ActionSave);
    toolbar_->addSeparator();
    toolbar_->addAction(undoAction_);
    toolbar_->addAction(redoAction_);
    toolbar_->addSeparator();
    auto* btnLayout = toolbar_->addAction("Auto Layout");
    connect(btnLayout, &QAction::triggered, this, &MainWindow::ActionAutoLayout);
    toolbar_->addSeparator();
    auto* btnZoomIn = toolbar_->addAction("Zoom +");
    connect(btnZoomIn, &QAction::triggered, this, &MainWindow::ActionZoomIn);
    auto* btnZoomOut = toolbar_->addAction("Zoom -");
    connect(btnZoomOut, &QAction::triggered, this, &MainWindow::ActionZoomOut);
    auto* btnFit = toolbar_->addAction("Fit");
    connect(btnFit, &QAction::triggered, this, &MainWindow::ActionFitToView);

    addToolBar(toolbar_);
}

QWidget* MainWindow::CreateTaskPanel()
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QLabel("Task Library");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(12);
    header->setFont(font);
    header->setStyleSheet("padding: 4px;");
    layout->addWidget(header);

    taskList_ = new TaskListWidget();
    taskList_->setDragEnabled(true);
    taskList_->setDragDropMode(QAbstractItemView::DragOnly);
    taskList_->setDefaultDropAction(Qt::CopyAction);
    taskList_->setToolTip("Drag to canvas or double-click to add");
    layout->addWidget(taskList_);

    PopulateTaskLibrary();

    return container;
}

void MainWindow::PopulateTaskLibrary()
{
    if (!taskList_) return;

    // 从 PluginRegistry 动态获取已注册的 task 类型，按名称前缀分组显示
    QStringList allTypes = vm_.availableTaskTypes();
    allTypes.sort();

    auto classify = [](const QString& type) -> QString {
        if (type.startsWith("opencv_"))   return "OpenCV Filter";
        if (type.contains("input") || type.contains("load"))  return "Input";
        if (type.contains("output") || type.contains("save") || type.contains("display")) return "Output";
        return "Process";
    };

    // 分组（保留首次出现顺序）
    QStringList sections;
    QHash<QString, QStringList> bySection;
    for (const auto& t : allTypes) {
        const QString s = classify(t);
        if (!bySection.contains(s)) sections.append(s);
        bySection[s].append(t);
    }

    auto addSection = [this](const QString& title) {
        auto* item = new QListWidgetItem("-- " + title + " --");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
        item->setForeground(QColor("#888888"));
        taskList_->addItem(item);
    };

    auto addTaskItem = [this](const QString& name) {
        auto* item = new QListWidgetItem(name);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        taskList_->addItem(item);
    };

    for (const auto& s : sections) {
        addSection(s);
        for (const auto& t : bySection[s]) addTaskItem(t);
    }

    // 若注册表为空，给出提示
    if (allTypes.isEmpty()) {
        auto* item = new QListWidgetItem("(no tasks registered)");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
        item->setForeground(QColor("#888888"));
        taskList_->addItem(item);
    }
}

QWidget* MainWindow::CreateImageResultPanel()
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QLabel("Image Result");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(12);
    header->setFont(font);
    header->setStyleSheet("padding: 4px;");
    layout->addWidget(header);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    imageResultLabel_ = new QLabel("No image");
    imageResultLabel_->setObjectName("ImageResultLabel");
    imageResultLabel_->setMinimumSize(300, 200);
    scrollArea->setWidget(imageResultLabel_);
    layout->addWidget(scrollArea);

    return container;
}

QWidget* MainWindow::CreateNodePropertyPanel()
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QLabel("Node Properties");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(12);
    header->setFont(font);
    header->setStyleSheet("padding: 4px;");
    layout->addWidget(header);

    nodePropertyGroup_ = new QGroupBox("Selected Node");
    nodePropertyLayout_ = new QFormLayout(nodePropertyGroup_);

    propIdEdit_ = new QLineEdit();
    propIdEdit_->setReadOnly(true);
    propTypeEdit_ = new QLineEdit();
    propTypeEdit_->setReadOnly(true);
    propXEdit_ = new QLineEdit();
    propXEdit_->setReadOnly(true);
    propYEdit_ = new QLineEdit();
    propYEdit_->setReadOnly(true);

    nodePropertyLayout_->addRow("Node ID:", propIdEdit_);
    nodePropertyLayout_->addRow("Type:", propTypeEdit_);
    nodePropertyLayout_->addRow("X:", propXEdit_);
    nodePropertyLayout_->addRow("Y:", propYEdit_);

    layout->addWidget(nodePropertyGroup_);

    // 参数表单容器：选中节点时按 paramSpecs 动态填充控件
    paramsGroup_ = new QGroupBox("Parameters");
    paramsLayout_ = new QFormLayout(paramsGroup_);
    layout->addWidget(paramsGroup_);
    layout->addStretch();

    ClearPropertyPanel();
    return container;
}

QWidget* MainWindow::CreateLogPanel()
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QLabel("Log");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(12);
    header->setFont(font);
    header->setStyleSheet("padding: 4px;");
    layout->addWidget(header);

    logWidget_ = new QPlainTextEdit();
    logWidget_->setReadOnly(true);
    logWidget_->appendPlainText("[INFO] Graph Studio started.");
    logWidget_->appendPlainText("[INFO] Ready.");
    layout->addWidget(logWidget_);

    return container;
}

QWidget* MainWindow::CreateOutputPanel()
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QLabel("Output");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(12);
    header->setFont(font);
    header->setStyleSheet("padding: 4px;");
    layout->addWidget(header);

    outputWidget_ = new QPlainTextEdit();
    outputWidget_->setReadOnly(true);
    outputWidget_->appendPlainText("Waiting for execution...");
    layout->addWidget(outputWidget_);

    return container;
}

void MainWindow::CreateCanvas()
{
    scene_ = new GraphScene(this);
    graphicsView_ = new GraphView(scene_);
    graphicsView_->setRenderHint(QPainter::Antialiasing);
    graphicsView_->setMinimumWidth(400);
}

void MainWindow::CreateStatusBar()
{
    statusBar_ = new QStatusBar(this);
    zoomLabel_ = new QLabel("Zoom: 100%");
    zoomLabel_->setStyleSheet("color: #ffffff; padding: 0 8px;");
    statusBar_->addPermanentWidget(zoomLabel_);
    statusBar_->showMessage("Ready");
    setStatusBar(statusBar_);
}

// ---- ViewModel signal handlers ----

void MainWindow::onTaskAdded(const NodeData& node)
{
    auto* item = new NodeItem(node.id, node.type);
    item->setPos(node.x, node.y);
    scene_->addItem(item);
    nodeItems_[node.id] = item;
}

void MainWindow::onTaskRemoved(const QString& taskId)
{
    // 先删除关联的 edge（完整删除对象 + 从两端 node 的 edges_ 注销）
    QString prefix = taskId + "->";
    QString suffix = "->" + taskId;
    for (auto edgeIt = edgeItems_.begin(); edgeIt != edgeItems_.end();) {
        const QString& key = edgeIt.key();
        if (key.startsWith(prefix) || key.endsWith(suffix)) {
            auto* edge = edgeIt.value();
            if (edge) {
                if (auto* s = edge->sourceNode()) s->unregisterEdge(edge);
                if (auto* t = edge->targetNode()) t->unregisterEdge(edge);
                scene_->removeItem(edge);
                delete edge;
            }
            edgeIt = edgeItems_.erase(edgeIt);
        } else {
            ++edgeIt;
        }
    }
    // 再删除 node 本身
    auto it = nodeItems_.find(taskId);
    if (it != nodeItems_.end()) {
        scene_->removeItem(it.value());
        delete it.value();
        nodeItems_.erase(it);
    }
    ClearPropertyPanel();
}

void MainWindow::onEdgeAdded(const EdgeData& edge)
{
    auto* src = nodeItems_.value(edge.fromId);
    auto* tgt = nodeItems_.value(edge.toId);
    if (!src || !tgt)
        return;

    auto* edgeItem = new EdgeItem(src, tgt);
    scene_->addItem(edgeItem);
    edgeItems_[edgeKey(edge.fromId, edge.toId)] = edgeItem;
}

void MainWindow::onEdgeRemoved(const QString& fromId, const QString& toId)
{
    QString key = edgeKey(fromId, toId);
    auto it = edgeItems_.find(key);
    if (it != edgeItems_.end()) {
        auto* edge = it.value();
        // 从两端 node 的 edges_ 集合注销，避免悬空引用
        if (auto* s = edge->sourceNode()) s->unregisterEdge(edge);
        if (auto* t = edge->targetNode()) t->unregisterEdge(edge);
        scene_->removeItem(edge);
        delete edge;
        edgeItems_.erase(it);
    }
}

void MainWindow::onNodeMovedVm(const QString& id, qreal x, qreal y)
{
    auto* item = nodeItems_.value(id);
    if (item) {
        item->setPos(x, y);
    }
    // Update property panel if this node is selected
    if (vm_.selectedNodeId() == id) {
        UpdatePropertyPanel(id);
    }
}

void MainWindow::onGraphReset()
{
    // Clear all scene items
    edgeItems_.clear();
    nodeItems_.clear();
    scene_->clear();
    ClearPropertyPanel();
}

void MainWindow::onLogMessage(const QString& msg)
{
    if (logWidget_)
        logWidget_->appendPlainText(msg);
}

void MainWindow::onSelectionChangedVm(const QString& nodeId)
{
    UpdatePropertyPanel(nodeId);
}

// ---- Scene signal handlers ----

void MainWindow::onSceneSelectionChanged()
{
    // Find selected node or edge
    auto selected = scene_->selectedItems();
    QString selectedNodeId;

    for (auto* item : selected) {
        if (item->type() == NodeItem::Type) {
            selectedNodeId = static_cast<NodeItem*>(item)->nodeId();
            break;
        }
    }

    if (selectedNodeId.isEmpty()) {
        vm_.clearSelection();
    } else {
        vm_.selectNode(selectedNodeId);
    }
}

void MainWindow::onEdgeCreationRequested(const QString& fromId, const QString& toId)
{
    commandStack_.push(std::make_unique<AddEdgeCommand>(vm_, fromId, toId));
}

void MainWindow::onNodeMovedScene(const QString& id, qreal x, qreal y)
{
    vm_.moveNode(id, x, y);
}

void MainWindow::onNodeDoubleClicked(const QString& id)
{
    vm_.selectNode(id);
}

// ---- Actions ----

void MainWindow::DeleteSelected()
{
    auto selected = scene_->selectedItems();
    QStringList nodesToDelete;
    QStringList edgesToDelete; // pairs: from, to

    for (auto* item : selected) {
        if (item->type() == NodeItem::Type) {
            nodesToDelete << static_cast<NodeItem*>(item)->nodeId();
        } else if (item->type() == EdgeItem::Type) {
            auto* edge = static_cast<EdgeItem*>(item);
            if (edge->sourceNode() && edge->targetNode()) {
                edgesToDelete << edge->fromId() << edge->toId();
            }
        }
    }

    if (nodesToDelete.isEmpty() && edgesToDelete.isEmpty())
        return;

    // Build a macro command: edges first, then nodes
    auto macro = std::make_unique<MacroCommand>("Delete Selection");
    for (int i = 0; i < edgesToDelete.size(); i += 2) {
        macro->add(std::make_unique<RemoveEdgeCommand>(vm_, edgesToDelete[i], edgesToDelete[i + 1]));
    }
    for (const auto& id : nodesToDelete) {
        macro->add(std::make_unique<RemoveTaskCommand>(vm_, id));
    }

    commandStack_.push(std::move(macro));
}

void MainWindow::CreateNodeAt(const QString& taskType, const QPointF& scenePos)
{
    commandStack_.push(std::make_unique<AddTaskCommand>(vm_, taskType, scenePos.x(), scenePos.y()));
}

void MainWindow::UpdateStatusBar()
{
    if (statusBar_) {
        statusBar_->showMessage(QString("Nodes: %1  |  Edges: %2")
                                    .arg(vm_.taskCount())
                                    .arg(vm_.edgeCount()));
    }
}

void MainWindow::UpdatePropertyPanel(const QString& nodeId)
{
    if (nodeId.isEmpty() || !vm_.hasNode(nodeId)) {
        ClearPropertyPanel();
        return;
    }

    if (!propIdEdit_) {
        ClearPropertyPanel();
        return;
    }

    NodeData data = vm_.nodeData(nodeId);
    propIdEdit_->setText(data.id);
    propTypeEdit_->setText(data.type);
    propXEdit_->setText(QString::number(data.x, 'f', 1));
    propYEdit_->setText(QString::number(data.y, 'f', 1));

    RebuildParamWidgets(nodeId);
}

void MainWindow::ClearPropertyPanel()
{
    if (propIdEdit_) propIdEdit_->setText("(none)");
    if (propTypeEdit_) propTypeEdit_->setText("(none)");
    if (propXEdit_) propXEdit_->setText("-");
    if (propYEdit_) propYEdit_->setText("-");
    // 清空动态参数控件
    if (paramsLayout_) {
        while (paramsLayout_->rowCount() > 0) {
            paramsLayout_->removeRow(0);
        }
    }
    paramWidgets_.clear();
}

// 按选中节点的 paramSpecs 动态生成参数控件（int→SpinBox、float→DoubleSpinBox、
// string→LineEdit、bool→CheckBox、enum→ComboBox）。
void MainWindow::RebuildParamWidgets(const QString& nodeId)
{
    if (!paramsLayout_) return;
    // 清空旧控件
    while (paramsLayout_->rowCount() > 0) {
        paramsLayout_->removeRow(0);
    }
    paramWidgets_.clear();
    if (nodeId.isEmpty() || !vm_.hasNode(nodeId)) return;

    NodeData data = vm_.nodeData(nodeId);
    QVariantList specs = vm_.paramSpecs(data.type);
    QVariantMap current = vm_.nodeParams(nodeId);

    for (const QVariant& sv : specs) {
        QVariantMap s = sv.toMap();
        QString name = s.value("name").toString();
        QString type = s.value("type").toString();
        QWidget* w = nullptr;

        if (type == "int") {
            auto* sb = new QSpinBox();
            if (s.contains("min")) sb->setMinimum(int(s.value("min").toDouble()));
            if (s.contains("max")) sb->setMaximum(int(s.value("max").toDouble()));
            if (s.contains("step")) sb->setSingleStep(int(s.value("step").toDouble()));
            sb->setValue(current.value(name, s.value("default")).toInt());
            connect(sb, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, name](int){ OnParamWidgetChanged(name); });
            w = sb;
        } else if (type == "float") {
            auto* sb = new QDoubleSpinBox();
            sb->setDecimals(3);
            if (s.contains("min")) sb->setMinimum(s.value("min").toDouble());
            if (s.contains("max")) sb->setMaximum(s.value("max").toDouble());
            if (s.contains("step")) sb->setSingleStep(s.value("step").toDouble());
            sb->setValue(current.value(name, s.value("default")).toDouble());
            connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, name](double){ OnParamWidgetChanged(name); });
            w = sb;
        } else if (type == "string") {
            auto* le = new QLineEdit();
            le->setText(current.value(name, s.value("default")).toString());
            connect(le, &QLineEdit::editingFinished, this, [this, name, le](){
                OnParamWidgetChanged(name);
            });
            w = le;
        } else if (type == "bool") {
            auto* cb = new QCheckBox();
            cb->setChecked(current.value(name, s.value("default")).toBool());
            connect(cb, &QCheckBox::toggled, this, [this, name](bool){
                OnParamWidgetChanged(name);
            });
            w = cb;
        } else if (type == "enum") {
            auto* combo = new QComboBox();
            QVariantList labels = s.value("enumLabels").toList();
            QVariantList values = s.value("enumValues").toList();
            int curVal = current.value(name, s.value("default")).toInt();
            int selectIdx = 0;
            for (int i = 0; i < labels.size() && i < values.size(); ++i) {
                combo->addItem(labels[i].toString(), values[i]);
                if (values[i].toInt() == curVal) selectIdx = i;
            }
            combo->setCurrentIndex(selectIdx);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, name](int){ OnParamWidgetChanged(name); });
            w = combo;
        }
        if (w) {
            paramsLayout_->addRow(name + ":", w);
            paramWidgets_[name] = w;
        }
    }
}

// 控件值变化 -> 走 ChangeParamCommand（支持 undo/redo）-> VM.setNodeParam
void MainWindow::OnParamWidgetChanged(const QString& key)
{
    QString nodeId = vm_.selectedNodeId();
    if (nodeId.isEmpty() || !paramWidgets_.contains(key)) return;
    QWidget* w = paramWidgets_[key];
    QVariant newValue;
    if (auto* sb = qobject_cast<QSpinBox*>(w)) newValue = sb->value();
    else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) newValue = dsb->value();
    else if (auto* le = qobject_cast<QLineEdit*>(w)) newValue = le->text();
    else if (auto* cb = qobject_cast<QCheckBox*>(w)) newValue = cb->isChecked();
    else if (auto* combo = qobject_cast<QComboBox*>(w)) newValue = combo->currentData();

    commandStack_.push(std::make_unique<ChangeParamCommand>(vm_, nodeId, key, newValue));
}

void MainWindow::ActionNew()
{
    vm_.clear();
    commandStack_.clear();
    currentFilePath_.clear();
}

void MainWindow::ActionOpen()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Graph", QString(), "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty())
        return;
    if (vm_.loadFromFile(path)) {
        currentFilePath_ = path;
    }
}

void MainWindow::ActionSave()
{
    if (currentFilePath_.isEmpty()) {
        ActionSaveAs();
    } else {
        vm_.saveToFile(currentFilePath_);
    }
}

void MainWindow::ActionSaveAs()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Graph", "graph.json", "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".json"))
        path += ".json";
    if (vm_.saveToFile(path)) {
        currentFilePath_ = path;
    }
}

void MainWindow::ActionAutoLayout()
{
    vm_.autoLayout();
}

void MainWindow::ActionZoomIn()
{
    graphicsView_->scale(1.2, 1.2);
    if (zoomLabel_)
        zoomLabel_->setText(QString("Zoom: %1%").arg(int(graphicsView_->transform().m11() * 100)));
}

void MainWindow::ActionZoomOut()
{
    graphicsView_->scale(1.0 / 1.2, 1.0 / 1.2);
    if (zoomLabel_)
        zoomLabel_->setText(QString("Zoom: %1%").arg(int(graphicsView_->transform().m11() * 100)));
}

void MainWindow::ActionFitToView()
{
    if (nodeItems_.isEmpty()) {
        graphicsView_->resetTransform();
        graphicsView_->centerOn(0, 0);
    } else {
        QRectF boundingRect;
        for (auto* item : nodeItems_) {
            boundingRect = boundingRect.united(item->mapToScene(item->boundingRect()).boundingRect());
        }
        graphicsView_->fitInView(boundingRect.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
    }
    if (zoomLabel_)
        zoomLabel_->setText(QString("Zoom: %1%").arg(int(graphicsView_->transform().m11() * 100)));
}

void MainWindow::ActionUndo()
{
    commandStack_.undo();
}

void MainWindow::ActionRedo()
{
    commandStack_.redo();
}

void MainWindow::UpdateUndoRedoActions()
{
    bool canUndo = commandStack_.canUndo();
    bool canRedo = commandStack_.canRedo();

    if (undoAction_) {
        undoAction_->setEnabled(canUndo);
        undoAction_->setText(canUndo ? QString("Undo: %1").arg(commandStack_.undoDescription()) : "Undo");
    }
    if (redoAction_) {
        redoAction_->setEnabled(canRedo);
        redoAction_->setText(canRedo ? QString("Redo: %1").arg(commandStack_.redoDescription()) : "Redo");
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        DeleteSelected();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
