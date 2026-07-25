#include <task_graph/dag.hpp>
#include <stdexcept>

namespace task_graph {

void DAG::add_task(TaskPtr task) {
    if (!task) {
        throw std::invalid_argument("Task cannot be null");
    }

    const std::string& id = task->id();
    if (tasks_.contains(id)) {
        throw std::runtime_error("Task with id '" + id + "' already exists");
    }

    tasks_[id] = std::move(task);
    in_degree_[id] = 0;
}

void DAG::add_plugin_task(const std::string& task_id) {
    auto plugin_task = PluginRegistry::instance().create_task(task_id);
    if (!plugin_task) {
        throw std::runtime_error("Plugin task '" + task_id + "' not found in registry");
    }

    auto task = std::make_shared<Task>(
        plugin_task->id(),
        [plugin_task](ExecutionContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

    add_task(task);
}

void DAG::add_dependency(const TaskId& from, const TaskId& to) {
    if (!tasks_.contains(from)) {
        throw std::runtime_error("Task '" + from + "' does not exist");
    }
    if (!tasks_.contains(to)) {
        throw std::runtime_error("Task '" + to + "' does not exist");
    }

    if (adjacency_[from].contains(to)) {
        return;
    }

    edges_.push_back({from, to});
    adjacency_[from].insert(to);
    reverse_adjacency_[to].insert(from);
    in_degree_[to]++;
}

void DAG::add_dependencies(const TaskId& from, const std::vector<TaskId>& tos) {
    for (const auto& to : tos) {
        add_dependency(from, to);
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

}
