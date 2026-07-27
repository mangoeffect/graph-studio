#pragma once

#include <task_graph/dag.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace task_graph {

struct ExecutionLayer {
    std::vector<TaskId> task_ids;
};

struct ExecutionPlan {
    std::vector<ExecutionLayer> layers;
    std::unordered_map<TaskId, size_t> task_to_layer;
    std::unordered_map<TaskId, std::vector<TaskId>> dependents;
};

// 构图期校验错误/警告。severity 区分 ERROR（必须修）与 WARNING（潜在问题）。
struct ValidationError {
    enum class Severity { ERROR, WARNING };
    Severity severity{Severity::ERROR};
    TaskId task_id;
    std::string port_name;
    std::string message;
};

class DAGCompiler {
public:
    ExecutionPlan compile(const DAG& dag);
    bool has_cycle(const DAG& dag) const;

    // 构图期契约校验。检查：
    //   1) 声明的 required input port 必须有 incoming edge
    //   2) incoming edge 的 from_port 类型与 to_port 类型匹配（若两边都注册了类型名）
    //   3) incoming edge 连到未声明的 input port（WARNING，便于渐进迁移）
    //   4) DAG 有环（ERROR）
    //   5) 多条 edge 写同一 to_port（WARNING，按 DAG::connect 当前策略允许）
    std::vector<ValidationError> validate(const DAG& dag) const;

    // 便捷封装：仅当存在 ERROR 级问题时返回 true
    bool has_errors(const DAG& dag) const;

private:
    std::vector<TaskId> topological_sort(const DAG& dag) const;
    std::vector<ExecutionLayer> build_layers(const DAG& dag, const std::vector<TaskId>& sorted_tasks) const;
};

}
