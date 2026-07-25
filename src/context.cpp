#include <execution_context.hpp>
#include <plugin_api.hpp>

namespace task_graph {

void ExecutionContext::log(LogLevel level, const std::string& msg) {
    tg_log(level, msg.c_str());
}

void ExecutionContext::set_result(const TaskId& task_id, const TaskResult& result) {
    std::unique_lock lock(results_mutex_);
    results_[task_id] = result;
}

std::optional<TaskResult> ExecutionContext::get_result(const TaskId& task_id) const {
    std::shared_lock lock(results_mutex_);
    auto it = results_.find(task_id);
    if (it != results_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ExecutionContext::set_value(const std::string& key, std::any value) {
    std::unique_lock lock(values_mutex_);
    values_[key] = std::move(value);
}

std::optional<std::any> ExecutionContext::get_value(const std::string& key) const {
    std::shared_lock lock(values_mutex_);
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    return std::nullopt;
}

}
