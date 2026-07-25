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

class TaskWithUniqueId : public IPluginTask {
public:
    TaskWithUniqueId(PluginTaskPtr delegate, const std::string& unique_id)
        : delegate_(std::move(delegate)), instance_id_(unique_id) {}
    
    const std::string& id() const override { return instance_id_; }
    const std::string& type() const override { return delegate_->type(); }
    TaskResult execute(IExecutionContext& ctx) override { return delegate_->execute(ctx); }
    const TaskConfig& config() const override { return delegate_->config(); }
    CheckResult check_input(const std::vector<std::any>& inputs) const override {
        return delegate_->check_input(inputs);
    }
    
private:
    PluginTaskPtr delegate_;
    std::string instance_id_;
};

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
        auto task = it->second(unique_id, TaskConfig{});
        if (task) {
            if (task->id() != unique_id) {
                return std::make_shared<TaskWithUniqueId>(std::move(task), unique_id);
            }
            return task;
        }
        return task;
    }
    return nullptr;
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_type, const TaskConfig& config) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_type);
    if (it != task_creators_.end()) {
        std::string unique_id = generate_unique_id(task_type);
        auto task = it->second(unique_id, config);
        if (task) {
            if (task->id() != unique_id) {
                return std::make_shared<TaskWithUniqueId>(std::move(task), unique_id);
            }
            return task;
        }
        return task;
    }
    return nullptr;
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_type);
    if (it != task_creators_.end()) {
        auto task = it->second(task_id, config);
        if (task) {
            if (task->id() != task_id) {
                return std::make_shared<TaskWithUniqueId>(std::move(task), task_id);
            }
            return task;
        }
        return task;
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