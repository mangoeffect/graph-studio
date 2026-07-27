#include "command/CommandStack.h"

using namespace graph_studio;

// ---- AddTaskCommand ----

AddTaskCommand::AddTaskCommand(GraphViewModel& vm, const QString& taskType, qreal x, qreal y)
    : vm_(vm), taskType_(taskType), x_(x), y_(y)
{
}

void AddTaskCommand::execute()
{
    if (taskId_.isEmpty()) {
        // First execution: let ViewModel generate the id
        taskId_ = vm_.addTask(taskType_, x_, y_);
    } else {
        // Redo: reuse the original id so edges still reference it
        vm_.addTask(taskType_, x_, y_, taskId_);
    }
}

void AddTaskCommand::undo()
{
    if (!taskId_.isEmpty()) {
        vm_.removeTask(taskId_);
    }
}

// ---- RemoveTaskCommand ----

RemoveTaskCommand::RemoveTaskCommand(GraphViewModel& vm, const QString& taskId)
    : vm_(vm), taskId_(taskId)
{
}

void RemoveTaskCommand::execute()
{
    // Snapshot node + connected edges before removal (only once)
    if (!snapshotTaken_) {
        node_ = vm_.nodeData(taskId_);
        for (const auto& e : vm_.edges()) {
            if (e.fromId == taskId_ || e.toId == taskId_) {
                connectedEdges_.append(e);
            }
        }
        snapshotTaken_ = true;
    }
    vm_.removeTask(taskId_);
}

void RemoveTaskCommand::undo()
{
    // Restore node (use original id)
    vm_.addTask(node_.type, node_.x, node_.y, node_.id);
    // Restore params（addTask 用的是声明默认值；这里覆盖回删除前的实际值）
    for (auto it = node_.params.begin(); it != node_.params.end(); ++it) {
        vm_.setNodeParam(node_.id, it.key(), it.value());
    }
    // Restore edges
    for (const auto& e : connectedEdges_) {
        vm_.addEdge(e.fromId, e.toId);
    }
}

// ---- AddEdgeCommand ----

AddEdgeCommand::AddEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& toId)
    : vm_(vm), fromId_(fromId), toId_(toId)
{
}

void AddEdgeCommand::execute()
{
    vm_.addEdge(fromId_, toId_);
}

void AddEdgeCommand::undo()
{
    vm_.removeEdge(fromId_, toId_);
}

// ---- RemoveEdgeCommand ----

RemoveEdgeCommand::RemoveEdgeCommand(GraphViewModel& vm, const QString& fromId, const QString& toId)
    : vm_(vm), fromId_(fromId), toId_(toId)
{
}

void RemoveEdgeCommand::execute()
{
    vm_.removeEdge(fromId_, toId_);
}

void RemoveEdgeCommand::undo()
{
    vm_.addEdge(fromId_, toId_);
}

// ---- ChangeParamCommand ----

ChangeParamCommand::ChangeParamCommand(GraphViewModel& vm, const QString& taskId,
                                       const QString& key, const QVariant& newValue)
    : vm_(vm), taskId_(taskId), key_(key), newValue_(newValue)
{
}

void ChangeParamCommand::execute()
{
    if (!snapshotTaken_) {
        // 记录旧值（仅首次执行；redo 时复用同一旧值）
        QVariantMap params = vm_.nodeParams(taskId_);
        oldValue_ = params.value(key_);
        snapshotTaken_ = true;
    }
    vm_.setNodeParam(taskId_, key_, newValue_);
}

void ChangeParamCommand::undo()
{
    vm_.setNodeParam(taskId_, key_, oldValue_);
}

// ---- MacroCommand ----

MacroCommand::MacroCommand(const QString& desc) : desc_(desc) {}

void MacroCommand::add(CommandPtr cmd)
{
    if (cmd)
        commands_.push_back(std::move(cmd));
}

void MacroCommand::execute()
{
    for (auto& c : commands_)
        c->execute();
}

void MacroCommand::undo()
{
    // Undo in reverse order
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
        (*it)->undo();
}

// ---- CommandStack ----

CommandStack::CommandStack(QObject* parent, int maxStack)
    : QObject(parent), maxStack_(maxStack)
{
}

void CommandStack::push(CommandPtr cmd)
{
    if (!cmd)
        return;

    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    // Clear redo stack on new action
    redoStack_.clear();

    // Enforce max size
    while (undoStack_.size() > static_cast<size_t>(maxStack_))
        undoStack_.erase(undoStack_.begin());

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit stackChanged();
}

bool CommandStack::undo()
{
    if (undoStack_.empty())
        return false;

    CommandPtr cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit stackChanged();
    return true;
}

bool CommandStack::redo()
{
    if (redoStack_.empty())
        return false;

    CommandPtr cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();
    undoStack_.push_back(std::move(cmd));

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit stackChanged();
    return true;
}

void CommandStack::clear()
{
    undoStack_.clear();
    redoStack_.clear();
    emit canUndoChanged(false);
    emit canRedoChanged(false);
    emit stackChanged();
}

bool CommandStack::canUndo() const { return !undoStack_.empty(); }
bool CommandStack::canRedo() const { return !redoStack_.empty(); }

QString CommandStack::undoDescription() const
{
    return undoStack_.empty() ? QString() : undoStack_.back()->description();
}

QString CommandStack::redoDescription() const
{
    return redoStack_.empty() ? QString() : redoStack_.back()->description();
}
