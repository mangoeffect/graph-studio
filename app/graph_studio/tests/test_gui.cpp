#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QTemporaryDir>

#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"
#include "view/MainWindow.h"
#include "view/GraphScene.h"
#include "view/GraphView.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include <plugin_api.hpp>

using namespace graph_studio;

// 模拟声明真实非默认输入端口（image）的 task，用于确定性复现 mediapipe
// 类节点的端口问题，不依赖运行时 dlopen 的插件。
namespace {
class PortedNode : public task_graph::INode {
public:
    using task_graph::INode::INode;
    const std::string& type() const override {
        static std::string t = "ported_node";
        return t;
    }
    task_graph::TaskResult execute(task_graph::TaskContext&) override {
        return task_graph::TaskResult{task_graph::TaskStatus::COMPLETED};
    }
    std::vector<task_graph::PortSpec> input_specs() const override {
        return { task_graph::PortSpec{"image", "", true} };
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return { task_graph::PortSpec{"out", "", false} };
    }
};

// 双输入/双输出端口的 task：同一对节点间可拖两条不同输入口的边
class DualPortNode : public task_graph::INode {
public:
    using task_graph::INode::INode;
    const std::string& type() const override {
        static std::string t = "dual_port_node";
        return t;
    }
    task_graph::TaskResult execute(task_graph::TaskContext&) override {
        return task_graph::TaskResult{task_graph::TaskStatus::COMPLETED};
    }
    std::vector<task_graph::PortSpec> input_specs() const override {
        return { task_graph::PortSpec{"image", "", true},
                 task_graph::PortSpec{"mask", "", true} };
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return { task_graph::PortSpec{"out", "", false},
                 task_graph::PortSpec{"aux", "", false} };
    }
};
}

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
    void testPortAwareEdgeDragAndSerialize();
    void testDualPortEdgesSeparateItems();
    void testDeleteTargetRemovesInboundEdges();
    void testDeleteTargetDualPortInbound();
    void testMultiSelectDeleteConnected();
    void testFileDropOpensGraph();
    void testFileDropPriorityOverText();
    void testNonJsonFileDropIgnored();
    void testWholeWindowFileDrop();

private:
    GraphModel* model_ = nullptr;
    GraphViewModel* vm_ = nullptr;
    MainWindow* window_ = nullptr;
    GraphView* view_ = nullptr;
    GraphScene* scene_ = nullptr;

    void mouseDrag(const QPoint& from, const QPoint& to, int steps = 6);
    void dropTask(const QString& taskType, const QPoint& viewportPos);
    // 模拟从文件管理器拖入 path 指向的本地文件（dragEnter + drop）。
    void sendFileDrop(QWidget* target, const QString& path);
    void sendFileDrop(QWidget* target, const QMimeData& mime);
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

// 模拟从文件管理器拖入本地 .json：GraphView::dropEvent 解析 text/uri-list
// （hasUrls 优先于 hasText）→ graphFileDropped → MainWindow::OpenGraphFile →
// 图被加载、currentFilePath_ 更新、窗口标题带上文件名。
void TestGui::sendFileDrop(QWidget* target, const QString& path)
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    sendFileDrop(target, mime);
}

void TestGui::sendFileDrop(QWidget* target, const QMimeData& mime)
{
    QPoint pos = target->rect().center();
    QDragEnterEvent dragEnter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &dragEnter);
    QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    QTest::qWait(30);
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

    // 端口中心位于节点 shape 圆内（shape() 已包含端口圆区域），
    // itemAt 可直接命中。使用端口正中心坐标。
    qreal outX = NodeItem::nodeWidth() / 2;
    qreal inX = -NodeItem::nodeWidth() / 2;
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

// 确定性地复现用户场景：一个声明了真实非默认输入端口（image）的 task，
// 从输出端口拖到它的输入端口。此前端口未并入序列化/快捷连线，保存的图
// 会把 to_port 写成 "in"，导致图校验失败、无输入边。
void TestGui::testPortAwareEdgeDragAndSerialize()
{
    task_graph::PluginRegistry::instance().register_task(
        "ported_node",
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<PortedNode>(id, cfg);
        });

    QString src = vm_->addTask("ported_node", -200, 0);
    QString dst = vm_->addTask("ported_node", 200, 0);
    QVERIFY(!src.isEmpty() && !dst.isEmpty());

    // 节点元数据应为该 task 声明的真实端口（非默认 in/out）
    QCOMPARE(vm_->nodeData(src).outputPorts, QStringList{QStringLiteral("out")});
    QCOMPARE(vm_->nodeData(dst).inputPorts, QStringList{QStringLiteral("image")});

    // GUI 的真实手动拖线：从输出端口中心拖到输入端口中心
    auto* nodeA = scene_->findNodeItem(src);
    auto* nodeB = scene_->findNodeItem(dst);
    QVERIFY(nodeA && nodeB);
    qreal outX = NodeItem::nodeWidth() / 2;
    qreal inX = -NodeItem::nodeWidth() / 2;
    QPoint start = view_->mapFromScene(nodeA->mapToScene(QPointF(outX, 0)));
    QPoint end = view_->mapFromScene(nodeB->mapToScene(QPointF(inX, 0)));
    mouseDrag(start, end);

    // 拖线产生的边必须带真实端口（out -> image）
    QCOMPARE(vm_->edgeCount(), 1);
    auto edges = vm_->edges();
    QCOMPARE(edges[0].fromId, src);
    QCOMPARE(edges[0].toId, dst);
    QCOMPARE(edges[0].fromPort, QStringLiteral("out"));
    QCOMPARE(edges[0].toPort, QStringLiteral("image"));

    // 序列化：to_port 必须写真实端口 "image"，绝不出现伪造的 "in"
    std::string json = model_->to_json_string();
    QVERIFY(json.find("\"to_port\": \"image\"") != std::string::npos);
    QVERIFY(json.find("\"to_port\": \"in\"") == std::string::npos);
    QVERIFY(json.find("\"from_port\": \"out\"") != std::string::npos);

    // round-trip：加载回同一 JSON，端口边原样恢复
    QVERIFY(model_->from_json_string(json));
    auto reloaded = vm_->edges();
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded[0].fromPort, QStringLiteral("out"));
    QCOMPARE(reloaded[0].toPort, QStringLiteral("image"));

    task_graph::PluginRegistry::instance().unregister_task("ported_node");
}

// 同一对节点间：可分别拖到两个输入口创建两条边；删除选中一条不影响另一条
void TestGui::testDualPortEdgesSeparateItems()
{
    task_graph::PluginRegistry::instance().register_task(
        "dual_port_node",
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<DualPortNode>(id, cfg);
        });

    QString a = vm_->addTask("dual_port_node", -200, 0);
    QString b = vm_->addTask("dual_port_node", 200, 0);
    auto* nodeA = scene_->findNodeItem(a);
    auto* nodeB = scene_->findNodeItem(b);
    QVERIFY(nodeA && nodeB);

    // ① out -> image
    mouseDrag(view_->mapFromScene(nodeA->outputPortPos("out")),
              view_->mapFromScene(nodeB->inputPortPos("image")));
    QCOMPARE(vm_->edgeCount(), 1);

    // ② aux -> mask（同一对节点，不同输入口）
    mouseDrag(view_->mapFromScene(nodeA->outputPortPos("aux")),
              view_->mapFromScene(nodeB->inputPortPos("mask")));
    QCOMPARE(vm_->edgeCount(), 2);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 2);

    auto edges = vm_->edges();
    bool sawImage = false, sawMask = false;
    for (const auto& e : edges) {
        if (e.fromId == a && e.toId == b && e.fromPort == "out" && e.toPort == "image") sawImage = true;
        if (e.fromId == a && e.toId == b && e.fromPort == "aux" && e.toPort == "mask") sawMask = true;
    }
    QVERIFY(sawImage);
    QVERIFY(sawMask);

    // 选中其中一条（out->image）并触发 Delete，应只删这一条
    EdgeItem* target = nullptr;
    for (auto* it : scene_->items()) {
        if (it->type() != EdgeItem::Type) continue;
        auto* e = static_cast<EdgeItem*>(it);
        if (e->sourcePort() == "out" && e->targetPort() == "image") { target = e; break; }
    }
    QVERIFY(target != nullptr);
    scene_->clearSelection();
    target->setSelected(true);
    QTest::qWait(30);
    QAction* delAct = findAction(window_, "Delete");
    QVERIFY(delAct != nullptr);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->edgeCount(), 1);
    edges = vm_->edges();
    QCOMPARE(edges.size(), 1);
    QCOMPARE(edges[0].fromPort, QStringLiteral("aux"));
    QCOMPARE(edges[0].toPort, QStringLiteral("mask"));
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 1);

    task_graph::PluginRegistry::instance().unregister_task("dual_port_node");
}

// 回归(崩溃)：删除仅作为“目标”的节点时，指向它的入边必须一并清理。
// 旧实现按 edgeKey 的 "-><id>:" 后缀匹配（键形如 "A:out->B:in"，末尾还带
// toPort），入边永远匹配不上 → EdgeItem 保留到已析构 NodeItem 的指针，
// 之后再删出边源节点时 unregisterEdge 触发 use-after-free（QSet::remove 0x...008）。
void TestGui::testDeleteTargetRemovesInboundEdges()
{
    QString a = vm_->addTask("alpha", -100, 0);
    QString b = vm_->addTask("beta", 100, 0);
    QVERIFY(vm_->addEdge(a, "out", b, "in"));
    QCOMPARE(vm_->edgeCount(), 1);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 1);

    // 只选中目标节点 b 并 Delete：入边应随节点一起消失
    auto* nodeB = scene_->findNodeItem(b);
    QVERIFY(nodeB != nullptr);
    scene_->clearSelection();
    nodeB->setSelected(true);
    QTest::qWait(30);
    QAction* delAct = findAction(window_, "Delete");
    QVERIFY(delAct != nullptr);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 1);
    QCOMPARE(vm_->edgeCount(), 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 1);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);

    // 再删源节点 a：修复前这里会崩（残留 EdgeItem.targetNode() == 已释放 b）
    auto* nodeA = scene_->findNodeItem(a);
    QVERIFY(nodeA != nullptr);
    scene_->clearSelection();
    nodeA->setSelected(true);
    QTest::qWait(30);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);
}

// 多端口同对双边的回归：删除“目标”节点，两条入边一起清理，无悬空 EdgeItem
void TestGui::testDeleteTargetDualPortInbound()
{
    task_graph::PluginRegistry::instance().register_task(
        "dual_port_node",
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<DualPortNode>(id, cfg);
        });

    QString a = vm_->addTask("dual_port_node", -200, 0);
    QString b = vm_->addTask("dual_port_node", 200, 0);
    QVERIFY(vm_->addEdge(a, "out", b, "image"));
    QVERIFY(vm_->addEdge(a, "aux", b, "mask"));
    QCOMPARE(vm_->edgeCount(), 2);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 2);

    auto* nodeB = scene_->findNodeItem(b);
    QVERIFY(nodeB != nullptr);
    scene_->clearSelection();
    nodeB->setSelected(true);
    QTest::qWait(30);
    QAction* delAct = findAction(window_, "Delete");
    QVERIFY(delAct != nullptr);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 1);
    QCOMPARE(vm_->edgeCount(), 0);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);

    auto* nodeA = scene_->findNodeItem(a);
    QVERIFY(nodeA != nullptr);
    scene_->clearSelection();
    nodeA->setSelected(true);
    QTest::qWait(30);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);

    task_graph::PluginRegistry::instance().unregister_task("dual_port_node");
}

// 同时选中互相连接的源+目标节点一键删除：顺序无关，任何顺序都不应触碰已释放节点
void TestGui::testMultiSelectDeleteConnected()
{
    QString a = vm_->addTask("alpha", -100, 0);
    QString b = vm_->addTask("beta", 100, 0);
    QVERIFY(vm_->addEdge(a, "out", b, "in"));
    QCOMPARE(vm_->edgeCount(), 1);

    scene_->clearSelection();
    scene_->findNodeItem(a)->setSelected(true);
    scene_->findNodeItem(b)->setSelected(true);
    QTest::qWait(30);
    QAction* delAct = findAction(window_, "Delete");
    QVERIFY(delAct != nullptr);
    delAct->trigger();
    QTest::qWait(30);

    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(vm_->edgeCount(), 0);
    QCOMPARE(countSceneItems(scene_, NodeItem::Type), 0);
    QCOMPARE(countSceneItems(scene_, EdgeItem::Type), 0);
}

// 把保存过的 .json 拖进画布 -> 图整体加载、标题更新（等价于 File→Open）
void TestGui::testFileDropOpensGraph()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QString fileA = tmpDir.filePath("drop_a.json");
    QString idA = vm_->addTask("alpha", 0, 0);
    QString idB = vm_->addTask("beta", 200, 0);
    QVERIFY(!idA.isEmpty() && !idB.isEmpty());
    QVERIFY(vm_->addEdge(idA, idB));
    QVERIFY(vm_->saveToFile(fileA));

    vm_->clear();
    QCOMPARE(vm_->taskCount(), 0);

    // 画布是 viewport 接收拖放，事件按 QGraphicsView 转发到 dropEvent
    sendFileDrop(view_, fileA);

    QCOMPARE(vm_->taskCount(), 2);
    QCOMPARE(vm_->edgeCount(), 1);
    QVERIFY(window_->windowTitle().contains("drop_a.json"));
    QVERIFY(window_->windowTitle().contains("Graph Studio"));
}

// 文件管理器拖 .json 常同时带 text/plain（文件路径或名称）。hasUrls 必须
// 优先于 hasText，否则会把路径当 task 类型建节点而不是加载图。
void TestGui::testFileDropPriorityOverText()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QString fileA = tmpDir.filePath("prio.json");
    QString idA = vm_->addTask("alpha", 0, 0);
    QString idB = vm_->addTask("beta", 200, 0);
    QVERIFY(vm_->addEdge(idA, idB));
    QVERIFY(vm_->saveToFile(fileA));
    vm_->clear();

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(fileA)});
    mime.setText(fileA);  // 恶意/真实场景：文件带文本内容

    // 与 sendFileDrop 相同路径（dragEnter + drop），但 mime 同时含 text+urls
    sendFileDrop(view_, mime);

    // 若 hasText 分支先执行会把 fileA 当成 task 类型 → 建节点而非加载图
    QCOMPARE(vm_->taskCount(), 2);
    QCOMPARE(vm_->edgeCount(), 1);
}

// 拖入非 .json 文件（图片等）→ 不应被当作图加载，画布保持不变
void TestGui::testNonJsonFileDropIgnored()
{
    QString idA = vm_->addTask("graph_only", 0, 0);
    QVERIFY(!idA.isEmpty());
    QCOMPARE(vm_->taskCount(), 1);

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString imgA = tmpDir.filePath("drop.png");
    sendFileDrop(view_, imgA);

    QCOMPARE(vm_->taskCount(), 1);         // 不加载、不新建
    QCOMPARE(vm_->edgeCount(), 0);
    QCOMPARE(window_->windowTitle(), QStringLiteral("Graph Studio"));
}

// 拖到画布外的窗口区域（属性面板/日志区等）→ MainWindow::dropEvent 兜底接收
void TestGui::testWholeWindowFileDrop()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString fileA = tmpDir.filePath("win_drop.json");
    QString idA = vm_->addTask("alpha", 0, 0);
    QVERIFY(!idA.isEmpty());
    QVERIFY(vm_->saveToFile(fileA));

    vm_->clear();
    sendFileDrop(window_, fileA);
    QCOMPARE(vm_->taskCount(), 1);
    QVERIFY(window_->windowTitle().contains("win_drop"));
}

// 自定义 main：GUI 测试必须用 QApplication（而非 QCoreApplication）
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    TestGui t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_gui.moc"
