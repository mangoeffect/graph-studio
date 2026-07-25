#include <task_graph/compiler.hpp>
#include <queue>
#include <unordered_set>
#include <stdexcept>

namespace task_graph {

// 环检测：基于拓扑排序结果判断是否存在环
// 若拓扑排序输出的任务数少于实际任务数，说明存在无法解析的循环依赖
bool DAGCompiler::has_cycle(const DAG& dag) {
    auto sorted = topological_sort(dag);
    return sorted.size() != dag.num_tasks();
}

// 拓扑排序：采用 Kahn 算法（基于入度的 BFS）
// 每次取出入度为 0 的任务加入结果，并递减其后继节点的入度
std::vector<TaskId> DAGCompiler::topological_sort(const DAG& dag) {
    std::vector<TaskId> result;
    auto in_degree = dag.in_degree();
    std::queue<TaskId> queue;

    // 初始化：将所有入度为 0 的任务（无依赖的起始任务）入队
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(id);
        }
    }

    while (!queue.empty()) {
        TaskId node = queue.front();
        queue.pop();
        result.push_back(node);

        // 遍历后继节点，削减入度；入度归零则可加入排序
        const auto& neighbors = dag.adjacency().at(node);
        for (const TaskId& neighbor : neighbors) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    return result;
}

// 执行分层：将任务按依赖关系划分到不同执行层
// 同一层的任务互相无依赖，可并行执行；任务所在层 = max(依赖所在层) + 1
std::vector<ExecutionLayer> DAGCompiler::build_layers(const DAG& dag, const std::vector<TaskId>& sorted_tasks) {
    std::vector<ExecutionLayer> layers;
    std::unordered_map<TaskId, size_t> task_to_layer;

    for (const TaskId& task_id : sorted_tasks) {
        size_t max_layer = 0;
        const auto& dependencies = dag.reverse_adjacency().at(task_id);

        // 当前任务的层级取决于其所有直接依赖的最大层级
        for (const TaskId& dep : dependencies) {
            auto it = task_to_layer.find(dep);
            if (it != task_to_layer.end()) {
                max_layer = std::max(max_layer, it->second + 1);
            }
        }

        task_to_layer[task_id] = max_layer;

        if (max_layer >= layers.size()) {
            layers.resize(max_layer + 1);
        }
        layers[max_layer].task_ids.push_back(task_id);
    }

    return layers;
}

// 编译入口：校验 DAG 合法性并生成可执行的执行计划
// 步骤：空图校验 → 环检测 → 拓扑排序 → 分层 → 构建任务层级与后继映射
ExecutionPlan DAGCompiler::compile(const DAG& dag) {
    if (dag.num_tasks() == 0) {
        throw std::invalid_argument("DAG contains no tasks");
    }

    if (has_cycle(dag)) {
        throw std::runtime_error("DAG contains cycles");
    }

    auto sorted_tasks = topological_sort(dag);
    auto layers = build_layers(dag, sorted_tasks);

    ExecutionPlan plan;
    plan.layers = std::move(layers);

    // 建立任务到层级的索引，便于快速查找
    for (size_t i = 0; i < plan.layers.size(); ++i) {
        for (const TaskId& task_id : plan.layers[i].task_ids) {
            plan.task_to_layer[task_id] = i;
        }
    }

    // 构建后继任务映射，用于执行时通知下游任务
    for (const auto& [from, tos] : dag.adjacency()) {
        for (const TaskId& to : tos) {
            plan.dependents[from].push_back(to);
        }
    }

    return plan;
}

}
