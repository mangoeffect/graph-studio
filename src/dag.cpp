#include <task_graph/dag.hpp>
#include <plugin_api.hpp>
#include <stdexcept>

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

}
