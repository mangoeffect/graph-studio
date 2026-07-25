#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/context.hpp>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include <vector>

namespace task_graph {

DAGExecutor::DAGExecutor(ExecutorConfig config)
    : thread_pool_(std::make_shared<ThreadPool>(config.thread_pool_size)),
      context_(std::make_shared<ExecutionContext>()) {}

DAGExecutor::~DAGExecutor() {
    cancel();
}

std::shared_future<void> DAGExecutor::execute(const DAG& dag) {
    if (running_) {
        throw std::runtime_error("Executor is already running");
    }

    running_ = true;
    cancelled_ = false;
    results_.clear();

    execution_future_ = std::async(std::launch::async, &DAGExecutor::run, this, std::cref(dag));
    return std::move(execution_future_);
}

void DAGExecutor::wait() {
    if (execution_future_.valid()) {
        execution_future_.wait();
    }
}

void DAGExecutor::cancel() {
    cancelled_ = true;
    if (execution_future_.valid()) {
        try {
            execution_future_.wait_for(std::chrono::seconds(1));
        } catch (...) {
        }
    }
    running_ = false;
}

std::unordered_map<TaskId, TaskResult> DAGExecutor::get_results() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
}

void DAGExecutor::run(const DAG& dag) {
    try {
        std::unordered_map<TaskId, std::atomic<size_t>> remaining_dependencies;
        std::unordered_map<TaskId, std::vector<TaskId>> dependents;

        for (const auto& [task_id, task] : dag.tasks()) {
            remaining_dependencies[task_id] = dag.in_degree().at(task_id);
        }

        for (const auto& [from, tos] : dag.adjacency()) {
            for (const TaskId& to : tos) {
                dependents[from].push_back(to);
            }
        }

        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::queue<TaskId> ready_queue;
        std::atomic<size_t> completed_count{0};

        for (const auto& [task_id, degree] : remaining_dependencies) {
            if (degree == 0) {
                ready_queue.push(task_id);
            }
        }

        std::vector<std::future<void>> futures;

        auto submit_task = [&](const TaskId& tid) {
            futures.push_back(thread_pool_->submit([this, &dag, tid, &remaining_dependencies, &dependents,
                                                   &queue_mutex, &queue_cv, &ready_queue, &completed_count]() {
                if (cancelled_) {
                    return;
                }

                TaskPtr task = dag.get_task(tid);
                if (!task) {
                    return;
                }

                TaskResult result = task->execute(*context_);

                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_[tid] = result;
                }
                context_->set_result(tid, result);

                for (const TaskId& dependent : dependents[tid]) {
                    size_t remaining = remaining_dependencies[dependent].fetch_sub(1);
                    if (remaining == 1) {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        ready_queue.push(dependent);
                        queue_cv.notify_one();
                    }
                }

                completed_count.fetch_add(1);
                queue_cv.notify_one();
            }));
        };

        while (completed_count < dag.num_tasks() && !cancelled_) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [&]() {
                return cancelled_ || !ready_queue.empty() || completed_count == dag.num_tasks();
            });

            if (cancelled_) {
                break;
            }

            if (completed_count == dag.num_tasks()) {
                break;
            }

            while (!ready_queue.empty()) {
                TaskId task_id = ready_queue.front();
                ready_queue.pop();
                lock.unlock();

                submit_task(task_id);

                lock.lock();
            }
        }

        for (auto& f : futures) {
            try {
                f.wait();
            } catch (...) {
            }
        }

    } catch (...) {
        running_ = false;
        throw;
    }

    running_ = false;
}

}
