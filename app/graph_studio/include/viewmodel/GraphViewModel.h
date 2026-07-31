#ifndef GRAPH_VIEW_MODEL_H
#define GRAPH_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

#include <memory>

#include "../model/GraphModel.h"

namespace task_graph {
class DAGExecutor;
}

namespace graph_studio {

struct NodeData {
    QString id;
    QString type;
    qreal x = 0;
    qreal y = 0;
    QVariantMap params;   // 当前参数值（key -> value），与 task param_specs 对应
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

    // 参数 schema 与读写：UI 通过 paramSpecs 拿到某 task type 的可编辑参数
    // （类型/默认值/范围/枚举），通过 nodeParams / setNodeParam 读写节点实例值。
    // 桥接层把 lib 侧的 task_graph::ParamSpec 拆成明确类型的 QVariantMap，避免
    // std::any 跨 lib/GUI 边界的 ABI 风险。
    Q_INVOKABLE QVariantList paramSpecs(const QString& taskType) const;
    Q_INVOKABLE QVariantMap nodeParams(const QString& taskId) const;
    Q_INVOKABLE bool setNodeParam(const QString& taskId, const QString& key, const QVariant& value);
    // 可用 task 类型（从 PluginRegistry 动态获取，替代硬编码列表）
    Q_INVOKABLE QStringList availableTaskTypes() const;
    Q_INVOKABLE bool hasTaskType(const QString& type) const;

    // Graph operations
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveToFile(const QString& filePath);
    Q_INVOKABLE bool loadFromFile(const QString& filePath);

    // Auto layout: compute positions by topological layers
    Q_INVOKABLE void autoLayout();

    // 执行：异步跑当前 DAG。profile_callback 在 executor 后台线程触发，
    // 通过排队信号 marshal 回 UI 线程；完成检测用 QTimer 轮询 future。
    Q_INVOKABLE void execute();
    Q_INVOKABLE void stop();

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
    void logMessage(const QString& msg);
    // 执行相关：phase 取值与 task_graph::ProfilePhase 对应（0=READY,1=STARTED,
    // 2=COMPLETED,3=FAILED,4=SKIPPED）。durationMs 仅 COMPLETED/FAILED 有效。
    void nodeStatusChanged(const QString& taskId, int phase, double durationMs);
    void executionStarted();
    void executionFinished();
    void executingChanged();

private:
    QString generateUniqueId(const QString& taskType) const;
    void rebuildDag();
    void removeEdgesOfNode(const QString& taskId);
    bool canReach(const QString& from, const QString& to) const;
    void finishExecution();
    void ensureExecutor();

    GraphModel& model_;
    QList<NodeData> nodeList_;
    QList<EdgeData> edgeList_;
    QString selectedNodeId_;
    mutable QHash<QString, int> typeCounter_;

    // 执行状态：executor_ 持有后台执行；完成由 completion_callback 通知。
    std::unique_ptr<task_graph::DAGExecutor> executor_;
    bool executing_ = false;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_MODEL_H
