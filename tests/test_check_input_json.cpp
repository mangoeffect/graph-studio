#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/dag_serializer.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <vector>
#include <string>

class StrictSumTask : public task_graph::Task {
public:
    StrictSumTask(const std::string& id) 
        : Task(id, [this](auto& ctx) { return execute_impl(ctx); }, create_config()) {}
    
private:
    static task_graph::TaskConfig create_config() {
        task_graph::TaskConfig cfg;
        cfg.dependencies = {"producer_a", "producer_b"};
        return cfg;
    }
    
    task_graph::TaskResult execute_impl(task_graph::IExecutionContext& ctx) {
        auto dep_a = ctx.template get_result_value<int>("producer_a");
        auto dep_b = ctx.template get_result_value<int>("producer_b");
        if (dep_a && dep_b) {
            int result = *dep_a + *dep_b;
            ctx.set_value("sum_result", result);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    }
    
    task_graph::CheckResult check_input(const std::vector<std::any>& inputs) const override {
        if (inputs.size() != 2) {
            return task_graph::CheckResult(false, "StrictSumTask requires exactly 2 inputs, got " + std::to_string(inputs.size()));
        }
        
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!inputs[i].has_value()) {
                return task_graph::CheckResult(false, "StrictSumTask input " + std::to_string(i) + " is empty");
            }
            try {
                std::any_cast<int>(inputs[i]);
            } catch (const std::bad_any_cast&) {
                return task_graph::CheckResult(false, "StrictSumTask input " + std::to_string(i) + " must be int");
            }
        }
        
        return task_graph::CheckResult(true);
    }
};

template<typename TaskType>
void add_or_update_task(task_graph::DAG& dag, const std::shared_ptr<TaskType>& task) {
    auto existing = dag.get_task(task->id());
    if (existing) {
        const_cast<std::unordered_map<task_graph::TaskId, task_graph::TaskPtr>&>(dag.tasks())[task->id()] = task;
    } else {
        dag.add_task(task);
    }
}

bool test_json_with_check_input_success() {
    std::cout << "Test: JSON DAG with check_input success... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "producer_a"},
                {"id": "producer_b"},
                {
                    "id": "sum_task",
                    "dependencies": ["producer_a", "producer_b"]
                }
            ],
            "edges": [
                {"from": "producer_a", "to": "sum_task"},
                {"from": "producer_b", "to": "sum_task"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });
    
    auto producer_b = std::make_shared<task_graph::Task>("producer_b", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 20};
    });
    
    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    
    add_or_update_task(dag, producer_a);
    add_or_update_task(dag, producer_b);
    add_or_update_task(dag, sum_task);
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (sum_task should be completed)" << std::endl;
        return false;
    }
    
    int sum_value = std::any_cast<int>(results["sum_task"].value);
    if (sum_value != 30) {
        std::cout << "FAILED (expected 30, got " << sum_value << ")" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_json_with_check_input_wrong_type() {
    std::cout << "Test: JSON DAG with check_input wrong type... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "producer_a"},
                {"id": "producer_b"},
                {
                    "id": "sum_task",
                    "dependencies": ["producer_a", "producer_b"]
                }
            ],
            "edges": [
                {"from": "producer_a", "to": "sum_task"},
                {"from": "producer_b", "to": "sum_task"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = std::string("wrong_type")};
    });
    
    auto producer_b = std::make_shared<task_graph::Task>("producer_b", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 20};
    });
    
    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    
    add_or_update_task(dag, producer_a);
    add_or_update_task(dag, producer_b);
    add_or_update_task(dag, sum_task);
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (sum_task should be failed due to wrong input type)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_json_with_check_input_missing_dependency() {
    std::cout << "Test: JSON DAG with check_input missing dependency... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "producer_a"},
                {
                    "id": "sum_task",
                    "dependencies": ["producer_a", "producer_b"]
                }
            ],
            "edges": [
                {"from": "producer_a", "to": "sum_task"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });
    
    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    
    add_or_update_task(dag, producer_a);
    add_or_update_task(dag, sum_task);
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (sum_task should be failed due to missing dependency)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_json_roundtrip_with_check_input() {
    std::cout << "Test: JSON roundtrip with check_input... ";
    
    task_graph::DAG original;
    
    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 15};
    });
    
    auto producer_b = std::make_shared<task_graph::Task>("producer_b", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 25};
    });
    
    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    
    original.add_task(producer_a);
    original.add_task(producer_b);
    original.add_task(sum_task);
    original.add_dependency("producer_a", "sum_task");
    original.add_dependency("producer_b", "sum_task");
    
    std::string json_str = task_graph::DAGSerializer::to_string(original);
    
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
    
    add_or_update_task(restored, producer_a);
    add_or_update_task(restored, producer_b);
    add_or_update_task(restored, sum_task);
    
    task_graph::DAGExecutor executor;
    executor.execute(restored).wait();
    
    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (sum_task should be completed after roundtrip)" << std::endl;
        return false;
    }
    
    int sum_value = std::any_cast<int>(results["sum_task"].value);
    if (sum_value != 40) {
        std::cout << "FAILED (expected 40, got " << sum_value << ")" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);
    
    std::cout << "=== Check Input JSON Tests ===\n" << std::endl;
    
    bool all_passed = true;
    all_passed &= test_json_with_check_input_success();
    all_passed &= test_json_with_check_input_wrong_type();
    all_passed &= test_json_with_check_input_missing_dependency();
    all_passed &= test_json_roundtrip_with_check_input();
    
    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
