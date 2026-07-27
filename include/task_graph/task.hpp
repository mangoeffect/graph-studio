#pragma once

#include <plugin_api.hpp>
#include <task_graph/task_context.hpp>
#include <functional>
#include <memory>

namespace task_graph {

using TaskFunction = std::function<TaskResult(TaskContext&)>;

// 通用 Task：通过 lambda 注入业务逻辑。
// 当 Task 用于包装 IPluginTask（如 DAG::add_plugin_task）时，可调用
// set_spec_delegate() 让 input_specs()/output_specs() 委托到底层 plugin，
// 这样构图期校验仍能拿到 plugin 声明的端口契约。
class Task : public IPluginTask {
public:
    Task(std::string id, TaskFunction func, TaskConfig config = {});
    Task(std::string id, std::string type, TaskFunction func, TaskConfig config = {});

    const std::string& type() const override { return type_; }
    const TaskFunction& func() const { return func_; }

    TaskResult execute(TaskContext& ctx) override;

    // 端口契约委托（用于 plugin 包装场景）
    std::vector<PortSpec> input_specs()  const override;
    std::vector<PortSpec> output_specs() const override;

    void set_spec_delegate(std::shared_ptr<IPluginTask> delegate) {
        spec_delegate_ = std::move(delegate);
    }

private:
    std::string type_;
    TaskFunction func_;
    std::shared_ptr<IPluginTask> spec_delegate_;  // 可选：plugin 包装时的 spec 来源
};

using TaskPtr = std::shared_ptr<Task>;

}
