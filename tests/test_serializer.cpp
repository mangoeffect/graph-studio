#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/plugin.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

bool test_dag_serialize() {
    std::cout << "Test: DAG serialization... ";

    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");

    std::string json_str = task_graph::DAGSerializer::to_string(dag);
    
    bool has_tasks = json_str.find("tasks") != std::string::npos;
    bool has_edges = json_str.find("edges") != std::string::npos;
    bool has_A = json_str.find("\"A\"") != std::string::npos;
    bool has_B = json_str.find("\"B\"") != std::string::npos;

    std::cout << (has_tasks && has_edges && has_A && has_B ? "PASSED" : "FAILED") << std::endl;
    return has_tasks && has_edges && has_A && has_B;
}

bool test_dag_deserialize() {
    std::cout << "Test: DAG deserialization... ";

    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "A"},
                {"id": "B"}
            ],
            "edges": [
                {"from": "A", "to": "B"}
            ]
        }
    )";

    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    bool has_A = dag.has_task("A");
    bool has_B = dag.has_task("B");
    bool has_dep = dag.adjacency().at("A").contains("B");

    std::cout << (has_A && has_B && has_dep ? "PASSED" : "FAILED") << std::endl;
    return has_A && has_B && has_dep;
}

bool test_dag_roundtrip() {
    std::cout << "Test: DAG roundtrip... ";

    task_graph::DAG original;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    auto task_c = std::make_shared<task_graph::Task>("C", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    original.add_task(task_a);
    original.add_task(task_b);
    original.add_task(task_c);
    original.add_dependency("A", "B");
    original.add_dependency("B", "C");

    std::string json_str = task_graph::DAGSerializer::to_string(original);
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
    
    bool same_tasks = original.num_tasks() == restored.num_tasks();
    bool same_edges = original.num_edges() == restored.num_edges();
    bool has_all_tasks = restored.has_task("A") && restored.has_task("B") && restored.has_task("C");

    std::cout << (same_tasks && same_edges && has_all_tasks ? "PASSED" : "FAILED") << std::endl;
    return same_tasks && same_edges && has_all_tasks;
}

bool test_dag_with_config() {
    std::cout << "Test: DAG with task config... ";

    task_graph::TaskConfig config;
    config.priority = task_graph::TaskPriority::HIGH;
    config.max_retries = 3;
    config.timeout = std::chrono::milliseconds(5000);
    config.skip_on_fail = true;

    task_graph::DAG original;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }, config);
    
    original.add_task(task_a);

    std::string json_str = task_graph::DAGSerializer::to_string(original);
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
    
    auto restored_task = restored.get_task("A");
    bool priority_ok = restored_task->config().priority == task_graph::TaskPriority::HIGH;
    bool retries_ok = restored_task->config().max_retries == 3;
    bool timeout_ok = restored_task->config().timeout == std::chrono::milliseconds(5000);
    bool skip_ok = restored_task->config().skip_on_fail == true;

    std::cout << (priority_ok && retries_ok && timeout_ok && skip_ok ? "PASSED" : "FAILED") << std::endl;
    return priority_ok && retries_ok && timeout_ok && skip_ok;
}

int main() {
    std::vector<bool> results;

    results.push_back(test_dag_serialize());
    results.push_back(test_dag_deserialize());
    results.push_back(test_dag_roundtrip());
    results.push_back(test_dag_with_config());

    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;

    return passed == results.size() ? 0 : 1;
}
