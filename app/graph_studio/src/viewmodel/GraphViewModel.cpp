#include "viewmodel/GraphViewModel.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#include <QQueue>
#include <QStack>
#include <algorithm>
#include <task_graph_api.hpp>
#include <nlohmann/json.hpp>
#include "CrashReporter.h"

using namespace graph_studio;

// 本文件职责（P4a 拆分后）：
//   - 图编辑操作（节点/连线/参数）+ DAG 事件 -> Qt 信号翻译
//   - 执行编排（DAGExecutor 驱动、事件编组、结果/性能数据采集）
// 参数/端口内省桥接见 catalog/TaskCatalog，图像结果采集与懒转换见
// viewmodel/ImageResultStore。

namespace {

const int kLogTrace = static_cast<int>(task_graph::LogLevel::TRACE);
const int kLogDebug = static_cast<int>(task_graph::LogLevel::DEBUG);
const int kLogInfo  = static_cast<int>(task_graph::LogLevel::INFO);
const int kLogWarn  = static_cast<int>(task_graph::LogLevel::WARN);
const int kLogError = static_cast<int>(task_graph::LogLevel::ERROR);
const int kLogFatal = static_cast<int>(task_graph::LogLevel::FATAL);

// 同步崩溃上报的图上下文（文件名 + 规模）。崩溃事件据此定位"崩在哪个图"；
// 未启用崩溃上报时为 no-op。只上报文件名，避免泄漏用户目录结构。
void syncGraphCrashContext(GraphModel& model, const QString& filePath) {
    SetGraphContext(QFileInfo(filePath).fileName().toStdString(),
                    static_cast<int>(model.dag().num_tasks()),
                    static_cast<int>(model.dag().edge_list().size()));
}

}  // namespace

GraphViewModel::GraphViewModel(GraphModel& model, QObject* parent)
    : QObject(parent), model_(model)
{
    dagSubId_ = model_.dag().subscribe([this](const task_graph::DAGChangeEvent& e) {
        onDagChanged(e);
    });

    // 注册框架日志 sink：把 TG_LOG_* / ctx.log() 等日志转发到 UI 线程。
    // sink 可能在 executor 工作线程触发，用 QueuedConnection 编组到 UI 线程后 emit。
    task_graph::set_log_sink(
        [this](const task_graph::LogEntry& e) {
            // 格式化：[thread_name/thread_id] [file:line] msg
            // 级别由信号参数携带；时间戳暂不显示（避免消息过长）
            QString prefix;
            if (!e.thread_name.empty()) {
                prefix += QStringLiteral("[%1/%2] ")
                    .arg(QString::fromStdString(e.thread_name),
                         QString::fromStdString(e.thread_id));
            } else if (!e.thread_id.empty()) {
                prefix += QStringLiteral("[T/%1] ")
                    .arg(QString::fromStdString(e.thread_id));
            }
            if (!e.filename.empty() && e.line > 0) {
                prefix += QStringLiteral("[%1:%2] ")
                    .arg(QString::fromStdString(e.filename),
                         QString::number(e.line));
            }
            QString qmsg = prefix + QString::fromStdString(e.msg);
            int ilevel = static_cast<int>(e.level);
            QMetaObject::invokeMethod(this, [this, ilevel, qmsg]() {
                emit logMessage(ilevel, qmsg);
            }, Qt::QueuedConnection);
        });
}

GraphViewModel::~GraphViewModel()
{
    task_graph::clear_log_sink();
    model_.dag().unsubscribe(dagSubId_);
    if (runLoop_) runLoop_->cancel();
    if (runThread_.joinable()) runThread_.join();
}

int GraphViewModel::taskCount() const { return static_cast<int>(model_.dag().num_tasks()); }
int GraphViewModel::edgeCount() const { return static_cast<int>(model_.dag().num_edges()); }
QString GraphViewModel::selectedNodeId() const { return selectedNodeId_; }
bool GraphViewModel::isExecuting() const { return executing_; }

QString GraphViewModel::generateUniqueId(const QString& taskType) const
{
    int& counter = typeCounter_[taskType];
    QString id;
    do {
        ++counter;
        id = taskType + "_" + QString::number(counter);
    } while (hasNode(id));
    return id;
}

// ====== DAG 事件处理：翻译为 Qt 信号 ======
void GraphViewModel::onDagChanged(const task_graph::DAGChangeEvent& e) {
    using Type = task_graph::DAGChangeEvent::Type;
    switch (e.type) {
    case Type::TaskAdded: {
        NodeData nd;
        nd.id = QString::fromStdString(e.task_id);
        nd.type = QString::fromStdString(e.task_type);
        QPointF pos = positions_.value(nd.id);
        nd.x = pos.x();
        nd.y = pos.y();
        nd.params = catalog_.defaultParams(nd.type);
        nd.inputPorts = catalog_.inputPorts(nd.type);
        nd.outputPorts = catalog_.outputPorts(nd.type);
        emit taskAdded(nd);
        emit taskCountChanged();
        emit logMessage(kLogInfo, "Task added: " + nd.id + " (" + nd.type + ")");
        break;
    }
    case Type::TaskRemoved: {
        QString id = QString::fromStdString(e.task_id);
        positions_.remove(id);
        if (selectedNodeId_ == id) {
            selectedNodeId_.clear();
            emit selectionChanged({});
        }
        emit taskRemoved(id);
        emit taskCountChanged();
        emit logMessage(kLogInfo, "Task removed: " + id);
        break;
    }
    case Type::TaskUpdated:
        emit nodeParamsChanged(QString::fromStdString(e.task_id));
        break;
    case Type::EdgeAdded: {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        emit edgeAdded(ed);
        emit edgeCountChanged();
        emit logMessage(kLogInfo, "Edge added: " + ed.fromId + " -> " + ed.toId);
        break;
    }
    case Type::EdgeRemoved: {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        emit edgeRemoved(ed);
        emit edgeCountChanged();
        emit logMessage(kLogInfo, "Edge removed: " + ed.fromId + " -> " + ed.toId);
        break;
    }
    case Type::GraphReset: {
        emit graphReset();
        rebuildNodesFromDag();
        emit taskCountChanged();
        emit edgeCountChanged();
        emit selectionChanged({});
        break;
    }
    }
}

// GraphReset 后从 DAG 重建全部节点/连线信号（走 catalog 缓存查询）
void GraphViewModel::rebuildNodesFromDag() {
    const auto& dag = model_.dag();
    for (const auto& id : dag.task_ids()) {
        NodeData nd;
        nd.id = QString::fromStdString(id);
        nd.type = QString::fromStdString(dag.task_type(id));
        QPointF pos = positions_.value(nd.id);
        nd.x = pos.x();
        nd.y = pos.y();
        auto cfg = dag.task_config(id);
        nd.params = TaskCatalog::paramsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                                 catalog_.infoOrEmpty(nd.type));
        nd.inputPorts = catalog_.inputPorts(nd.type);
        nd.outputPorts = catalog_.outputPorts(nd.type);
        emit taskAdded(nd);
    }
    for (const auto& e : dag.edges()) {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        emit edgeAdded(ed);
    }
}

// ====== 操作：只写 DAG，事件自动驱动 UI 信号 ======

QString GraphViewModel::addTask(const QString& taskType, qreal x, qreal y, const QString& taskId)
{
    QString id = taskId.isEmpty() ? generateUniqueId(taskType) : taskId;
    if (hasNode(id)) {
        emit logMessage(kLogWarn, "Task already exists: " + id);
        return {};
    }

    positions_[id] = QPointF(x, y);
    if (!model_.add_task(id.toStdString(), taskType.toStdString())) {
        positions_.remove(id);
        emit logMessage(kLogError, "Failed to add task to DAG: " + id);
        return {};
    }
    return id;
}

bool GraphViewModel::removeTask(const QString& taskId)
{
    if (!hasNode(taskId)) return false;
    model_.remove_task(taskId.toStdString());
    return true;
}

bool GraphViewModel::moveNode(const QString& taskId, qreal x, qreal y)
{
    if (!hasNode(taskId)) return false;
    positions_[taskId] = QPointF(x, y);
    emit nodeMoved(taskId, x, y);
    return true;
}

bool GraphViewModel::addEdge(const QString& fromId, const QString& toId)
{
    // 便捷重载：按两侧 task 类型自动解析端口名（保留旧调用语义的默认解析）
    auto fromType = nodeData(fromId).type;
    auto toType = nodeData(toId).type;
    return addEdge(fromId, catalog_.defaultOutputPort(fromType),
                   toId, catalog_.defaultInputPort(toType));
}

bool GraphViewModel::addEdge(const QString& fromId, const QString& fromPort,
                             const QString& toId, const QString& toPort)
{
    if (fromId == toId) {
        emit logMessage(kLogWarn, "Cannot create self-loop: " + fromId);
        return false;
    }
    if (!hasNode(fromId) || !hasNode(toId)) {
        emit logMessage(kLogWarn, "Node not found for edge");
        return false;
    }
    // 端口级幂等：完全相同的四元组视为已存在（同 pair 不同端口可新增）
    if (model_.has_edge(fromId.toStdString(), fromPort.toStdString(),
                        toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogWarn, "Edge already exists: " + fromId + ":" + fromPort +
                        " -> " + toId + ":" + toPort);
        return false;
    }
    // 输入端口只允许一个数据源：目标 to_port 已被任何上游占用时拒绝。
    // （core 层仅为 warning/last-write-wins；编辑器层做更严格的保护。）
    if (model_.input_port_filled(toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogWarn, "Input port already connected: " + toId + ":" + toPort);
        return false;
    }
    if (canReach(toId, fromId)) {
        emit logMessage(kLogWarn, "Cycle detected, cannot add edge: " + fromId + " -> " + toId);
        return false;
    }
    if (!model_.add_edge(fromId.toStdString(), fromPort.toStdString(),
                         toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogError, "Failed to add edge to DAG: " + fromId + " -> " + toId);
        return false;
    }
    return true;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& toId)
{
    if (!model_.has_edge(fromId.toStdString(), toId.toStdString())) return false;
    model_.remove_edge(fromId.toStdString(), toId.toStdString());
    return true;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& fromPort,
                                const QString& toId, const QString& toPort)
{
    if (!model_.has_edge(fromId.toStdString(), fromPort.toStdString(),
                         toId.toStdString(), toPort.toStdString())) return false;
    model_.remove_edge(fromId.toStdString(), fromPort.toStdString(),
                       toId.toStdString(), toPort.toStdString());
    return true;
}

void GraphViewModel::selectNode(const QString& taskId)
{
    if (selectedNodeId_ == taskId) return;
    selectedNodeId_ = taskId;
    emit selectionChanged(taskId);
}

void GraphViewModel::clearSelection()
{
    if (selectedNodeId_.isEmpty()) return;
    selectedNodeId_.clear();
    emit selectionChanged({});
}

// ====== 查询：从 DAG 读 ======

QList<NodeData> GraphViewModel::nodes() const
{
    QList<NodeData> result;
    const auto& dag = model_.dag();
    for (const auto& id : dag.task_ids()) {
        result.append(makeNodeData(id));
    }
    return result;
}

NodeData GraphViewModel::makeNodeData(const std::string& id) const
{
    NodeData nd;
    nd.id = QString::fromStdString(id);
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return nd;
    nd.type = QString::fromStdString(dag.task_type(id));
    QPointF pos = positions_.value(nd.id);
    nd.x = pos.x();
    nd.y = pos.y();
    auto cfg = dag.task_config(id);
    nd.params = TaskCatalog::paramsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                             catalog_.infoOrEmpty(nd.type));
    nd.inputPorts = catalog_.inputPorts(nd.type);
    nd.outputPorts = catalog_.outputPorts(nd.type);
    return nd;
}

QList<EdgeData> GraphViewModel::edges() const
{
    QList<EdgeData> result;
    for (const auto& e : model_.dag().edges()) {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        result.append(ed);
    }
    return result;
}

bool GraphViewModel::hasNode(const QString& taskId) const
{
    return model_.dag().has_task(taskId.toStdString());
}

NodeData GraphViewModel::nodeData(const QString& taskId) const
{
    return makeNodeData(taskId.toStdString());
}

QVariantList GraphViewModel::paramSpecs(const QString& taskType) const
{
    return catalog_.paramSpecs(taskType);
}

QVariantMap GraphViewModel::nodeParams(const QString& taskId) const
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    auto cfg = dag.task_config(id);
    if (!cfg) return {};
    return TaskCatalog::paramsToVariant(cfg->params,
                                        catalog_.infoOrEmpty(dag.task_type(id)));
}

bool GraphViewModel::setNodeParam(const QString& taskId, const QString& key, const QVariant& value)
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return false;

    const TaskCatalog::TypeInfo* ti = catalog_.info(dag.task_type(id));
    if (!ti) return false;
    task_graph::TaskParams params = model_.task_params(id);
    if (!TaskCatalog::setParam(params, key, value, *ti)) return false;
    model_.update_task_params(id, params);
    emit logMessage(kLogInfo, "Param updated: " + taskId + "." + key);
    return true;
}

QStringList GraphViewModel::availableTaskTypes() const
{
    QStringList out;
    for (const auto& t : task_graph::PluginRegistry::instance().available_tasks()) {
        out.append(QString::fromStdString(t));
    }
    return out;
}

bool GraphViewModel::hasTaskType(const QString& type) const
{
    return task_graph::PluginRegistry::instance().has_task(type.toStdString());
}

QString GraphViewModel::classifyTask(const QString& type)
{
    // 优先级从上到下，首个命中即返回。
    // 读写类：按前缀语义先判定，避免 opencv_image_read/write 被 opencv_* 兜底吞掉
    if (type.endsWith("_read") || type.contains("video_capture") || type.contains("video_reader"))
        return QStringLiteral("Input");
    if (type.endsWith("_write") || type.contains("video_writer") ||
        type.contains("display") || type.contains("save"))
        return QStringLiteral("Output");

    // OpenCV 子域（按 task type 前缀细分，替代原先单一的 "OpenCV Filter" 分组）
    if (type.startsWith("opencv_resize") || type.startsWith("opencv_flip") ||
        type.startsWith("opencv_rotate") || type.startsWith("opencv_warp") ||
        type.startsWith("opencv_transpose") || type.startsWith("opencv_pyr_"))
        return QStringLiteral("OpenCV Geometry");
    if (type.startsWith("opencv_cvt_color") || type.startsWith("opencv_threshold") ||
        type.startsWith("opencv_apply_color_map"))
        return QStringLiteral("OpenCV Color");
    if (type.startsWith("opencv_canny") || type.startsWith("opencv_hough") ||
        type.contains("contour"))
        return QStringLiteral("OpenCV Edges");
    if (type.startsWith("opencv_blur") || type.startsWith("opencv_gaussian") ||
        type.startsWith("opencv_median") || type.startsWith("opencv_bilateral") ||
        type.startsWith("opencv_box") || type.startsWith("opencv_sobel") ||
        type.startsWith("opencv_scharr") || type.startsWith("opencv_laplacian") ||
        type.startsWith("opencv_filter_2d") || type.startsWith("opencv_sep_filter") ||
        type.startsWith("opencv_sqr_box") || type.startsWith("opencv_gabor") ||
        type.startsWith("opencv_dilate") || type.startsWith("opencv_erode") ||
        type.startsWith("opencv_morphology"))
        return QStringLiteral("OpenCV Filter");
    if (type.startsWith("opencv_"))
        return QStringLiteral("OpenCV");

    // 其他内置子模块
    if (type.startsWith("color_grade_")) return QStringLiteral("Color Grading");
    if (type.startsWith("gpu_"))   return QStringLiteral("GPU");
    // 混合模式子模块（Photoshop 27 种；type 名即 "blend"，GPU 优先/CPU 兜底）。
    if (type == QStringLiteral("blend"))
        return QStringLiteral("Blend");
    // 视觉 AI 子模块（face/matting 独立子库任务；后续同类任务在此追加）。
    // mp_*/mnn_*/js_script 任务层已移除（引擎保留在核心库供后端消费）。
    if (type == QStringLiteral("face_detect") || type == QStringLiteral("matting"))
        return QStringLiteral("Vision");

    // 宽松启发式兜底
    if (type.contains("input") || type.contains("load"))  return QStringLiteral("Input");
    if (type.contains("output") || type.contains("save") || type.contains("display"))
        return QStringLiteral("Output");
    return QStringLiteral("Process");
}

QStringList GraphViewModel::inputPorts(const QString& taskType) const
{
    return catalog_.inputPorts(taskType);
}

QStringList GraphViewModel::outputPorts(const QString& taskType) const
{
    return catalog_.outputPorts(taskType);
}

void GraphViewModel::clear()
{
    positions_.clear();
    typeCounter_.clear();
    selectedNodeId_.clear();
    model_.clear();
    emit selectionChanged({});
    emit logMessage(kLogInfo, "Graph cleared");
    // 清空后同步崩溃上下文，避免崩溃报告携带已不存在的旧图信息。
    syncGraphCrashContext(model_, QString());
}

bool GraphViewModel::saveToFile(const QString& filePath)
{
    nlohmann::json positions;
    for (const auto& id : model_.dag().task_ids()) {
        QPointF pos = positions_.value(QString::fromStdString(id));
        nlohmann::json p;
        p["x"] = pos.x();
        p["y"] = pos.y();
        p["type"] = model_.dag().task_type(id);
        positions[id] = p;
    }
    nlohmann::json metadata;
    if (!positions.empty()) metadata["positions"] = positions;

    std::string json_str = model_.to_json_string(metadata.dump());
    if (json_str.empty()) {
        emit logMessage(kLogError, "Failed to serialize DAG");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(kLogError, "Cannot open file for writing: " + filePath);
        return false;
    }
    QTextStream stream(&file);
    stream << QString::fromStdString(json_str);
    file.close();

    emit logMessage(kLogInfo, "Graph saved to: " + filePath);
    syncGraphCrashContext(model_, filePath);
    return true;
}

bool GraphViewModel::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(kLogError, "Cannot open file: " + filePath);
        return false;
    }
    QString json = QTextStream(&file).readAll();
    file.close();

    // graph.json 所在目录作为相对路径基准，注入到每个 task 的 _source_dir。
    // 这样 graph 内引用的图片/模型/脚本可以用相对路径，整个 graph 目录
    // 挪到别处仍可执行。
    return loadFromJsonData(json, QFileInfo(filePath).absolutePath(),
                            "Graph loaded from: " + filePath);
}

bool GraphViewModel::loadFromString(const QString& json, const QString& baseDir)
{
    return loadFromJsonData(json, baseDir, "Graph loaded from string");
}

bool GraphViewModel::loadFromJsonData(const QString& json, const QString& graphDir,
                                      const QString& logLabel)
{
    positions_.clear();
    selectedNodeId_.clear();

    std::string metadata_str = model_.from_json_string_with_metadata(
        json.toStdString(), graphDir.toStdString());
    if (metadata_str.empty() && !model_.task_count()) {
        emit logMessage(kLogError, "Failed to parse DAG JSON");
        return false;
    }

    nlohmann::json metadata;
    try { metadata = nlohmann::json::parse(metadata_str); } catch (...) {}

    if (metadata.contains("positions")) {
        for (const auto& id : model_.dag().task_ids()) {
            if (metadata["positions"].contains(id)) {
                qreal x = metadata["positions"][id]["x"].get<qreal>();
                qreal y = metadata["positions"][id]["y"].get<qreal>();
                positions_[QString::fromStdString(id)] = QPointF(x, y);
            }
        }
    } else {
        autoLayout();
    }

    // GraphReset 事件已在 reset_from 中触发，onDagChanged 已重建 UI。
    // 如有位置数据，更新已创建的 NodeItem 位置。
    if (metadata.contains("positions")) {
        for (const auto& id : model_.dag().task_ids()) {
            if (metadata["positions"].contains(id)) {
                qreal x = metadata["positions"][id]["x"].get<qreal>();
                qreal y = metadata["positions"][id]["y"].get<qreal>();
                emit nodeMoved(QString::fromStdString(id), x, y);
            }
        }
    }

    emit logMessage(kLogInfo, logLabel);
    syncGraphCrashContext(model_, logLabel);
    return true;
}

void GraphViewModel::autoLayout()
{
    if (model_.dag().num_tasks() == 0) return;

    QHash<QString, int> layer;
    QHash<QString, QSet<QString>> succ;
    QHash<QString, QSet<QString>> pred;
    QHash<QString, int> inDegree;

    QStringList allIds;
    for (const auto& id : model_.dag().task_ids()) {
        QString qid = QString::fromStdString(id);
        allIds.append(qid);
        layer[qid] = 0;
        inDegree[qid] = 0;
    }
    for (const auto& e : model_.dag().edge_list()) {
        QString from = QString::fromStdString(e.from);
        QString to = QString::fromStdString(e.to);
        succ[from].insert(to);
        pred[to].insert(from);
        inDegree[to]++;
    }

    QQueue<QString> queue;
    for (const auto& qid : allIds) {
        if (inDegree[qid] == 0) queue.enqueue(qid);
    }

    QHash<QString, int> tempInDegree = inDegree;
    while (!queue.isEmpty()) {
        QString cur = queue.dequeue();
        int maxLayer = 0;
        for (const auto& p : pred[cur]) {
            if (layer[p] + 1 > maxLayer) maxLayer = layer[p] + 1;
        }
        layer[cur] = maxLayer;
        for (const auto& s : succ[cur]) {
            if (--tempInDegree[s] == 0) queue.enqueue(s);
        }
    }

    QHash<int, QStringList> layerNodes;
    int maxLayer = 0;
    for (const auto& qid : allIds) {
        int l = layer[qid];
        layerNodes[l].append(qid);
        if (l > maxLayer) maxLayer = l;
    }

    const qreal xSpacing = 220;
    const qreal ySpacing = 120;
    const qreal startX = -maxLayer * xSpacing / 2;

    for (int l = 0; l <= maxLayer; ++l) {
        const auto& nodes = layerNodes[l];
        qreal totalHeight = (nodes.size() - 1) * ySpacing;
        qreal y = -totalHeight / 2;
        for (const auto& id : nodes) {
            qreal x = startX + l * xSpacing;
            positions_[id] = QPointF(x, y);
            emit nodeMoved(id, x, y);
            y += ySpacing;
        }
    }

    emit logMessage(kLogInfo, "Auto layout applied");
}

void GraphViewModel::execute()
{
    // 单次运行同样保留最新一轮完整结果（LastRun 单槽）：UI 需要采集图像输出。
    task_graph::RunPolicy p;
    p.mode = task_graph::RunPolicy::Mode::Once;
    p.retention = task_graph::RunPolicy::ResultRetention::LastRun;
    startSession(std::move(p));
}

void GraphViewModel::runN(int n)
{
    task_graph::RunPolicy p;
    p.mode = task_graph::RunPolicy::Mode::NTimes;
    p.count = static_cast<size_t>(std::max(1, n));
    p.retention = task_graph::RunPolicy::ResultRetention::LastRun;
    startSession(std::move(p));
}

void GraphViewModel::runLoopMode()
{
    task_graph::RunPolicy p;
    p.mode = task_graph::RunPolicy::Mode::Loop;
    p.retention = task_graph::RunPolicy::ResultRetention::LastRun;
    startSession(std::move(p));
}

void GraphViewModel::pause()
{
    if (runLoop_) runLoop_->pause();
    emit pausedChanged();
}

void GraphViewModel::resume()
{
    if (runLoop_) runLoop_->resume();
    emit pausedChanged();
}

void GraphViewModel::startSession(task_graph::RunPolicy policy)
{
    if (sessionActive_) {
        emit logMessage(kLogWarn, "Execution already in progress");
        return;
    }
    if (taskCount() == 0) {
        emit logMessage(kLogWarn, "Nothing to execute: graph is empty");
        return;
    }

    task_graph::DAGCompiler compiler;
    const auto issues = compiler.validate(model_.dag());
    bool hasError = false;
    for (const auto& issue : issues) {
        const bool isError = issue.severity == task_graph::ValidationError::Severity::ERROR;
        hasError = hasError || isError;
        emit logMessage(isError ? kLogError : kLogWarn,
                        QStringLiteral("%1%2%3")
                            .arg(issue.task_id.empty() ? QString()
                                                       : QStringLiteral("%1: ").arg(QString::fromStdString(issue.task_id)))
                            .arg(issue.port_name.empty() ? QString()
                                                         : QStringLiteral("[%1] ").arg(QString::fromStdString(issue.port_name)))
                            .arg(QString::fromStdString(issue.message)));
    }
    if (hasError) {
        emit logMessage(kLogError, "Execution aborted due to validation errors");
        return;
    }

    // —— 组装 RunLoop（每会话新建，规避跨会话状态残留）——
    task_graph::ExecutorConfig config;
    config.enable_profiling = true;
    // 任务事件编组回 UI 线程（节点状态动画 / 日志 / 崩溃 breadcrumb）。
    // DagCompleted 不在此处理：轮粒度流程由 run callback 驱动。
    config.callback = [this](const task_graph::ExecutionEvent& e) {
        if (e.type == task_graph::ExecutionEvent::Type::DagCompleted) return;
        QMetaObject::invokeMethod(this, [this, e]() { onExecutionEvent(e); },
                                  Qt::QueuedConnection);
    };
    runLoop_ = std::make_unique<task_graph::RunLoop>(std::move(policy), std::move(config));

    // 每轮 summary：工作线程侧在轮间读 profiler 生成 ProfileFrame（安全窗口），
    // 完整结果快照（LastRun 策略）一并编组回 UI 线程交付
    const auto policyMode = runLoop_->policy().mode;
    runLoop_->set_run_callback([this, policyMode](const task_graph::RunSummary& s) {
        std::optional<task_graph::RunResult> full;
        if (auto last = runLoop_->last_result()) full = std::move(*last);
        appendProfileFrame(policyMode);
        QMetaObject::invokeMethod(this, [this, s, full]() {
            onRunSummary(s, full);
        }, Qt::QueuedConnection);
    });

    executing_ = true;
    sessionActive_ = true;
    emit executingChanged();
    emit executionStarted();
    emit logMessage(kLogInfo, QStringLiteral("Executing %1 tasks...")
                        .arg(taskCount()));

    const auto* dagPtr = &model_.dag();
    if (runThread_.joinable()) runThread_.join();
    runThread_ = std::thread([this, dagPtr]() {
        runLoop_->run_all(*dagPtr);
        QMetaObject::invokeMethod(this, [this]() { finishSession(); },
                                  Qt::QueuedConnection);
    });
}

void GraphViewModel::appendProfileFrame(task_graph::RunPolicy::Mode mode)
{
    if (!runLoop_) return;
    const auto& profiler = runLoop_->executor().profiler();
    const auto dagStats = profiler.compute_dag_stats();
    const auto taskStats = profiler.compute_task_stats();

    ProfileFrame frame;
    frame.dag.totalMs = std::chrono::duration<double, std::milli>(dagStats.total_duration).count();
    frame.dag.totalTasks = static_cast<int>(dagStats.total_tasks);
    frame.dag.completedTasks = static_cast<int>(dagStats.completed_tasks);
    frame.dag.failedTasks = static_cast<int>(dagStats.failed_tasks);
    frame.dag.skippedTasks = static_cast<int>(dagStats.skipped_tasks);
    frame.dag.criticalPathMs = std::chrono::duration<double, std::milli>(dagStats.critical_path).count();

    for (const auto& ts : taskStats) {
        ProfileTaskInfo info;
        info.taskId = QString::fromStdString(ts.task_id);
        info.taskType = QString::fromStdString(ts.task_type);
        info.waitMs = std::chrono::duration<double, std::milli>(ts.wait_duration).count();
        info.execMs = std::chrono::duration<double, std::milli>(ts.exec_duration).count();
        info.totalMs = std::chrono::duration<double, std::milli>(ts.total_duration).count();
        if (dagStats.has_start && ts.has_start) {
            info.startMs = std::chrono::duration<double, std::milli>(
                ts.start_time - dagStats.start_time).count();
        }
        if (dagStats.has_start && ts.has_end) {
            info.endMs = std::chrono::duration<double, std::milli>(
                ts.end_time - dagStats.start_time).count();
        }
        if (ts.final_status == task_graph::TaskStatus::COMPLETED) info.status = 0;
        else if (ts.final_status == task_graph::TaskStatus::FAILED) info.status = 1;
        else if (ts.final_status == task_graph::TaskStatus::SKIPPED) info.status = 2;
        else info.status = 1;
        frame.tasks.append(info);
    }

    frame.traceJson = QString::fromStdString(profiler.to_trace_string(false));
    frame.reportJson = QString::fromStdString(profiler.to_json_string(true));

    profileFrames_.append(frame);
    const int cap = profileFrameCap(mode);
    while (profileFrames_.size() > cap) {
        profileFrames_.removeFirst();
    }
}

void GraphViewModel::onRunSummary(const task_graph::RunSummary& s,
                                  std::optional<task_graph::RunResult> full)
{
    const double ms = std::chrono::duration<double, std::milli>(s.duration).count();
    emit logMessage(kLogInfo, QStringLiteral("Run %1 finished: %2 ok, %3 failed (%4 ms)")
                        .arg(s.run_index)
                        .arg(s.completed_tasks)
                        .arg(s.failed_tasks)
                        .arg(ms, 0, 'f', 2));
    if (!s.ok && !s.first_failure_task_id.empty()) {
        emit logMessage(kLogError, QStringLiteral("Run %1 first failure: %2: %3")
                                      .arg(s.run_index)
                                      .arg(QString::fromStdString(s.first_failure_task_id))
                                      .arg(QString::fromStdString(s.first_failure_reason)));
    }

    // 每轮刷新图像结果（单槽：只保留最新一轮，循环模式不积累）
    if (full) {
        imageStore_.collectFrom(full->results);
    }
    emit runFinished(static_cast<int>(s.run_index), s.ok,
                     static_cast<int>(s.completed_tasks),
                     static_cast<int>(s.failed_tasks), ms);
    if (full && !imageStore_.isEmpty()) {
        emit imageResultsReady(imageStore_.keys());
    }
    if (!profileFrames_.isEmpty()) {
        emit profileDataReady(profileFrames_.size() - 1);
    }
}

void GraphViewModel::finishSession()
{
    if (!sessionActive_) return;
    sessionActive_ = false;
    executing_ = false;
    emit executingChanged();
    emit pausedChanged();
    emit executionFinished();
    emit logMessage(kLogInfo, "Session finished");
}

void GraphViewModel::onExecutionEvent(const task_graph::ExecutionEvent& e) {
    using Type = task_graph::ExecutionEvent::Type;
    switch (e.type) {
    case Type::TaskStarted:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::STARTED), 0);
        break;
    case Type::TaskCompleted:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::COMPLETED),
                              std::chrono::duration<double, std::milli>(e.duration).count());
        emit logMessage(kLogInfo, QStringLiteral("%1  (%2 ms)")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(std::chrono::duration<double, std::milli>(e.duration).count(), 0, 'f', 2));
        break;
    case Type::TaskFailed:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::FAILED),
                              std::chrono::duration<double, std::milli>(e.duration).count());
        emit logMessage(kLogError, QStringLiteral("%1%2")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(e.failure_reason.empty() ? QString() : QStringLiteral(": %1").arg(QString::fromStdString(e.failure_reason))));
        // 任务失败原因进崩溃 breadcrumb：崩溃若发生在后续执行中，
        // 报告里能看到最近的失败任务与原因。
        AddExecutionBreadcrumb(e.task_id, e.failure_reason);
        break;
    default:
        break;
    }
}

void GraphViewModel::stop()
{
    if (!sessionActive_ || !runLoop_) return;
    emit logMessage(kLogWarn, "Cancelling execution...");
    // cancel 同时解除暂停；工作线程的 run_all 随当前轮返回后收尾，
    // finishSession 由队列事件驱动（不在此直接改状态）。
    runLoop_->cancel();
}

bool GraphViewModel::canReach(const QString& from, const QString& to) const
{
    if (from == to) return true;

    QSet<QString> visited;
    QStack<QString> stack;
    stack.push(from);

    while (!stack.isEmpty()) {
        QString cur = stack.pop();
        if (cur == to) return true;
        if (visited.contains(cur)) continue;
        visited.insert(cur);

        for (const auto& e : model_.dag().outgoing_edges(cur.toStdString())) {
            QString next = QString::fromStdString(e.to);
            if (!visited.contains(next))
                stack.push(next);
        }
    }
    return false;
}

QStringList GraphViewModel::imageResultKeys() const
{
    return imageStore_.keys();
}

QImage GraphViewModel::imageResult(const QString& key) const
{
    return imageStore_.image(key);
}

QString GraphViewModel::profileTraceJson() const
{
    if (profileFrames_.isEmpty()) return {};
    return profileFrames_.last().traceJson;
}

QString GraphViewModel::profileReportJson() const
{
    if (profileFrames_.isEmpty()) return {};
    return profileFrames_.last().reportJson;
}

const GraphViewModel::ProfileFrame* GraphViewModel::profileFrame(int index) const
{
    if (index < 0 || index >= profileFrames_.size()) return nullptr;
    return &profileFrames_[index];
}

GraphViewModel::ProfileFrame GraphViewModel::profileAverage() const
{
    ProfileFrame avg;
    if (profileFrames_.isEmpty()) return avg;

    int n = profileFrames_.size();

    // Average DAG stats
    double sumTotal = 0, sumCritical = 0;
    int sumTasks = 0, sumCompleted = 0, sumFailed = 0, sumSkipped = 0;
    for (const auto& f : profileFrames_) {
        sumTotal += f.dag.totalMs;
        sumCritical += f.dag.criticalPathMs;
        sumTasks += f.dag.totalTasks;
        sumCompleted += f.dag.completedTasks;
        sumFailed += f.dag.failedTasks;
        sumSkipped += f.dag.skippedTasks;
    }
    avg.dag.totalMs = sumTotal / n;
    avg.dag.criticalPathMs = sumCritical / n;
    avg.dag.totalTasks = sumTasks / n;
    avg.dag.completedTasks = sumCompleted / n;
    avg.dag.failedTasks = sumFailed / n;
    avg.dag.skippedTasks = sumSkipped / n;

    // Average per-task stats: match by taskId across frames
    // Use first frame's task list as template, average matching tasks from all frames
    const auto& templateFrame = profileFrames_.first();
    for (const auto& tmplTask : templateFrame.tasks) {
        ProfileTaskInfo avgTask;
        avgTask.taskId = tmplTask.taskId;
        avgTask.taskType = tmplTask.taskType;
        avgTask.status = tmplTask.status;

        double sumWait = 0, sumExec = 0, sumTotal = 0, sumStart = 0, sumEnd = 0;
        int count = 0;
        for (const auto& f : profileFrames_) {
            for (const auto& t : f.tasks) {
                if (t.taskId == tmplTask.taskId) {
                    sumWait += t.waitMs;
                    sumExec += t.execMs;
                    sumTotal += t.totalMs;
                    sumStart += t.startMs;
                    sumEnd += t.endMs;
                    ++count;
                    break;
                }
            }
        }
        if (count > 0) {
            avgTask.waitMs = sumWait / count;
            avgTask.execMs = sumExec / count;
            avgTask.totalMs = sumTotal / count;
            avgTask.startMs = sumStart / count;
            avgTask.endMs = sumEnd / count;
        }
        avg.tasks.append(avgTask);
    }

    return avg;
}

void GraphViewModel::clearProfileHistory()
{
    profileFrames_.clear();
}
