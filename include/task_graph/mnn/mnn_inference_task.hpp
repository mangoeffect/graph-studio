#pragma once

// MNN（Alibaba）推理任务：直接编译进 libtask_graph（非子模块插件）。
//
//   mnn_inference          任意 .mnn 模型的通用图像推理，输出全部输出张量；
//   mnn_image_classifier   分类收尾（可选 labels 文件 + top-k + 可选 softmax）。
//
// 构建开关 TASK_GRAPH_ENABLE_MNN（CMake）：找到 scripts/build_mnn.py 产出的
// libMNN.a 时定义 TASK_GRAPH_MNN_AVAILABLE，为真实推理；否则任务照常注册、
// execute 返回 FAILED 并给出可读错误（引用该任务的图仍可反序列化/编辑）。
//
// 引擎生命周期：每个 task 实例在 on_init() 持有自己的 Interpreter+Session 并在
// execute() 复用。MNN 无跨线程共享 session 的官方保证（上游 issue #592/#2419），
// DAG 的并行分支各自持独立实例，天然满足；GPU 后端 runSession 为异步提交，
// 拷回 host 张量时同步。

#include <memory>
#include <string>
#include <vector>

#include <plugin_api.hpp>
#include <task_graph/mnn/mnn_types.hpp>

namespace task_graph {

class MnnEngine;

class MnnImageTaskBase : public INode {
public:
    std::vector<PortSpec> input_specs() const override;    // "in": Image | cv::Mat
    std::vector<PortSpec> output_specs() const override;   // "out"
    std::vector<ParamSpec> param_specs() const override;   // 基础参数（子类可扩展）

protected:
    MnnImageTaskBase(const std::string& id, const TaskConfig& cfg = {});

    // on_init 阶段：解析模型路径（ModelFinder 优先、_source_dir 相对路径回退）
    // 并创建引擎。失败写入 err 且不视为致命——execute 时会再次失败并上报。
    bool init_engine(std::string& err);

    // execute 阶段：取输入图（Image 或 cv::Mat）→ 预处理 → 推理 → 输出张量。
    bool run_image(TaskContext& ctx, std::vector<MnnTensorData>& tensors,
                   std::string& err);

    std::shared_ptr<MnnEngine> engine_;
};

// 通用推理：输入图像 → 模型全部输出张量
class MnnInferenceTask : public MnnImageTaskBase {
public:
    MnnInferenceTask(const std::string& id, const TaskConfig& cfg = {})
        : MnnImageTaskBase(id, cfg) {}

    const std::string& type() const override {
        static const std::string t("mnn_inference");
        return t;
    }

protected:
    void on_init() override;
    TaskResult execute(TaskContext& ctx) override;
};

// 分类：继承通用推理全部参数 + labels/top_k/softmax
class MnnImageClassifierTask : public MnnImageTaskBase {
public:
    MnnImageClassifierTask(const std::string& id, const TaskConfig& cfg = {})
        : MnnImageTaskBase(id, cfg) {}

    const std::string& type() const override {
        static const std::string t("mnn_image_classifier");
        return t;
    }

    std::vector<ParamSpec> param_specs() const override;

protected:
    void on_init() override;
    TaskResult execute(TaskContext& ctx) override;

private:
    bool softmax_{false};
    int top_k_{5};
    std::string labels_path_;
};

}  // namespace task_graph
