#ifndef GRAPH_VIEW_MODEL_H
#define GRAPH_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include <QList>

#include "../model/GraphModel.h"

namespace graph_studio {

struct NodeData {
    QString id;
    QString type;
    qreal x;
    qreal y;
};

struct EdgeData {
    QString fromId;
    QString toId;
};

class GraphViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int taskCount READ taskCount NOTIFY taskCountChanged)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY edgeCountChanged)

public:
    explicit GraphViewModel(GraphModel& model, QObject* parent = nullptr);
    ~GraphViewModel() override;

    int taskCount() const;
    int edgeCount() const;

    Q_INVOKABLE bool addTask(const QString& taskId, const QString& taskType, qreal x = 0, qreal y = 0);
    Q_INVOKABLE bool removeTask(const QString& taskId);
    Q_INVOKABLE bool addEdge(const QString& fromId, const QString& toId);
    Q_INVOKABLE bool removeEdge(const QString& fromId, const QString& toId);

    Q_INVOKABLE QList<NodeData> nodes() const;
    Q_INVOKABLE QList<EdgeData> edges() const;

signals:
    void taskAdded(const NodeData& node);
    void taskRemoved(const QString& taskId);
    void edgeAdded(const EdgeData& edge);
    void edgeRemoved(const QString& fromId, const QString& toId);
    void taskCountChanged();
    void edgeCountChanged();

private:
    GraphModel& model_;
    QList<NodeData> nodeList_;
    QList<EdgeData> edgeList_;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_MODEL_H