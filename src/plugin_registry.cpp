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

PluginRegistry::~PluginRegistry() {
    // Linux 退出期：libtask_graph.so 先于插件 .so 走 _dl_fini，插件的
    // __attribute__((destructor)) 注销回调会踩已析构的哈希表/互斥量
    // （CI 上 opencv 插件图测试退出 SegFault 的根因）。置标志让后续
    // 访问直接 no-op；macOS 的 dyld 析构顺序不同，不触发。
    destroyed_ = true;
}

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::register_task(const std::string& task_type,
                                   std::function<PluginTaskPtr(const std::string&, const TaskConfig&)> creator) {
    if (destroyed_) return;  // 退出期再注册：无意义且不安全
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_[task_type] = std::move(creator);
}

void PluginRegistry::unregister_task(const std::string& task_type) {
    if (destroyed_) return;  // 退出期卸载：容器即将随进程消失，跳过
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