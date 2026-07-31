#include <task_graph/dag.hpp>
#include <plugin_api.hpp>
#include <stdexcept>
#include <algorithm>

namespace task_graph {

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

    tasks_[id] = std::move(task);
    in_degree_[id] = 0;
    TG_LOG_DEBUG("Added task '" + id + "' to DAG");
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

    tasks_[id] = std::move(task);
    in_degree_[id] = 0;
    TG_LOG_DEBUG("Added task '" + id + "' to DAG");
}

// 添加插件任务（自动生成唯一 ID）
// 通过 PluginRegistry 按 task_type 创建实例，并包装为框架内部 Task 加入 DAG
void DAG::add_plugin_task(const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task type '" + task_type + "' to DAG");

    auto plugin_task = PluginRegistry::instance().create_task(task_type);
    if (!plugin_task) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    // 包装为 Task，但保留 plugin_task 的 specs（通过共享所有权传递给 Task 的 spec 委托）
    auto plugin_ptr = std::shared_ptr<IPluginTask>(plugin_task);
    auto task = std::make_shared<Task>(
        plugin_task->id(),
        plugin_task->type(),
        [plugin_ptr](TaskContext& ctx) {
            return plugin_ptr->execute(ctx);
        },
        plugin_task->config()
    );
    task->set_spec_delegate(plugin_ptr);

    add_task(task);
    TG_LOG_INFO("Added plugin task instance '" + task->id() + "' of type '" + task_type + "' to DAG");
}

void DAG::add_plugin_task(const std::string& task_id, const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");

    auto plugin_task = PluginRegistry::instance().create_task(task_id, task_type, TaskConfig{});
    if (!plugin_task) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    auto plugin_ptr = std::shared_ptr<IPluginTask>(plugin_task);
    auto task = std::make_shared<Task>(
        task_id,
        plugin_task->type(),
        [plugin_ptr](TaskContext& ctx) {
            return plugin_ptr->execute(ctx);
        },
        plugin_task->config()
    );
    task->set_spec_delegate(plugin_ptr);

    add_task(task_id, task);
    TG_LOG_INFO("Added plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");
}

void DAG::add_plugin_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) {
    TG_LOG_DEBUG("Adding plugin task instance '" + task_id + "' of type '" + task_type + "' with config to DAG");

    auto plugin_task = PluginRegistry::instance().create_task(task_id, task_type, config);
    if (!plugin_task) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    auto plugin_ptr = std::shared_ptr<IPluginTask>(plugin_task);
    auto task = std::make_shared<Task>(
        task_id,
        plugin_task->type(),
        [plugin_ptr](TaskContext& ctx) {
            return plugin_ptr->execute(ctx);
        },
        plugin_task->config()
    );
    task->set_spec_delegate(plugin_ptr);

    add_task(task_id, task);
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

    const std::string& type = it->second->type();
    if (!type.empty() && type != id &&
        PluginRegistry::instance().has_task(type)) {
        // plugin task：重建 IPluginTask + Task 包装，保证 config 与 spec delegate 一致
        auto plugin_task = PluginRegistry::instance().create_task(id, type, config);
        if (!plugin_task) {
            TG_LOG_ERROR("Failed to recreate plugin task '" + type + "' for update");
            throw std::runtime_error("Failed to recreate plugin task '" + type + "'");
        }
        auto plugin_ptr = std::shared_ptr<IPluginTask>(plugin_task);
        auto wrapper = std::make_shared<Task>(
            id, type,
            [plugin_ptr](TaskContext& ctx) { return plugin_ptr->execute(ctx); },
            config);
        wrapper->set_spec_delegate(plugin_ptr);
        tasks_[id] = wrapper;
        TG_LOG_DEBUG("Updated plugin task '" + id + "' config (recreated instance)");
    } else {
        // 普通 lambda Task：直接更新 config_（Task::set_config 同步 spec_delegate_）
        it->second->set_config(config);
        TG_LOG_DEBUG("Updated task '" + id + "' config (in-place)");
    }
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
    if (!tasks_.contains(from) || !tasks_.contains(to)) {
        TG_LOG_WARN("Cannot remove edge: task '" + from + "' or '" + to + "' does not exist");
        return;
    }

    // 收集需要删除的边在 edges_ 中的下标
    std::vector<size_t> to_remove;
    auto in_it = incoming_idx_.find(to);
    if (in_it != incoming_idx_.end()) {
        for (size_t idx : in_it->second) {
            if (edges_[idx].from == from) {
                to_remove.push_back(idx);
            }
        }
    }
    if (to_remove.empty()) {
        TG_LOG_DEBUG("Edge '" + from + "' -> '" + to + "' not found, nothing to remove");
        return;
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
}

void DAG::remove_task(const TaskId& id) {
    if (!tasks_.contains(id)) {
        TG_LOG_WARN("Cannot remove task: '" + id + "' does not exist");
        return;
    }

    // 先移除所有关联边（outgoing + incoming）
    auto out_edges = outgoing_edges(id);
    auto in_edges = incoming_edges(id);
    for (const auto& e : out_edges) {
        remove_edge(e.from, e.to);
    }
    for (const auto& e : in_edges) {
        remove_edge(e.from, e.to);
    }

    // 再删除 task 本身
    tasks_.erase(id);
    in_degree_.erase(id);
    adjacency_.erase(id);
    reverse_adjacency_.erase(id);
    incoming_idx_.erase(id);
    outgoing_idx_.erase(id);

    TG_LOG_INFO("Removed task '" + id + "' from DAG");
}

// ====================== 值类型查询 ======================
bool DAG::has_edge(const TaskId& from, const TaskId& to) const {
    auto it = adjacency_.find(from);
    if (it == adjacency_.end()) return false;
    return it->second.contains(to);
}

std::vector<TaskId> DAG::task_ids() const {
    std::vector<TaskId> ids;
    ids.reserve(tasks_.size());
    for (const auto& [id, _] : tasks_) {
        ids.push_back(id);
    }
    return ids;
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

}
