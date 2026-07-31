#ifndef GRAPH_VIEW_MODEL_H
#define GRAPH_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QPointF>
#include <QImage>

#include <memory>

#include "../model/GraphModel.h"
#include <task_graph_api.hpp>

namespace task_graph {
class DAGExecutor;
}

namespace graph_studio {

struct NodeData {
    QString id;
    QString type;
    qreal x = 0;
    qreal y = 0;
    QVariantMap params;
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
    Q_PROPERTY(bool executing READ isExecuting NOTIFY executingChanged)

public:
    explicit GraphViewModel(GraphModel& model, QObject* parent = nullptr);
    ~GraphViewModel() override;

    int taskCount() const;
    int edgeCount() const;
    QString selectedNodeId() const;
    bool isExecuting() const;

    Q_INVOKABLE QString addTask(const QString& taskType, qreal x = 0, qreal y = 0, const QString& taskId = QString());
    Q_INVOKABLE bool removeTask(const QString& taskId);
    Q_INVOKABLE bool moveNode(const QString& taskId, qreal x, qreal y);

    Q_INVOKABLE bool addEdge(const QString& fromId, const QString& toId);
    Q_INVOKABLE bool removeEdge(const QString& fromId, const QString& toId);

    Q_INVOKABLE void selectNode(const QString& taskId);
    Q_INVOKABLE void clearSelection();

    Q_INVOKABLE QList<NodeData> nodes() const;
    Q_INVOKABLE QList<EdgeData> edges() const;
    Q_INVOKABLE bool hasNode(const QString& taskId) const;
    Q_INVOKABLE NodeData nodeData(const QString& taskId) const;

    Q_INVOKABLE QVariantList paramSpecs(const QString& taskType) const;
    Q_INVOKABLE QVariantMap nodeParams(const QString& taskId) const;
    Q_INVOKABLE bool setNodeParam(const QString& taskId, const QString& key, const QVariant& value);
    Q_INVOKABLE QStringList availableTaskTypes() const;
    Q_INVOKABLE bool hasTaskType(const QString& type) const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveToFile(const QString& filePath);
    Q_INVOKABLE bool loadFromFile(const QString& filePath);

    Q_INVOKABLE void autoLayout();

    Q_INVOKABLE void execute();
    Q_INVOKABLE void stop();

    // 执行后的图像结果查询。key 格式 "nodeId:port"（单输出端口名为 "out"）。
    // 仅在 finishExecution 后填充；执行前/失败节点不产生条目。
    QStringList imageResultKeys() const;
    QImage imageResult(const QString& key) const;

signals:
    void taskAdded(const NodeData& node);
    void taskRemoved(const QString& taskId);
    void edgeAdded(const EdgeData& edge);
    void edgeRemoved(const QString& fromId, const QString& toId);
    void nodeMoved(const QString& taskId, qreal x, qreal y);
    void taskCountChanged();
    void edgeCountChanged();
    void selectionChanged(const QString& nodeId);
    void nodeParamsChanged(const QString& nodeId);
    void graphReset();
    void logMessage(int level, const QString& msg);
    void nodeStatusChanged(const QString& taskId, int phase, double durationMs);
    void executionStarted();
    void executionFinished();
    void executingChanged();
    // 执行完成且采集到图像结果时发出，keys 为 imageResultKeys 的快照。
    void imageResultsReady(QStringList keys);

private:
    QString generateUniqueId(const QString& taskType) const;
    void onDagChanged(const task_graph::DAGChangeEvent& e);
    void onExecutionEvent(const task_graph::ExecutionEvent& e);
    bool canReach(const QString& from, const QString& to) const;
    void finishExecution();
    void ensureExecutor();

    GraphModel& model_;
    QHash<QString, QPointF> positions_;
    QString selectedNodeId_;
    mutable QHash<QString, int> typeCounter_;
    size_t dagSubId_{0};

    std::unique_ptr<task_graph::DAGExecutor> executor_;
    bool executing_ = false;

    // 执行后采集的图像结果缓存：key="nodeId:port" -> QImage(零拷贝共享源像素)。
    // QImage 通过 cleanup function 持有源 cv::Mat/shared_ptr 的引用计数，
    // 析构时自动释放，无需手动管理生命周期。
    QHash<QString, QImage> imageResults_;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_MODEL_H
