#include <plugin_api.hpp>
#include <atomic>
#include <sstream>

namespace task_graph {

namespace {

std::atomic<uint64_t> task_id_counter{0};

std::string generate_unique_id(const std::string& task_type) {
    std::stringstream ss;
    ss << task_type << "_" << task_id_counter.fetch_add(1);
    return ss.str();
}

}

PluginRegistry::PluginRegistry() = default;

PluginRegistry::~PluginRegistry() = default;

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::register_task(const std::string& task_type,
                                   std::function<PluginTaskPtr(const std::string&, const TaskConfig&)> creator) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_[task_type] = std::move(creator);
}

void PluginRegistry::unregister_task(const std::string& task_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_.erase(task_type);
}

bool PluginRegistry::has_task(const std::string& task_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_creators_.find(task_type) != task_creators_.end();
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_type);
    if (it != task_creators_.end()) {
        std::string unique_id = generate_unique_id(task_type);
        return it->second(unique_id, TaskConfig{});
    }
    return nullptr;
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_type, const TaskConfig& config) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_type);
    if (it != task_creators_.end()) {
        std::string unique_id = generate_unique_id(task_type);
        return it->second(unique_id, config);
    }
    return nullptr;
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_type);
    if (it != task_creators_.end()) {
        return it->second(task_id, config);
    }
    return nullptr;
}

std::vector<std::string> PluginRegistry::available_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [type, _] : task_creators_) {
        result.push_back(type);
    }
    return result;
}

}