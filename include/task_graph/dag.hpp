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

struct Edge {
    TaskId from;
    TaskId to;
};

class DAG {
public:
    DAG() = default;

    void add_task(TaskPtr task);
    void add_task(const std::string& id, TaskPtr task);
    void add_plugin_task(const std::string& task_type);
    void add_plugin_task(const std::string& task_id, const std::string& task_type);
    void add_plugin_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config);
    void add_dependency(const TaskId& from, const TaskId& to);
    void add_dependencies(const TaskId& from, const std::vector<TaskId>& tos);

    bool has_task(const TaskId& id) const;
    TaskPtr get_task(const TaskId& id) const;
    void replace_task(const std::string& id, TaskPtr task);

    const std::unordered_map<TaskId, TaskPtr>& tasks() const { return tasks_; }
    const std::unordered_map<TaskId, std::unordered_set<TaskId>>& adjacency() const { return adjacency_; }
    const std::unordered_map<TaskId, std::unordered_set<TaskId>>& reverse_adjacency() const { return reverse_adjacency_; }
    const std::unordered_map<TaskId, size_t>& in_degree() const { return in_degree_; }

    size_t num_tasks() const { return tasks_.size(); }
    size_t num_edges() const { return edges_.size(); }

private:
    std::unordered_map<TaskId, TaskPtr> tasks_;
    std::vector<Edge> edges_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> adjacency_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> reverse_adjacency_;
    std::unordered_map<TaskId, size_t> in_degree_;
};

using DAGPtr = std::shared_ptr<DAG>;

}
