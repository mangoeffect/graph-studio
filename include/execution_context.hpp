#pragma once

#include <plugin_api.hpp>
#include <mutex>
#include <shared_mutex>

namespace task_graph {

class ExecutionContext : public IExecutionContext {
public:
    ExecutionContext() = default;
    ~ExecutionContext() = default;

    void set_result(const TaskId& task_id, const TaskResult& result) override;
    std::optional<TaskResult> get_result(const TaskId& task_id) const override;

    void set_value(const std::string& key, std::any value) override;
    std::optional<std::any> get_value(const std::string& key) const override;

private:
    mutable std::shared_mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::shared_mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;
};

}
