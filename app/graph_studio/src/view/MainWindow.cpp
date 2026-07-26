#include "view/MainWindow.h"
#include "view/GraphScene.h"
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
#include <QComboBox>
#include <QScrollArea>
#include <QPainter>

using namespace graph_studio;

MainWindow::MainWindow(GraphViewModel& vm, QWidget* parent)
    : QMainWindow(parent), vm_(vm)
{
    setWindowTitle("Graph Studio");
    resize(1600, 1000);
    setMinimumSize(1200, 700);

    ApplyDarkTheme();
    InitializeLayout();
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

void MainWindow::CreateMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("New");
    fileMenu->addAction("Open...");
    fileMenu->addAction("Save");
    fileMenu->addAction("Save As...");
    fileMenu->addSeparator();
    fileMenu->addAction("Exit");

    auto* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("Undo");
    editMenu->addAction("Redo");
    editMenu->addSeparator();
    editMenu->addAction("Delete");

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Zoom In");
    viewMenu->addAction("Zoom Out");
    viewMenu->addAction("Fit to View");
    viewMenu->addSeparator();
    viewMenu->addAction("Auto Layout");

    auto* runMenu = menuBar()->addMenu("&Run");
    runMenu->addAction("Execute");
    runMenu->addAction("Stop");

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About");
}

void MainWindow::CreateToolbar()
{
    toolbar_ = new QToolBar("Main Toolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(20, 20));

    toolbar_->addAction("New");
    toolbar_->addAction("Open");
    toolbar_->addAction("Save");
    toolbar_->addSeparator();
    toolbar_->addAction("Undo");
    toolbar_->addAction("Redo");
    toolbar_->addSeparator();
    toolbar_->addAction("Auto Layout");
    toolbar_->addSeparator();
    toolbar_->addAction("Execute");
    toolbar_->addAction("Stop");

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

    taskList_ = new QListWidget();
    layout->addWidget(taskList_);

    PopulateTaskLibrary();

    return container;
}

void MainWindow::PopulateTaskLibrary()
{
    if (!taskList_) return;

    const QStringList inputTasks = {"file_input", "camera_input", "image_load"};
    const QStringList processTasks = {"data_processor", "opencv_blur_filter", "opencv_gaussian_blur_filter",
                                       "opencv_sobel_filter", "opencv_laplacian_filter", "resize", "convert_color"};
    const QStringList outputTasks = {"file_output", "display", "save_image"};

    auto* inputItem = new QListWidgetItem("-- Input --");
    inputItem->setFlags(inputItem->flags() & ~Qt::ItemIsEnabled);
    inputItem->setForeground(QColor("#888888"));
    taskList_->addItem(inputItem);
    for (const auto& t : inputTasks) taskList_->addItem(t);

    auto* processItem = new QListWidgetItem("-- Process --");
    processItem->setFlags(processItem->flags() & ~Qt::ItemIsEnabled);
    processItem->setForeground(QColor("#888888"));
    taskList_->addItem(processItem);
    for (const auto& t : processTasks) taskList_->addItem(t);

    auto* outputItem = new QListWidgetItem("-- Output --");
    outputItem->setFlags(outputItem->flags() & ~Qt::ItemIsEnabled);
    outputItem->setForeground(QColor("#888888"));
    taskList_->addItem(outputItem);
    for (const auto& t : outputTasks) taskList_->addItem(t);
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

    nodePropertyLayout_->addRow("Node ID:", new QLineEdit("(none)"));
    nodePropertyLayout_->addRow("Type:", new QLineEdit("(none)"));
    nodePropertyLayout_->addRow("Enabled:", new QComboBox());
    nodePropertyLayout_->addRow("Timeout:", new QSpinBox());

    layout->addWidget(nodePropertyGroup_);
    layout->addStretch();

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
    graphicsView_ = new QGraphicsView(scene_);
    graphicsView_->setRenderHint(QPainter::Antialiasing);
    graphicsView_->setDragMode(QGraphicsView::RubberBandDrag);
    graphicsView_->setRenderHint(QPainter::SmoothPixmapTransform);
    graphicsView_->setMinimumWidth(400);

    auto* node1 = new NodeItem("file_input", "Input");
    node1->setPos(-300, -50);
    scene_->addItem(node1);

    auto* node2 = new NodeItem("blur_filter", "Process");
    node2->setPos(-50, -50);
    scene_->addItem(node2);

    auto* node3 = new NodeItem("sobel_filter", "Process");
    node3->setPos(200, -100);
    scene_->addItem(node3);

    auto* node4 = new NodeItem("display", "Output");
    node4->setPos(450, -50);
    scene_->addItem(node4);

    auto* node5 = new NodeItem("save_image", "Output");
    node5->setPos(200, 100);
    scene_->addItem(node5);

    scene_->addItem(new EdgeItem(node1, node2));
    scene_->addItem(new EdgeItem(node2, node3));
    scene_->addItem(new EdgeItem(node3, node4));
    scene_->addItem(new EdgeItem(node2, node5));

    graphicsView_->centerOn(0, 0);
}

void MainWindow::CreateStatusBar()
{
    statusBar_ = new QStatusBar(this);
    statusBar_->showMessage("Ready  |  Nodes: 5  |  Edges: 4");
    setStatusBar(statusBar_);
}