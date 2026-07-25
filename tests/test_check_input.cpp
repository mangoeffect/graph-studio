#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <vector>
#include <task_graph/task_context.hpp>

class SumTask : public task_graph::IPluginTask {
public:
    using task_graph::IPluginTask::IPluginTask;
    
    const std::string& type() const override { return type_; }
    
    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override {
        auto a = ctx.get<int>("input_a");
        auto b = ctx.get<int>("input_b");
        if (a && b) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *a + *b};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    }
    
    task_graph::CheckResult check_input(const std::vector<std::any>& inputs) const override {
        if (inputs.size() != 2) {
            return task_graph::CheckResult(false, "Expected 2 inputs, got " + std::to_string(inputs.size()));
        }
        
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!inputs[i].has_value()) {
                return task_graph::CheckResult(false, "Input " + std::to_string(i) + " is empty");
            }
            try {
                std::any_cast<int>(inputs[i]);
            } catch (const std::bad_any_cast&) {
                return task_graph::CheckResult(false, "Input " + std::to_string(i) + " is not an int");
            }
        }
        
        return task_graph::CheckResult(true);
    }
    
private:
    static const std::string type_;
};

const std::string SumTask::type_ = "sum_task";

bool test_check_input_success() {
    std::cout << "Test: check_input success case... ";
    
    SumTask task("sum");
    std::vector<std::any> inputs = {10, 20};
    auto result = task.check_input(inputs);
    
    if (!result.success) {
        std::cout << "FAILED (expected success)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_wrong_count() {
    std::cout << "Test: check_input wrong input count... ";
    
    SumTask task("sum");
    std::vector<std::any> inputs = {10};
    auto result = task.check_input(inputs);
    
    if (result.success) {
        std::cout << "FAILED (expected failure)" << std::endl;
        return false;
    }
    
    if (result.error_message.find("Expected 2 inputs") == std::string::npos) {
        std::cout << "FAILED (wrong error message)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_wrong_type() {
    std::cout << "Test: check_input wrong type... ";
    
    SumTask task("sum");
    std::vector<std::any> inputs = {10, std::string("not int")};
    auto result = task.check_input(inputs);
    
    if (result.success) {
        std::cout << "FAILED (expected failure)" << std::endl;
        return false;
    }
    
    if (result.error_message.find("is not an int") == std::string::npos) {
        std::cout << "FAILED (wrong error message)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_empty_input() {
    std::cout << "Test: check_input empty input... ";
    
    SumTask task("sum");
    std::vector<std::any> inputs = {10, std::any()};
    auto result = task.check_input(inputs);
    
    if (result.success) {
        std::cout << "FAILED (expected failure)" << std::endl;
        return false;
    }
    
    if (result.error_message.find("is empty") == std::string::npos) {
        std::cout << "FAILED (wrong error message)" << std::endl;
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
    all_passed &= test_check_input_wrong_count();
    all_passed &= test_check_input_wrong_type();
    all_passed &= test_check_input_empty_input();
    
    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}