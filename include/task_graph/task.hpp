#pragma once

#include <plugin_api.hpp>
#include <task_graph/task_context.hpp>
#include <functional>
#include <memory>

namespace task_graph {

using TaskFunction = std::function<TaskResult(TaskContext&)>;

// LambdaNode：通过 lambda 注入业务逻辑的节点。
// 替代旧 Task + spec_delegate_ 模式。如需声明端口/参数契约，
// 请直接子类化 INode。
class LambdaNode : public INode {
public:
    LambdaNode(std::string id, TaskFunction func, TaskConfig config = {});
    LambdaNode(std::string id, std::string type, TaskFunction func, TaskConfig config = {});

    const std::string& type() const override { return type_; }
    const TaskFunction& func() const { return func_; }

    // 通用 lambda 包装：无固定端口契约。如需声明端口，请直接子类化 INode
    // 或使用可配置 spec 的专用节点。
    std::vector<PortSpec> input_specs() const override;
    std::vector<PortSpec> output_specs() const override;

    TaskResult execute(TaskContext& ctx) override;

private:
    std::string type_;
    TaskFunction func_;
};

// 向后兼容别名
using Task = LambdaNode;
using TaskPtr = NodePtr;

}
