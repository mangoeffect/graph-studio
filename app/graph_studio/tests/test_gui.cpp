#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>

#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"
#include "view/MainWindow.h"
#include "view/GraphScene.h"
#include "view/GraphView.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"

using namespace graph_studio;

// 默认无头运行：未显式指定 QT_QPA_PLATFORM 时回落到 offscreen，便于 CI/ctest。
// 开发者想观察画面时可设 QT_QPA_PLATFORM=cocoa 等覆盖。
namespace {
const bool _ensureOffscreenPlatform = []() {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    return true;
}();
}

// ---- 工具函数 ----
static int countSceneItems(QGraphicsScene* scene, int itemType)
{
    int n = 0;
    for (auto* it : scene->items())
        if (it->type() == itemType)
            ++n;
    return n;
}

static QAction* findAction(QWidget* w, const QString& textStartsWith)
{
    for (auto* a : w->findChildren<QAction*>())
        if (a->text().startsWith(textStartsWith))
            return a;
    return nullptr;
}

class TestGui : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void testVmToSceneSync();
    void testNodeCreationViaDrop();
    void testSelectionViaClick();
    void testNodeMoveViaDrag();
    void testDeleteViaAction();
    void testUndoRedo();
    void testZoomActions();
    void testEdgeCreationViaPortDrag();

private:
    GraphModel* model_ = nullptr;
    GraphViewModel* vm_ = nullptr;
    MainWindow* window_ = nullptr;
    GraphView* view_ = nullptr;
    GraphScene* scene_ = nullptr;

    void mouseDrag(const QPoint& from, const QPoint& to, int steps = 6);
    void dropTask(const QString& taskType, const QPoint& viewportPos);
};

void TestGui::init()
{
    model_ = new GraphModel();
    vm_ = new GraphViewModel(*model_);
    window_ = new MainWindow(*vm_);
    window_->show();
    QTest::qWait(80);

    view_ = window_->findChild<GraphView*>();
    scene_ = view_ ? qobject_cast<GraphScene*>(view_->scene()) : nullptr;
    QVERIFY(view_ != nullptr);
    QVERIFY(scene_ != nullptr);

    // 无插件时构造函数不会播种演示图，初始应为空。
    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(vm_->edgeCount(), 0);
}

void TestGui::cleanup()
{
    delete window_;
    delete vm_;
    delete model_;
    window_ = nullptr;
    vm_ = nullptr;
    model_ = nullptr;
}

void TestGui::mouseDrag(const QPoint& from, const QPoint& to, int steps)
{
    auto* vp = view_->viewport();
    QTest::mousePress(vp, Qt::LeftButton, {}, from);
    for (int i = 1; i <= steps; ++i) {
        QPoint p(from.x() + (to.x() - from.x()) * i / steps,
                 from.y() + (to.y() - from.y()) * i / steps);
        QTest::mouseMove(vp, p);
        QTest::qWait(10);
    }
    QTest::mouseRelease(vp, Qt::LeftButton, {}, to);
    QTest::qWait(30);
}

// 模拟从任务库拖到画布：触发 GraphView::taskDropped 信号 ->
// CreateNodeAt。这是"从面板拖拽建节点"的核心连接（dropEvent 仅做 mime 解析
// 后发射该信号，真实 QDrag 在 offscreen 不可靠，故直接发射信号测下游链路）。
void TestGui::dropTask(const QString& taskType, const QPoint& viewportPos)
{
    QPointF scenePos = view_->mapToScene(viewportPos);
    QMetaObject::invokeMethod(view_, "taskDropped",
                              Q_ARG(QString, taskType), Q_ARG(QPointF, scenePos));
    QTest::qWait(30);
}

// VM 增删节点/边 -> View 层（NodeItem/EdgeItem）正确同步
void TestGui::testVmToSceneSync()
{
    QString idA = vm_->addTask("alpha", 100, 200);
    QString idB = vm_->addTask("beta", -50, 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 2);

    auto* node = scene_->findNodeItem(idA);
    QVERIFY(node != nullptr);
    QCOMPARE(node->pos().x(), 100.0);
    QCOMPARE(node->pos().y(), 200.0);
    QCOMPARE(node->nodeType(), QString("alpha"));

    QVERIFY(vm_->addEdge(idA, idB));
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 1);
    QCOMPARE(vm_->edgeCount(), 1);

    QVERIFY(vm_->removeTask(idA));
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);
    // 删节点连带删边
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);
    QCOMPARE(vm_->edgeCount(), 0);
}

// 从任务库拖到画布 -> CreateNodeAt -> 节点出现在 VM 与画布
void TestGui::testNodeCreationViaDrop()
{
    QCOMPARE(vm_->taskCount(), 0);
    QPoint vpCenter = view_->viewport()->rect().center();
    dropTask("my_task", vpCenter);

    QCOMPARE(vm_->taskCount(), 1);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);

    // 节点应创建在视口中心对应的场景坐标处
    QPointF sceneCenter = view_->mapToScene(vpCenter);
    auto nodes = vm_->nodes();
    QCOMPARE(nodes.size(), 1);
    QVERIFY(qAbs(nodes[0].x - sceneCenter.x()) < 1.0);
    QVERIFY(qAbs(nodes[0].y - sceneCenter.y()) < 1.0);
}

// 点击节点 -> 场景选中 -> VM selectedNodeId 同步
void TestGui::testSelectionViaClick()
{
    QString idA = vm_->addTask("a", 0, 0);
    QString idB = vm_->addTask("b", 200, 0);

    auto* nodeA = scene_->findNodeItem(idA);
    auto* nodeB = scene_->findNodeItem(idB);
    QVERIFY(nodeA && nodeB);

    // 点击节点中心（非端口）触发选中
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {},
                      view_->mapFromScene(nodeA->scenePos()));
    QTest::qWait(30);
    QCOMPARE(vm_->selectedNodeId(), idA);

    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {},
                      view_->mapFromScene(nodeB->scenePos()));
    QTest::qWait(30);
    QCOMPARE(vm_->selectedNodeId(), idB);
}

// 拖拽节点 -> 松开后 VM 位置更新
void TestGui::testNodeMoveViaDrag()
{
    QString id = vm_->addTask("movable", 0, 0);
    auto* node = scene_->findNodeItem(id);
    QVERIFY(node != nullptr);

    QSignalSpy spy(vm_, &GraphViewModel::nodeMoved);
    QPoint start = view_->mapFromScene(node->scenePos());
    QPoint end = view_->mapFromScene(QPointF(90, 60));

    mouseDrag(start, end);

    QVERIFY(spy.count() >= 1);
    NodeData d = vm_->nodeData(id);
    QCOMPARE(d.x, 90.0);
    QCOMPARE(d.y, 60.0);
}

// 选中节点 -> 触发 Delete action -> 节点从 VM 与画布移除
void TestGui::testDeleteViaAction()
{
    QString id = vm_->addTask("del", 0, 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);

    auto* node = scene_->findNodeItem(id);
    QVERIFY(node != nullptr);
    node->setSelected(true);
    QTest::qWait(30);
    QCOMPARE(vm_->selectedNodeId(), id);

    QAction* delAct = findAction(window_, "Delete");
    QVERIFY(delAct != nullptr);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 0);
}

// 建节点(经拖放) -> Undo 撤销 -> Redo 恢复，画布与 VM 一致
void TestGui::testUndoRedo()
{
    dropTask("ur_task", view_->viewport()->rect().center());
    QCOMPARE(vm_->taskCount(), 1);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);

    QAction* undoAct = findAction(window_, "Undo");
    QVERIFY(undoAct != nullptr);
    QVERIFY(undoAct->isEnabled());
    undoAct->trigger();
    QTest::qWait(30);
    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 0);

    QAction* redoAct = findAction(window_, "Redo");
    QVERIFY(redoAct != nullptr);
    QVERIFY(redoAct->isEnabled());
    redoAct->trigger();
    QTest::qWait(30);
    QCOMPARE(vm_->taskCount(), 1);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);
}

// Zoom +/- action 改变视图缩放并刷新状态栏标签
void TestGui::testZoomActions()
{
    qreal m11Before = view_->transform().m11();

    QAction* zoomIn = findAction(window_, "Zoom +");
    QVERIFY(zoomIn != nullptr);
    zoomIn->trigger();
    QTest::qWait(20);
    QVERIFY(view_->transform().m11() > m11Before + 0.01);

    QAction* zoomOut = findAction(window_, "Zoom -");
    QVERIFY(zoomOut != nullptr);
    zoomOut->trigger();
    QTest::qWait(20);
    QVERIFY(qAbs(view_->transform().m11() - m11Before) < 0.01);

    // 状态栏缩放标签应含百分号
    QLabel* zoomLabel = nullptr;
    for (auto* l : window_->findChildren<QLabel*>())
        if (l->text().startsWith("Zoom:")) { zoomLabel = l; break; }
    QVERIFY(zoomLabel != nullptr);
    QVERIFY(zoomLabel->text().contains("%"));
}

// 从节点输出端口拖到另一节点输入端口 -> 创建依赖边
void TestGui::testEdgeCreationViaPortDrag()
{
    QString idA = vm_->addTask("src", -150, 0);
    QString idB = vm_->addTask("dst", 150, 0);
    auto* nodeA = scene_->findNodeItem(idA);
    auto* nodeB = scene_->findNodeItem(idB);
    QVERIFY(nodeA && nodeB);

    QCOMPARE(vm_->edgeCount(), 0);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);

    // 端口中心位于节点 boundingRect 边界上（x = ±nodeWidth/2），QRectF 半开，
    // itemAt 会落空。故向内偏移几像素：仍在 hitPort 命中半径(PORT_RADIUS+6)内，
    // 同时保证落在节点 shape 内部。
    qreal outX = NodeItem::nodeWidth() / 2 - 4;
    qreal inX = -NodeItem::nodeWidth() / 2 + 4;
    QPoint start = view_->mapFromScene(nodeA->mapToScene(QPointF(outX, 0)));
    QPoint end = view_->mapFromScene(nodeB->mapToScene(QPointF(inX, 0)));

    mouseDrag(start, end);

    QCOMPARE(vm_->edgeCount(), 1);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 1);
    // 校验边方向
    auto edges = vm_->edges();
    QCOMPARE(edges.size(), 1);
    QCOMPARE(edges[0].fromId, idA);
    QCOMPARE(edges[0].toId, idB);
}

// 自定义 main：GUI 测试必须用 QApplication（而非 QCoreApplication）
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    TestGui t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_gui.moc"
