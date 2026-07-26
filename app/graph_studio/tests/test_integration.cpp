#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QFile>
#include <QTextStream>
#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"

using namespace graph_studio;

class TestIntegration : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void testFullWorkflow();
    void testSaveLoad();
    void testUndoRedoComplexSequence();

private:
    GraphModel* model_ = nullptr;
    GraphViewModel* vm_ = nullptr;
    CommandStack* stack_ = nullptr;
};

void TestIntegration::init()
{
    model_ = new GraphModel();
    vm_ = new GraphViewModel(*model_);
    stack_ = new CommandStack();
}

void TestIntegration::cleanup()
{
    delete stack_;
    delete vm_;
    delete model_;
}

void TestIntegration::testFullWorkflow()
{
    // Simulate building a pipeline: input -> blur -> sobel -> display
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "input", -300, 0));
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "blur", -100, 0));
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "sobel", 100, 0));
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "display", 300, 0));
    QCOMPARE(vm_->taskCount(), 4);

    auto nodes = vm_->nodes();
    QString idIn = nodes[0].id;
    QString idBlur = nodes[1].id;
    QString idSobel = nodes[2].id;
    QString idDisplay = nodes[3].id;

    stack_->push(std::make_unique<AddEdgeCommand>(*vm_, idIn, idBlur));
    stack_->push(std::make_unique<AddEdgeCommand>(*vm_, idBlur, idSobel));
    stack_->push(std::make_unique<AddEdgeCommand>(*vm_, idSobel, idDisplay));
    QCOMPARE(vm_->edgeCount(), 3);

    // Select a node
    vm_->selectNode(idBlur);
    QCOMPARE(vm_->selectedNodeId(), idBlur);

    // Delete the middle node - should remove its edges too
    stack_->push(std::make_unique<RemoveTaskCommand>(*vm_, idBlur));
    QCOMPARE(vm_->taskCount(), 3);
    QCOMPARE(vm_->edgeCount(), 1); // only sobel->display remains

    // Undo deletion - node and edges restored
    QVERIFY(stack_->undo());
    QCOMPARE(vm_->taskCount(), 4);
    QCOMPARE(vm_->edgeCount(), 3);

    // Auto layout
    vm_->autoLayout();
    // Verify nodes have been positioned
    for (const auto& n : vm_->nodes()) {
        // After layout, positions should be set (not all zero unless single source)
        QVERIFY(n.x != 0 || n.y != 0 || vm_->nodes().size() == 1);
    }
}

void TestIntegration::testSaveLoad()
{
    // Build a graph
    vm_->addTask("task_a", 10, 20);
    vm_->addTask("task_b", 30, 40);
    auto nodes = vm_->nodes();
    QString idA = nodes[0].id;
    QString idB = nodes[1].id;
    vm_->addEdge(idA, idB);
    QCOMPARE(vm_->taskCount(), 2);
    QCOMPARE(vm_->edgeCount(), 1);

    // Save to temp file
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    tmpFile.close();
    QString path = tmpFile.fileName() + ".json";

    QVERIFY(vm_->saveToFile(path));

    // Load into a fresh ViewModel
    GraphModel model2;
    GraphViewModel vm2(model2);
    QVERIFY(vm2.loadFromFile(path));

    QCOMPARE(vm2.taskCount(), 2);
    QCOMPARE(vm2.edgeCount(), 1);

    // Cleanup
    QFile::remove(path);
}

void TestIntegration::testUndoRedoComplexSequence()
{
    // Perform a series of operations and verify undo/redo integrity
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "a", 0, 0));
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "b", 0, 0));
    QString idA = vm_->nodes()[0].id;
    QString idB = vm_->nodes()[1].id;
    stack_->push(std::make_unique<AddEdgeCommand>(*vm_, idA, idB));
    stack_->push(std::make_unique<AddTaskCommand>(*vm_, "c", 0, 0));
    QString idC = vm_->nodes()[2].id;
    stack_->push(std::make_unique<AddEdgeCommand>(*vm_, idB, idC));

    QCOMPARE(vm_->taskCount(), 3);
    QCOMPARE(vm_->edgeCount(), 2);

    // Undo all (5 operations)
    for (int i = 0; i < 5; ++i) {
        QVERIFY(stack_->undo());
    }
    QCOMPARE(vm_->taskCount(), 0);
    QCOMPARE(vm_->edgeCount(), 0);

    // Redo all
    for (int i = 0; i < 5; ++i) {
        QVERIFY(stack_->redo());
    }
    QCOMPARE(vm_->taskCount(), 3);
    QCOMPARE(vm_->edgeCount(), 2);

    // Verify edges are correctly restored
    bool hasAB = false, hasBC = false;
    for (const auto& e : vm_->edges()) {
        if (e.fromId == idA && e.toId == idB) hasAB = true;
        if (e.fromId == idB && e.toId == idC) hasBC = true;
    }
    QVERIFY(hasAB);
    QVERIFY(hasBC);
}

QTEST_MAIN(TestIntegration)
#include "test_integration.moc"
