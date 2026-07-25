#pragma once

#include <plugin_api.hpp>
#include <functional>
#include <memory>

namespace task_graph {

using TaskFunction = std::function<TaskResult(IExecutionContext&)>;

class Task : public IPluginTask {
public:
    Task(std::string id, TaskFunction func, TaskConfig config = {});
    Task(std::string id, std::string type, TaskFunction func, TaskConfig config = {});

    const std::string& id() const override { return id_; }
    const std::string& type() const override { return type_; }
    const TaskFunction& func() const { return func_; }
    const TaskConfig& config() const override { return config_; }

    TaskResult execute(IExecutionContext& ctx) override;
    CheckResult check_input(const std::vector<std::any>& inputs) const override;

private:
    std::string id_;
    std::string type_;
    TaskFunction func_;
    TaskConfig config_;
};

using TaskPtr = std::shared_ptr<Task>;

}
