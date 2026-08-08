#ifndef COMMAND_STACK_H
#define COMMAND_STACK_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <memory>
#include <vector>

#include "viewmodel/GraphViewModel.h"

namespace graph_studio {

// Abstract command interface
class Command {
public:
    virtual ~Command() = default;
    // execute 返回是否真正生效；返回 false 时 CommandStack::push 不记录该命令，
    // 避免"被拒绝的操作"（如目标输入口已被占用）留下幽灵 undo 记录。
    virtual bool execute() = 0;
    virtual void undo() = 0;
    virtual QString description() const = 0;
};

using CommandPtr = std::unique_ptr<Command>;

// Add a task node
class AddTaskCommand : public Command {
public:
    AddTaskCommand(GraphViewModel& vm, const QString& taskType, qreal x, qreal y);
    bool execute() override;
    void undo() override;
    QString description() const override { return "Add Task"; }
    QString taskId() const { return taskId_; }

private:
    GraphViewModel& vm_;
    QString taskType_;
    qreal x_;
    qreal y_;
    QString taskId_;
};

// Remove a task node (records connected edges for restoration)
class RemoveTaskCommand : public Command {
public:
    RemoveTaskCommand(GraphViewModel& vm, const QString& taskId);
    bool execute() override;
    void undo() override;
    QString description() const override { return "Remove Task"; }

private:
    GraphViewModel& vm_;
    QString taskId_;
    // Snapshot for undo
    NodeData node_;
    QList<EdgeData> connectedEdges_;
    bool snapshotTaken_ = false;
};

// Add a dependency edge
class AddEdgeCommand : public Command {
public:
    AddEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& fromPort,
                   const QString& toId, const QString& toPort);
    // 便捷重载：默认 out -> in 端口
    AddEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& toId);
    bool execute() override;
    void undo() override;
    QString description() const override { return "Add Edge"; }

private:
    GraphViewModel& vm_;
    QString fromId_;
    QString fromPort_;
    QString toId_;
    QString toPort_;
};

// Remove a dependency edge
class RemoveEdgeCommand : public Command {
public:
    RemoveEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& fromPort,
                      const QString& toId, const QString& toPort);
    bool execute() override;
    void undo() override;
    QString description() const override { return "Remove Edge"; }

private:
    GraphViewModel& vm_;
    QString fromId_;
    QString fromPort_;
    QString toId_;
    QString toPort_;
};

// Change a single parameter value on a node. Records old value for undo.
class ChangeParamCommand : public Command {
public:
    ChangeParamCommand(GraphViewModel& vm, const QString& taskId,
                       const QString& key, const QVariant& newValue);
    bool execute() override;
    void undo() override;
    QString description() const override { return "Change Parameter"; }

private:
    GraphViewModel& vm_;
    QString taskId_;
    QString key_;
    QVariant oldValue_;
    QVariant newValue_;
    bool snapshotTaken_ = false;
};

// Macro command: groups multiple commands (e.g. delete selection)
class MacroCommand : public Command {
public:
    explicit MacroCommand(const QString& desc);
    void add(CommandPtr cmd);
    bool execute() override;
    void undo() override;
    QString description() const override { return desc_; }

private:
    QString desc_;
    std::vector<CommandPtr> commands_;
};

// Command stack managing undo/redo history
class CommandStack : public QObject {
    Q_OBJECT
public:
    explicit CommandStack(QObject* parent = nullptr, int maxStack = 100);

    void push(CommandPtr cmd);
    bool undo();
    bool redo();
    void clear();

    bool canUndo() const;
    bool canRedo() const;
    QString undoDescription() const;
    QString redoDescription() const;

signals:
    void canUndoChanged(bool can);
    void canRedoChanged(bool can);
    void stackChanged();

private:
    std::vector<CommandPtr> undoStack_;
    std::vector<CommandPtr> redoStack_;
    int maxStack_;
};

} // namespace graph_studio

#endif // COMMAND_STACK_H
