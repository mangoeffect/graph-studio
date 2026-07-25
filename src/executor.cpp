#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/task_context.hpp>
#include <plugin_api.hpp>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include <vector>
#include <any>

namespace task_graph {

DAGExecutor::DAGExecutor(ExecutorConfig config)
    : thread_pool_(std::make_shared<ThreadPool>(config.thread_pool_size)) {}

DAGExecutor::~DAGExecutor() {
    cancel();
}

std::shared_future<void> DAGExecutor::execute(const DAG& dag) {
    if (running_) {
        TG_LOG_ERROR("Cannot execute DAG: executor is already running");
        throw std::runtime_error("Executor is already running");
    }

    TG_LOG_INFO("Starting DAG execution with " + std::to_string(dag.num_tasks()) + " tasks and " +
                std::to_string(dag.num_edges()) + " edges");

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
    if (!running_) {
        return;
    }
    
    cancelled_ = true;
    TG_LOG_WARN("DAG execution cancelled");
    
    if (execution_future_.valid()) {
        try {
            execution_future_.wait_for(std::chrono::seconds(1));
        } catch (...) {
            TG_LOG_ERROR("Error during cancel wait");
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
        TG_LOG_DEBUG("Initializing task dependencies tracking");
        
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

        size_t initial_ready = 0;
        for (const auto& [task_id, degree] : remaining_dependencies) {
            if (degree == 0) {
                ready_queue.push(task_id);
                initial_ready++;
            }
        }

        TG_LOG_INFO("Found " + std::to_string(initial_ready) + " tasks ready for immediate execution");

        std::vector<std::future<void>> futures;

        auto submit_task = [&](const TaskId& tid) {
            futures.push_back(thread_pool_->submit([this, &dag, tid, &remaining_dependencies, &dependents,
                                                   &queue_mutex, &queue_cv, &ready_queue, &completed_count]() {
                if (cancelled_) {
                    TG_LOG_DEBUG("Task '" + tid + "' skipped due to cancellation");
                    return;
                }

                TaskPtr task = dag.get_task(tid);
                if (!task) {
                    TG_LOG_ERROR("Task '" + tid + "' not found in DAG");
                    return;
                }

                auto it = dag.reverse_adjacency().find(tid);
                std::vector<TaskId> deps;
                if (it != dag.reverse_adjacency().end()) {
                    deps = std::vector<TaskId>(it->second.begin(), it->second.end());
                }

                std::unordered_map<TaskId, TaskResult> input_results;
                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    for (const auto& dep : deps) {
                        auto it = results_.find(dep);
                        if (it == results_.end() || it->second.status != TaskStatus::COMPLETED) {
                            TG_LOG_ERROR("Task '" + tid + "' dependency '" + dep + "' not completed");
                            results_[tid] = TaskResult{.status = TaskStatus::FAILED};
                            completed_count.fetch_add(1);
                            for (const TaskId& dependent : dependents[tid]) {
                                size_t remaining = remaining_dependencies[dependent].fetch_sub(1);
                                if (remaining == 1) {
                                    std::lock_guard<std::mutex> qlock(queue_mutex);
                                    ready_queue.push(dependent);
                                }
                            }
                            queue_cv.notify_one();
                            return;
                        }
                        input_results[dep] = it->second;
                    }
                }

                std::vector<std::any> inputs;
                for (const auto& [dep, result] : input_results) {
                    if (result.value.has_value()) {
                        inputs.push_back(result.value);
                    } else {
                        inputs.push_back(std::any());
                    }
                }

                CheckResult check_result = task->check_input(inputs);
                if (!check_result.success) {
                    TG_LOG_ERROR("Task '" + tid + "' input check failed: " + check_result.error_message);
                    {
                        std::lock_guard<std::mutex> lock(results_mutex_);
                        results_[tid] = TaskResult{.status = TaskStatus::FAILED};
                    }
                    completed_count.fetch_add(1);
                    for (const TaskId& dependent : dependents[tid]) {
                        size_t remaining = remaining_dependencies[dependent].fetch_sub(1);
                        if (remaining == 1) {
                            std::lock_guard<std::mutex> qlock(queue_mutex);
                            ready_queue.push(dependent);
                        }
                    }
                    queue_cv.notify_one();
                    return;
                }

                TG_LOG_DEBUG("Submitting task '" + tid + "' for execution");
                
                TaskContext task_ctx(task->config().params, deps, input_results);
                TaskResult result = task->execute(task_ctx);

                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_[tid] = result;
                }

                for (const TaskId& dependent : dependents[tid]) {
                    size_t remaining = remaining_dependencies[dependent].fetch_sub(1);
                    if (remaining == 1) {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        ready_queue.push(dependent);
                        TG_LOG_DEBUG("Task '" + dependent + "' becomes ready after dependency completion");
                        queue_cv.notify_one();
                    }
                }

                completed_count.fetch_add(1);
                queue_cv.notify_one();
            }));
        };

        TG_LOG_DEBUG("Starting task scheduling loop");

        while (completed_count < dag.num_tasks() && !cancelled_) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [&]() {
                return cancelled_ || !ready_queue.empty() || completed_count == dag.num_tasks();
            });

            if (cancelled_) {
                TG_LOG_INFO("Execution cancelled, exiting loop");
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

        TG_LOG_DEBUG("Waiting for all task futures to complete");

        for (auto& f : futures) {
            try {
                f.wait();
            } catch (...) {
                TG_LOG_ERROR("Error waiting for task future");
            }
        }

        TG_LOG_INFO("DAG execution completed: " + std::to_string(completed_count) + "/" + 
                    std::to_string(dag.num_tasks()) + " tasks completed");

    } catch (const std::exception& e) {
        running_ = false;
        TG_LOG_ERROR("DAG execution failed with exception: " + std::string(e.what()));
        throw;
    } catch (...) {
        running_ = false;
        TG_LOG_ERROR("DAG execution failed with unknown exception");
        throw;
    }

    running_ = false;
}

}
