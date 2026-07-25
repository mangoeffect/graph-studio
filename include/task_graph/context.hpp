#pragma once

#include <task_graph/task.hpp>
#include <unordered_map>
#include <string>
#include <any>
#include <mutex>
#include <shared_mutex>

namespace task_graph {

class ExecutionContext {
public:
    ExecutionContext() = default;

    void set_result(const TaskId& task_id, const TaskResult& result);
    std::optional<TaskResult> get_result(const TaskId& task_id) const;

    void set_value(const std::string& key, std::any value);
    std::optional<std::any> get_value(const std::string& key) const;

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto opt = get_value(key);
        if (!opt) {
            return std::nullopt;
        }
        try {
            return std::any_cast<T>(*opt);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

private:
    mutable std::shared_mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::shared_mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;
};

using ExecutionContextPtr = std::shared_ptr<ExecutionContext>;

}
