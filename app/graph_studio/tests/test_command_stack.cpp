#include <QtTest/QtTest>
#include <QSignalSpy>
#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"
#include <plugin_api.hpp>

using namespace graph_studio;

// 双输入/双输出端口的插件 task，用于多端口边撤销/重做测试
namespace {
class MultiPortNode : public task_graph::INode {
public:
    using task_graph::INode::INode;
    const std::string& type() const override { static std::string t = "multi_port_node"; return t; }
    task_graph::TaskResult execute(task_graph::TaskContext&) override {
        return task_graph::TaskResult{task_graph::TaskStatus::COMPLETED};
    }
    std::vector<task_graph::PortSpec> input_specs() const override {
        return { task_graph::PortSpec{"image", "", true}, task_graph::PortSpec{"mask", "", true} };
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return { task_graph::PortSpec{"out", "", false}, task_graph::PortSpec{"aux", "", false} };
    }
};
}

class TestCommandStack : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testAddTaskUndoRedo();
    void testAddEdgeUndoRedo();
    void testAddEdgeMultiPortUndoRedo();
    void testAddEdgeRejectedNotPushed();
    void testRemoveTaskUndoRestoresEdges();
    void testCanUndoRedo();
    void testRedoClearsOnNewCommand();
    void testMacroCommand();
    void testClearStack();
    void testUndoRedoDescription();
};

GraphModel* model = nullptr;
GraphViewModel* vm = nullptr;
CommandStack* stack = nullptr;

void TestCommandStack::initTestCase()
{
    model = new GraphModel();
    vm = new GraphViewModel(*model);
    stack = new CommandStack();
}

void TestCommandStack::cleanupTestCase()
{
    delete stack;
    delete vm;
    delete model;
}

void TestCommandStack::testAddTaskUndoRedo()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "filter", 10, 20));
    QCOMPARE(vm->taskCount(), 1);

    QVERIFY(stack->undo());
    QCOMPARE(vm->taskCount(), 0);

    QVERIFY(stack->redo());
    QCOMPARE(vm->taskCount(), 1);
}

void TestCommandStack::testAddEdgeUndoRedo()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "a", 0, 0));
    stack->push(std::make_unique<AddTaskCommand>(*vm, "b", 0, 0));
    QString idA = vm->nodes()[0].id;
    QString idB = vm->nodes()[1].id;

    stack->push(std::make_unique<AddEdgeCommand>(*vm, idA, idB));
    QCOMPARE(vm->edgeCount(), 1);

    QVERIFY(stack->undo());
    QCOMPARE(vm->edgeCount(), 0);

    QVERIFY(stack->redo());
    QCOMPARE(vm->edgeCount(), 1);
}

void TestCommandStack::testAddEdgeMultiPortUndoRedo()
{
    vm->clear();
    stack->clear();
    task_graph::PluginRegistry::instance().register_task(
        "multi_port_node",
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<MultiPortNode>(id, cfg);
        });

    QString a = vm->addTask("multi_port_node");
    QString b = vm->addTask("multi_port_node");

    // 同一对节点间两条不同端口边
    stack->push(std::make_unique<AddEdgeCommand>(*vm, a, "out", b, "image"));
    stack->push(std::make_unique<AddEdgeCommand>(*vm, a, "aux", b, "mask"));
    QCOMPARE(vm->edgeCount(), 2);

    // undo 只撤最后一条（aux->mask），不得误删 out->image
    QVERIFY(stack->undo());
    QCOMPARE(vm->edgeCount(), 1);
    auto edges = vm->edges();
    QCOMPARE(edges[0].fromPort, QStringLiteral("out"));
    QCOMPARE(edges[0].toPort, QStringLiteral("image"));

    QVERIFY(stack->undo());
    QCOMPARE(vm->edgeCount(), 0);

    QVERIFY(stack->redo());
    QVERIFY(stack->redo());
    QCOMPARE(vm->edgeCount(), 2);

    // RemoveEdgeCommand 只删指定端口那条；undo 恢复
    stack->push(std::make_unique<RemoveEdgeCommand>(*vm, a, "out", b, "image"));
    QCOMPARE(vm->edgeCount(), 1);
    edges = vm->edges();
    QCOMPARE(edges[0].fromPort, QStringLiteral("aux"));
    QCOMPARE(edges[0].toPort, QStringLiteral("mask"));
    QVERIFY(stack->undo());
    QCOMPARE(vm->edgeCount(), 2);

    task_graph::PluginRegistry::instance().unregister_task("multi_port_node");
}

void TestCommandStack::testAddEdgeRejectedNotPushed()
{
    vm->clear();
    stack->clear();
    task_graph::PluginRegistry::instance().register_task(
        "multi_port_node",
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<MultiPortNode>(id, cfg);
        });

    // 目标输入口已被占用时，AddEdgeCommand 被拒绝且不得留下幽灵 undo 记录
    QString a = vm->addTask("multi_port_node");
    QString b = vm->addTask("multi_port_node");
    QString c = vm->addTask("multi_port_node");

    stack->push(std::make_unique<AddEdgeCommand>(*vm, a, "out", b, "image"));
    QCOMPARE(vm->edgeCount(), 1);

    // b.image 已被 a 占用 -> 拒绝，不新增边
    stack->push(std::make_unique<AddEdgeCommand>(*vm, c, "out", b, "image"));
    QCOMPARE(vm->edgeCount(), 1);

    // undo 只撤真正生效的那条；被拒绝的命令不得留在栈里
    QVERIFY(stack->undo());
    QCOMPARE(vm->edgeCount(), 0);
    QVERIFY(!stack->canUndo());
    QVERIFY(stack->redo());
    QCOMPARE(vm->edgeCount(), 1);

    task_graph::PluginRegistry::instance().unregister_task("multi_port_node");
}

void TestCommandStack::testRemoveTaskUndoRestoresEdges()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "src", 0, 0));
    stack->push(std::make_unique<AddTaskCommand>(*vm, "dst", 0, 0));
    QString idSrc = vm->nodes()[0].id;
    QString idDst = vm->nodes()[1].id;

    stack->push(std::make_unique<AddEdgeCommand>(*vm, idSrc, idDst));
    QCOMPARE(vm->edgeCount(), 1);

    // Remove the source node (also removes the edge)
    stack->push(std::make_unique<RemoveTaskCommand>(*vm, idSrc));
    QCOMPARE(vm->taskCount(), 1);
    QCOMPARE(vm->edgeCount(), 0);

    // Undo: node + edge should be restored
    QVERIFY(stack->undo());
    QCOMPARE(vm->taskCount(), 2);
    QCOMPARE(vm->edgeCount(), 1);
}

void TestCommandStack::testCanUndoRedo()
{
    vm->clear();
    stack->clear();

    QVERIFY(!stack->canUndo());
    QVERIFY(!stack->canRedo());

    stack->push(std::make_unique<AddTaskCommand>(*vm, "x", 0, 0));
    QVERIFY(stack->canUndo());
    QVERIFY(!stack->canRedo());

    stack->undo();
    QVERIFY(!stack->canUndo());
    QVERIFY(stack->canRedo());
}

void TestCommandStack::testRedoClearsOnNewCommand()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "a", 0, 0));
    stack->push(std::make_unique<AddTaskCommand>(*vm, "b", 0, 0));
    stack->undo(); // now redo stack has 1
    QVERIFY(stack->canRedo());

    // New command should clear redo stack
    stack->push(std::make_unique<AddTaskCommand>(*vm, "c", 0, 0));
    QVERIFY(!stack->canRedo());
}

void TestCommandStack::testMacroCommand()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "a", 0, 0));
    stack->push(std::make_unique<AddTaskCommand>(*vm, "b", 0, 0));
    QString idA = vm->nodes()[0].id;
    QString idB = vm->nodes()[1].id;

    QCOMPARE(vm->taskCount(), 2);

    auto macro = std::make_unique<MacroCommand>("Delete All");
    macro->add(std::make_unique<RemoveTaskCommand>(*vm, idA));
    macro->add(std::make_unique<RemoveTaskCommand>(*vm, idB));
    stack->push(std::move(macro));

    QCOMPARE(vm->taskCount(), 0);

    // Single undo restores both
    QVERIFY(stack->undo());
    QCOMPARE(vm->taskCount(), 2);
}

void TestCommandStack::testClearStack()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "a", 0, 0));
    QVERIFY(stack->canUndo());

    QSignalSpy spy(stack, &CommandStack::canUndoChanged);
    stack->clear();
    QVERIFY(!stack->canUndo());
    QVERIFY(!stack->canRedo());
    QCOMPARE(spy.count(), 1);
}

void TestCommandStack::testUndoRedoDescription()
{
    vm->clear();
    stack->clear();

    stack->push(std::make_unique<AddTaskCommand>(*vm, "a", 0, 0));
    QCOMPARE(stack->undoDescription(), QString("Add Task"));

    stack->undo();
    QCOMPARE(stack->redoDescription(), QString("Add Task"));
}

QTEST_MAIN(TestCommandStack)
#include "test_command_stack.moc"
