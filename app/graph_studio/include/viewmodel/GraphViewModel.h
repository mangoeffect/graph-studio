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

#include <any>
#include <memory>
#include <optional>
#include <thread>

#include "../model/GraphModel.h"
#include "viewmodel/ImageResultStore.h"
#include "catalog/TaskCatalog.h"
#include <task_graph/run_loop.hpp>

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
    QStringList inputPorts;   // 节点声明的输入端口名（input_specs）
    QStringList outputPorts;  // 节点声明的输出端口名（output_specs）
};

struct EdgeData {
    QString fromId;
    QString toId;
    QString fromPort;  // 输出端口名（如 "out"）
    QString toPort;    // 输入端口名（如 "image"）
};

class GraphViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int taskCount READ taskCount NOTIFY taskCountChanged)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY edgeCountChanged)
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY selectionChanged)
    Q_PROPERTY(bool executing READ isExecuting NOTIFY executingChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)

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
    Q_INVOKABLE bool addEdge(const QString& fromId, const QString& fromPort,
                             const QString& toId, const QString& toPort);
    Q_INVOKABLE bool removeEdge(const QString& fromId, const QString& toId);
    // 端口级删除：只移除该 (from,fromPort,to,toPort) 一条边
    Q_INVOKABLE bool removeEdge(const QString& fromId, const QString& fromPort,
                                const QString& toId, const QString& toPort);

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

    // 按 task type 字符串启发式归类
    // （Input/Output/OpenCV 子域/GPU/MediaPipe/MNN/Vision（face_detect、
    // matting 等视觉 AI 子模块）/Scripting/…）。
    // 单一来源：MainWindow 侧边栏、GraphScene 右键菜单、NodeItem 着色共用。
    static QString classifyTask(const QString& type);

    // 端口查询：按 task 类型返回其 input_specs()/output_specs() 的端口名。
    // 无声明时返回空（UI 退化为默认 in/out 锚点）。
    Q_INVOKABLE QStringList inputPorts(const QString& taskType) const;
    Q_INVOKABLE QStringList outputPorts(const QString& taskType) const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveToFile(const QString& filePath);
    Q_INVOKABLE bool loadFromFile(const QString& filePath);
    // 从 JSON 字符串直接加载（WASM E2E 测试桥 / 剪贴板导入）。baseDir 为
    // 图内相对路径的解析基准，空则走默认规则（ModelFinder / 绝对路径）。
    Q_INVOKABLE bool loadFromString(const QString& json, const QString& baseDir = QString());

    Q_INVOKABLE void autoLayout();

    Q_INVOKABLE void execute();
    Q_INVOKABLE void stop();

    // —— 运行模型（task_graph::RunLoop 驱动，阻塞在内部工作线程）——
    // runOnce = execute()；runN 连续 n 次；runLoopMode 循环直到 stop()。
    // 每轮结束发 runFinished + imageResultsReady（仅最新一轮，单槽）。
    Q_INVOKABLE void runOnce() { execute(); }
    Q_INVOKABLE void runN(int n);
    Q_INVOKABLE void runLoopMode();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    bool isPaused() const { return runLoop_ && runLoop_->is_paused(); }

    // 执行后的图像结果查询。key 格式 "nodeId:port"（单输出端口名为 "out"）。
    // 仅在 finishExecution 后填充；执行前/失败节点不产生条目。
    // 收集阶段只做类型探测、不做转换——GPU 驻留（gpu_texture）的输出保持
    // 原样不下载；首次取某 key 时才 ensure_cpu 按需同步并缓存 QImage。
    QStringList imageResultKeys() const;
    QImage imageResult(const QString& key) const;

    // 性能分析数据（执行完成后填充）
    struct ProfileTaskInfo {
        QString taskId;
        QString taskType;
        double waitMs;
        double execMs;
        double totalMs;
        double startMs;  // 相对 DAG 开始的偏移
        double endMs;
        int status;      // 0=completed 1=failed 2=skipped
    };
    struct ProfileDagInfo {
        double totalMs;
        int totalTasks;
        int completedTasks;
        int failedTasks;
        int skippedTasks;
        double criticalPathMs;
    };
    struct ProfileFrame {
        ProfileDagInfo dag;
        QList<ProfileTaskInfo> tasks;
        QString traceJson;
        QString reportJson;
    };

    int profileFrameCount() const { return profileFrames_.size(); }
    const ProfileFrame* profileFrame(int index) const;
    ProfileFrame profileAverage() const;
    QString profileTraceJson() const;
    QString profileReportJson() const;
    void clearProfileHistory();

signals:
    void taskAdded(const NodeData& node);
    void taskRemoved(const QString& taskId);
    void edgeAdded(const EdgeData& edge);
    void edgeRemoved(const EdgeData& edge);
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
    void pausedChanged();
    // 每轮运行结束（runN/runLoopMode 下的轮粒度信号；execute 单轮也发）
    void runFinished(int runIndex, bool ok, int completedTasks, int failedTasks, double durationMs);
    void imageResultsReady(QStringList keys);
    void profileDataReady(int frameIndex);

private:
    QString generateUniqueId(const QString& taskType) const;
    void onDagChanged(const task_graph::DAGChangeEvent& e);
    void onExecutionEvent(const task_graph::ExecutionEvent& e);
    bool canReach(const QString& from, const QString& to) const;
    // GraphReset 后从 DAG 重建节点/连线信号（走 catalog 缓存）
    void rebuildNodesFromDag();
    // 由 DAG 构造单个节点的展示数据（id/type/位置/参数/端口）
    NodeData makeNodeData(const std::string& id) const;
    // loadFromFile/loadFromString 共同实现：json + 相对路径基准 + 日志文案
    bool loadFromJsonData(const QString& json, const QString& graphDir,
                          const QString& logLabel);

    GraphModel& model_;
    QHash<QString, QPointF> positions_;
    QString selectedNodeId_;
    mutable QHash<QString, int> typeCounter_;
    size_t dagSubId_{0};

    TaskCatalog catalog_;        // task 类型内省缓存（端口/参数 spec）
    ImageResultStore imageStore_;  // 执行后图像结果（懒转换 + 单槽）

    // 运行会话（RunLoop 内含 DAGExecutor）。会话在 runThread_ 上阻塞驱动，
    // 事件经 QueuedConnection 编组回 UI 线程；跨轮重数据只保留最新一轮
    // （retention=LastRun），满足循环模式不积累大内存的约束。
    std::unique_ptr<task_graph::RunLoop> runLoop_;
    std::thread runThread_;
    bool executing_ = false;
    bool sessionActive_{false};

    // 运行会话内部流程（均在 UI 线程调用，除非注明）
    void startSession(task_graph::RunPolicy policy);
    void appendProfileFrame(task_graph::RunPolicy::Mode mode);  // 工作线程回调内调用
    void onRunSummary(const task_graph::RunSummary& s,
                      std::optional<task_graph::RunResult> full);  // UI 线程（编组后）
    void finishSession();  // UI 线程（编组后）
    int profileFrameCap(task_graph::RunPolicy::Mode mode) const {
        return mode == task_graph::RunPolicy::Mode::Loop ? LOOP_PROFILE_FRAMES
                                                         : MAX_PROFILE_FRAMES;
    }

    // 性能分析数据：多帧历史（每次执行追加一帧，最多 MAX_PROFILE_FRAMES；
    // 循环模式下降为 LOOP_PROFILE_FRAMES，避免 trace 字符串积累）
    static constexpr int MAX_PROFILE_FRAMES = 100;
    static constexpr int LOOP_PROFILE_FRAMES = 10;
    QList<ProfileFrame> profileFrames_;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_MODEL_H
