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

void DAG::add_plugin_task(const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task type '" + task_type + "' to DAG");
    
    auto plugin_task = PluginRegistry::instance().create_task(task_type);
    if (!plugin_task) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    auto task = std::make_shared<Task>(
        plugin_task->id(),
        plugin_task->type(),
        [plugin_task](IExecutionContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

    add_task(task);
    TG_LOG_INFO("Added plugin task instance '" + task->id() + "' of type '" + task_type + "' to DAG");
}

void DAG::add_plugin_task(const std::string& task_id, const std::string& task_type) {
    TG_LOG_DEBUG("Adding plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");
    
    auto plugin_task = PluginRegistry::instance().create_task(task_type);
    if (!plugin_task) {
        TG_LOG_ERROR("Plugin task type '" + task_type + "' not found in registry");
        throw std::runtime_error("Plugin task type '" + task_type + "' not found in registry");
    }

    auto task = std::make_shared<Task>(
        task_id,
        plugin_task->type(),
        [plugin_task](IExecutionContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

    add_task(task_id, task);
    TG_LOG_INFO("Added plugin task instance '" + task_id + "' of type '" + task_type + "' to DAG");
}

void DAG::add_dependency(const TaskId& from, const TaskId& to) {
    if (!tasks_.contains(from)) {
        TG_LOG_ERROR("Cannot add dependency: task '" + from + "' does not exist");
        throw std::runtime_error("Task '" + from + "' does not exist");
    }
    if (!tasks_.contains(to)) {
        TG_LOG_ERROR("Cannot add dependency: task '" + to + "' does not exist");
        throw std::runtime_error("Task '" + to + "' does not exist");
    }

    if (adjacency_[from].contains(to)) {
        TG_LOG_DEBUG("Dependency from '" + from + "' to '" + to + "' already exists, skipping");
        return;
    }

    edges_.push_back({from, to});
    adjacency_[from].insert(to);
    reverse_adjacency_[to].insert(from);
    in_degree_[to]++;
    TG_LOG_DEBUG("Added dependency: " + from + " -> " + to);
}

void DAG::add_dependencies(const TaskId& from, const std::vector<TaskId>& tos) {
    TG_LOG_DEBUG("Adding multiple dependencies from '" + from + "'");
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
