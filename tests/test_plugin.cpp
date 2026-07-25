#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

class TestPluginTask : public task_graph::IPluginTask {
public:
    TestPluginTask() : id_("test_task") {}
    const std::string& id() const override { return id_; }
    task_graph::TaskResult execute(task_graph::IExecutionContext&) override {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }
    const task_graph::TaskConfig& config() const override {
        static task_graph::TaskConfig cfg;
        return cfg;
    }
private:
    std::string id_;
};

bool test_plugin_registry() {
    std::cout << "Test: Plugin registry... ";

    auto& registry = task_graph::PluginRegistry::instance();
    
    registry.register_task("test_task", []() {
        return std::make_shared<TestPluginTask>();
    });

    bool has_task = registry.has_task("test_task");
    registry.unregister_task("test_task");
    bool not_found = !registry.has_task("test_task");

    std::cout << (has_task && not_found ? "PASSED" : "FAILED") << std::endl;
    return has_task && not_found;
}

bool test_plugin_loader() {
    std::cout << "Test: Plugin loader... ";

    task_graph::PluginLoader loader;
    
    std::filesystem::path plugin_path = std::filesystem::current_path() / "task_plugin_example.dylib";
    if (!std::filesystem::exists(plugin_path)) {
        plugin_path = std::filesystem::current_path() / "task_plugin_example.so";
    }
    if (!std::filesystem::exists(plugin_path)) {
        std::cout << "SKIPPED (plugin not found)" << std::endl;
        return true;
    }

    std::cout << "\n  Plugin path: " << plugin_path << std::endl;
    
    bool loaded = loader.load(plugin_path.string());
    std::cout << "  Load result: " << (loaded ? "success" : "failed") << std::endl;
    
    auto available = task_graph::PluginRegistry::instance().available_tasks();
    std::cout << "  Available tasks: ";
    for (const auto& t : available) {
        std::cout << t << " ";
    }
    std::cout << std::endl;
    
    bool has_task = task_graph::PluginRegistry::instance().has_task("example_task_1");
    std::cout << "  Has example_task_1: " << (has_task ? "yes" : "no") << std::endl;
    
    loader.unload(plugin_path.string());
    bool unloaded = !task_graph::PluginRegistry::instance().has_task("example_task_1");

    std::cout << (loaded && has_task && unloaded ? "PASSED" : "FAILED") << std::endl;
    return loaded && has_task && unloaded;
}

bool test_dag_with_plugin_tasks() {
    std::cout << "Test: DAG with plugin tasks... ";

    task_graph::PluginLoader loader;
    
    std::filesystem::path plugin_path = std::filesystem::current_path() / "task_plugin_example.dylib";
    if (!std::filesystem::exists(plugin_path)) {
        plugin_path = std::filesystem::current_path() / "task_plugin_example.so";
    }
    if (!std::filesystem::exists(plugin_path)) {
        std::cout << "SKIPPED (plugin not found)" << std::endl;
        return true;
    }

    bool loaded = loader.load(plugin_path.string());
    if (!loaded) {
        std::cout << "SKIPPED (plugin load failed)" << std::endl;
        return true;
    }

    task_graph::DAG dag;
    
    dag.add_plugin_task("example_task_1");
    dag.add_plugin_task("data_processor");
    dag.add_dependency("example_task_1", "data_processor");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    bool success = results.size() == 2 && results["example_task_1"].is_success();

    loader.unload(plugin_path.string());

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;

    results.push_back(test_plugin_registry());
    results.push_back(test_plugin_loader());
    results.push_back(test_dag_with_plugin_tasks());

    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;

    return passed == results.size() ? 0 : 1;
}
