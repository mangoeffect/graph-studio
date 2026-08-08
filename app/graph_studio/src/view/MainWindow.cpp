#include "view/MainWindow.h"
#include "view/GraphScene.h"
#include "view/GraphView.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include "view/ProfilePanel.h"
#include "view/GpuImageViewer.h"
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
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <QScrollArea>
#include <QPainter>
#include <QPixmap>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QKeyEvent>
#include <QShortcut>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

using namespace graph_studio;

static QString edgeKey(const QString& from, const QString& fromPort,
                       const QString& to, const QString& toPort)
{
    return from + ":" + fromPort + "->" + to + ":" + toPort;
}

static QString edgeKey(const EdgeData& e)
{
    return edgeKey(e.fromId, e.fromPort, e.toId, e.toPort);
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

    graphicsView_->centerOn(0, 0);
    UpdateStatusBar();
    UpdateUndoRedoActions();
}

MainWindow::~MainWindow()
{
    // 断开 scene 到本窗口的所有连接，防止子对象析构时 (deleteChildren)
    // 删除处于选中状态的 item 发出 selectionChanged，回调半析构的 MainWindow。
    if (scene_) {
        scene_->disconnect(this);
    }
}

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

        QTabWidget::pane { border: 1px solid #3c3c3c; background-color: #1e1e1e; }
        QTabBar::tab {
            background-color: #2d2d30;
            color: #d4d4d4;
            border: 1px solid #3c3c3c;
            padding: 4px 16px;
            border-top-left-radius: 3px;
            border-top-right-radius: 3px;
        }
        QTabBar::tab:selected { background-color: #1e1e1e; border-bottom: 2px solid #007acc; }
        QTabBar::tab:hover:!selected { background-color: #3c3c3c; }
        QTabBar { background-color: #2d2d30; }
        QTableWidget {
            background-color: #1e1e1e;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            gridline-color: #3c3c3c;
            color: #d4d4d4;
        }
        QTableWidget::item { padding: 2px; }
        QTableWidget::item:selected { background-color: #094771; color: #ffffff; }
        QHeaderView::section {
            background-color: #2d2d30;
            color: #d4d4d4;
            border: 1px solid #3c3c3c;
            padding: 3px;
            font-weight: bold;
        }
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
    bottomSplitter_->addWidget(CreateBottomTabs());

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
    bottomSplitter_->setStretchFactor(1, 4);
    bottomSplitter_->setSizes({250, 900});

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
        // 当前选中节点的参数被改（含 undo/redo、外部调用）后，同步刷新参数控件。
        // 注意：必须用 QTimer::singleShot(0,...) defer 到下一轮事件循环，
        // 避免在 widget 自己的 signal/event 处理栈里删除 widget（use-after-free）。
        // 另：由 OnParamWidgetChanged 触发的（selfParamEdit_）说明控件已是新值，
        // 跳过刷新即可，不需要重建。
        if (selfParamEdit_) return;
        QString target = id;
        QTimer::singleShot(0, this, [this, target]() {
            if (!target.isEmpty() && target == vm_.selectedNodeId()) {
                RebuildParamWidgets(target);
            }
        });
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

    // 执行相关（ViewModel 已用 QueuedConnection marshal 回 UI 线程）
    connect(&vm_, &GraphViewModel::nodeStatusChanged, this, &MainWindow::onNodeStatusChanged);
    connect(&vm_, &GraphViewModel::executionStarted, this, &MainWindow::onExecutionStarted);
    connect(&vm_, &GraphViewModel::executionFinished, this, &MainWindow::onExecutionFinished);
    connect(&vm_, &GraphViewModel::executingChanged, this, &MainWindow::onExecutingChanged);

    // 图像结果：执行完成且采集到图像后，填充下拉框并默认显示
    connect(&vm_, &GraphViewModel::imageResultsReady, this, &MainWindow::onImageResultsReady);
    if (resultSelector_) {
        connect(resultSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MainWindow::onResultSelectorChanged);
    }

    // 性能分析：执行完成后填充 Profile tab
    if (profilePanel_) {
        connect(&vm_, &GraphViewModel::profileDataReady,
                profilePanel_, &ProfilePanel::onProfileDataReady);
    }
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

    auto* runMenu = menuBar()->addMenu("&Run");
    runAction_ = runMenu->addAction("Run");
    runAction_->setShortcut(QKeySequence("Ctrl+R"));
    connect(runAction_, &QAction::triggered, this, &MainWindow::ActionRun);
    stopAction_ = runMenu->addAction("Stop");
    stopAction_->setShortcut(QKeySequence("Ctrl+."));
    stopAction_->setEnabled(false);
    connect(stopAction_, &QAction::triggered, this, &MainWindow::ActionStop);

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
    toolbar_->addAction(runAction_);
    toolbar_->addAction(stopAction_);
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

    resultSelector_ = new QComboBox();
    resultSelector_->setPlaceholderText(QStringLiteral("No results"));
    resultSelector_->setEnabled(false);
    layout->addWidget(resultSelector_);

    // GPU-accelerated image viewer
    imageViewer_ = new GpuImageViewer();
    imageViewer_->setMinimumSize(300, 200);
    layout->addWidget(imageViewer_, 1);

    // Pixel info bar
    pixelInfoLabel_ = new QLabel("x: -, y: -");
    pixelInfoLabel_->setStyleSheet(
        "color: #d4d4d4; background-color: #2d2d30; padding: 2px 6px; "
        "border-top: 1px solid #3c3c3c; font-family: Menlo, Consolas, monospace; font-size: 11px;");
    layout->addWidget(pixelInfoLabel_);

    connect(imageViewer_, &GpuImageViewer::pixelInfoChanged,
            pixelInfoLabel_, &QLabel::setText);

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
    logWidget_->setMaximumBlockCount(5000);
    logWidget_->appendHtml("<span style=\"color:#d4d4d4\">[INFO] Graph Studio started.</span>");
    logWidget_->appendHtml("<span style=\"color:#d4d4d4\">[INFO] Ready.</span>");
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

QWidget* MainWindow::CreateBottomTabs()
{
    bottomTabs_ = new QTabWidget();
    bottomTabs_->setTabPosition(QTabWidget::South);

    bottomTabs_->addTab(CreateLogPanel(), "Log");
    bottomTabs_->addTab(CreateOutputPanel(), "Output");

    profilePanel_ = new ProfilePanel(vm_);
    bottomTabs_->addTab(profilePanel_, "Profile");

    return bottomTabs_;
}

void MainWindow::CreateCanvas()
{
    scene_ = new GraphScene(this);
    scene_->setAvailableTaskTypes(vm_.availableTaskTypes());
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
    item->setPorts(node.inputPorts, node.outputPorts);
    item->setPos(node.x, node.y);
    scene_->addItem(item);
    nodeItems_[node.id] = item;
}

void MainWindow::onTaskRemoved(const QString& taskId)
{
    // 先删除关联的 edge（完整删除对象 + 从两端 node 的 edges_ 注销）。
    // 键格式："from:fromPort->to:toPort"，按 "from:" / "->to:" 前缀匹配。
    QString prefix = taskId + ":";
    QString suffix = "->" + taskId + ":";
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

    auto* edgeItem = new EdgeItem(src, tgt, edge.fromPort, edge.toPort);
    scene_->addItem(edgeItem);
    edgeItems_[edgeKey(edge)] = edgeItem;
}

void MainWindow::onEdgeRemoved(const EdgeData& edge)
{
    QString key = edgeKey(edge);
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

void MainWindow::onLogMessage(int level, const QString& msg)
{
    if (!logWidget_) return;

    auto lvl = static_cast<task_graph::LogLevel>(level);
    QString color;
    QString tag;
    switch (lvl) {
        case task_graph::LogLevel::TRACE: color = "#7f8c8d"; tag = "TRACE"; break;
        case task_graph::LogLevel::DEBUG: color = "#7f8c8d"; tag = "DEBUG"; break;
        case task_graph::LogLevel::INFO:  color = "#d4d4d4"; tag = "INFO";  break;
        case task_graph::LogLevel::WARN:  color = "#f39c12"; tag = "WARN";  break;
        case task_graph::LogLevel::ERROR: color = "#e74c3c"; tag = "ERROR"; break;
        case task_graph::LogLevel::FATAL: color = "#e74c3c"; tag = "FATAL"; break;
        default:                          color = "#d4d4d4"; tag = "INFO";  break;
    }
    logWidget_->appendHtml(
        QStringLiteral("<span style=\"color:%1\">[%2] %3</span>")
            .arg(color, tag, msg.toHtmlEscaped()));
}

void MainWindow::onSelectionChangedVm(const QString& nodeId)
{
    UpdatePropertyPanel(nodeId);
    // 联动图像面板：若选中节点有图像结果，自动切换下拉框显示它
    if (resultSelector_ && !nodeId.isEmpty()) {
        for (int i = 0; i < resultSelector_->count(); ++i) {
            if (resultSelector_->itemData(i).toString().section(':', 0, 0) == nodeId) {
                resultSelector_->setCurrentIndex(i);  // 触发 onResultSelectorChanged
                break;
            }
        }
    }
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

void MainWindow::onEdgeCreationRequested(const QString& fromId, const QString& fromPort,
                                         const QString& toId, const QString& toPort)
{
    commandStack_.push(std::make_unique<AddEdgeCommand>(vm_, fromId, fromPort, toId, toPort));
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
    QStringList edgesToDelete; // groups of 4: from, fromPort, to, toPort

    for (auto* item : selected) {
        if (item->type() == NodeItem::Type) {
            nodesToDelete << static_cast<NodeItem*>(item)->nodeId();
        } else if (item->type() == EdgeItem::Type) {
            auto* edge = static_cast<EdgeItem*>(item);
            if (edge->sourceNode() && edge->targetNode()) {
                edgesToDelete << edge->fromId() << edge->sourcePort()
                              << edge->toId() << edge->targetPort();
            }
        }
    }

    if (nodesToDelete.isEmpty() && edgesToDelete.isEmpty())
        return;

    // Build a macro command: edges first, then nodes
    auto macro = std::make_unique<MacroCommand>("Delete Selection");
    for (int i = 0; i < edgesToDelete.size(); i += 4) {
        macro->add(std::make_unique<RemoveEdgeCommand>(vm_,
                                                       edgesToDelete[i], edgesToDelete[i + 1],
                                                       edgesToDelete[i + 2], edgesToDelete[i + 3]));
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
            // widget=="file"：在输入框旁渲染浏览按钮。容器作为 form 行控件，
            // 但 paramWidgets_ 仍存 QLineEdit，保证 OnParamWidgetChanged 的
            // qobject_cast<QLineEdit*> 不变。
            if (s.value("widget").toString() == "file") {
                QString filter = s.value("fileFilter").toString();
                if (filter.isEmpty()) filter = "All Files (*)";
                auto* container = new QWidget();
                auto* h = new QHBoxLayout(container);
                h->setContentsMargins(0, 0, 0, 0);
                h->addWidget(le, 1);
                auto* browse = new QPushButton(QStringLiteral("…"));
                browse->setFixedWidth(28);
                connect(browse, &QPushButton::clicked, this,
                        [this, name, le, filter](){ OnBrowseFile(name, le, filter); });
                h->addWidget(browse, 0);
                w = container;
                paramsLayout_->addRow(name + ":", w);
                paramWidgets_[name] = le;   // 存 le 而非容器
                continue;
            }
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
// 置位 selfParamEdit_ 防止 nodeParamsChanged 回响重建控件（避免删除正在编辑的自己）
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

    selfParamEdit_ = true;
    commandStack_.push(std::make_unique<ChangeParamCommand>(vm_, nodeId, key, newValue));
    selfParamEdit_ = false;
}

// 文件路径参数的浏览按钮：桌面拿真实路径，WASM 走异步上传 -> MEMFS 临时文件。
// 回填 QLineEdit 后手动触发 OnParamWidgetChanged 写回 model。
void MainWindow::OnBrowseFile(const QString& key, QLineEdit* le, const QString& filter)
{
    if (!le) return;
#ifdef __EMSCRIPTEN__
    // WASM：同步模态对话框不可用，getOpenFileContent 异步返回文件字节，
    // 写入 MEMFS 后把该路径作为参数值（下游 task 用该路径 imread）。
    QString capturedKey = key;
    QFileDialog::getOpenFileContent(filter,
        [this, capturedKey, le](const QString& fileName, const QByteArray& content) {
            if (fileName.isEmpty() || content.isEmpty()) return;
            const QString memfsPath = "/tmp/_gs_param_" + QFileInfo(fileName).fileName();
            {
                QFile f(memfsPath);
                if (!f.open(QIODevice::WriteOnly)) return;
                f.write(content);
            }
            le->setText(memfsPath);
            OnParamWidgetChanged(capturedKey);
        });
#else
    QString path = QFileDialog::getOpenFileName(this, "Select File", QString(), filter);
    if (path.isEmpty()) return;
    le->setText(path);
    OnParamWidgetChanged(key);
#endif
}

void MainWindow::ActionNew()
{
    vm_.clear();
    commandStack_.clear();
    currentFilePath_.clear();
}

void MainWindow::ActionOpen()
{
#ifdef __EMSCRIPTEN__
    // WASM：浏览器不允许同步模态文件选择，必须异步回调。
    // 上传内容写到 MEMFS 临时文件，再交给 VM 加载。
    QFileDialog::getOpenFileContent("JSON Files (*.json);;All Files (*)",
        [this](const QString& fileName, const QByteArray& content) {
            if (fileName.isEmpty() || content.isEmpty()) return;
            const QString tmpPath = "/tmp/_graph_studio_loaded.json";
            {
                QFile f(tmpPath);
                if (!f.open(QIODevice::WriteOnly)) return;
                f.write(content);
            }
            if (vm_.loadFromFile(tmpPath)) {
                currentFilePath_ = fileName;  // 记录用户选择的展示名
            }
        });
#else
    QString path = QFileDialog::getOpenFileName(this, "Open Graph", QString(), "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty())
        return;
    if (vm_.loadFromFile(path)) {
        currentFilePath_ = path;
    }
#endif
}

void MainWindow::ActionSave()
{
    if (currentFilePath_.isEmpty()) {
        ActionSaveAs();
    } else {
#ifdef __EMSCRIPTEN__
        // WASM：currentFilePath_ 是上次"另存为"用的展示名，重做下载
        ActionSaveAs();
#else
        vm_.saveToFile(currentFilePath_);
#endif
    }
}

void MainWindow::ActionSaveAs()
{
#ifdef __EMSCRIPTEN__
    // WASM：先写 MEMFS 临时文件，再读出来触发浏览器下载
    const QString tmpPath = "/tmp/_graph_studio_save.json";
    if (!vm_.saveToFile(tmpPath)) return;
    QFile f(tmpPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray content = f.readAll();
    QString displayName = currentFilePath_.isEmpty() ? QStringLiteral("graph.json")
                                                     : QFileInfo(currentFilePath_).fileName();
    QFileDialog::saveFileContent(content, displayName);
#else
    QString path = QFileDialog::getSaveFileName(this, "Save Graph", "graph.json", "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".json"))
        path += ".json";
    if (vm_.saveToFile(path)) {
        currentFilePath_ = path;
    }
#endif
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

void MainWindow::UpdateRunActions()
{
    const bool exec = vm_.isExecuting();
    if (runAction_) runAction_->setEnabled(!exec);
    if (stopAction_) stopAction_->setEnabled(exec);
}

void MainWindow::ActionRun()
{
    vm_.execute();
}

void MainWindow::ActionStop()
{
    vm_.stop();
}

void MainWindow::onNodeStatusChanged(const QString& id, int phase, double durationMs)
{
    // phase 值对应 task_graph::ProfilePhase：
    //   0=READY 1=STARTED 2=COMPLETED 3=FAILED 4=SKIPPED
    auto it = nodeItems_.find(id);
    if (it == nodeItems_.end() || !it.value())
        return;
    using RS = NodeItem::RunStatus;
    switch (phase) {
        case 1: it.value()->setRunStatus(RS::Running); break;
        case 2: it.value()->setRunStatus(RS::Completed); break;
        case 3: it.value()->setRunStatus(RS::Failed); break;
        case 4: it.value()->setRunStatus(RS::None); break;
        default: break;  // READY 不改色
    }
}

void MainWindow::onExecutionStarted()
{
    // 重置所有节点执行配色，准备新一轮
    for (auto* item : nodeItems_) {
        if (item) item->setRunStatus(NodeItem::RunStatus::None);
    }
    if (outputWidget_)
        outputWidget_->setPlainText("Executing...");
    // 清空上一轮图像结果面板
    if (resultSelector_) {
        resultSelector_->blockSignals(true);
        resultSelector_->clear();
        resultSelector_->setEnabled(false);
        resultSelector_->blockSignals(false);
    }
    if (imageViewer_) {
        imageViewer_->clearImage();
    }
    UpdateRunActions();
}

void MainWindow::onExecutionFinished()
{
    UpdateRunActions();
    if (statusBar_)
        statusBar_->showMessage("Execution finished", 3000);
    // Switch to Profile tab to show execution analysis
    if (bottomTabs_ && bottomTabs_->indexOf(profilePanel_) >= 0) {
        bottomTabs_->setCurrentWidget(profilePanel_);
    }
}

void MainWindow::onExecutingChanged()
{
    UpdateRunActions();
}

void MainWindow::onImageResultsReady(const QStringList& keys)
{
    RebuildResultSelector(keys);
}

void MainWindow::onResultSelectorChanged(int index)
{
    if (!resultSelector_ || index < 0) return;
    QString key = resultSelector_->itemData(index).toString();
    ShowResultImage(key);
}

void MainWindow::RebuildResultSelector(const QStringList& keys)
{
    if (!resultSelector_) return;
    resultSelector_->blockSignals(true);
    resultSelector_->clear();
    // 按 nodeId 分组：单图像端口的节点显示 "nodeId (type)"，多端口显示 "nodeId:port (type)"
    QHash<QString, int> countByNode;
    for (const auto& k : keys) countByNode[k.section(':', 0, 0)]++;
    for (const auto& k : keys) {
        QString nodeId = k.section(':', 0, 0);
        QString port = k.section(':', 1);
        QString type = vm_.hasNode(nodeId) ? vm_.nodeData(nodeId).type : nodeId;
        QString label = (countByNode.value(nodeId, 0) == 1)
                            ? QStringLiteral("%1 (%2)").arg(nodeId, type)
                            : QStringLiteral("%1:%2 (%3)").arg(nodeId, port, type);
        resultSelector_->addItem(label, k);
    }
    resultSelector_->setEnabled(!keys.isEmpty());

    // 默认选中：选中节点优先，否则第一个
    int defaultIdx = 0;
    QString selId = vm_.selectedNodeId();
    if (!selId.isEmpty() && resultSelector_->count() > 0) {
        for (int i = 0; i < resultSelector_->count(); ++i) {
            if (resultSelector_->itemData(i).toString().section(':', 0, 0) == selId) {
                defaultIdx = i;
                break;
            }
        }
    }
    resultSelector_->setCurrentIndex(defaultIdx);
    resultSelector_->blockSignals(false);
    // blockSignals 期间未触发 currentIndexChanged，手动显示默认
    ShowResultImage(resultSelector_->count() > 0
                        ? resultSelector_->itemData(defaultIdx).toString()
                        : QString());
}

void MainWindow::ShowResultImage(const QString& key)
{
    if (!imageViewer_) return;
    if (key.isEmpty()) {
        imageViewer_->clearImage();
        return;
    }
    QImage img = vm_.imageResult(key);
    if (img.isNull()) {
        imageViewer_->clearImage();
        return;
    }
    imageViewer_->setImage(img);
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
