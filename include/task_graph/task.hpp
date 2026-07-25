#pragma once

#include <plugin_api.hpp>
#include <functional>
#include <memory>

namespace task_graph {

using TaskFunction = std::function<TaskResult(ExecutionContext&)>;

class Task {
public:
    Task(std::string id, TaskFunction func, TaskConfig config = {});

    const std::string& id() const { return id_; }
    const TaskFunction& func() const { return func_; }
    const TaskConfig& config() const { return config_; }

    TaskResult execute(ExecutionContext& ctx);

private:
    std::string id_;
    TaskFunction func_;
    TaskConfig config_;
};

using TaskPtr = std::shared_ptr<Task>;

}
