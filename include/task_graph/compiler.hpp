#pragma once

#include <task_graph/dag.hpp>
#include <vector>
#include <string>
#include <memory>

namespace task_graph {

struct ExecutionLayer {
    std::vector<TaskId> task_ids;
};

struct ExecutionPlan {
    std::vector<ExecutionLayer> layers;
    std::unordered_map<TaskId, size_t> task_to_layer;
    std::unordered_map<TaskId, std::vector<TaskId>> dependents;
};

class DAGCompiler {
public:
    ExecutionPlan compile(const DAG& dag);
    bool has_cycle(const DAG& dag);

private:
    std::vector<TaskId> topological_sort(const DAG& dag);
    std::vector<ExecutionLayer> build_layers(const DAG& dag, const std::vector<TaskId>& sorted_tasks);
};

}
