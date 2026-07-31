#pragma once

#include <task_graph/dag.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace task_graph {

class DAGSerializer {
public:
    struct DeserializeResult {
        DAG dag;
        nlohmann::json metadata;  // app 专属元数据（positions 等），round-trip 保留
    };

    // 基础版本（无元数据，兼容现有调用）
    static nlohmann::json serialize(const DAG& dag);
    static DAG deserialize(const nlohmann::json& j);
    static std::string to_string(const DAG& dag, int indent = 4);
    static DAG from_string(const std::string& s);

    // 带元数据的版本（app 可注入 positions 等 UI-only 数据）
    static nlohmann::json serialize(const DAG& dag, const nlohmann::json& metadata);
    static std::string to_string(const DAG& dag, const nlohmann::json& metadata, int indent = 4);
    static DeserializeResult deserialize_with_metadata(const nlohmann::json& j);
    static DeserializeResult from_string_with_metadata(const std::string& s);
};

}
