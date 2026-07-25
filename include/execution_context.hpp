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

    void log(LogLevel level, const std::string& msg) override;

    void declare_dependency(const TaskId& task_id) override;
    bool validate_dependencies() const override;
    std::vector<TaskId> dependencies() const override;

    void clear_result(const TaskId& task_id) override;
    void clear_all_results() override;

    const TaskParams& params() const override { return params_; }
    void set_params(const TaskParams& params) { params_ = params; }

private:
    mutable std::shared_mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::shared_mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;

    mutable std::shared_mutex dependencies_mutex_;
    std::vector<TaskId> dependencies_;

    TaskParams params_;
};

}
