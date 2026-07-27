#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <vector>
#include <unordered_map>
#include <task_graph/task_context.hpp>

// 端口化后的 SumTask：声明两个 int 输入端口 "a"/"b"。
// 不再手写 check_input；IPluginTask 默认实现会按 specs 自动校验。
class SumTask : public task_graph::IPluginTask {
public:
    using task_graph::IPluginTask::IPluginTask;

    const std::string& type() const override { return type_; }

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {
            task_graph::make_port<int>("a"),
            task_graph::make_port<int>("b"),
        };
    }

    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override {
        // execute 内部用 blackboard 测试读取（task 内部局部状态）；不影响 check_input 测试
        auto a = ctx.get<int>("input_a");
        auto b = ctx.get<int>("input_b");
        if (a && b) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED,
                                          .value = *a + *b};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    }

private:
    static const std::string type_;
};

const std::string SumTask::type_ = "sum_task";

// 构造输入 map 的便捷函数（端口名 → any）
static std::unordered_map<std::string, std::any> make_inputs(
    std::initializer_list<std::pair<std::string, std::any>> kv) {
    std::unordered_map<std::string, std::any> m;
    for (const auto& p : kv) m.emplace(p.first, p.second);
    return m;
}

bool test_check_input_success() {
    std::cout << "Test: check_input success case... ";

    SumTask task("sum");
    auto inputs = make_inputs({{"a", 10}, {"b", 20}});
    auto result = task.check_input(inputs);

    if (!result.success) {
        std::cout << "FAILED (expected success): " << result.error_message << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_missing_port() {
    std::cout << "Test: check_input missing required port... ";

    SumTask task("sum");
    auto inputs = make_inputs({{"a", 10}});  // 缺 b
    auto result = task.check_input(inputs);

    if (result.success) {
        std::cout << "FAILED (expected failure)" << std::endl;
        return false;
    }

    if (result.error_message.find("missing required input port") == std::string::npos ||
        result.error_message.find("'b'") == std::string::npos) {
        std::cout << "FAILED (wrong error message): " << result.error_message << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_wrong_type() {
    std::cout << "Test: check_input wrong type... ";

    SumTask task("sum");
    auto inputs = make_inputs({{"a", 10}, {"b", std::string("not int")}});
    auto result = task.check_input(inputs);

    if (result.success) {
        std::cout << "FAILED (expected failure)" << std::endl;
        return false;
    }

    if (result.error_message.find("expected int") == std::string::npos ||
        result.error_message.find("got std::string") == std::string::npos) {
        std::cout << "FAILED (wrong error message): " << result.error_message << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_extra_port_ok() {
    // 多余的输入端口不视为错误（允许渐进迁移与扩展）
    std::cout << "Test: check_input extra port is allowed... ";

    SumTask task("sum");
    auto inputs = make_inputs({{"a", 10}, {"b", 20}, {"c", 99}});
    auto result = task.check_input(inputs);

    if (!result.success) {
        std::cout << "FAILED (expected success): " << result.error_message << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);

    std::cout << "=== Check Input Tests ===\n" << std::endl;

    bool all_passed = true;
    all_passed &= test_check_input_success();
    all_passed &= test_check_input_missing_port();
    all_passed &= test_check_input_wrong_type();
    all_passed &= test_check_input_extra_port_ok();

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
