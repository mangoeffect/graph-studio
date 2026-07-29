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

using namespace graph_studio;

// ---- ParamSpec 桥接：把 lib 侧 ParamSpec 拆成 QVariantMap（明确类型） ----
// 避免 std::any 跨 lib/GUI 边界；GUI 侧只消费 QVariant。
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
    // 默认值按类型取
    try {
        switch (s.type) {
            case task_graph::ParamType::Int:
            case task_graph::ParamType::Enum:
                m["default"] = std::any_cast<int>(s.default_value); break;
            case task_graph::ParamType::Float:
                m["default"] = std::any_cast<float>(s.default_value); break;
            case task_graph::ParamType::String:
                m["default"] = QString::fromStdString(std::any_cast<std::string>(s.default_value)); break;
            case task_graph::ParamType::Bool:
                m["default"] = std::any_cast<bool>(s.default_value); break;
        }
    } catch (const std::bad_any_cast&) { /* default 留空 */ }
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

// 把 TaskParams（any）转成 QVariantMap（明确类型），供 UI 读取
QVariantMap taskParamsToVariant(const task_graph::TaskParams& p,
                                const std::vector<task_graph::ParamSpec>& specs) {
    QVariantMap out;
    std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
    for (const auto& s : specs) by_name[s.name] = &s;
    for (const auto& [key, value] : p.params()) {
        QString k = QString::fromStdString(key);
        auto it = by_name.find(key);
        if (it != by_name.end()) {
            const auto& s = *it->second;
            try {
                switch (s.type) {
                    case task_graph::ParamType::Int:
                    case task_graph::ParamType::Enum:
                        out[k] = std::any_cast<int>(value); break;
                    case task_graph::ParamType::Float:
                        out[k] = std::any_cast<float>(value); break;
                    case task_graph::ParamType::String:
                        out[k] = QString::fromStdString(std::any_cast<std::string>(value)); break;
                    case task_graph::ParamType::Bool:
                        out[k] = std::any_cast<bool>(value); break;
                }
            } catch (const std::bad_any_cast&) { /* skip */ }
        } else {
            // 未声明：按运行时类型尽力转换
            if (value.type() == typeid(int))         out[k] = std::any_cast<int>(value);
            else if (value.type() == typeid(float))  out[k] = std::any_cast<float>(value);
            else if (value.type() == typeid(bool))   out[k] = std::any_cast<bool>(value);
            else if (value.type() == typeid(std::string))
                out[k] = QString::fromStdString(std::any_cast<std::string>(value));
        }
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
    rebuildDag();

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
            rebuildDag();
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
    // Build a combined JSON: positions + DAG structure
    QString json = QString::fromStdString(model_.to_json_string());
    if (json.isEmpty()) {
        emit logMessage("[ERROR] Failed to serialize DAG");
        return false;
    }

    // Inject node positions into JSON for round-trip
    // We use nlohmann json via model, append a "positions" field
    // Simple approach: append positions as a separate section
    QString positions;
    for (int i = 0; i < nodeList_.size(); ++i) {
        const auto& n = nodeList_[i];
        if (i > 0) positions += ",";
        positions += QString("\"%1\":{\"x\":%2,\"y\":%3,\"type\":\"%4\"}")
                         .arg(n.id).arg(n.x).arg(n.y).arg(n.type);
    }

    // Insert positions before closing brace
    json.chop(1); // remove trailing }
    if (!json.endsWith("{"))
        json += ",";
    json += "\"positions\":{" + positions + "}}";

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage("[ERROR] Cannot open file for writing: " + filePath);
        return false;
    }
    QTextStream stream(&file);
    stream << json;
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

    if (!model_.from_json_string(json.toStdString())) {
        emit logMessage("[ERROR] Failed to parse DAG JSON");
        return false;
    }

    // Rebuild ViewModel lists from the new DAG
    nodeList_.clear();
    edgeList_.clear();
    selectedNodeId_.clear();

    const auto& dag = model_.dag();
    for (const auto& [id, taskPtr] : dag.tasks()) {
        NodeData nd;
        nd.id = QString::fromStdString(id);
        nd.type = QString::fromStdString(taskPtr->type());
        // 从 DAG task config 恢复 params（按声明类型桥接为 QVariantMap）
        nd.params = taskParamsToVariant(taskPtr->config().params,
                                        queryParamSpecs(taskPtr->type()));
        nodeList_.append(nd);
    }
    for (const auto& [from, targets] : dag.adjacency()) {
        for (const auto& to : targets) {
            EdgeData ed;
            ed.fromId = QString::fromStdString(from);
            ed.toId = QString::fromStdString(to);
            edgeList_.append(ed);
        }
    }

    // TODO: parse positions from JSON (stored as custom field)
    // For now, auto-layout loaded graph
    emit graphReset();
    emit taskCountChanged();
    emit edgeCountChanged();
    emit selectionChanged({});
    emit logMessage("[INFO] Graph loaded from: " + filePath);

    autoLayout();
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

    // 保证 DAG 与当前 UI 状态一致（参数编辑已即时同步，此处重建保险起见）。
    rebuildDag();

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

    // std::shared_future 无 Qt 信号，用 QTimer 轮询完成。WASM 下 execute() 已同步
    // 返回，首次触发即完成。
    if (!completionTimer_) {
        completionTimer_ = new QTimer(this);
        completionTimer_->setInterval(50);
        connect(completionTimer_, &QTimer::timeout, this, [this]() {
            if (executor_ && !executor_->is_running()) {
                completionTimer_->stop();
                finishExecution();
            }
        });
    }
    completionTimer_->start();
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
    executor_ = std::make_unique<task_graph::DAGExecutor>(config);
}

void GraphViewModel::stop()
{
    if (!executing_ || !executor_)
        return;
    emit logMessage("[WARN] Cancelling execution...");
    executor_->cancel();
    // cancel() 会等待后台线程收敛；随后收尾。
    if (completionTimer_)
        completionTimer_->stop();
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
