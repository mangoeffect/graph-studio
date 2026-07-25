#pragma once

#include <plugin_api.hpp>
#include <task_graph/task_context.hpp>
#include <functional>
#include <memory>

namespace task_graph {

using TaskFunction = std::function<TaskResult(TaskContext&)>;

class Task : public IPluginTask {
public:
    Task(std::string id, TaskFunction func, TaskConfig config = {});
    Task(std::string id, std::string type, TaskFunction func, TaskConfig config = {});

    const std::string& type() const override { return type_; }
    const TaskFunction& func() const { return func_; }

    TaskResult execute(TaskContext& ctx) override;
    CheckResult check_input(const std::vector<std::any>& inputs) const override;

private:
    std::string type_;
    TaskFunction func_;
};

using TaskPtr = std::shared_ptr<Task>;

}
