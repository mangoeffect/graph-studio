// 端到端验证：多输出 task + 命名端口 + 构图期校验
// 演示新的 port 模型如何解决原始方案中"单 task 无法输出多个数据"的问题。
#include <task_graph/task_graph.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/compiler.hpp>
#include <task_graph/task_context.hpp>
#include <iostream>

// Detector 同时输出 image 和 mask（多输出场景）
class DetectorTask : public task_graph::Task {
public:
    DetectorTask(const std::string& id)
        : LambdaNode(id, [](task_graph::TaskContext& ctx) {
            (void)ctx;
            task_graph::Image img(640, 480, 3);
            task_graph::Image mask(640, 480, 1);
            // 多输出：用 outputs map，端口名 "image"/"mask"
            task_graph::TaskResult r;
            r.status = task_graph::TaskStatus::COMPLETED;
            r.outputs["image"] = img;
            r.outputs["mask"]  = mask;
            return r;
        }) {}

    std::vector<task_graph::PortSpec> output_specs() const override {
        return {
            task_graph::make_port<task_graph::Image>("image"),
            task_graph::make_port<task_graph::Image>("mask"),
        };
    }
};

// Annotator 消费 image，输出标注后的 image
class AnnotatorTask : public task_graph::Task {
public:
    AnnotatorTask(const std::string& id)
        : LambdaNode(id, [](task_graph::TaskContext& ctx) {
            auto in = ctx.input<task_graph::Image>("in");
            if (!in) return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED,
                                          .value = *in};
        }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<task_graph::Image>("in")};
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return {task_graph::make_port<task_graph::Image>("out")};
    }
};

// StatsTask 消费 mask，输出统计信息（int）
class StatsTask : public task_graph::Task {
public:
    StatsTask(const std::string& id)
        : LambdaNode(id, [](task_graph::TaskContext& ctx) {
            auto in = ctx.input<task_graph::Image>("in");
            if (!in) return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED,
                                          .value = static_cast<int>(in->width * in->height)};
        }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<task_graph::Image>("in")};
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return {task_graph::make_port<int>("out")};
    }
};

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);

    task_graph::DAG dag;
    dag.add_task(std::make_shared<DetectorTask>("det"));
    dag.add_task(std::make_shared<AnnotatorTask>("ann"));
    dag.add_task(std::make_shared<StatsTask>("stats"));

    // 一个 detector 的输出连接到两个不同下游的不同输入端口
    dag.connect("det", "image", "ann",   "in");
    dag.connect("det", "mask",  "stats", "in");

    std::cout << "=== Port-based multi-output pipeline ===\n";
    std::cout << "  det:image -> ann:in\n";
    std::cout << "  det:mask  -> stats:in\n\n";

    // 构图期校验
    task_graph::DAGCompiler compiler;
    auto errs = compiler.validate(dag);
    bool has_error = false;
    for (const auto& e : errs) {
        std::cout << "  [" << (e.severity == task_graph::ValidationError::Severity::ERROR ? "ERR " : "WARN")
                  << "] " << e.task_id << ":" << e.port_name << " - " << e.message << "\n";
        if (e.severity == task_graph::ValidationError::Severity::ERROR) has_error = true;
    }
    if (has_error) {
        std::cout << "Validation failed.\n";
        return 1;
    }
    std::cout << "Validation passed.\n\n";

    // 执行
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    std::cout << "Execution results:\n";
    for (const auto& [id, r] : results) {
        std::cout << "  " << id << ": "
                  << (r.is_success() ? "SUCCESS" : "FAILED") << "\n";
    }

    // 验证 stats 真的拿到了 mask
    auto stats_result = results.at("stats");
    if (stats_result.is_success()) {
        int pixels = std::any_cast<int>(stats_result.value);
        std::cout << "\nstats processed mask pixels: " << pixels << " (expected 640*480=307200)\n";
        return pixels == 640 * 480 ? 0 : 1;
    }
    return 1;
}
