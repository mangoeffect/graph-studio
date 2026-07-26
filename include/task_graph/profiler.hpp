#pragma once

#include <plugin_api.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <atomic>
#include <mutex>

namespace task_graph {

// 任务生命周期阶段
enum class ProfilePhase {
    READY,      // 任务就绪，已加入就绪队列
    STARTED,    // 任务实际开始执行
    COMPLETED,  // 任务成功完成
    FAILED,     // 任务执行失败（含输入校验失败、依赖未完成等）
    SKIPPED     // 任务因取消跳过
};

// 单次性能事件：executor 在任务生命周期各节点产生
struct TaskProfileEvent {
    std::string task_id;
    std::string task_type;
    ProfilePhase phase;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::nanoseconds duration{0};  // 仅 COMPLETED/FAILED 有效（execute() 纯计算耗时）
};

// DAG 级性能事件
enum class DagProfilePhase {
    STARTED,    // DAG 开始执行
    COMPLETED   // DAG 执行结束
};

struct DagProfileEvent {
    DagProfilePhase phase;
    std::chrono::steady_clock::time_point timestamp;
    size_t total_tasks{0};
};

// 性能事件回调：用户可自定义（打印、写文件、接入外部系统）
using ProfileCallback = std::function<void(const TaskProfileEvent&)>;
using DagProfileCallback = std::function<void(const DagProfileEvent&)>;

// 单个任务的聚合统计
struct TaskStats {
    std::string task_id;
    std::string task_type;
    TaskStatus final_status{TaskStatus::PENDING};  // 复用框架 TaskStatus
    std::chrono::nanoseconds wait_duration{0};     // 就绪 → 开始执行的等待耗时
    std::chrono::nanoseconds exec_duration{0};     // execute() 纯计算耗时
    std::chrono::nanoseconds total_duration{0};    // 就绪 → 完成/失败 的总耗时
    std::chrono::steady_clock::time_point ready_time;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    bool has_ready{false};
    bool has_start{false};
    bool has_end{false};
};

// DAG 级聚合统计
struct DagStats {
    std::chrono::nanoseconds total_duration{0};     // wall time
    size_t total_tasks{0};
    size_t completed_tasks{0};
    size_t failed_tasks{0};
    size_t skipped_tasks{0};
    std::chrono::nanoseconds critical_path{0};      // 关键路径（最长依赖链）耗时
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    bool has_start{false};
    bool has_end{false};
};

// 默认 ProfileCollector：聚合所有事件，支持 JSON 导出
// 线程安全：所有接口加锁，可在多线程 executor 中安全调用
class ProfileCollector {
public:
    ProfileCollector() = default;

    // 事件采集接口（由 executor 回调，多线程安全）
    void on_task_event(const TaskProfileEvent& event);
    void on_dag_event(const DagProfileEvent& event);

    // 聚合结果查询
    const std::vector<TaskProfileEvent>& raw_events() const { return raw_events_; }
    std::vector<TaskStats> compute_task_stats() const;
    DagStats compute_dag_stats() const;

    // JSON 导出（包含 DAG 概览 + 每个任务的统计 + 原始事件流）
    std::string to_json_string(bool pretty = true) const;

    // Chrome Trace Event 格式导出（.json）：可在 chrome://tracing 或 Perfetto 打开
    // 每个任务产生两个 complete 事件：等待段（READY→STARTED）与执行段（STARTED→END）
    std::string to_trace_string(bool pretty = false) const;

    // 文件落盘：成功返回 true
    bool save_json_report(const std::string& filepath, bool pretty = true) const;
    bool save_trace_report(const std::string& filepath) const;

private:
    mutable std::mutex mutex_;
    std::vector<TaskProfileEvent> raw_events_;
    std::optional<DagProfileEvent> dag_start_;
    std::optional<DagProfileEvent> dag_end_;

    // 内部实现：不加锁版本（由持有锁的公有方法调用）
    std::vector<TaskStats> compute_task_stats_impl() const;
};

}  // namespace task_graph
