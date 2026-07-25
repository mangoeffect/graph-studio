#pragma once

#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/context.hpp>
#include <unordered_map>
#include <atomic>
#include <future>
#include <vector>
#include <functional>

namespace task_graph {

struct ExecutorConfig {
    size_t thread_pool_size{std::thread::hardware_concurrency()};
    std::chrono::milliseconds timeout{0};
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

private:
    void run(const DAG& dag);
    void process_task(const DAG& dag, const ExecutionPlan& plan, const TaskId& task_id);

    ThreadPoolPtr thread_pool_;
    ExecutionContextPtr context_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    std::shared_future<void> execution_future_;

    mutable std::mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;
};

using DAGExecutorPtr = std::shared_ptr<DAGExecutor>;

}
