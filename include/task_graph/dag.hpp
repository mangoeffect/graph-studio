#pragma once

#include <task_graph/task.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_manager.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>

namespace task_graph {

// 端口化依赖边：from/from_port 的输出连到 to/to_port 的输入。
// 默认端口名 "out"/"in" 兼容单输入/单输出场景的便捷用法。
struct Edge {
    TaskId      from;
    std::string from_port{"out"};
    TaskId      to;
    std::string to_port{"in"};
};

class DAG {
public:
    DAG() = default;

    void add_task(TaskPtr task);
    void add_task(const std::string& id, TaskPtr task);
    void add_plugin_task(const std::string& task_type);
    void add_plugin_task(const std::string& task_id, const std::string& task_type);
    void add_plugin_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config);

    // 端口化连接 API（推荐）
    void connect(const TaskId& from, std::string from_port,
                 const TaskId& to,   std::string to_port);

    // 便捷重载：默认 out→in，适合单端口 task
    void connect(const TaskId& from, const TaskId& to);

    // 旧 API 保留为 wrapper（构图期已不再推荐，但 examples/tests 大量使用）
    void add_dependency(const TaskId& from, const TaskId& to) { connect(from, to); }
    void add_dependencies(const TaskId& from, const std::vector<TaskId>& tos);

    bool has_task(const TaskId& id) const;
    TaskPtr get_task(const TaskId& id) const;
    void replace_task(const std::string& id, TaskPtr task);

    // ====== 图查询接口 ======
    const std::unordered_map<TaskId, TaskPtr>& tasks() const { return tasks_; }

    // task 级邻接（保留供旧代码使用：adjacency_ 仅按 task 去重）
    const std::unordered_map<TaskId, std::unordered_set<TaskId>>& adjacency() const { return adjacency_; }
    const std::unordered_map<TaskId, std::unordered_set<TaskId>>& reverse_adjacency() const { return reverse_adjacency_; }
    const std::unordered_map<TaskId, size_t>& in_degree() const { return in_degree_; }

    // 端口级查询（executor 用）：给定 task id 返回所有连入/连出的边
    std::vector<Edge> incoming_edges(const TaskId& tid) const;
    std::vector<Edge> outgoing_edges(const TaskId& tid) const;
    const std::vector<Edge>& edges() const { return edges_; }

    size_t num_tasks() const { return tasks_.size(); }
    size_t num_edges() const { return edges_.size(); }

private:
    std::unordered_map<TaskId, TaskPtr> tasks_;
    std::vector<Edge> edges_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> adjacency_;        // task 级去重视图
    std::unordered_map<TaskId, std::unordered_set<TaskId>> reverse_adjacency_;
    std::unordered_map<TaskId, size_t> in_degree_;

    // 端口级索引：task id → 边在 edges_ 中的下标列表
    std::unordered_map<TaskId, std::vector<size_t>> incoming_idx_;
    std::unordered_map<TaskId, std::vector<size_t>> outgoing_idx_;
};

using DAGPtr = std::shared_ptr<DAG>;

}
