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
    // base_dir：graph.json 所在目录的绝对路径，会被注入到每个 task 的
    // _source_dir 参数（详见 <task_graph/path_utils.hpp>）。默认空串=不注入
    // 拼接信息，行为与历史调用完全一致（测试/程序化构造 DAG 时保持默认）。
    static nlohmann::json serialize(const DAG& dag);
    static DAG deserialize(const nlohmann::json& j, const std::string& base_dir = "");
    static std::string to_string(const DAG& dag, int indent = 4);
    static DAG from_string(const std::string& s, const std::string& base_dir = "");

    // 带元数据的版本（app 可注入 positions 等 UI-only 数据）
    static nlohmann::json serialize(const DAG& dag, const nlohmann::json& metadata);
    static std::string to_string(const DAG& dag, const nlohmann::json& metadata, int indent = 4);
    static DeserializeResult deserialize_with_metadata(const nlohmann::json& j,
                                                       const std::string& base_dir = "");
    static DeserializeResult from_string_with_metadata(const std::string& s,
                                                       const std::string& base_dir = "");
};

}
