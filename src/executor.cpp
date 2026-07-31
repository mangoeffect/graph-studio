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
    : thread_pool_(std::make_shared<ThreadPool>(config.thread_pool_size)),
      config_(std::move(config)) {}

DAGExecutor::~DAGExecutor() {
    cancel();
}

// 触发任务级性能事件：同时通知用户回调和内置 collector
void DAGExecutor::emit_task_event(const std::string& task_id, const std::string& task_type,
                                   ProfilePhase phase, std::chrono::nanoseconds duration) {
    if (!config_.enable_profiling && !config_.profile_callback) {
        return;
    }
    TaskProfileEvent event{
        task_id, task_type, phase, std::chrono::steady_clock::now(), duration
    };
    if (config_.enable_profiling) {
        profiler_.on_task_event(event);
    }
    if (config_.profile_callback) {
        config_.profile_callback(event);
    }
}

// 触发 DAG 级性能事件
void DAGExecutor::emit_dag_event(DagProfilePhase phase, size_t total_tasks) {
    if (!config_.enable_profiling && !config_.dag_profile_callback) {
        return;
    }
    DagProfileEvent event{phase, std::chrono::steady_clock::now(), total_tasks};
    if (config_.enable_profiling) {
        profiler_.on_dag_event(event);
    }
    if (config_.dag_profile_callback) {
        config_.dag_profile_callback(event);
    }
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

#ifdef __EMSCRIPTEN__
    // WASM：直接同步执行（避免主线程 std::async 在 singlethread build 下 abort；
    // 即便 wasm_multithread 也建议由 ThreadPool 在 Worker 内并行，主入口走同步）。
    // run() 内部仍可通过 ThreadPool 在 pthreads build 中并行调度 task。
    try {
        run(dag);
    } catch (...) {
        running_ = false;
        throw;
    }
    running_ = false;
    std::promise<void> done;
    done.set_value();
    execution_future_ = done.get_future().share();
#else
    execution_future_ = std::async(std::launch::async, &DAGExecutor::run, this, std::cref(dag));
#endif
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

// DAG 执行核心：基于动态就绪队列的任务调度
// 维护每个任务的剩余依赖计数，任务完成时递减下游依赖计数，归零即加入就绪队列
void DAGExecutor::run(const DAG& dag) {
    try {
        emit_dag_event(DagProfilePhase::STARTED, dag.num_tasks());

        // 预初始化所有 task（如 GPU shader 预编译），异常不阻断执行
        for (const auto& [task_id, task] : dag.tasks()) {
            try {
                task->init();
            } catch (...) {
                TG_LOG_WARN("Task '" + task_id + "' init() threw exception, ignoring");
            }
        }

        TG_LOG_DEBUG("Initializing task dependencies tracking");
        
        // remaining_dependencies：每个任务尚未完成的依赖数，归零表示可执行
        // dependents：每个任务完成后需要通知的下游任务列表
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

        // 就绪队列：存放所有依赖已满足、可立即执行的任务
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::queue<TaskId> ready_queue;
        std::atomic<size_t> completed_count{0};

        // 初始化就绪队列：将所有入度为 0 的无依赖任务加入
        size_t initial_ready = 0;
        for (const auto& [task_id, degree] : remaining_dependencies) {
            if (degree == 0) {
                ready_queue.push(task_id);
                initial_ready++;
            }
        }

        TG_LOG_INFO("Found " + std::to_string(initial_ready) + " tasks ready for immediate execution");

        std::vector<std::future<void>> futures;

        // 统一的失败传播：写 FAILED 结果、触发事件、递减下游依赖计数、唤醒调度
        auto fail_and_propagate = [&](const TaskId& tid,
                                      const std::string& task_type,
                                      const std::string& reason) {
            TG_LOG_ERROR("Task '" + tid + "' failed: " + reason);
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_[tid] = TaskResult{.status = TaskStatus::FAILED};
            }
            completed_count.fetch_add(1);
            emit_task_event(tid, task_type, ProfilePhase::FAILED);
            for (const TaskId& dependent : dependents[tid]) {
                size_t remaining = remaining_dependencies[dependent].fetch_sub(1);
                if (remaining == 1) {
                    std::lock_guard<std::mutex> qlock(queue_mutex);
                    ready_queue.push(dependent);
                }
            }
            queue_cv.notify_one();
        };

        // 任务提交闭包：封装单个任务的完整执行流程
        auto submit_task = [&](const TaskId& tid) {
            futures.push_back(thread_pool_->submit([this, &dag, tid, &remaining_dependencies, &dependents,
                                                    &queue_mutex, &queue_cv, &ready_queue, &completed_count,
                                                    &fail_and_propagate]() {
                TaskPtr task = dag.get_task(tid);

                if (cancelled_) {
                    TG_LOG_DEBUG("Task '" + tid + "' skipped due to cancellation");
                    emit_task_event(tid, task ? task->type() : std::string{}, ProfilePhase::SKIPPED);
                    return;
                }

                if (!task) {
                    TG_LOG_ERROR("Task '" + tid + "' not found in DAG");
                    return;
                }

                // READY：任务已被调度器取出，进入处理流程
                emit_task_event(tid, task->type(), ProfilePhase::READY);

                // === Step 1: 取上游 TaskResult，做成功性检查 ===
                const auto in_edges = dag.incoming_edges(tid);
                std::vector<TaskId> deps;
                deps.reserve(in_edges.size());
                for (const auto& e : in_edges) {
                    if (std::find(deps.begin(), deps.end(), e.from) == deps.end()) {
                        deps.push_back(e.from);
                    }
                }

                std::unordered_map<TaskId, TaskResult> input_results;
                std::string failed_dep;
                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    for (const auto& dep : deps) {
                        auto it = results_.find(dep);
                        if (it == results_.end() || it->second.status != TaskStatus::COMPLETED) {
                            failed_dep = dep;
                            break;
                        }
                        input_results[dep] = it->second;
                    }
                }
                // 锁外调用 fail_and_propagate（避免与 results_mutex_ 死锁）
                if (!failed_dep.empty()) {
                    fail_and_propagate(tid, task->type(),
                        "dependency '" + failed_dep + "' not completed");
                    return;
                }

                // === Step 2: 按 Edge.to_port 构造绑定输入（确定性顺序，由 edge 顺序决定）===
                // 上游输出的解析：优先 outputs[port]，回退 value（视为 "out"）。
                // 若上游既无 outputs 也无 value，绑定空 any（task 可选用 nullopt 处理）。
                auto resolve_upstream = [](const TaskResult& r,
                                           const std::string& port) -> std::any {
                    if (!r.outputs.empty()) {
                        auto it = r.outputs.find(port);
                        if (it != r.outputs.end()) return it->second;
                        // outputs 非空但找不到 port：返回空 any（视为缺数据）
                        return std::any{};
                    }
                    // outputs 为空：value 视为 "out" 端口（兼容旧 TaskResult）
                    if (r.value.has_value()) return r.value;
                    return std::any{};
                };

                std::unordered_map<std::string, std::any> inputs_by_port;
                for (const auto& e : in_edges) {
                    auto it = input_results.find(e.from);
                    if (it == input_results.end()) {
                        fail_and_propagate(tid, task->type(),
                            "internal error: upstream '" + e.from + "' missing from snapshot");
                        return;
                    }
                    inputs_by_port[e.to_port] = resolve_upstream(it->second, e.from_port);
                }

                // === Step 3: 输入校验（按 port 的 map） ===
                CheckResult check_result = task->check_input(inputs_by_port);
                if (!check_result.success) {
                    fail_and_propagate(tid, task->type(),
                        "input check failed: " + check_result.error_message);
                    return;
                }

                TG_LOG_DEBUG("Submitting task '" + tid + "' for execution");

                // === Step 4: 构造 TaskContext 执行 ===
                emit_task_event(tid, task->type(), ProfilePhase::STARTED);
                auto exec_start = std::chrono::steady_clock::now();

                TaskContext task_ctx(task->config().params, deps, input_results,
                                     std::move(inputs_by_port));
                TaskResult result = task->execute(task_ctx);

                auto exec_duration = std::chrono::steady_clock::now() - exec_start;
                result.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(exec_duration);

                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_[tid] = result;
                }

                // COMPLETED/FAILED：依据执行结果触发，附带 execute 耗时
                emit_task_event(tid, task->type(),
                                result.is_success() ? ProfilePhase::COMPLETED : ProfilePhase::FAILED,
                                result.duration);

                // === Step 5: 推进下游调度 ===
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

        // 主调度循环：持续运行直到所有任务完成或被取消
        while (completed_count < dag.num_tasks() && !cancelled_) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            // 阻塞等待：直到有任务就绪、全部完成或被取消
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

            // 批量取出当前所有就绪任务并提交到线程池并行执行
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

        emit_dag_event(DagProfilePhase::COMPLETED, dag.num_tasks());

    } catch (const std::exception& e) {
        running_ = false;
        emit_dag_event(DagProfilePhase::COMPLETED, dag.num_tasks());
        TG_LOG_ERROR("DAG execution failed with exception: " + std::string(e.what()));
        if (config_.completion_callback) config_.completion_callback();
        throw;
    } catch (...) {
        running_ = false;
        emit_dag_event(DagProfilePhase::COMPLETED, dag.num_tasks());
        TG_LOG_ERROR("DAG execution failed with unknown exception");
        if (config_.completion_callback) config_.completion_callback();
        throw;
    }

    running_ = false;
    if (config_.completion_callback) config_.completion_callback();
}

}
