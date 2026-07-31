#pragma once

#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/profiler.hpp>
#include <unordered_map>
#include <atomic>
#include <future>
#include <vector>
#include <functional>

namespace task_graph {

struct ExecutorConfig {
    size_t thread_pool_size{std::thread::hardware_concurrency()};
    std::chrono::milliseconds timeout{0};

    // 性能 profiler 配置
    bool enable_profiling{false};              // 是否启用 profiler 埋点
    ProfileCallback profile_callback;          // 任务级事件回调（可选，自定义处理）
    DagProfileCallback dag_profile_callback;   // DAG 级事件回调（可选）

    // 执行完成回调（在 executor 线程触发，调用方需自行 marshal 回 UI 线程）
    std::function<void()> completion_callback;
};

class DAGExecutor {
public:
    DAGExecutor(ExecutorConfig config = {});
    ~DAGExecutor();

    std::shared_future<void> execute(const DAG& dag);
    void wait();

    void cancel();
    bool is_running() const { return running_; }

    std::unordered_map<TaskId, TaskResult> get_results() const;

    // 获取内置 ProfileCollector（始终可用；enable_profiling=true 时才会采集数据）
    const ProfileCollector& profiler() const { return profiler_; }

private:
    void run(const DAG& dag);
    void process_task(const DAG& dag, const ExecutionPlan& plan, const TaskId& task_id);

    // 触发性能事件（enable_profiling 为 false 时无开销）
    void emit_task_event(const std::string& task_id, const std::string& task_type,
                         ProfilePhase phase, std::chrono::nanoseconds duration = std::chrono::nanoseconds{0});
    void emit_dag_event(DagProfilePhase phase, size_t total_tasks);

    ThreadPoolPtr thread_pool_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    std::shared_future<void> execution_future_;

    mutable std::mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    ExecutorConfig config_;
    ProfileCollector profiler_;
};

using DAGExecutorPtr = std::shared_ptr<DAGExecutor>;

}
