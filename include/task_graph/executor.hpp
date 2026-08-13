#pragma once

#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/profiler.hpp>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <future>
#include <vector>
#include <functional>

namespace task_graph {

// 统一执行事件：替代 profile_callback + dag_profile_callback + completion_callback
struct ExecutionEvent {
    enum class Type {
        DagStarted,
        DagCompleted,
        TaskReady,
        TaskStarted,
        TaskCompleted,
        TaskFailed,
        TaskSkipped,
    } type;

    // Task 事件
    TaskId task_id;
    std::string task_type;
    std::chrono::nanoseconds duration{0};
    std::string failure_reason;          // TaskFailed 时携带原因

    // DagCompleted 事件
    size_t total_tasks{0};
    size_t completed_tasks{0};
    size_t failed_tasks{0};
    std::string exception_message;       // DAG 异常时的 what()

    std::chrono::steady_clock::time_point timestamp;
};

using ExecutionCallback = std::function<void(const ExecutionEvent&)>;

struct ExecutorConfig {
    size_t thread_pool_size{std::thread::hardware_concurrency()};
    std::chrono::milliseconds timeout{0};

    // 统一事件回调（在 executor 线程触发，调用方需自行 marshal 回 UI 线程）
    ExecutionCallback callback;

    // 是否启用内置 ProfileCollector 采集（通过 profiler() 只读访问）
    bool enable_profiling{false};
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

    // 流式模式（视频等时序源）：检测 IStreamSource 并在 run() 入口提前派发。
    // 返回 true 表示已处理（stream 模式），run() 直接返回；false 表示无源，
    // 回退到原 one-shot 路径。v1 仅支持单个源 + cone 内含汇才进入 stream。
    bool try_run_stream(const DAG& dag);
    void run_stream(const DAG& dag, NodePtr src, IStreamSource* stream_src,
                    const std::vector<std::pair<NodePtr, IStreamSink*>>& sinks,
                    const std::unordered_set<TaskId>& cone);
    // 同步执行单个 task（stream 模式用，按拓扑序逐个调用）：
    // 从 results_ 取上游、check_input、execute、存回 results_、emit 事件。
    TaskResult execute_one(const DAG& dag, const TaskId& tid);

    // 触发统一执行事件（同时喂给 ProfileCollector 和用户 callback）
    void emit_event(const ExecutionEvent& e);
    void emit_event(ExecutionEvent::Type type, const std::string& task_id = {},
                    const std::string& task_type = {},
                    std::chrono::nanoseconds duration = std::chrono::nanoseconds{0},
                    const std::string& failure_reason = {});

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
