#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

bool test_subnode_tasks_available() {
    std::cout << "Test: Subnode tasks available... ";

    auto& registry = task_graph::PluginRegistry::instance();
    auto available = registry.available_tasks();

    bool has_example_1 = std::find(available.begin(), available.end(), "example_task_1") != available.end();
    bool has_example_2 = std::find(available.begin(), available.end(), "example_task_2") != available.end();
    bool has_processor = std::find(available.begin(), available.end(), "data_processor") != available.end();

    std::cout << (has_example_1 && has_example_2 && has_processor ? "PASSED" : "FAILED") << std::endl;
    return has_example_1 && has_example_2 && has_processor;
}

bool test_subnode_dag_execution() {
    std::cout << "Test: Subnode DAG execution... ";

    task_graph::DAG dag;

    dag.add_plugin_task("example_task_1");
    dag.add_plugin_task("example_task_2");
    dag.add_plugin_task("data_processor");
    
    dag.add_dependency("example_task_1", "data_processor");
    dag.add_dependency("example_task_2", "data_processor");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    
    bool success = results.size() == 3;
    success &= results["example_task_1"].is_success();
    success &= results["example_task_2"].is_success();
    success &= results["data_processor"].is_success();

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_individual_task_execution() {
    std::cout << "Test: Individual task execution... ";

    task_graph::DAG dag;
    
    dag.add_plugin_task("example_task_1");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    bool success = results.size() == 1 && results["example_task_1"].is_success();

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;

    results.push_back(test_subnode_tasks_available());
    results.push_back(test_subnode_dag_execution());
    results.push_back(test_individual_task_execution());

    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;

    return passed == results.size() ? 0 : 1;
}
