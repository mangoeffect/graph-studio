#include <QtTest/QtTest>
#include <QSignalSpy>
#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include <plugin_api.hpp>

using namespace graph_studio;
using namespace task_graph;

// 模拟一个声明了非默认端口的插件 task（如 mediapipe 任务的 image 输入口）
class PortedNode : public INode {
public:
    using INode::INode;
    const std::string& type() const override { static std::string t = "ported_node"; return t; }
    TaskResult execute(TaskContext&) override { return TaskResult{.status = TaskStatus::COMPLETED}; }
    std::vector<PortSpec> input_specs() const override {
        return { PortSpec{"image", "", true} };
    }
    std::vector<PortSpec> output_specs() const override {
        return { PortSpec{"out", "", false} };
    }
};

class TestGraphViewModel : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testAddTask();
    void testAddTaskAutoId();
    void testRemoveTask();
    void testRemoveTaskRemovesConnectedEdges();
    void testAddEdge();
    void testAddEdgePortAware();
    void testAddEdgeCycle();
    void testAddEdgeDuplicate();
    void testAddEdgeSelfLoop();
    void testMoveNode();
    void testSelection();
    void testClearGraph();
    void testAutoLayout();
};

GraphModel* model = nullptr;
GraphViewModel* vm = nullptr;

void TestGraphViewModel::initTestCase()
{
    model = new GraphModel();
    vm = new GraphViewModel(*model);
}

void TestGraphViewModel::cleanupTestCase()
{
    delete vm;
    delete model;
}

void TestGraphViewModel::testAddTask()
{
    QSignalSpy spy(vm, &GraphViewModel::taskAdded);
    QString id = vm->addTask("filter", 100, 200);
    QVERIFY(!id.isEmpty());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm->taskCount(), 1);
    QVERIFY(vm->hasNode(id));

    NodeData data = vm->nodeData(id);
    QCOMPARE(data.type, QString("filter"));
    QCOMPARE(data.x, 100.0);
    QCOMPARE(data.y, 200.0);
}

void TestGraphViewModel::testAddTaskAutoId()
{
    QString id1 = vm->addTask("blur", 0, 0);
    QString id2 = vm->addTask("blur", 0, 0);
    QVERIFY(id1 != id2);
    QVERIFY(id1.startsWith("blur_"));
    QVERIFY(id2.startsWith("blur_"));
}

void TestGraphViewModel::testRemoveTask()
{
    QString id = vm->addTask("remove_me", 0, 0);
    int before = vm->taskCount();
    QSignalSpy spy(vm, &GraphViewModel::taskRemoved);
    bool ok = vm->removeTask(id);
    QVERIFY(ok);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm->taskCount(), before - 1);
    QVERIFY(!vm->hasNode(id));

    // Remove non-existent returns false
    QVERIFY(!vm->removeTask("nonexistent"));
}

void TestGraphViewModel::testRemoveTaskRemovesConnectedEdges()
{
    QString a = vm->addTask("a");
    QString b = vm->addTask("b");
    QVERIFY(vm->addEdge(a, b));
    QCOMPARE(vm->edgeCount(), 1);
    vm->removeTask(a);
    QCOMPARE(vm->edgeCount(), 0);
}

void TestGraphViewModel::testAddEdge()
{
    vm->clear();
    QString a = vm->addTask("src");
    QString b = vm->addTask("dst");
    QSignalSpy spy(vm, &GraphViewModel::edgeAdded);
    bool ok = vm->addEdge(a, b);
    QVERIFY(ok);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm->edgeCount(), 1);
}

// 核心回归：声明了非默认输入端口（image）的 task，连线时 to_port 必须写对。
void TestGraphViewModel::testAddEdgePortAware()
{
    vm->clear();
    PluginRegistry::instance().register_task(
        "ported_node",
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<PortedNode>(id, cfg);
        });

    QString a = vm->addTask("ported_node");
    QString b = vm->addTask("ported_node");
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    // 节点端口元数据应从 input_specs/output_specs 发现
    QCOMPARE(vm->nodeData(a).inputPorts, QStringList{QStringLiteral("image")});
    QCOMPARE(vm->nodeData(a).outputPorts, QStringList{QStringLiteral("out")});

    // 显式端口连线
    QVERIFY(vm->addEdge(a, "out", b, "image"));

    // 便捷重载应自动解析出真实端口（required 输入口 "image"）
    QString c = vm->addTask("ported_node");
    QVERIFY(vm->addEdge(c, b));

    // 序列化 round-trip 保留端口名
    auto edges = vm->edges();
    bool hasImageEdge = false;
    for (const auto& e : edges) {
        if (e.toPort == "image" && e.fromPort == "out") hasImageEdge = true;
    }
    QVERIFY(hasImageEdge);

    std::string json = model->to_json_string();
    QString qjson = QString::fromStdString(json);
    QVERIFY(qjson.contains("\"from_port\": \"out\""));
    QVERIFY(qjson.contains("\"to_port\": \"image\""));

    // 加载回来仍保留端口（验证 DAGSerializer 反序列化恢复）
    vm->clear();
    QVERIFY(model->from_json_string(json));
    edges = vm->edges();
    QVERIFY(!edges.isEmpty());
    for (const auto& e : edges)
        QVERIFY(e.fromPort == "out" && e.toPort == "image");

    PluginRegistry::instance().unregister_task("ported_node");
}

void TestGraphViewModel::testAddEdgeCycle()
{
    vm->clear();
    QString a = vm->addTask("a");
    QString b = vm->addTask("b");
    vm->addEdge(a, b);
    // b -> a would create a cycle, should fail
    bool ok = vm->addEdge(b, a);
    QVERIFY(!ok);
}

void TestGraphViewModel::testAddEdgeDuplicate()
{
    vm->clear();
    QString a = vm->addTask("a");
    QString b = vm->addTask("b");
    vm->addEdge(a, b);
    QVERIFY(!vm->addEdge(a, b));
}

void TestGraphViewModel::testAddEdgeSelfLoop()
{
    vm->clear();
    QString a = vm->addTask("a");
    QVERIFY(!vm->addEdge(a, a));
}

void TestGraphViewModel::testMoveNode()
{
    vm->clear();
    QString id = vm->addTask("movable", 0, 0);
    QSignalSpy spy(vm, &GraphViewModel::nodeMoved);
    bool ok = vm->moveNode(id, 42, 99);
    QVERIFY(ok);
    QCOMPARE(spy.count(), 1);
    NodeData data = vm->nodeData(id);
    QCOMPARE(data.x, 42.0);
    QCOMPARE(data.y, 99.0);
}

void TestGraphViewModel::testSelection()
{
    vm->clear();
    QString id = vm->addTask("selectable");
    QSignalSpy spy(vm, &GraphViewModel::selectionChanged);
    vm->selectNode(id);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm->selectedNodeId(), id);
    vm->clearSelection();
    QCOMPARE(vm->selectedNodeId(), QString());
}

void TestGraphViewModel::testClearGraph()
{
    vm->addTask("x");
    vm->addTask("y");
    QSignalSpy spy(vm, &GraphViewModel::graphReset);
    vm->clear();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm->taskCount(), 0);
    QCOMPARE(vm->edgeCount(), 0);
}

void TestGraphViewModel::testAutoLayout()
{
    vm->clear();
    QString a = vm->addTask("a");
    QString b = vm->addTask("b");
    QString c = vm->addTask("c");
    vm->addEdge(a, b);
    vm->addEdge(b, c);

    QSignalSpy spy(vm, &GraphViewModel::nodeMoved);
    vm->autoLayout();

    // Auto layout should emit nodeMoved for each node at least once
    QVERIFY(spy.count() >= 3);

    // After layout, nodes should have distinct positions
    NodeData na = vm->nodeData(a);
    NodeData nb = vm->nodeData(b);
    NodeData nc = vm->nodeData(c);

    // Layers should advance left to right
    QVERIFY(na.x < nb.x);
    QVERIFY(nb.x < nc.x);
}

QTEST_MAIN(TestGraphViewModel)
#include "test_graph_view_model.moc"
