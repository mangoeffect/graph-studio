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

    // 将插件任务封装为 Task：lambda 桥接 plugin_task->execute 与框架执行接口
    auto task = std::make_shared<Task>(
        plugin_task->id(),
        plugin_task->type(),
        [plugin_task](TaskContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

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

    auto task = std::make_shared<Task>(
        task_id,
        plugin_task->type(),
        [plugin_task](TaskContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

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

    auto task = std::make_shared<Task>(
        task_id,
        plugin_task->type(),
        [plugin_task](TaskContext& ctx) {
            return plugin_task->execute(ctx);
        },
        plugin_task->config()
    );

    add_task(task_id, task);
    TG_LOG_INFO("Added plugin task instance '" + task_id + "' of type '" + task_type + "' with config to DAG");
}

// 添加依赖边：from → to 表示 to 依赖 from（from 完成后 to 才可执行）
// 同步维护邻接表、逆邻接表和入度计数，供编译与调度阶段使用
void DAG::add_dependency(const TaskId& from, const TaskId& to) {
    if (!tasks_.contains(from)) {
        TG_LOG_ERROR("Cannot add dependency: task '" + from + "' does not exist");
        throw std::runtime_error("Task '" + from + "' does not exist");
    }
    if (!tasks_.contains(to)) {
        TG_LOG_ERROR("Cannot add dependency: task '" + to + "' does not exist");
        throw std::runtime_error("Task '" + to + "' does not exist");
    }

    // 去重：已存在的依赖边直接跳过，避免重复计数
    if (adjacency_[from].contains(to)) {
        TG_LOG_DEBUG("Dependency from '" + from + "' to '" + to + "' already exists, skipping");
        return;
    }

    edges_.push_back({from, to});
    adjacency_[from].insert(to);          // 正向邻接：from 的后继任务集合
    reverse_adjacency_[to].insert(from);  // 逆向邻接：to 的前驱任务集合
    in_degree_[to]++;                     // 入度 +1，调度时据此判断是否就绪
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

}
