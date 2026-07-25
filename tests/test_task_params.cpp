#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

bool test_task_params_set_get() {
    std::cout << "Test: TaskParams set and get... ";
    
    task_graph::TaskParams params;
    
    params.set_int("kernel_size", 5);
    params.set_float("sigma", 1.5f);
    params.set_string("mode", "RGB");
    
    bool int_ok = params.get_int("kernel_size") == 5;
    bool float_ok = params.get_float("sigma") == 1.5f;
    bool string_ok = params.get_string("mode") == "RGB";
    
    bool missing_int = !params.get_int("missing_key").has_value();
    bool missing_float = !params.get_float("missing_key").has_value();
    bool missing_string = !params.get_string("missing_key").has_value();
    
    bool has_param_ok = params.has_param("kernel_size") && !params.has_param("nonexistent");
    
    std::cout << (int_ok && float_ok && string_ok && missing_int && missing_float && missing_string && has_param_ok ? "PASSED" : "FAILED") << std::endl;
    return int_ok && float_ok && string_ok && missing_int && missing_float && missing_string && has_param_ok;
}

bool test_task_params_clear() {
    std::cout << "Test: TaskParams clear... ";
    
    task_graph::TaskParams params;
    
    params.set_int("value1", 10);
    params.set_string("value2", "test");
    
    params.clear();
    
    bool cleared = !params.has_param("value1") && !params.has_param("value2");
    
    std::cout << (cleared ? "PASSED" : "FAILED") << std::endl;
    return cleared;
}

bool test_task_config_with_params() {
    std::cout << "Test: TaskConfig with params... ";
    
    task_graph::TaskConfig config;
    config.params.set_int("threshold", 128);
    config.params.set_float("scale", 0.5f);
    config.params.set_string("algorithm", "canny");
    
    bool has_threshold = config.params.get_int("threshold") == 128;
    bool has_scale = config.params.get_float("scale") == 0.5f;
    bool has_algorithm = config.params.get_string("algorithm") == "canny";
    
    std::cout << (has_threshold && has_scale && has_algorithm ? "PASSED" : "FAILED") << std::endl;
    return has_threshold && has_scale && has_algorithm;
}

bool test_task_execution_with_params() {
    std::cout << "Test: Task execution with params... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 42};
    });
    dag.add_task(input_task);
    
    task_graph::TaskConfig config;
    config.params.set_int("multiplier", 2);
    config.params.set_float("factor", 0.5f);
    config.params.set_string("operation", "multiply");
    
    auto process_task = std::make_shared<task_graph::Task>(
        "process",
        [](task_graph::IExecutionContext& ctx) {
            auto input = ctx.get_result_value<int>("input");
            auto multiplier = ctx.get_param_int("multiplier");
            auto factor = ctx.get_param_float("factor");
            auto operation = ctx.get_param_string("operation");
            
            if (input && multiplier && factor && operation && *operation == "multiply") {
                int result = *input * *multiplier * static_cast<int>(*factor * 10);
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config
    );
    dag.add_task(process_task);
    dag.add_dependency("input", "process");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    bool success = results["input"].is_success() && results["process"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_params_serialization() {
    std::cout << "Test: Params serialization... ";
    
    task_graph::DAG dag;
    
    task_graph::TaskConfig config;
    config.params.set_int("width", 640);
    config.params.set_float("ratio", 1.777f);
    config.params.set_string("format", "JPEG");
    
    auto task = std::make_shared<task_graph::Task>(
        "encoder",
        [](auto& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        },
        config
    );
    dag.add_task(task);
    
    std::string json_str = task_graph::DAGSerializer::to_string(dag);
    
    bool has_width = json_str.find("\"width\":") != std::string::npos;
    bool has_ratio = json_str.find("\"ratio\":") != std::string::npos;
    bool has_format = json_str.find("\"format\":") != std::string::npos;
    bool has_params = json_str.find("\"params\"") != std::string::npos;
    
    std::cout << (has_width && has_ratio && has_format && has_params ? "PASSED" : "FAILED") << std::endl;
    return has_width && has_ratio && has_format && has_params;
}

bool test_params_deserialization() {
    std::cout << "Test: Params deserialization... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {
                    "id": "filter",
                    "params": {
                        "kernel_size": 7,
                        "sigma": 2.0,
                        "mode": "GRAY"
                    }
                }
            ],
            "edges": []
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    auto task = dag.get_task("filter");
    if (!task) {
        std::cout << "FAILED" << std::endl;
        return false;
    }
    
    const auto& params = task->config().params;
    bool int_ok = params.get_int("kernel_size") == 7;
    bool float_ok = params.get_float("sigma") == 2.0f;
    bool string_ok = params.get_string("mode") == "GRAY";
    
    std::cout << (int_ok && float_ok && string_ok ? "PASSED" : "FAILED") << std::endl;
    return int_ok && float_ok && string_ok;
}

bool test_params_roundtrip() {
    std::cout << "Test: Params roundtrip... ";
    
    task_graph::DAG original;
    
    task_graph::TaskConfig config;
    config.params.set_int("count", 100);
    config.params.set_float("accuracy", 0.95f);
    config.params.set_string("model", "resnet50");
    
    auto task = std::make_shared<task_graph::Task>(
        "inference",
        [](auto& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        },
        config
    );
    original.add_task(task);
    
    std::string json_str = task_graph::DAGSerializer::to_string(original);
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
    
    auto restored_task = restored.get_task("inference");
    if (!restored_task) {
        std::cout << "FAILED" << std::endl;
        return false;
    }
    
    const auto& params = restored_task->config().params;
    bool int_ok = params.get_int("count") == 100;
    bool float_ok = params.get_float("accuracy") == 0.95f;
    bool string_ok = params.get_string("model") == "resnet50";
    
    std::cout << (int_ok && float_ok && string_ok ? "PASSED" : "FAILED") << std::endl;
    return int_ok && float_ok && string_ok;
}

bool test_cascade_with_params() {
    std::cout << "Test: Cascade tasks with params... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });
    dag.add_task(input_task);
    
    task_graph::TaskConfig config1;
    config1.params.set_int("add_value", 5);
    auto add_task = std::make_shared<task_graph::Task>(
        "add",
        [](task_graph::IExecutionContext& ctx) {
            auto input = ctx.get_result_value<int>("input");
            auto add_val = ctx.get_param_int("add_value");
            if (input && add_val) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *input + *add_val};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config1
    );
    dag.add_task(add_task);
    
    task_graph::TaskConfig config2;
    config2.params.set_int("multiply_value", 2);
    auto multiply_task = std::make_shared<task_graph::Task>(
        "multiply",
        [](task_graph::IExecutionContext& ctx) {
            auto input = ctx.get_result_value<int>("add");
            auto mul_val = ctx.get_param_int("multiply_value");
            if (input && mul_val) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *input * *mul_val};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config2
    );
    dag.add_task(multiply_task);
    
    dag.add_dependency("input", "add");
    dag.add_dependency("add", "multiply");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    bool success = results["input"].is_success() && 
                   results["add"].is_success() && 
                   results["multiply"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;
    
    results.push_back(test_task_params_set_get());
    results.push_back(test_task_params_clear());
    results.push_back(test_task_config_with_params());
    results.push_back(test_task_execution_with_params());
    results.push_back(test_params_serialization());
    results.push_back(test_params_deserialization());
    results.push_back(test_params_roundtrip());
    results.push_back(test_cascade_with_params());
    
    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << std::count(results.begin(), results.end(), true) << "/" << results.size() << " tests passed" << std::endl;
    
    return std::all_of(results.begin(), results.end(), [](bool b) { return b; }) ? 0 : 1;
}