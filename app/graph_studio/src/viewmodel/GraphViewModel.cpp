#include "viewmodel/GraphViewModel.h"

#include <QFile>
#include <QTextStream>
#include <QSet>
#include <QQueue>
#include <QStack>
#include <algorithm>
#include <task_graph/plugin.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <plugin_api.hpp>
#include <nlohmann/json.hpp>

using namespace graph_studio;

// ---- ParamSpec 桥接：把 lib 侧 ParamSpec 拆成 QVariantMap（明确类型） ----
namespace {
QVariantMap paramSpecToVariant(const task_graph::ParamSpec& s) {
    QVariantMap m;
    m["name"] = QString::fromStdString(s.name);
    m["description"] = QString::fromStdString(s.description);
    switch (s.type) {
        case task_graph::ParamType::Int:    m["type"] = QStringLiteral("int");    break;
        case task_graph::ParamType::Float:  m["type"] = QStringLiteral("float");  break;
        case task_graph::ParamType::String: m["type"] = QStringLiteral("string"); break;
        case task_graph::ParamType::Bool:   m["type"] = QStringLiteral("bool");   break;
        case task_graph::ParamType::Enum:   m["type"] = QStringLiteral("enum");   break;
    }
    if (s.min_value) m["min"] = *s.min_value;
    if (s.max_value) m["max"] = *s.max_value;
    if (s.step)      m["step"] = *s.step;
    if (auto v = s.default_as_int())        m["default"] = *v;
    else if (auto v = s.default_as_float()) m["default"] = *v;
    else if (auto v = s.default_as_bool())  m["default"] = *v;
    else if (auto v = s.default_as_string()) m["default"] = QString::fromStdString(*v);
    if (s.type == task_graph::ParamType::Enum && !s.enum_values.empty()) {
        QVariantList labels, values;
        for (const auto& [label, value] : s.enum_values) {
            labels.append(QString::fromStdString(label));
            values.append(value);
        }
        m["enumLabels"] = labels;
        m["enumValues"] = values;
    }
    if (!s.widget_hint.empty()) m["widget"] = QString::fromStdString(s.widget_hint);
    if (!s.file_filter.empty()) m["fileFilter"] = QString::fromStdString(s.file_filter);
    return m;
}

std::vector<task_graph::ParamSpec> queryParamSpecs(const std::string& task_type) {
    if (!task_graph::PluginRegistry::instance().has_task(task_type)) return {};
    auto probe = task_graph::PluginRegistry::instance().create_task(task_type);
    return probe ? probe->param_specs() : std::vector<task_graph::ParamSpec>{};
}

QVariantMap defaultParamsForType(const std::string& task_type) {
    QVariantMap out;
    for (const auto& s : queryParamSpecs(task_type)) {
        QVariantMap vm = paramSpecToVariant(s);
        if (vm.contains("default")) out[QString::fromStdString(s.name)] = vm["default"];
    }
    return out;
}

QVariantMap taskParamsToVariant(const task_graph::TaskParams& p,
                                 const std::vector<task_graph::ParamSpec>& specs) {
    QVariantMap out;
    std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
    for (const auto& s : specs) by_name[s.name] = &s;

    for (const auto& s : specs) {
        QString k = QString::fromStdString(s.name);
        switch (s.type) {
            case task_graph::ParamType::Int:
            case task_graph::ParamType::Enum:
                if (auto v = p.get_int(s.name)) out[k] = *v;
                break;
            case task_graph::ParamType::Float:
                if (auto v = p.get_float(s.name)) out[k] = *v;
                break;
            case task_graph::ParamType::String:
                if (auto v = p.get_string(s.name)) out[k] = QString::fromStdString(*v);
                break;
            case task_graph::ParamType::Bool:
                if (auto v = p.get_bool(s.name)) out[k] = *v;
                break;
        }
    }
    for (const auto& [key, _] : p.params()) {
        if (by_name.contains(key)) continue;
        QString k = QString::fromStdString(key);
        if (auto v = p.get_int(key))         out[k] = *v;
        else if (auto v = p.get_float(key))  out[k] = *v;
        else if (auto v = p.get_bool(key))   out[k] = *v;
        else if (auto v = p.get_string(key)) out[k] = QString::fromStdString(*v);
    }
    return out;
}

void applyVariantToParams(const QString& key, const QVariant& value,
                          const task_graph::ParamSpec& spec,
                          task_graph::TaskParams& out) {
    const std::string k = key.toStdString();
    switch (spec.type) {
        case task_graph::ParamType::Int:
        case task_graph::ParamType::Enum:
            out.set_int(k, value.toInt()); break;
        case task_graph::ParamType::Float:
            out.set_float(k, value.toFloat()); break;
        case task_graph::ParamType::String:
            out.set_string(k, value.toString().toStdString()); break;
        case task_graph::ParamType::Bool:
            out.set_bool(k, value.toBool()); break;
    }
}
}  // namespace

GraphViewModel::GraphViewModel(GraphModel& model, QObject* parent)
    : QObject(parent), model_(model)
{
    dagSubId_ = model_.dag().subscribe([this](const task_graph::DAGChangeEvent& e) {
        onDagChanged(e);
    });
}

GraphViewModel::~GraphViewModel()
{
    model_.dag().unsubscribe(dagSubId_);
    if (executor_) {
        executor_->cancel();
        executor_->wait();
    }
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
        nd.params = defaultParamsForType(e.task_type);
        emit taskAdded(nd);
        emit taskCountChanged();
        emit logMessage("[INFO] Task added: " + nd.id + " (" + nd.type + ")");
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
        emit logMessage("[INFO] Task removed: " + id);
        break;
    }
    case Type::TaskUpdated:
        emit nodeParamsChanged(QString::fromStdString(e.task_id));
        break;
    case Type::EdgeAdded: {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        emit edgeAdded(ed);
        emit edgeCountChanged();
        emit logMessage("[INFO] Edge added: " + ed.fromId + " -> " + ed.toId);
        break;
    }
    case Type::EdgeRemoved:
        emit edgeRemoved(QString::fromStdString(e.from), QString::fromStdString(e.to));
        emit edgeCountChanged();
        emit logMessage("[INFO] Edge removed: " +
                        QString::fromStdString(e.from) + " -> " + QString::fromStdString(e.to));
        break;
    case Type::GraphReset: {
        emit graphReset();
        const auto& dag = model_.dag();
        for (const auto& id : dag.task_ids()) {
            NodeData nd;
            nd.id = QString::fromStdString(id);
            nd.type = QString::fromStdString(dag.task_type(id));
            QPointF pos = positions_.value(nd.id);
            nd.x = pos.x();
            nd.y = pos.y();
            auto cfg = dag.task_config(id);
            nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                            queryParamSpecs(dag.task_type(id)));
            emit taskAdded(nd);
        }
        for (const auto& e : dag.edge_list()) {
            EdgeData ed;
            ed.fromId = QString::fromStdString(e.from);
            ed.toId = QString::fromStdString(e.to);
            emit edgeAdded(ed);
        }
        emit taskCountChanged();
        emit edgeCountChanged();
        emit selectionChanged({});
        break;
    }
    }
}

// ====== 操作：只写 DAG，事件自动驱动 UI 信号 ======

QString GraphViewModel::addTask(const QString& taskType, qreal x, qreal y, const QString& taskId)
{
    QString id = taskId.isEmpty() ? generateUniqueId(taskType) : taskId;
    if (hasNode(id)) {
        emit logMessage("[WARNING] Task already exists: " + id);
        return {};
    }

    positions_[id] = QPointF(x, y);
    if (!model_.add_task(id.toStdString(), taskType.toStdString())) {
        positions_.remove(id);
        emit logMessage("[ERROR] Failed to add task to DAG: " + id);
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
    if (fromId == toId) {
        emit logMessage("[WARNING] Cannot create self-loop: " + fromId);
        return false;
    }
    if (!hasNode(fromId) || !hasNode(toId)) {
        emit logMessage("[WARNING] Node not found for edge");
        return false;
    }
    if (model_.has_edge(fromId.toStdString(), toId.toStdString())) {
        emit logMessage("[WARNING] Edge already exists: " + fromId + " -> " + toId);
        return false;
    }
    if (canReach(toId, fromId)) {
        emit logMessage("[WARNING] Cycle detected, cannot add edge: " + fromId + " -> " + toId);
        return false;
    }
    if (!model_.add_edge(fromId.toStdString(), toId.toStdString())) {
        emit logMessage("[ERROR] Failed to add edge to DAG: " + fromId + " -> " + toId);
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
        NodeData nd;
        nd.id = QString::fromStdString(id);
        nd.type = QString::fromStdString(dag.task_type(id));
        QPointF pos = positions_.value(nd.id);
        nd.x = pos.x();
        nd.y = pos.y();
        auto cfg = dag.task_config(id);
        nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                        queryParamSpecs(dag.task_type(id)));
        result.append(nd);
    }
    return result;
}

QList<EdgeData> GraphViewModel::edges() const
{
    QList<EdgeData> result;
    for (const auto& e : model_.dag().edge_list()) {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
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
    NodeData nd;
    nd.id = taskId;
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return nd;
    nd.type = QString::fromStdString(dag.task_type(id));
    QPointF pos = positions_.value(taskId);
    nd.x = pos.x();
    nd.y = pos.y();
    auto cfg = dag.task_config(id);
    nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                    queryParamSpecs(dag.task_type(id)));
    return nd;
}

QVariantList GraphViewModel::paramSpecs(const QString& taskType) const
{
    QVariantList out;
    for (const auto& s : queryParamSpecs(taskType.toStdString())) {
        out.append(paramSpecToVariant(s));
    }
    return out;
}

QVariantMap GraphViewModel::nodeParams(const QString& taskId) const
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    auto cfg = dag.task_config(id);
    if (!cfg) return {};
    return taskParamsToVariant(cfg->params, queryParamSpecs(dag.task_type(id)));
}

bool GraphViewModel::setNodeParam(const QString& taskId, const QString& key, const QVariant& value)
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return false;

    std::string type = dag.task_type(id);
    auto specs = queryParamSpecs(type);
    for (const auto& s : specs) {
        if (s.name == key.toStdString()) {
            task_graph::TaskParams params = model_.task_params(id);
            applyVariantToParams(key, value, s, params);
            model_.update_task_params(id, params);
            emit logMessage("[INFO] Param updated: " + taskId + "." + key);
            return true;
        }
    }
    return false;
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

void GraphViewModel::clear()
{
    positions_.clear();
    typeCounter_.clear();
    selectedNodeId_.clear();
    model_.clear();
    emit selectionChanged({});
    emit logMessage("[INFO] Graph cleared");
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
        emit logMessage("[ERROR] Failed to serialize DAG");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage("[ERROR] Cannot open file for writing: " + filePath);
        return false;
    }
    QTextStream stream(&file);
    stream << QString::fromStdString(json_str);
    file.close();

    emit logMessage("[INFO] Graph saved to: " + filePath);
    return true;
}

bool GraphViewModel::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage("[ERROR] Cannot open file: " + filePath);
        return false;
    }
    QString json = QTextStream(&file).readAll();
    file.close();

    positions_.clear();
    selectedNodeId_.clear();

    std::string metadata_str = model_.from_json_string_with_metadata(json.toStdString());
    if (metadata_str.empty() && !model_.task_count()) {
        emit logMessage("[ERROR] Failed to parse DAG JSON");
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

    emit logMessage("[INFO] Graph loaded from: " + filePath);
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

    emit logMessage("[INFO] Auto layout applied");
}

void GraphViewModel::execute()
{
    if (executing_) {
        emit logMessage("[WARN] Execution already in progress");
        return;
    }
    if (taskCount() == 0) {
        emit logMessage("[WARN] Nothing to execute: graph is empty");
        return;
    }

    task_graph::DAGCompiler compiler;
    const auto issues = compiler.validate(model_.dag());
    bool hasError = false;
    for (const auto& issue : issues) {
        const bool isError = issue.severity == task_graph::ValidationError::Severity::ERROR;
        hasError = hasError || isError;
        emit logMessage(QStringLiteral("[%1] %2%3%4")
                            .arg(isError ? "ERROR" : "WARN")
                            .arg(issue.task_id.empty() ? QString()
                                                       : QStringLiteral("%1: ").arg(QString::fromStdString(issue.task_id)))
                            .arg(issue.port_name.empty() ? QString()
                                                          : QStringLiteral("[%1] ").arg(QString::fromStdString(issue.port_name)))
                            .arg(QString::fromStdString(issue.message)));
    }
    if (hasError) {
        emit logMessage("[ERROR] Execution aborted due to validation errors");
        return;
    }

    executing_ = true;
    emit executingChanged();
    emit executionStarted();
    emit logMessage(QStringLiteral("[INFO] Executing %1 tasks...").arg(taskCount()));

    ensureExecutor();

    try {
        executor_->execute(model_.dag());
    } catch (const std::exception& ex) {
        emit logMessage(QStringLiteral("[ERROR] Failed to start execution: %1").arg(ex.what()));
        executing_ = false;
        emit executingChanged();
        return;
    }
}

void GraphViewModel::ensureExecutor()
{
    if (executor_) return;
    task_graph::ExecutorConfig config;
    config.enable_profiling = true;
    config.callback = [this](const task_graph::ExecutionEvent& e) {
        QMetaObject::invokeMethod(this, [this, e]() { onExecutionEvent(e); },
                                  Qt::QueuedConnection);
    };
    executor_ = std::make_unique<task_graph::DAGExecutor>(config);
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
        emit logMessage(QStringLiteral("[OK] %1  (%2 ms)")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(std::chrono::duration<double, std::milli>(e.duration).count(), 0, 'f', 2));
        break;
    case Type::TaskFailed:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::FAILED),
                              std::chrono::duration<double, std::milli>(e.duration).count());
        emit logMessage(QStringLiteral("[FAIL] %1%2")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(e.failure_reason.empty() ? QString() : QStringLiteral(": %1").arg(QString::fromStdString(e.failure_reason))));
        break;
    case Type::DagCompleted:
        finishExecution();
        break;
    default:
        break;
    }
}

void GraphViewModel::stop()
{
    if (!executing_ || !executor_) return;
    emit logMessage("[WARN] Cancelling execution...");
    executor_->cancel();
    finishExecution();
}

void GraphViewModel::finishExecution()
{
    if (!executing_) return;

    int completed = 0, failed = 0;
    if (executor_) {
        const auto results = executor_->get_results();
        for (const auto& [id, result] : results) {
            const bool ok = result.is_success();
            completed += ok ? 1 : 0;
            failed += ok ? 0 : 1;
            const double ms = std::chrono::duration<double, std::milli>(result.duration).count();
            emit logMessage(QStringLiteral("[%1] %2  (%3 ms)")
                                .arg(ok ? "OK" : "FAIL")
                                .arg(QString::fromStdString(id))
                                .arg(ms, 0, 'f', 2));
        }
    }
    emit logMessage(QStringLiteral("[INFO] Execution finished: %1 ok, %2 failed")
                        .arg(completed)
                        .arg(failed));

    executing_ = false;
    emit executingChanged();
    emit executionFinished();
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
