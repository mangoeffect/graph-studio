#include "viewmodel/GraphViewModel.h"

using namespace graph_studio;

GraphViewModel::GraphViewModel(GraphModel& model, QObject* parent)
    : QObject(parent), model_(model)
{
}

GraphViewModel::~GraphViewModel() = default;

int GraphViewModel::taskCount() const
{
    return nodeList_.size();
}

int GraphViewModel::edgeCount() const
{
    return edgeList_.size();
}

bool GraphViewModel::addTask(const QString& taskId, const QString& taskType, qreal x, qreal y)
{
    bool success = model_.add_task(taskId.toStdString(), taskType.toStdString());
    if (success) {
        NodeData data;
        data.id = taskId;
        data.type = taskType;
        data.x = x;
        data.y = y;
        nodeList_.append(data);
        emit taskAdded(data);
        emit taskCountChanged();
    }
    return success;
}

bool GraphViewModel::removeTask(const QString& taskId)
{
    for (int i = 0; i < nodeList_.size(); ++i) {
        if (nodeList_[i].id == taskId) {
            nodeList_.removeAt(i);
            emit taskRemoved(taskId);
            emit taskCountChanged();
            return true;
        }
    }
    return false;
}

bool GraphViewModel::addEdge(const QString& fromId, const QString& toId)
{
    bool success = model_.add_edge(fromId.toStdString(), toId.toStdString());
    if (success) {
        EdgeData data;
        data.fromId = fromId;
        data.toId = toId;
        edgeList_.append(data);
        emit edgeAdded(data);
        emit edgeCountChanged();
    }
    return success;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& toId)
{
    for (int i = 0; i < edgeList_.size(); ++i) {
        if (edgeList_[i].fromId == fromId && edgeList_[i].toId == toId) {
            edgeList_.removeAt(i);
            emit edgeRemoved(fromId, toId);
            emit edgeCountChanged();
            return true;
        }
    }
    return false;
}

QList<NodeData> GraphViewModel::nodes() const
{
    return nodeList_;
}

QList<EdgeData> GraphViewModel::edges() const
{
    return edgeList_;
}