#include "view/MainWindow.h"
#include "view/GraphScene.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include "viewmodel/GraphViewModel.h"

#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QVBoxLayout>

using namespace graph_studio;

MainWindow::MainWindow(GraphViewModel& vm, QWidget* parent)
    : QMainWindow(parent), vm_(vm)
{
    setWindowTitle("Graph Studio");
    resize(1280, 720);
    setMinimumSize(800, 600);

    InitializeLayout();
}

MainWindow::~MainWindow() = default;

void MainWindow::InitializeLayout()
{
    CreateToolbar();
    CreateSidebar();
    CreateCanvas();
    CreateStatusBar();
}

void MainWindow::CreateToolbar()
{
    toolbar_ = new QToolBar("Main Toolbar");
    toolbar_->setMovable(false);

    auto* newAction = new QAction("New", this);
    auto* openAction = new QAction("Open", this);
    auto* saveAction = new QAction("Save", this);
    toolbar_->addActions({newAction, openAction, saveAction});

    toolbar_->addSeparator();

    auto* undoAction = new QAction("Undo", this);
    auto* redoAction = new QAction("Redo", this);
    toolbar_->addActions({undoAction, redoAction});

    toolbar_->addSeparator();

    auto* layoutAction = new QAction("Layout", this);
    auto* executeAction = new QAction("Execute", this);
    auto* stopAction = new QAction("Stop", this);
    toolbar_->addActions({layoutAction, executeAction, stopAction});

    addToolBar(toolbar_);
}

void MainWindow::CreateSidebar()
{
    sidebar_ = new QDockWidget("Task Library", this);
    sidebar_->setAllowedAreas(Qt::LeftDockWidgetArea);
    sidebar_->setFeatures(QDockWidget::DockWidgetMovable);

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    auto* header = new QLabel("Task Library");
    QFont font = header->font();
    font.setBold(true);
    font.setPointSize(14);
    header->setFont(font);
    layout->addWidget(header);

    layout->addStretch();

    sidebar_->setWidget(widget);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar_);
}

void MainWindow::CreateCanvas()
{
    scene_ = new GraphScene(this);
    graphicsView_ = new QGraphicsView(scene_);
    graphicsView_->setRenderHint(QPainter::Antialiasing);
    graphicsView_->setDragMode(QGraphicsView::RubberBandDrag);
    graphicsView_->setRenderHint(QPainter::SmoothPixmapTransform);

    auto* node1 = new NodeItem("Task_1", "Input");
    node1->setPos(-200, -50);
    scene_->addItem(node1);

    auto* node2 = new NodeItem("Task_2", "Process");
    node2->setPos(0, -50);
    scene_->addItem(node2);

    auto* node3 = new NodeItem("Task_3", "Process");
    node3->setPos(200, -50);
    scene_->addItem(node3);

    auto* node4 = new NodeItem("Task_4", "Output");
    node4->setPos(400, -50);
    scene_->addItem(node4);

    auto* node5 = new NodeItem("Task_5", "Process");
    node5->setPos(0, 150);
    scene_->addItem(node5);

    auto* edge1 = new EdgeItem(node1, node2);
    scene_->addItem(edge1);

    auto* edge2 = new EdgeItem(node2, node3);
    scene_->addItem(edge2);

    auto* edge3 = new EdgeItem(node3, node4);
    scene_->addItem(edge3);

    auto* edge4 = new EdgeItem(node2, node5);
    scene_->addItem(edge4);

    auto* edge5 = new EdgeItem(node5, node3);
    scene_->addItem(edge5);

    graphicsView_->centerOn(0, 0);

    setCentralWidget(graphicsView_);
}

void MainWindow::CreateStatusBar()
{
    statusBar_ = new QStatusBar(this);
    statusBar_->showMessage("Ready");
    setStatusBar(statusBar_);
}