#include <task_graph/compiler.hpp>
#include <queue>
#include <unordered_set>
#include <stdexcept>

namespace task_graph {

bool DAGCompiler::has_cycle(const DAG& dag) {
    auto sorted = topological_sort(dag);
    return sorted.size() != dag.num_tasks();
}

std::vector<TaskId> DAGCompiler::topological_sort(const DAG& dag) {
    std::vector<TaskId> result;
    auto in_degree = dag.in_degree();
    std::queue<TaskId> queue;

    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(id);
        }
    }

    while (!queue.empty()) {
        TaskId node = queue.front();
        queue.pop();
        result.push_back(node);

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

std::vector<ExecutionLayer> DAGCompiler::build_layers(const DAG& dag, const std::vector<TaskId>& sorted_tasks) {
    std::vector<ExecutionLayer> layers;
    std::unordered_map<TaskId, size_t> task_to_layer;

    for (const TaskId& task_id : sorted_tasks) {
        size_t max_layer = 0;
        const auto& dependencies = dag.reverse_adjacency().at(task_id);

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

    for (size_t i = 0; i < plan.layers.size(); ++i) {
        for (const TaskId& task_id : plan.layers[i].task_ids) {
            plan.task_to_layer[task_id] = i;
        }
    }

    for (const auto& [from, tos] : dag.adjacency()) {
        for (const TaskId& to : tos) {
            plan.dependents[from].push_back(to);
        }
    }

    return plan;
}

}
