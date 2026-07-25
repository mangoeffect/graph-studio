#include <plugin_api.hpp>

namespace task_graph {

PluginRegistry::PluginRegistry() = default;

PluginRegistry::~PluginRegistry() = default;

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::register_task(const std::string& task_id,
                                   std::function<PluginTaskPtr()> creator) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_[task_id] = std::move(creator);
}

void PluginRegistry::unregister_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_.erase(task_id);
}

bool PluginRegistry::has_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_creators_.find(task_id) != task_creators_.end();
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_id);
    if (it != task_creators_.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> PluginRegistry::available_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [id, _] : task_creators_) {
        result.push_back(id);
    }
    return result;
}

}
