#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_manager.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <set>

class TestPluginTask : public task_graph::IPluginTask {
public:
    TestPluginTask(const std::string& id, const task_graph::TaskConfig& config = task_graph::TaskConfig()) 
        : id_(id), config_(config) {}
    
    const std::string& id() const override { return id_; }
    const std::string& type() const override { 
        static const std::string type = "test_plugin_type";
        return type; 
    }
    
    task_graph::TaskResult execute(task_graph::IExecutionContext& ctx) override {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }
    
    const task_graph::TaskConfig& config() const override {
        return config_;
    }
    
    task_graph::CheckResult check_input(const std::vector<std::any>& inputs) const override {
        return task_graph::CheckResult(true);
    }
    
private:
    std::string id_;
    task_graph::TaskConfig config_;
};

bool test_unique_task_id() {
    std::cout << "Test: Unique task ID generation... ";
    
    task_graph::PluginRegistry::instance().register_task(
        "test_plugin_type",
        [](const std::string& id, const task_graph::TaskConfig& config) { 
            return std::make_shared<TestPluginTask>(id, config); 
        }
    );
    
    auto task1 = task_graph::PluginRegistry::instance().create_task("test_plugin_type");
    auto task2 = task_graph::PluginRegistry::instance().create_task("test_plugin_type");
    auto task3 = task_graph::PluginRegistry::instance().create_task("test_plugin_type");
    
    bool ids_unique = task1->id() != task2->id() && 
                      task2->id() != task3->id() && 
                      task1->id() != task3->id();
    
    bool types_same = task1->type() == "test_plugin_type" && 
                      task2->type() == "test_plugin_type" && 
                      task3->type() == "test_plugin_type";
    
    task_graph::PluginRegistry::instance().unregister_task("test_plugin_type");
    
    std::cout << (ids_unique && types_same ? "PASSED" : "FAILED") << std::endl;
    return ids_unique && types_same;
}

bool test_task_manager() {
    std::cout << "Test: TaskManager lifecycle management... ";
    
    task_graph::TaskManager manager;
    
    auto task1 = std::make_shared<TestPluginTask>("test1");
    auto task2 = std::make_shared<TestPluginTask>("test2");
    
    manager.add_task(task1);
    manager.add_task("custom_id", task2);
    
    bool has_task1 = manager.has_task(task1->id());
    bool has_task2 = manager.has_task("custom_id");
    bool size_ok = manager.size() == 2;
    
    auto retrieved1 = manager.get_task(task1->id());
    auto retrieved2 = manager.get_task("custom_id");
    
    bool retrieve_ok = retrieved1 == task1 && retrieved2 == task2;
    
    manager.remove_task(task1->id());
    bool removed = !manager.has_task(task1->id()) && manager.size() == 1;
    
    manager.clear_all();
    bool cleared = manager.size() == 0;
    
    std::cout << (has_task1 && has_task2 && size_ok && retrieve_ok && removed && cleared ? "PASSED" : "FAILED") << std::endl;
    return has_task1 && has_task2 && size_ok && retrieve_ok && removed && cleared;
}

bool test_dag_multiple_same_type() {
    std::cout << "Test: DAG with multiple tasks of same type... ";
    
    task_graph::PluginRegistry::instance().register_task(
        "test_plugin_type",
        [](const std::string& id, const task_graph::TaskConfig& config) { 
            return std::make_shared<TestPluginTask>(id, config); 
        }
    );
    
    task_graph::DAG dag;
    dag.add_plugin_task("test_plugin_type");
    dag.add_plugin_task("test_plugin_type");
    dag.add_plugin_task("test_plugin_type");
    
    bool size_ok = dag.num_tasks() == 3;
    
    std::set<std::string> ids;
    std::set<std::string> types;
    for (const auto& [id, task] : dag.tasks()) {
        ids.insert(id);
        types.insert(task->type());
    }
    
    bool ids_unique = ids.size() == 3;
    bool types_same = types.size() == 1 && *types.begin() == "test_plugin_type";
    
    task_graph::PluginRegistry::instance().unregister_task("test_plugin_type");
    
    std::cout << (size_ok && ids_unique && types_same ? "PASSED" : "FAILED") << std::endl;
    return size_ok && ids_unique && types_same;
}

bool test_json_type_id_separate() {
    std::cout << "Test: JSON type and id separation... ";
    
    task_graph::PluginRegistry::instance().register_task(
        "test_plugin_type",
        [](const std::string& id, const task_graph::TaskConfig& config) { 
            return std::make_shared<TestPluginTask>(id, config); 
        }
    );
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "my_filter_1", "type": "test_plugin_type"},
                {"id": "my_filter_2", "type": "test_plugin_type"},
                {"id": "my_filter_3", "type": "test_plugin_type"}
            ],
            "edges": [
                {"from": "my_filter_1", "to": "my_filter_2"},
                {"from": "my_filter_2", "to": "my_filter_3"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    bool has_filter1 = dag.has_task("my_filter_1");
    bool has_filter2 = dag.has_task("my_filter_2");
    bool has_filter3 = dag.has_task("my_filter_3");
    
    auto filter1 = dag.get_task("my_filter_1");
    auto filter2 = dag.get_task("my_filter_2");
    auto filter3 = dag.get_task("my_filter_3");
    
    bool type_ok = filter1->type() == "test_plugin_type" && 
                   filter2->type() == "test_plugin_type" && 
                   filter3->type() == "test_plugin_type";
    
    bool id_ok = filter1->id() == "my_filter_1" && 
                 filter2->id() == "my_filter_2" && 
                 filter3->id() == "my_filter_3";
    
    bool edges_ok = dag.adjacency().at("my_filter_1").contains("my_filter_2") &&
                    dag.adjacency().at("my_filter_2").contains("my_filter_3");
    
    task_graph::PluginRegistry::instance().unregister_task("test_plugin_type");
    
    std::cout << (has_filter1 && has_filter2 && has_filter3 && type_ok && id_ok && edges_ok ? "PASSED" : "FAILED") << std::endl;
    return has_filter1 && has_filter2 && has_filter3 && type_ok && id_ok && edges_ok;
}

bool test_backward_compatibility() {
    std::cout << "Test: Backward compatibility (id only)... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "simple_task"},
                {"id": "another_task"}
            ],
            "edges": [
                {"from": "simple_task", "to": "another_task"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    bool has_simple = dag.has_task("simple_task");
    bool has_another = dag.has_task("another_task");
    
    auto simple = dag.get_task("simple_task");
    auto another = dag.get_task("another_task");
    
    bool type_eq_id = simple->type() == simple->id() && another->type() == another->id();
    
    std::cout << (has_simple && has_another && type_eq_id ? "PASSED" : "FAILED") << std::endl;
    return has_simple && has_another && type_eq_id;
}

int main() {
    std::vector<bool> results;
    
    results.push_back(test_unique_task_id());
    results.push_back(test_task_manager());
    results.push_back(test_dag_multiple_same_type());
    results.push_back(test_json_type_id_separate());
    results.push_back(test_backward_compatibility());
    
    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;
    
    return passed == results.size() ? 0 : 1;
}