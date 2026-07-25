#include <task_graph/task_manager.hpp>

namespace task_graph {

void TaskManager::add_task(PluginTaskPtr task) {
    if (!task) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[task->id()] = std::move(task);
}

void TaskManager::add_task(const std::string& id, PluginTaskPtr task) {
    if (!task) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[id] = std::move(task);
}

PluginTaskPtr TaskManager::get_task(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        return it->second;
    }
    return nullptr;
}

bool TaskManager::has_task(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.find(id) != tasks_.end();
}

void TaskManager::remove_task(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(id);
}

void TaskManager::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
}

std::size_t TaskManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::vector<std::string> TaskManager::task_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(tasks_.size());
    for (const auto& [id, _] : tasks_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<PluginTaskPtr> TaskManager::all_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginTaskPtr> tasks;
    tasks.reserve(tasks_.size());
    for (const auto& [_, task] : tasks_) {
        tasks.push_back(task);
    }
    return tasks;
}

}
