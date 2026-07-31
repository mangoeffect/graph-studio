#pragma once

#include <plugin_api.hpp>
#include <task_graph/task_context.hpp>
#include <string>
#include <vector>

namespace task_graph {

// GPU 图像 task 基类：统一处理 ensure_gpu -> compile -> dispatch -> 输出 Image(GPU)。
// 子类只需声明 type()、execute()（调 run_gpu_op）和 param_specs()。
class GpuImageTaskBase : public IPluginTask {
public:
    using IPluginTask::IPluginTask;

    std::vector<PortSpec> input_specs() const override;
    std::vector<PortSpec> output_specs() const override;

    void init() override;

protected:
    // 通用 GPU 图像处理流程：按 op_name 查找算子，dispatch compute，返回 GPU-resident Image。
    TaskResult run_gpu_op(TaskContext& ctx, const std::string& op_name);
};

// ---- 预置算子 task ----

class GpuBoxBlurTask : public GpuImageTaskBase {
public:
    using GpuImageTaskBase::GpuImageTaskBase;
    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
};

class GpuGaussianBlurTask : public GpuImageTaskBase {
public:
    using GpuImageTaskBase::GpuImageTaskBase;
    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
};

class GpuGrayscaleTask : public GpuImageTaskBase {
public:
    using GpuImageTaskBase::GpuImageTaskBase;
    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
};

class GpuBrightnessContrastTask : public GpuImageTaskBase {
public:
    using GpuImageTaskBase::GpuImageTaskBase;
    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
};

class GpuResizeTask : public GpuImageTaskBase {
public:
    using GpuImageTaskBase::GpuImageTaskBase;
    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
};

// 通用 compute task：构造时指定 op_name，运行时按 op 配置执行。
// 不通过 PluginRegistry 注册，供需要动态指定算子的场景使用。
class GpuComputeTask : public GpuImageTaskBase {
public:
    GpuComputeTask(const std::string& id, const std::string& op_name,
                   const TaskConfig& config = TaskConfig());

    const std::string& type() const override;
    TaskResult execute(TaskContext& ctx) override;
    std::vector<ParamSpec> param_specs() const override;
    void init() override;

private:
    std::string op_name_;
    std::string type_str_;
};

}  // namespace task_graph
