#include <task_graph/dag.hpp>
#include <plugin_api.hpp>
#include <stdexcept>
#include <algorithm>

namespace task_graph {

// 端口规范化：当给定端口名未在 task 声明的端口列表中，而 task 恰好只有
// 一个输入/输出端口时，把未声明名改写为真实端口名。用于兼容旧数据/便捷调用
// 中伪造的默认端口名（如 mp_face_detector 的真实输入端口是 "image"，
// 而旧版编辑器写成了 "in"）。声明了多个端口的 task 不做改写（保持原样，
// 交由 DAGCompiler::validate() 提示未声明端口 warning）。
static std::string canonical_single_port(const std::vector<PortSpec>& specs,
                                         const std::string& port) {
    if (specs.size() != 1) return port;
    if (port.empty() || specs[0].name == port) return port;
    return specs[0].name;
}

void DAG::add_task(TaskPtr task) {
    if (!task) {
        TG_LOG_ERROR("Cannot add null task to DAG");
        throw std::invalid_argument("Task cannot be null");
    }

    const std::string& id = task->id();
    if (tasks_.contains(id)) {
        TG_LOG_ERROR("Task with id '" + id + "' already exists in DAG");
        throw std::runtime_error("Task with id '" + id + "' already exists");
    }

    std::string type = task->type();
    tasks_[id] = std::move(task);
    in_degree_[id] = 0;
    insertion_order_.push_back(id);
    TG_LOG_DEBUG("Added task '" + id + "' to DAG");
    notify({DAGChangeEvent::Type::TaskAdded, id, std::move(type)});
}

void DAG::add_task(const std::string& id, TaskPtr task) {
    if (!task) {
        TG_LOG_ERROR("Cannot add null task to DAG");
        throw std::invalid_argument("Task cannot be null");
    }

    if (tasks_.contains(id)) {
        TG_LOG_ERROR("Task with id '" + id + "' already exists in DAG");
        throw std::runtime_error("Task with id '" + id + "' already exists");
    }

    std::string type = task->type();
    tasks_[id] = std::move(task);
    in_degree_[id] = 0;
    insertion_order_.push_back(id);
    TG_LOG_DEBUG("Added task '" + id + "' to DAG");
    notify({DAGChangeEvent::Type::TaskAdded, id, std::move(type)});
}

// 添加插件任务（自动生成唯一 ID）
// 通过 PluginRegistry 按 task_type 创建 INode 实例，直接加入 DAG（无包装器）
void DAG::add_plugin_task(const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task type '" + task_type + "' to DAG");

    auto node = PluginRegistry::instance().create_task(task_type);
    if (!node) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    add_task(node);
    TG_LOG_INFO("Added plugin task instance '" + node->id() + "' of type '" + task_type + "' to DAG");
}

void DAG::add_plugin_task(const std::string& task_id, const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");

    auto node = PluginRegistry::instance().create_task(task_id, task_type, TaskConfig{});
    if (!node) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    add_task(task_id, node);
    TG_LOG_INFO("Added plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");
}

void DAG::add_plugin_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) {
    TG_LOG_DEBUG("Adding plugin task instance '" + task_id + "' of type '" + task_type + "' with config to DAG");

    auto node = PluginRegistry::instance().create_task(task_id, task_type, config);
    if (!node) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    add_task(task_id, node);
    TG_LOG_INFO("Added plugin task instance '" + task_id + "' of type '" + task_type + "' with config to DAG");
}

// 端口化连接：维护 edges_、端口索引、task 级去重邻接表、入度。
// 同一 (from, from_port, to, to_port) 四元组重复连接视为幂等（跳过）。
// 同一 (to, to_port) 已被任何边占用时拒绝（一个输入端口只能有一个数据源）。
void DAG::connect(const TaskId& from, std::string from_port,
                  const TaskId& to,   std::string to_port) {
    if (!tasks_.contains(from)) {
        TG_LOG_ERROR("Cannot connect: task '" + from + "' does not exist");
        throw std::runtime_error("Task '" + from + "' does not exist");
    }
    if (!tasks_.contains(to)) {
        TG_LOG_ERROR("Cannot connect: task '" + to + "' does not exist");
        throw std::runtime_error("Task '" + to + "' does not exist");
    }

    // 端口规范化：把 legacy/便捷默认端口名改写为 task 声明的唯一端口
    // （仅当端口名未声明且 task 恰好一个同名端口时）。这样保存/加载的图
    // 即便带着伪造的 "in"/"out"，运行时也能正确绑定到 image 等真实端口。
    {
        const auto from_task = tasks_.at(from);
        const auto to_task = tasks_.at(to);
        from_port = canonical_single_port(from_task->output_specs(), from_port);
        to_port   = canonical_single_port(to_task->input_specs(), to_port);
    }

    // 端口级幂等：完全相同的四元组直接跳过
    for (size_t idx : incoming_idx_[to]) {
        const Edge& e = edges_[idx];
        if (e.from == from && e.from_port == from_port &&
            e.to == to && e.to_port == to_port) {
            TG_LOG_DEBUG("Edge " + from + ":" + from_port + " -> " +
                         to + ":" + to_port + " already exists, skipping");
            return;
        }
    }

    // 同 to_port 冲突：log warning。严格校验留给 compiler.validate()（commit 3）。
    // 菱形依赖（A→C, B→C 默认端口 "in"）是合法的调度结构，C 可以不读 input。
    for (size_t idx : incoming_idx_[to]) {
        const Edge& e = edges_[idx];
        if (e.to_port == to_port) {
            TG_LOG_WARN("Input port '" + to_port + "' of task '" + to +
                        "' already connected by '" + e.from + ":" + e.from_port +
                        "'; overwriting with '" + from + ":" + from_port + "'");
            break;
        }
    }

    size_t new_idx = edges_.size();
    edges_.push_back(Edge{from, std::move(from_port), to, std::move(to_port)});

    // task 级去重邻接：仅在新连接引入新 task 依赖关系时更新
    bool new_task_edge = !adjacency_[from].contains(to);
    if (new_task_edge) {
        adjacency_[from].insert(to);
        reverse_adjacency_[to].insert(from);
        in_degree_[to]++;
    }

    incoming_idx_[to].push_back(new_idx);
    outgoing_idx_[from].push_back(new_idx);

    TG_LOG_DEBUG("Connected " + from + ":" + edges_[new_idx].from_port +
                 " -> " + to + ":" + edges_[new_idx].to_port);
    notify({DAGChangeEvent::Type::EdgeAdded, {}, {}, from, to,
            edges_[new_idx].from_port, edges_[new_idx].to_port});
}

void DAG::connect(const TaskId& from, const TaskId& to) {
    connect(from, "out", to, "in");
}

void DAG::add_dependencies(const TaskId& from, const std::vector<TaskId>& tos) {
    TG_LOG_DEBUG("Adding multiple dependencies from '" + from + "'");
    for (const auto& to : tos) {
        connect(from, to);
    }
}

bool DAG::has_task(const TaskId& id) const {
    return tasks_.contains(id);
}

TaskPtr DAG::get_task(const TaskId& id) const {
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        return it->second;
    }
    return nullptr;
}

void DAG::replace_task(const std::string& id, TaskPtr task) {
    if (!task) {
        TG_LOG_ERROR("Cannot replace with null task");
        throw std::invalid_argument("Task cannot be null");
    }

    if (!tasks_.contains(id)) {
        TG_LOG_ERROR("Cannot replace task: '" + id + "' does not exist");
        throw std::runtime_error("Task '" + id + "' does not exist");
    }

    tasks_[id] = std::move(task);
    TG_LOG_DEBUG("Replaced task '" + id + "'");
}

std::vector<Edge> DAG::incoming_edges(const TaskId& tid) const {
    std::vector<Edge> result;
    auto it = incoming_idx_.find(tid);
    if (it == incoming_idx_.end()) {
        return result;
    }
    result.reserve(it->second.size());
    for (size_t idx : it->second) {
        result.push_back(edges_[idx]);
    }
    return result;
}

std::vector<Edge> DAG::outgoing_edges(const TaskId& tid) const {
    std::vector<Edge> result;
    auto it = outgoing_idx_.find(tid);
    if (it == outgoing_idx_.end()) {
        return result;
    }
    result.reserve(it->second.size());
    for (size_t idx : it->second) {
        result.push_back(edges_[idx]);
    }
    return result;
}

// ====================== 就地更新 config ======================
void DAG::update_task_config(const TaskId& id, const TaskConfig& config) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        TG_LOG_ERROR("Cannot update task: '" + id + "' does not exist");
        throw std::runtime_error("Task '" + id + "' does not exist");
    }
    // 直接调用 INode::set_config()，无需重建实例
    it->second->set_config(config);
    TG_LOG_DEBUG("Updated task '" + id + "' config");
    notify({DAGChangeEvent::Type::TaskUpdated, id});
}

void DAG::update_task_params(const TaskId& id, const TaskParams& params) {
    auto task = get_task(id);
    if (!task) {
        TG_LOG_ERROR("Cannot update params: task '" + id + "' does not exist");
        throw std::runtime_error("Task '" + id + "' does not exist");
    }
    TaskConfig new_config = task->config();
    new_config.params = params;
    update_task_config(id, new_config);
}

// ====================== 增量删除 ======================
void DAG::remove_edge(const TaskId& from, const TaskId& to) {
    // 收集该 pair 下被移除的全部边端口（在 impl 删除前快照），
    // 逐条发送带端口的 EdgeRemoved 事件，保证 UI 的端口级 key 能匹配。
    std::vector<std::pair<std::string, std::string>> removed_ports;
    auto in_it = incoming_idx_.find(to);
    if (in_it != incoming_idx_.end()) {
        for (size_t idx : in_it->second) {
            const Edge& e = edges_[idx];
            if (e.from == from) {
                removed_ports.emplace_back(e.from_port, e.to_port);
            }
        }
    }
    if (!remove_edge_impl(from, to, nullptr, nullptr)) {
        return;
    }
    for (const auto& [from_port, to_port] : removed_ports) {
        notify({DAGChangeEvent::Type::EdgeRemoved, {}, {}, from, to, from_port, to_port});
    }
}

void DAG::remove_edge(const TaskId& from, const std::string& from_port,
                      const TaskId& to,   const std::string& to_port) {
    bool removed = remove_edge_impl(from, to, &from_port, &to_port);
    if (removed) {
        notify({DAGChangeEvent::Type::EdgeRemoved, {}, {}, from, to, from_port, to_port});
    }
}

// 删除 from->to 中匹配端口过滤条件的边；filter 为 null 时删除该 pair 下全部边。
// 返回是否至少删除了一条。
bool DAG::remove_edge_impl(const TaskId& from, const TaskId& to,
                           const std::string* from_port_filter,
                           const std::string* to_port_filter) {
    if (!tasks_.contains(from) || !tasks_.contains(to)) {
        TG_LOG_WARN("Cannot remove edge: task '" + from + "' or '" + to + "' does not exist");
        return false;
    }

    // 收集需要删除的边在 edges_ 中的下标
    std::vector<size_t> to_remove;
    auto in_it = incoming_idx_.find(to);
    if (in_it != incoming_idx_.end()) {
        for (size_t idx : in_it->second) {
            const Edge& e = edges_[idx];
            if (e.from != from) continue;
            if (to_port_filter && e.to_port != *to_port_filter) continue;
            if (from_port_filter && e.from_port != *from_port_filter) continue;
            to_remove.push_back(idx);
        }
    }
    if (to_remove.empty()) {
        TG_LOG_DEBUG("Edge '" + from + "' -> '" + to + "' not found, nothing to remove");
        return false;
    }

    // 检查移除后是否还有 from->to 的边（端口级可能有多条）
    bool has_remaining = false;
    auto out_it = outgoing_idx_.find(from);
    if (out_it != outgoing_idx_.end()) {
        for (size_t idx : out_it->second) {
            if (edges_[idx].from == from && edges_[idx].to == to &&
                std::find(to_remove.begin(), to_remove.end(), idx) == to_remove.end()) {
                has_remaining = true;
                break;
            }
        }
    }

    // 更新 task 级邻接：仅当没有剩余边时才移除
    if (!has_remaining) {
        adjacency_[from].erase(to);
        reverse_adjacency_[to].erase(from);
        if (adjacency_[from].empty()) adjacency_.erase(from);
        if (reverse_adjacency_[to].empty()) reverse_adjacency_.erase(to);
        // in_degree 仅按 task 级去重计算，减 1
        if (in_degree_.contains(to) && in_degree_[to] > 0) {
            in_degree_[to]--;
        }
    }

    // 从 edges_ 中删除（swap-remove 会导致下标位移，重建索引更安全）
    std::sort(to_remove.rbegin(), to_remove.rend());
    for (size_t idx : to_remove) {
        edges_.erase(edges_.begin() + static_cast<ptrdiff_t>(idx));
    }

    // 重建 incoming_idx_ / outgoing_idx_（下标已位移）
    incoming_idx_.clear();
    outgoing_idx_.clear();
    for (size_t i = 0; i < edges_.size(); ++i) {
        incoming_idx_[edges_[i].to].push_back(i);
        outgoing_idx_[edges_[i].from].push_back(i);
    }

    TG_LOG_DEBUG("Removed " + std::to_string(to_remove.size()) +
                 " edge(s) from '" + from + "' to '" + to + "'");
    return true;
}

void DAG::remove_task(const TaskId& id) {
    if (!tasks_.contains(id)) {
        TG_LOG_WARN("Cannot remove task: '" + id + "' does not exist");
        return;
    }

    // 先移除所有关联边（outgoing + incoming）—— 用 impl 避免逐条发事件
    auto out_edges = outgoing_edges(id);
    auto in_edges = incoming_edges(id);
    for (const auto& e : out_edges) {
        remove_edge_impl(e.from, e.to);
    }
    for (const auto& e : in_edges) {
        remove_edge_impl(e.from, e.to);
    }

    // 再删除 task 本身
    tasks_.erase(id);
    in_degree_.erase(id);
    adjacency_.erase(id);
    reverse_adjacency_.erase(id);
    incoming_idx_.erase(id);
    outgoing_idx_.erase(id);
    insertion_order_.erase(
        std::remove(insertion_order_.begin(), insertion_order_.end(), id),
        insertion_order_.end());

    TG_LOG_INFO("Removed task '" + id + "' from DAG");
    notify({DAGChangeEvent::Type::TaskRemoved, id});
}

// ====================== 值类型查询 ======================
bool DAG::has_edge(const TaskId& from, const TaskId& to) const {
    auto it = adjacency_.find(from);
    if (it == adjacency_.end()) return false;
    return it->second.contains(to);
}

bool DAG::has_edge(const TaskId& from, const std::string& from_port,
                   const TaskId& to, const std::string& to_port) const {
    if (!tasks_.contains(from) || !tasks_.contains(to)) return false;
    auto it = incoming_idx_.find(to);
    if (it == incoming_idx_.end()) return false;
    for (size_t idx : it->second) {
        const Edge& e = edges_[idx];
        if (e.from == from && e.from_port == from_port && e.to_port == to_port) return true;
    }
    return false;
}

std::vector<TaskId> DAG::task_ids() const {
    return insertion_order_;
}

std::optional<TaskConfig> DAG::task_config(const TaskId& id) const {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return std::nullopt;
    return it->second->config();
}

std::string DAG::task_type(const TaskId& id) const {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return {};
    return it->second->type();
}

std::vector<DAG::EdgeRef> DAG::edge_list() const {
    std::vector<EdgeRef> result;
    result.reserve(adjacency_.size());
    for (const auto& [from, tos] : adjacency_) {
        for (const auto& to : tos) {
            result.push_back({from, to});
        }
    }
    return result;
}

// ====================== 清空 ======================
void DAG::clear() {
    tasks_.clear();
    edges_.clear();
    adjacency_.clear();
    reverse_adjacency_.clear();
    in_degree_.clear();
    incoming_idx_.clear();
    outgoing_idx_.clear();
    insertion_order_.clear();
    TG_LOG_INFO("DAG cleared");
    notify({DAGChangeEvent::Type::GraphReset});
}

void DAG::reset_from(DAG&& other) {
    tasks_ = std::move(other.tasks_);
    edges_ = std::move(other.edges_);
    adjacency_ = std::move(other.adjacency_);
    reverse_adjacency_ = std::move(other.reverse_adjacency_);
    in_degree_ = std::move(other.in_degree_);
    incoming_idx_ = std::move(other.incoming_idx_);
    outgoing_idx_ = std::move(other.outgoing_idx_);
    insertion_order_ = std::move(other.insertion_order_);
    TG_LOG_INFO("DAG reset from another DAG");
    notify({DAGChangeEvent::Type::GraphReset});
}

// ====================== 变更订阅 ======================

DAG& DAG::operator=(DAG&& other) noexcept {
    tasks_ = std::move(other.tasks_);
    edges_ = std::move(other.edges_);
    adjacency_ = std::move(other.adjacency_);
    reverse_adjacency_ = std::move(other.reverse_adjacency_);
    in_degree_ = std::move(other.in_degree_);
    incoming_idx_ = std::move(other.incoming_idx_);
    outgoing_idx_ = std::move(other.outgoing_idx_);
    insertion_order_ = std::move(other.insertion_order_);
    // observers_ / observers_mutex_ 保持默认初始化（观察者属于原实例，不随数据移动）
    return *this;
}

size_t DAG::subscribe(ChangeCallback cb) const {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    size_t id = next_observer_id_++;
    observers_.emplace_back(id, std::move(cb));
    return id;
}

void DAG::unsubscribe(size_t id) const {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
                       [id](const auto& p) { return p.first == id; }),
        observers_.end());
}

void DAG::notify(const DAGChangeEvent& e) const {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    for (const auto& [_, cb] : observers_) {
        if (cb) cb(e);
    }
}

}
