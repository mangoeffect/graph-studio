#include <task_graph/compiler.hpp>
#include <task_graph/task.hpp>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>

namespace task_graph {

// 环检测：基于拓扑排序结果判断是否存在环
// 若拓扑排序输出的任务数少于实际任务数，说明存在无法解析的循环依赖
bool DAGCompiler::has_cycle(const DAG& dag) const {
    auto sorted = topological_sort(dag);
    return sorted.size() != dag.num_tasks();
}

// 拓扑排序：采用 Kahn 算法（基于入度的 BFS）
// 每次取出入度为 0 的任务加入结果，并递减其后继节点的入度
std::vector<TaskId> DAGCompiler::topological_sort(const DAG& dag) const {
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
        auto it = dag.adjacency().find(node);
        if (it == dag.adjacency().end()) continue;  // 叶子节点无后继
        for (const TaskId& neighbor : it->second) {
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
std::vector<ExecutionLayer> DAGCompiler::build_layers(const DAG& dag, const std::vector<TaskId>& sorted_tasks) const {
    std::vector<ExecutionLayer> layers;
    std::unordered_map<TaskId, size_t> task_to_layer;

    for (const TaskId& task_id : sorted_tasks) {
        size_t max_layer = 0;
        auto rit = dag.reverse_adjacency().find(task_id);
        if (rit != dag.reverse_adjacency().end()) {
            for (const TaskId& dep : rit->second) {
                auto it = task_to_layer.find(dep);
                if (it != task_to_layer.end()) {
                    max_layer = std::max(max_layer, it->second + 1);
                }
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

// 在 specs 中查找指定名称的端口
static const PortSpec* find_spec(const std::vector<PortSpec>& specs,
                                  const std::string& name) {
    for (const auto& s : specs) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

std::vector<ValidationError> DAGCompiler::validate(const DAG& dag) const {
    std::vector<ValidationError> errs;

    if (dag.num_tasks() == 0) {
        errs.push_back({ValidationError::Severity::ERROR, "", "",
                        "DAG contains no tasks"});
        return errs;
    }

    // 1) 环检测
    if (has_cycle(dag)) {
        errs.push_back({ValidationError::Severity::ERROR, "", "",
                        "DAG contains cycles"});
        // 有环时拓扑排序不完整，但端口校验仍可继续（按现有边检查）
    }

    // 收集每个 (to, to_port) 的来源数量，用于检测多源冲突
    std::unordered_map<std::string, size_t> port_source_count;
    for (const auto& e : dag.edges()) {
        std::string key = e.to + ":" + e.to_port;
        port_source_count[key]++;
    }

    for (const auto& [tid, task] : dag.tasks()) {
        const auto in_edges = dag.incoming_edges(tid);
        const auto in_specs  = task->input_specs();
        const auto out_specs = task->output_specs();

        // 2) 必填 input port 必须有 incoming edge
        for (const auto& spec : in_specs) {
            if (!spec.required) continue;
            bool found = std::any_of(in_edges.begin(), in_edges.end(),
                [&](const Edge& e){ return e.to_port == spec.name; });
            if (!found) {
                errs.push_back({ValidationError::Severity::ERROR, tid, spec.name,
                                "required input port '" + spec.name + "' is not connected"});
            }
        }

        // 3) 已连接端口的类型与契约一致性
        for (const auto& e : in_edges) {
            const auto* in_spec = find_spec(in_specs, e.to_port);

            // 3a) 连到未声明的 input port（WARNING）
            if (!in_spec) {
                errs.push_back({ValidationError::Severity::WARNING, tid, e.to_port,
                    "edge connects to undeclared input port '" + e.to_port +
                    "' (task '" + tid + "' did not declare it)"});
                continue;
            }

            // 3b) 类型匹配：from_port 类型与 to_port 类型，若两边都注册了
            auto from_task = dag.get_task(e.from);
            if (!from_task) continue;
            // 注意：output_specs() 返回 by-value，必须存到局部变量再取地址，
            // 否则 find_spec 返回的指针指向已析构的临时对象（UB）。
            auto from_out_specs = from_task->output_specs();
            const auto* out_spec = find_spec(from_out_specs, e.from_port);
            if (!out_spec) {
                // 上游未声明 output_specs（多数 Task 不声明）：放过，不强约束
                continue;
            }
            if (!out_spec->type_name.empty() && !in_spec->type_name.empty()
                && out_spec->type_name != in_spec->type_name) {
                errs.push_back({ValidationError::Severity::ERROR, tid, e.to_port,
                    "type mismatch on port '" + e.to_port + "': upstream '" +
                    e.from + ":" + e.from_port + "' provides " + out_spec->type_name +
                    ", expected " + in_spec->type_name});
            }
        }

        // 4) 多条 edge 写同一 to_port（WARNING）
        for (const auto& e : in_edges) {
            std::string key = e.to + ":" + e.to_port;
            if (port_source_count[key] > 1) {
                // 只为每个 (to, to_port) 报一次 warning
                if (std::none_of(errs.begin(), errs.end(), [&](const ValidationError& ve) {
                        return ve.task_id == tid && ve.port_name == e.to_port &&
                               ve.message.find("multiple sources") != std::string::npos;
                    })) {
                    errs.push_back({ValidationError::Severity::WARNING, tid, e.to_port,
                        "input port '" + e.to_port + "' has " +
                        std::to_string(port_source_count[key]) +
                        " sources; last-write-wins at runtime"});
                }
                break;
            }
        }
    }

    return errs;
}

bool DAGCompiler::has_errors(const DAG& dag) const {
    auto errs = validate(dag);
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::ERROR) return true;
    }
    return false;
}

}
