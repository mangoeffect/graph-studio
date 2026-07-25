#pragma once

#include <task_graph/dag.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace task_graph {

class DAGSerializer {
public:
    static nlohmann::json serialize(const DAG& dag);
    static DAG deserialize(const nlohmann::json& j);
    
    static std::string to_string(const DAG& dag, int indent = 4);
    static DAG from_string(const std::string& s);
};

}
