#pragma once

#include <task_graph/task.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_manager.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <mutex>
#include <functional>

namespace task_graph {

// 端口化依赖边：from/from_port 的输出连到 to/to_port 的输入。
// 默认端口名 "out"/"in" 兼容单输入/单输出场景的便捷用法。
struct Edge {
    TaskId      from;
    std::string from_port{"out"};
    TaskId      to;
    std::string to_port{"in"};
};

// DAG 变更事件：所有 mutation 操作后发出，供观察者（如 ViewModel）同步 UI 状态。
struct DAGChangeEvent {
    enum class Type {
        TaskAdded,
        TaskRemoved,
        TaskUpdated,
        EdgeAdded,
        EdgeRemoved,
        GraphReset,     // clear 后消费者需重新查询全部
    } type;

    // TaskAdded / TaskRemoved / TaskUpdated
    TaskId task_id;
    std::string task_type;

    // EdgeAdded / EdgeRemoved
    TaskId from;
    TaskId to;
    std::string from_port;
    std::string to_port;
};

class DAG {
public:
    DAG() = default;
    DAG(DAG&& other) noexcept { *this = std::move(other); }
    DAG& operator=(DAG&& other) noexcept;
    DAG(const DAG&) = delete;
    DAG& operator=(const DAG&) = delete;

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

    // 就地更新 task 的 config（params/priority/timeout 等）。
    // plugin task 会重建 IPluginTask 以保证 config 与 spec delegate 一致；
    // 普通 lambda Task 直接更新 config_。
    void update_task_config(const TaskId& id, const TaskConfig& config);
    void update_task_params(const TaskId& id, const TaskParams& params);

    // 增量删除 task（同时移除关联边）与 edge
    void remove_task(const TaskId& id);
    void remove_edge(const TaskId& from, const TaskId& to);

    // 清空所有 task 和 edge（发出 GraphReset 事件）
    void clear();

    // 用另一个 DAG 的数据替换当前内容（发出 GraphReset 事件，保留订阅者）
    void reset_from(DAG&& other);

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

    // ====== 值类型查询（推荐消费者使用，解耦内部存储布局）======
    bool has_edge(const TaskId& from, const TaskId& to) const;
    std::vector<TaskId> task_ids() const;
    std::optional<TaskConfig> task_config(const TaskId& id) const;
    std::string task_type(const TaskId& id) const;

    struct EdgeRef { TaskId from; TaskId to; };
    std::vector<EdgeRef> edge_list() const;

    // ====== 变更订阅 ======
    using ChangeCallback = std::function<void(const DAGChangeEvent&)>;
    size_t subscribe(ChangeCallback cb) const;
    void unsubscribe(size_t id) const;

private:
    std::unordered_map<TaskId, TaskPtr> tasks_;
    std::vector<Edge> edges_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> adjacency_;        // task 级去重视图
    std::unordered_map<TaskId, std::unordered_set<TaskId>> reverse_adjacency_;
    std::unordered_map<TaskId, size_t> in_degree_;

    // 端口级索引：task id -> 边在 edges_ 中的下标列表
    std::unordered_map<TaskId, std::vector<size_t>> incoming_idx_;
    std::unordered_map<TaskId, std::vector<size_t>> outgoing_idx_;

    // 插入顺序（保证 task_ids()/nodes() 返回稳定顺序）
    std::vector<TaskId> insertion_order_;

    // 观察者列表
    mutable std::mutex observers_mutex_;
    mutable std::vector<std::pair<size_t, ChangeCallback>> observers_;
    mutable size_t next_observer_id_{0};
    void notify(const DAGChangeEvent& e) const;

    // 内部删除（不发出事件，供 remove_task 批量删除边时使用）
    void remove_edge_impl(const TaskId& from, const TaskId& to);
};

using DAGPtr = std::shared_ptr<DAG>;

}
