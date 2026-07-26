#include "viewmodel/GraphViewModel.h"

#include <QFile>
#include <QTextStream>
#include <QSet>
#include <QQueue>
#include <QStack>
#include <algorithm>

using namespace graph_studio;

GraphViewModel::GraphViewModel(GraphModel& model, QObject* parent)
    : QObject(parent), model_(model)
{
}

GraphViewModel::~GraphViewModel() = default;

int GraphViewModel::taskCount() const { return nodeList_.size(); }
int GraphViewModel::edgeCount() const { return edgeList_.size(); }
QString GraphViewModel::selectedNodeId() const { return selectedNodeId_; }

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

void GraphViewModel::rebuildDag()
{
    std::vector<std::pair<std::string, std::string>> tasks;
    std::vector<std::pair<std::string, std::string>> edges;
    for (const auto& n : nodeList_) {
        tasks.emplace_back(n.id.toStdString(), n.type.toStdString());
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
