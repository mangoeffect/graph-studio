#include <task_graph/dag_serializer.hpp>
#include <task_graph/plugin.hpp>
#include <stdexcept>

namespace task_graph {

nlohmann::json DAGSerializer::serialize(const DAG& dag) {
    nlohmann::json j;
    
    j["version"] = "1.0";
    j["tasks"] = nlohmann::json::array();
    
    for (const auto& [id, task] : dag.tasks()) {
        nlohmann::json task_json;
        task_json["id"] = id;
        
        if (!task->type().empty() && task->type() != id) {
            task_json["type"] = task->type();
        }
        
        const auto& config = task->config();
        task_json["priority"] = static_cast<int>(config.priority);
        task_json["max_retries"] = config.max_retries;
        task_json["timeout_ms"] = config.timeout.count();
        task_json["skip_on_fail"] = config.skip_on_fail;
        
        nlohmann::json deps_json = nlohmann::json::array();
        for (const auto& dep : config.dependencies) {
            deps_json.push_back(dep);
        }
        task_json["dependencies"] = deps_json;
        
        j["tasks"].push_back(task_json);
    }
    
    j["edges"] = nlohmann::json::array();
    
    for (const auto& [from, to_set] : dag.adjacency()) {
        for (const auto& to : to_set) {
            nlohmann::json edge_json;
            edge_json["from"] = from;
            edge_json["to"] = to;
            j["edges"].push_back(edge_json);
        }
    }
    
    return j;
}

DAG DAGSerializer::deserialize(const nlohmann::json& j) {
    DAG dag;
    
    if (!j.contains("version")) {
        throw std::runtime_error("Missing version in DAG JSON");
    }
    
    std::string version = j["version"].get<std::string>();
    if (version != "1.0") {
        throw std::runtime_error("Unsupported DAG version: " + version);
    }
    
    if (!j.contains("tasks")) {
        throw std::runtime_error("Missing tasks in DAG JSON");
    }
    
    for (const auto& task_json : j["tasks"]) {
        std::string id = task_json["id"].get<std::string>();
        std::string type;
        
        if (task_json.contains("type")) {
            type = task_json["type"].get<std::string>();
        } else {
            type = id;
        }
        
        bool is_plugin_task = PluginRegistry::instance().has_task(type);
        
        if (is_plugin_task) {
            if (id == type) {
                dag.add_plugin_task(type);
            } else {
                dag.add_plugin_task(id, type);
            }
        } else {
            TaskConfig config;
            if (task_json.contains("priority")) {
                config.priority = static_cast<TaskPriority>(task_json["priority"].get<int>());
            }
            if (task_json.contains("max_retries")) {
                config.max_retries = task_json["max_retries"].get<size_t>();
            }
            if (task_json.contains("timeout_ms")) {
                config.timeout = std::chrono::milliseconds(task_json["timeout_ms"].get<int>());
            }
            if (task_json.contains("skip_on_fail")) {
                config.skip_on_fail = task_json["skip_on_fail"].get<bool>();
            }
            if (task_json.contains("dependencies")) {
                const auto& deps_json = task_json["dependencies"];
                for (const auto& dep : deps_json) {
                    config.dependencies.push_back(dep.get<std::string>());
                }
            }
            
            auto task = std::make_shared<Task>(
                id,
                type,
                [id](IExecutionContext& ctx) {
                    return TaskResult{.status = TaskStatus::COMPLETED};
                },
                config
            );
            dag.add_task(task);
        }
    }
    
    if (j.contains("edges")) {
        for (const auto& edge_json : j["edges"]) {
            std::string from = edge_json["from"].get<std::string>();
            std::string to = edge_json["to"].get<std::string>();
            dag.add_dependency(from, to);
        }
    }
    
    return dag;
}

std::string DAGSerializer::to_string(const DAG& dag, int indent) {
    return serialize(dag).dump(indent);
}

DAG DAGSerializer::from_string(const std::string& s) {
    return deserialize(nlohmann::json::parse(s));
}

}
