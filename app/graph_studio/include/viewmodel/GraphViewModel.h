#ifndef GRAPH_VIEW_MODEL_H
#define GRAPH_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QStringList>

#include "../model/GraphModel.h"

namespace graph_studio {

struct NodeData {
    QString id;
    QString type;
    qreal x = 0;
    qreal y = 0;
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
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY selectionChanged)

public:
    explicit GraphViewModel(GraphModel& model, QObject* parent = nullptr);
    ~GraphViewModel() override;

    int taskCount() const;
    int edgeCount() const;
    QString selectedNodeId() const;

    // Node operations - generate unique id if empty
    Q_INVOKABLE QString addTask(const QString& taskType, qreal x = 0, qreal y = 0, const QString& taskId = QString());
    Q_INVOKABLE bool removeTask(const QString& taskId);
    Q_INVOKABLE bool moveNode(const QString& taskId, qreal x, qreal y);

    // Edge operations
    Q_INVOKABLE bool addEdge(const QString& fromId, const QString& toId);
    Q_INVOKABLE bool removeEdge(const QString& fromId, const QString& toId);

    // Selection
    Q_INVOKABLE void selectNode(const QString& taskId);
    Q_INVOKABLE void clearSelection();

    // Queries
    Q_INVOKABLE QList<NodeData> nodes() const;
    Q_INVOKABLE QList<EdgeData> edges() const;
    Q_INVOKABLE bool hasNode(const QString& taskId) const;
    Q_INVOKABLE NodeData nodeData(const QString& taskId) const;

    // Graph operations
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveToFile(const QString& filePath);
    Q_INVOKABLE bool loadFromFile(const QString& filePath);

    // Auto layout: compute positions by topological layers
    Q_INVOKABLE void autoLayout();

signals:
    void taskAdded(const NodeData& node);
    void taskRemoved(const QString& taskId);
    void edgeAdded(const EdgeData& edge);
    void edgeRemoved(const QString& fromId, const QString& toId);
    void nodeMoved(const QString& taskId, qreal x, qreal y);
    void taskCountChanged();
    void edgeCountChanged();
    void selectionChanged(const QString& nodeId);
    void graphReset();
    void logMessage(const QString& msg);

private:
    QString generateUniqueId(const QString& taskType) const;
    void rebuildDag();
    void removeEdgesOfNode(const QString& taskId);

    GraphModel& model_;
    QList<NodeData> nodeList_;
    QList<EdgeData> edgeList_;
    QString selectedNodeId_;
    mutable QHash<QString, int> typeCounter_;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_MODEL_H
