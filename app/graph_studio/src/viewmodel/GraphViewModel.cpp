#include "viewmodel/GraphViewModel.h"

#include <QFile>
#include <QTextStream>
#include <QSet>
#include <QQueue>
#include <QStack>
#include <QTimer>
#include <algorithm>
#include <task_graph/plugin.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <plugin_api.hpp>
#include <nlohmann/json.hpp>

using namespace graph_studio;

// ---- ParamSpec 桥接：把 lib 侧 ParamSpec 拆成 QVariantMap（明确类型） ----
// 避免 std::any 跨 lib/GUI 边界；GUI 侧只消费 QVariant。
// 使用 SDK 的 typed accessor（default_as_int/float/string/bool），WASM 安全。
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
    // 默认值按类型取（使用 SDK typed accessor，无需 std::any_cast）
    if (auto v = s.default_as_int())        m["default"] = *v;
    else if (auto v = s.default_as_float()) m["default"] = *v;
    else if (auto v = s.default_as_bool())  m["default"] = *v;
    else if (auto v = s.default_as_string()) m["default"] = QString::fromStdString(*v);
    // 枚举可选值
    if (s.type == task_graph::ParamType::Enum && !s.enum_values.empty()) {
        QVariantList labels, values;
        for (const auto& [label, value] : s.enum_values) {
            labels.append(QString::fromStdString(label));
            values.append(value);
        }
        m["enumLabels"] = labels;
        m["enumValues"] = values;
    }
    // UI 渲染提示（如文件浏览按钮）
    if (!s.widget_hint.empty()) m["widget"] = QString::fromStdString(s.widget_hint);
    if (!s.file_filter.empty()) m["fileFilter"] = QString::fromStdString(s.file_filter);
    return m;
}

// 取某 task type 的 param_specs（实例化临时 task 读 schema）
std::vector<task_graph::ParamSpec> queryParamSpecs(const std::string& task_type) {
    if (!task_graph::PluginRegistry::instance().has_task(task_type)) return {};
    auto probe = task_graph::PluginRegistry::instance().create_task(task_type);
    return probe ? probe->param_specs() : std::vector<task_graph::ParamSpec>{};
}

// 用声明 specs 的默认值初始化 params 容器
QVariantMap defaultParamsForType(const std::string& task_type) {
    QVariantMap out;
    for (const auto& s : queryParamSpecs(task_type)) {
        QVariantMap vm = paramSpecToVariant(s);
        if (vm.contains("default")) out[QString::fromStdString(s.name)] = vm["default"];
    }
    return out;
}

// 把 TaskParams 转成 QVariantMap，供 UI 读取。
// 使用 SDK typed getter（get_int/get_float/...），避免 std::any_cast + typeid 泄漏。
QVariantMap taskParamsToVariant(const task_graph::TaskParams& p,
                                 const std::vector<task_graph::ParamSpec>& specs) {
    QVariantMap out;
    std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
    for (const auto& s : specs) by_name[s.name] = &s;

    // 已声明参数：按 spec 类型用 typed getter 读取
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

    // 未声明参数：遍历 keys（仅取 key 名，值仍走 typed getter）
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

// 把 QVariant 值按 spec 类型写回 TaskParams
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
}

GraphViewModel::~GraphViewModel()
{
    // 确保后台执行线程停止后再析构，避免 profile_callback 回调悬空 this。
    if (executor_) {
        executor_->cancel();
        executor_->wait();
    }
}

int GraphViewModel::taskCount() const { return nodeList_.size(); }
int GraphViewModel::edgeCount() const { return edgeList_.size(); }
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

QString GraphViewModel::addTask(const QString& taskType, qreal x, qreal y, const QString& taskId)
{
    QString id = taskId.isEmpty() ? generateUniqueId(taskType) : taskId;
    if (hasNode(id)) {
        emit logMessage("[WARNING] Task already exists: " + id);
        return {};
    }

    if (!model_.add_task(id.toStdString(), taskType.toStdString())) {
        emit logMessage("[ERROR] Failed to add task to DAG: " + id);
        return {};
    }

    NodeData data;
    data.id = id;
    data.type = taskType;
    data.x = x;
    data.y = y;
    // 按该 type 声明的 param_specs 用默认值初始化 params
    data.params = defaultParamsForType(taskType.toStdString());
    nodeList_.append(data);
    emit taskAdded(data);
    emit taskCountChanged();
    emit logMessage("[INFO] Task added: " + id + " (" + taskType + ")");
    return id;
}

bool GraphViewModel::removeTask(const QString& taskId)
{
    bool found = false;
    for (int i = 0; i < nodeList_.size(); ++i) {
        if (nodeList_[i].id == taskId) {
            nodeList_.removeAt(i);
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    removeEdgesOfNode(taskId);
    model_.remove_task(taskId.toStdString());

    if (selectedNodeId_ == taskId) {
        selectedNodeId_.clear();
        emit selectionChanged({});
    }

    emit taskRemoved(taskId);
    emit taskCountChanged();
    emit logMessage("[INFO] Task removed: " + taskId);
    return true;
}

bool GraphViewModel::moveNode(const QString& taskId, qreal x, qreal y)
{
    for (auto& node : nodeList_) {
        if (node.id == taskId) {
            node.x = x;
            node.y = y;
            emit nodeMoved(taskId, x, y);
            return true;
        }
    }
    return false;
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
    for (const auto& e : edgeList_) {
        if (e.fromId == fromId && e.toId == toId) {
            emit logMessage("[WARNING] Edge already exists: " + fromId + " -> " + toId);
            return false;
        }
    }

    // Cycle detection: adding from->to would create a cycle if to can already
    // reach from through existing edges.
    if (canReach(toId, fromId)) {
        emit logMessage("[WARNING] Cycle detected, cannot add edge: " + fromId + " -> " + toId);
        return false;
    }

    if (!model_.add_edge(fromId.toStdString(), toId.toStdString())) {
        emit logMessage("[ERROR] Failed to add edge to DAG (possible cycle): " + fromId + " -> " + toId);
        return false;
    }

    EdgeData data;
    data.fromId = fromId;
    data.toId = toId;
    edgeList_.append(data);
    emit edgeAdded(data);
    emit edgeCountChanged();
    emit logMessage("[INFO] Edge added: " + fromId + " -> " + toId);
    return true;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& toId)
{
    for (int i = 0; i < edgeList_.size(); ++i) {
        if (edgeList_[i].fromId == fromId && edgeList_[i].toId == toId) {
            edgeList_.removeAt(i);
            model_.remove_edge(fromId.toStdString(), toId.toStdString());
            emit edgeRemoved(fromId, toId);
            emit edgeCountChanged();
            emit logMessage("[INFO] Edge removed: " + fromId + " -> " + toId);
            return true;
        }
    }
    return false;
}

void GraphViewModel::selectNode(const QString& taskId)
{
    if (selectedNodeId_ == taskId)
        return;
    selectedNodeId_ = taskId;
    emit selectionChanged(taskId);
}

void GraphViewModel::clearSelection()
{
    if (selectedNodeId_.isEmpty())
        return;
    selectedNodeId_.clear();
    emit selectionChanged({});
}

QList<NodeData> GraphViewModel::nodes() const { return nodeList_; }
QList<EdgeData> GraphViewModel::edges() const { return edgeList_; }

bool GraphViewModel::hasNode(const QString& taskId) const
{
    for (const auto& n : nodeList_) {
        if (n.id == taskId)
            return true;
    }
    return false;
}

NodeData GraphViewModel::nodeData(const QString& taskId) const
{
    for (const auto& n : nodeList_) {
        if (n.id == taskId)
            return n;
    }
    return {};
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
    for (const auto& n : nodeList_) {
        if (n.id == taskId) return n.params;
    }
    return {};
}

bool GraphViewModel::setNodeParam(const QString& taskId, const QString& key, const QVariant& value)
{
    for (auto& n : nodeList_) {
        if (n.id != taskId) continue;
        n.params[key] = value;
        // 同步到 Model/DAG（按声明类型写回）
        auto specs = queryParamSpecs(n.type.toStdString());
        for (const auto& s : specs) {
            if (s.name == key.toStdString()) {
                task_graph::TaskParams params = model_.task_params(taskId.toStdString());
                applyVariantToParams(key, value, s, params);
                model_.update_task_params(taskId.toStdString(), params);
                break;
            }
        }
        emit nodeParamsChanged(taskId);
        emit logMessage("[INFO] Param updated: " + taskId + "." + key);
        return true;
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
    nodeList_.clear();
    edgeList_.clear();
    selectedNodeId_.clear();
    typeCounter_.clear();
    model_.clear();
    emit graphReset();
    emit taskCountChanged();
    emit edgeCountChanged();
    emit selectionChanged({});
    emit logMessage("[INFO] Graph cleared");
}

bool GraphViewModel::saveToFile(const QString& filePath)
{
    // 构建元数据 JSON（节点位置等 UI-only 数据）
    nlohmann::json positions;
    for (const auto& n : nodeList_) {
        nlohmann::json pos;
        pos["x"] = n.x;
        pos["y"] = n.y;
        pos["type"] = n.type.toStdString();
        positions[n.id.toStdString()] = pos;
    }
    nlohmann::json metadata;
    if (!positions.empty()) metadata["positions"] = positions;

    // 用 SDK 序列化器输出带元数据的 JSON（无字符串手术）
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

    // 用带元数据的反序列化（positions 等 UI-only 数据 round-trip）
    std::string metadata_str = model_.from_json_string_with_metadata(json.toStdString());
    if (metadata_str.empty() && !model_.task_count()) {
        emit logMessage("[ERROR] Failed to parse DAG JSON");
        return false;
    }

    // 从 metadata 恢复 UI 状态
    nlohmann::json metadata;
    try { metadata = nlohmann::json::parse(metadata_str); } catch (...) {}

    // 重建 ViewModel 列表（使用值类型查询，不遍历内部容器）
    nodeList_.clear();
    edgeList_.clear();
    selectedNodeId_.clear();

    const auto& dag = model_.dag();
    for (const auto& id : dag.task_ids()) {
        NodeData nd;
        nd.id = QString::fromStdString(id);
        nd.type = QString::fromStdString(dag.task_type(id));
        auto config_opt = dag.task_config(id);
        nd.params = taskParamsToVariant(config_opt ? config_opt->params : task_graph::TaskParams{},
                                        queryParamSpecs(dag.task_type(id)));
        // 恢复位置
        if (metadata.contains("positions") && metadata["positions"].contains(id)) {
            nd.x = metadata["positions"][id]["x"].get<qreal>();
            nd.y = metadata["positions"][id]["y"].get<qreal>();
        }
        nodeList_.append(nd);
    }
    for (const auto& e : dag.edge_list()) {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        edgeList_.append(ed);
    }

    // 无位置数据时自动布局
    bool has_positions = metadata.contains("positions") && !metadata["positions"].empty();
    if (!has_positions) autoLayout();

    // 重建场景
    emit graphReset();
    for (const auto& nd : nodeList_)
        emit taskAdded(nd);
    for (const auto& ed : edgeList_)
        emit edgeAdded(ed);

    emit taskCountChanged();
    emit edgeCountChanged();
    emit selectionChanged({});
    emit logMessage("[INFO] Graph loaded from: " + filePath);

    return true;
}

void GraphViewModel::autoLayout()
{
    if (nodeList_.isEmpty())
        return;

    // Topological sort into layers (longest path from source)
    QHash<QString, int> layer;
    QHash<QString, QSet<QString>> succ;
    QHash<QString, QSet<QString>> pred;
    QHash<QString, int> inDegree;

    for (const auto& n : nodeList_) {
        layer[n.id] = 0;
        inDegree[n.id] = 0;
    }
    for (const auto& e : edgeList_) {
        succ[e.fromId].insert(e.toId);
        pred[e.toId].insert(e.fromId);
        inDegree[e.toId]++;
    }

    // Kahn-like longest-path layering
    QQueue<QString> queue;
    for (const auto& n : nodeList_) {
        if (inDegree[n.id] == 0)
            queue.enqueue(n.id);
    }

    QHash<QString, int> tempInDegree = inDegree;
    int processed = 0;
    while (!queue.isEmpty()) {
        QString cur = queue.dequeue();
        int maxLayer = 0;
        for (const auto& p : pred[cur]) {
            if (layer[p] + 1 > maxLayer)
                maxLayer = layer[p] + 1;
        }
        layer[cur] = maxLayer;

        for (const auto& s : succ[cur]) {
            if (--tempInDegree[s] == 0)
                queue.enqueue(s);
        }
        processed++;
    }
    // Handle cycles (shouldn't happen in DAG) - assign layer 0
    if (processed < nodeList_.size()) {
        for (const auto& n : nodeList_) {
            if (layer[n.id] == 0 && inDegree[n.id] > 0)
                layer[n.id] = 0;
        }
    }

    // Group nodes by layer
    QHash<int, QStringList> layerNodes;
    int maxLayer = 0;
    for (const auto& n : nodeList_) {
        int l = layer[n.id];
        layerNodes[l].append(n.id);
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
            for (auto& node : nodeList_) {
                if (node.id == id) {
                    node.x = startX + l * xSpacing;
                    node.y = y;
                    emit nodeMoved(id, node.x, node.y);
                    break;
                }
            }
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
    if (nodeList_.isEmpty()) {
        emit logMessage("[WARN] Nothing to execute: graph is empty");
        return;
    }

    // DAG 已通过增量 API 与 UI 状态同步，无需全量重建。

    // 执行前校验：有环或 ERROR 级契约问题则中止。
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
    emit logMessage(QStringLiteral("[INFO] Executing %1 tasks...").arg(nodeList_.size()));

    // executor 可复用（execute() 内部会清 results_、用 running_ 守卫重入，
    // 线程池为成员）；profile_callback 只捕获稳定的 this，一次构造即可。
    ensureExecutor();

    try {
        executor_->execute(model_.dag());
    } catch (const std::exception& ex) {
        emit logMessage(QStringLiteral("[ERROR] Failed to start execution: %1").arg(ex.what()));
        // 不销毁 executor_，保留给下次尝试复用。
        executing_ = false;
        emit executingChanged();
        return;
    }

    // WASM 下 execute() 同步返回，completion_callback 已在 execute() 内触发，
    // finishExecution 由 QueuedConnection 在事件循环中执行。
}

void GraphViewModel::ensureExecutor()
{
    if (executor_)
        return;
    task_graph::ExecutorConfig config;
    config.enable_profiling = true;
    // 回调在 executor 后台线程触发：只发排队信号，绝不碰 widget。
    config.profile_callback = [this](const task_graph::TaskProfileEvent& e) {
        const QString id = QString::fromStdString(e.task_id);
        const int phase = static_cast<int>(e.phase);
        const double ms = std::chrono::duration<double, std::milli>(e.duration).count();
        QMetaObject::invokeMethod(
            this,
            [this, id, phase, ms]() { emit nodeStatusChanged(id, phase, ms); },
            Qt::QueuedConnection);
    };
    // 执行完成回调（替代 QTimer 轮询）：marshal 回 UI 线程收尾
    config.completion_callback = [this]() {
        QMetaObject::invokeMethod(this, [this]() { finishExecution(); },
                                  Qt::QueuedConnection);
    };
    executor_ = std::make_unique<task_graph::DAGExecutor>(config);
}

void GraphViewModel::stop()
{
    if (!executing_ || !executor_)
        return;
    emit logMessage("[WARN] Cancelling execution...");
    executor_->cancel();
    // cancel() 会等待后台线程收敛；completion_callback 会触发 finishExecution，
    // 但 cancel 路径下手动收尾以确保即时响应。
    finishExecution();
}

void GraphViewModel::finishExecution()
{
    if (!executing_)
        return;

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

void GraphViewModel::rebuildDag()
{
    std::vector<graph_studio::GraphModel::NodeSpec> tasks;
    std::vector<std::pair<std::string, std::string>> edges;
    for (const auto& n : nodeList_) {
        graph_studio::GraphModel::NodeSpec spec;
        spec.id = n.id.toStdString();
        spec.type = n.type.toStdString();
        // 把 NodeData.params 写入 TaskConfig.params（按该 type 的声明类型转换）
        auto specs = queryParamSpecs(spec.type);
        std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
        for (const auto& s : specs) by_name[s.name] = &s;
        for (auto it = n.params.begin(); it != n.params.end(); ++it) {
            std::string k = it.key().toStdString();
            auto sit = by_name.find(k);
            if (sit != by_name.end()) {
                applyVariantToParams(it.key(), it.value(), *sit->second, spec.config.params);
            }
        }
        tasks.push_back(std::move(spec));
    }
    for (const auto& e : edgeList_) {
        edges.emplace_back(e.fromId.toStdString(), e.toId.toStdString());
    }
    model_.rebuild(tasks, edges);
}

void GraphViewModel::removeEdgesOfNode(const QString& taskId)
{
    for (int i = edgeList_.size() - 1; i >= 0; --i) {
        if (edgeList_[i].fromId == taskId || edgeList_[i].toId == taskId) {
            edgeList_.removeAt(i);
        }
    }
}

bool GraphViewModel::canReach(const QString& from, const QString& to) const
{
    // DFS: check if there's a path from 'from' to 'to' via existing edges
    if (from == to)
        return true;

    QSet<QString> visited;
    QStack<QString> stack;
    stack.push(from);

    while (!stack.isEmpty()) {
        QString cur = stack.pop();
        if (cur == to)
            return true;
        if (visited.contains(cur))
            continue;
        visited.insert(cur);

        for (const auto& e : edgeList_) {
            if (e.fromId == cur && !visited.contains(e.toId))
                stack.push(e.toId);
        }
    }
    return false;
}
