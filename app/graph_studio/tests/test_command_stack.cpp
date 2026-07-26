#include <QtTest/QtTest>
#include <QSignalSpy>
#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"

using namespace graph_studio;

class TestCommandStack : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testAddTaskUndoRedo();
    void testAddEdgeUndoRedo();
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
