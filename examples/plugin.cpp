#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <iostream>
#include <string>
#include <filesystem>

int main() {
    std::cout << "=== Plugin Example ===\n" << std::endl;

    task_graph::PluginLoader loader;
    
    std::filesystem::path plugin_path = std::filesystem::current_path() / "task_plugin_example.dylib";
    if (!std::filesystem::exists(plugin_path)) {
        plugin_path = std::filesystem::current_path() / "task_plugin_example.so";
    }
    
    if (!std::filesystem::exists(plugin_path)) {
        std::cout << "Plugin not found: " << plugin_path << std::endl;
        std::cout << "Please build the plugin first." << std::endl;
        return 1;
    }

    std::cout << "Loading plugin: " << plugin_path << std::endl;
    
    bool loaded = loader.load(plugin_path.string());
    if (!loaded) {
        std::cout << "Failed to load plugin" << std::endl;
        return 1;
    }

    std::cout << "Plugin loaded successfully!" << std::endl;
    
    auto& registry = task_graph::PluginRegistry::instance();
    auto available = registry.available_tasks();
    
    std::cout << "\nAvailable plugin tasks: ";
    for (const auto& task_id : available) {
        std::cout << task_id << " ";
    }
    std::cout << std::endl;

    task_graph::DAG dag;

    dag.add_plugin_task("example_task_1");
    dag.add_plugin_task("example_task_2");
    dag.add_plugin_task("data_processor");
    
    dag.add_dependency("example_task_1", "data_processor");
    dag.add_dependency("example_task_2", "data_processor");

    std::cout << "\nDAG structure:\n";
    std::cout << "  example_task_1 ---\n";
    std::cout << "                    --> data_processor\n";
    std::cout << "  example_task_2 ---" << std::endl;

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    std::cout << "\nExecution results:\n";
    for (const auto& [id, result] : results) {
        std::cout << "  " << id << ": " 
                  << (result.is_success() ? "SUCCESS" : "FAILED") << std::endl;
    }

    loader.unload(plugin_path.string());
    std::cout << "\nPlugin unloaded." << std::endl;

    std::cout << "\nDone!\n";
    return 0;
}
