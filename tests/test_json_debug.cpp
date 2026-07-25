#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

int main() {
    try {
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

        std::cout << "Serializing DAG..." << std::endl;
        std::string json_str = task_graph::DAGSerializer::to_string(dag);
        std::cout << "JSON output:" << std::endl;
        std::cout << json_str << std::endl;
        
        std::cout << "\nDeserializing DAG..." << std::endl;
        task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
        std::cout << "Restored DAG has " << restored.num_tasks() << " tasks" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
