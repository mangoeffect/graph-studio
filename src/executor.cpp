#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <task_graph/thread_pool.hpp>
#include <task_graph/task_context.hpp>
#include <plugin_api.hpp>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <condition_variable>
#include <vector>
#include <any>
#include <algorithm>

namespace task_graph {

DAGExecutor::DAGExecutor(ExecutorConfig config)
    : thread_pool_(std::make_shared<ThreadPool>(config.thread_pool_size)),
      config_(std::move(config)) {}

DAGExecutor::~DAGExecutor() {
    cancel();
}

// 触发统一执行事件：喂给 ProfileCollector（如启用）和用户 callback
void DAGExecutor::emit_event(const ExecutionEvent& e) {
    if (config_.enable_profiling) {
        // 适配为 ProfileCollector 的内部事件类型
        if (e.type == ExecutionEvent::Type::DagStarted) {
            profiler_.on_dag_event({DagProfilePhase::STARTED, e.timestamp, e.total_tasks});
        } else if (e.type == ExecutionEvent::Type::DagCompleted) {
            profiler_.on_dag_event({DagProfilePhase::COMPLETED, e.timestamp, e.total_tasks});
        } else {
            ProfilePhase phase = ProfilePhase::READY;
            switch (e.type) {
                case ExecutionEvent::Type::TaskReady:    phase = ProfilePhase::READY; break;
                case ExecutionEvent::Type::TaskStarted:  phase = ProfilePhase::STARTED; break;
                case ExecutionEvent::Type::TaskCompleted: phase = ProfilePhase::COMPLETED; break;
                case ExecutionEvent::Type::TaskFailed:   phase = ProfilePhase::FAILED; break;
                case ExecutionEvent::Type::TaskSkipped:  phase = ProfilePhase::SKIPPED; break;
                default: break;
            }
            profiler_.on_task_event({e.task_id, e.task_type, phase, e.timestamp, e.duration});
        }
    }
    if (config_.callback) {
        config_.callback(e);
    }
}

void DAGExecutor::emit_event(ExecutionEvent::Type type, const std::string& task_id,
                              const std::string& task_type,
                              std::chrono::nanoseconds duration,
                              const std::string& failure_reason) {
    ExecutionEvent e;
    e.type = type;
    e.task_id = task_id;
    e.task_type = task_type;
    e.duration = duration;
    e.failure_reason = failure_reason;
    e.timestamp = std::chrono::steady_clock::now();
    emit_event(e);
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

// ====== stream 模式辅助（匿名命名空间，无实例状态）======
namespace {

// 从 root 沿 outgoing edges 的传递可达闭包（含 root 自身）。
std::unordered_set<TaskId> transitive_downstream(const DAG& dag, const TaskId& root) {
    std::unordered_set<TaskId> seen{root};
    std::queue<TaskId> q;
    q.push(root);
    while (!q.empty()) {
        TaskId cur = q.front();
        q.pop();
        for (const auto& e : dag.outgoing_edges(cur)) {
            if (seen.insert(e.to).second) q.push(e.to);
        }
    }
    return seen;
}

// 对 task 子集做拓扑排序（Kahn），仅计子集内部边。按 dag.task_ids()（插入序）
// 入队以保证跨平台确定性。
std::vector<TaskId> topo_sort_subset(const DAG& dag,
                                     const std::unordered_set<TaskId>& subset) {
    std::unordered_map<TaskId, size_t> indeg;
    std::unordered_map<TaskId, std::vector<TaskId>> adj;
    for (const auto& tid : subset) {
        indeg[tid] = 0;
        adj[tid] = {};
    }
    for (const auto& tid : subset) {
        for (const auto& e : dag.outgoing_edges(tid)) {
            if (subset.count(e.to)) {
                adj[tid].push_back(e.to);
                indeg[e.to]++;
            }
        }
    }
    std::queue<TaskId> q;
    for (const auto& tid : dag.task_ids()) {
        if (subset.count(tid) && indeg[tid] == 0) q.push(tid);
    }
    std::vector<TaskId> order;
    while (!q.empty()) {
        TaskId cur = q.front();
        q.pop();
        order.push_back(cur);
        for (const auto& nb : adj[cur]) {
            if (--indeg[nb] == 0) q.push(nb);
        }
    }
    return order;
}

}  // namespace

// 同步执行单个 task（stream 模式按拓扑序逐个调用）。逻辑等价于 one-shot 路径的
// submit_task 核心：取上游、check_input、execute、存回 results_、emit 事件，
// 但不做就绪队列推进（顺序由调用方用 topo 序保证）。
TaskResult DAGExecutor::execute_one(const DAG& dag, const TaskId& tid) {
    TaskPtr task = dag.get_task(tid);
    if (!task) {
        TG_LOG_ERROR("execute_one: task '" + tid + "' not found");
        return TaskResult{.status = TaskStatus::FAILED};
    }

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

    auto fail_store = [&](const std::string& reason) -> TaskResult {
        TaskResult r{.status = TaskStatus::FAILED};
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_[tid] = r;
        }
        emit_event(ExecutionEvent::Type::TaskFailed, tid, task->type(),
                   std::chrono::nanoseconds{0}, reason);
        return r;
    };

    if (!failed_dep.empty()) {
        return fail_store("dependency '" + failed_dep + "' not completed");
    }

    // 按 Edge.to_port 绑定输入（与 one-shot 路径一致的 resolve 逻辑）
    auto resolve_upstream = [](const TaskResult& r, const std::string& port) -> std::any {
        if (!r.outputs.empty()) {
            auto it = r.outputs.find(port);
            if (it != r.outputs.end()) return it->second;
            return std::any{};
        }
        if (r.value.has_value()) return r.value;
        return std::any{};
    };

    std::unordered_map<std::string, std::any> inputs_by_port;
    for (const auto& e : in_edges) {
        auto it = input_results.find(e.from);
        if (it == input_results.end()) {
            return fail_store("internal error: upstream '" + e.from + "' missing from snapshot");
        }
        inputs_by_port[e.to_port] = resolve_upstream(it->second, e.from_port);
    }

    CheckResult check_result = task->check_input(inputs_by_port);
    if (!check_result.success) {
        return fail_store("input check failed: " + check_result.error_message);
    }

    emit_event(ExecutionEvent::Type::TaskStarted, tid, task->type());
    auto exec_start = std::chrono::steady_clock::now();
    TaskContext task_ctx(task->config().params, deps, input_results, std::move(inputs_by_port));
    TaskResult result = task->execute(task_ctx);
    result.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - exec_start);

    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_[tid] = result;
    }
    if (result.is_success()) {
        emit_event(ExecutionEvent::Type::TaskCompleted, tid, task->type(), result.duration);
    } else {
        emit_event(ExecutionEvent::Type::TaskFailed, tid, task->type(), result.duration);
    }
    return result;
}

bool DAGExecutor::try_run_stream(const DAG& dag) {
    NodePtr src;
    IStreamSource* stream_src = nullptr;
    std::vector<std::pair<NodePtr, IStreamSink*>> sinks;
    for (const auto& [id, task] : dag.tasks()) {
        if (auto* s = dynamic_cast<IStreamSource*>(task.get())) {
            if (!stream_src) {
                src = task;
                stream_src = s;
            } else {
                TG_LOG_WARN("stream mode: multiple IStreamSource not supported in v1; "
                            "using first ('" + src->id() + "'), ignoring '" + id + "'");
            }
        }
        if (auto* k = dynamic_cast<IStreamSink*>(task.get())) {
            sinks.emplace_back(task, k);
        }
    }
    if (!stream_src) return false;  // 无源 → one-shot

    std::unordered_set<TaskId> cone = transitive_downstream(dag, src->id());

    // 仅当 cone 内存在 IStreamSink 才进入 stream；否则回退 one-shot
    // （源 execute() 返回首帧，支持缩略图等单帧用例）。
    bool sink_in_cone = false;
    for (const auto& [t, k] : sinks) {
        if (cone.count(t->id())) { sink_in_cone = true; break; }
    }
    if (!sink_in_cone) return false;

    run_stream(dag, src, stream_src, sinks, cone);
    return true;
}

void DAGExecutor::run_stream(const DAG& dag, NodePtr src, IStreamSource* stream_src,
                             const std::vector<std::pair<NodePtr, IStreamSink*>>& sinks,
                             const std::unordered_set<TaskId>& cone) {
    TG_LOG_INFO("Stream mode: source='" + src->id() + "'");

    // 1) preamble = cone 之外的任务，跑一次，结果留在 results_ 供每帧复用。
    //    preamble 任一失败即视为致命：其下游 cone 任务会因取不到上游而级联失败，
    //    直接跳过帧循环（避免无谓的视频打开/缓冲开销）。
    std::unordered_set<TaskId> preamble;
    for (const auto& [id, t] : dag.tasks()) {
        if (!cone.count(id)) preamble.insert(id);
    }
    bool failed = false;
    for (const auto& tid : topo_sort_subset(dag, preamble)) {
        if (cancelled_) { failed = true; break; }
        TaskResult r = execute_one(dag, tid);
        if (r.is_failed()) {
            TG_LOG_ERROR("stream preamble task '" + tid + "' failed; aborting stream");
            failed = true;
            break;
        }
    }

    // 2) cone 内的 sink（仅这些需要 on_stream_end 收尾）
    std::vector<IStreamSink*> cone_sinks;
    for (const auto& [t, k] : sinks) {
        if (cone.count(t->id())) cone_sinks.push_back(k);
    }

    // 3) cone \ {src} 的拓扑执行序（每帧按此序重跑）
    std::unordered_set<TaskId> cone_exec_set = cone;
    cone_exec_set.erase(src->id());
    std::vector<TaskId> cone_exec = topo_sort_subset(dag, cone_exec_set);

    // 4) 帧循环：next_frame → 注入 results_ → 按 topo 序跑 cone
    //    源的每帧与 execute_one 一致地配对发出 TaskStarted/TaskCompleted 事件，
    //    保证事件消费者（profiler/GUI）看到平衡的起止。
    size_t frame_idx = 0;
    if (!failed) {
        stream_src->reset_stream();
    }
    while (!failed && !cancelled_) {
        TaskContext src_ctx(src->config().params, {}, {}, {});
        emit_event(ExecutionEvent::Type::TaskStarted, src->id(), src->type());
        auto fstart = std::chrono::steady_clock::now();
        TaskResult fr = stream_src->next_frame(src_ctx);
        fr.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - fstart);

        if (fr.status == TaskStatus::STREAM_END) {
            emit_event(ExecutionEvent::Type::TaskCompleted, src->id(), src->type(), fr.duration);
            break;
        }
        if (fr.is_failed()) {
            TG_LOG_ERROR("stream source '" + src->id() + "' failed at frame " +
                         std::to_string(frame_idx));
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_[src->id()] = fr;
            }
            emit_event(ExecutionEvent::Type::TaskFailed, src->id(), src->type(), fr.duration,
                       "next_frame failed");
            failed = true;
            break;
        }
        // 注入当前帧作为源的输出 → 下游 cone 自动消费（submit_task/execute_one 按
        // task id 从 results_ 取上游，故注入即可被识别）。
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_[src->id()] = fr;
        }
        emit_event(ExecutionEvent::Type::TaskCompleted, src->id(), src->type(), fr.duration);

        // 按 topo 序跑 cone；某帧失败时把该帧未及运行的下游 cone 任务标记为 FAILED，
        // 避免其残留上一帧的 COMPLETED 结果污染 get_results()/DagCompleted 统计
        // （对齐 one-shot 路径 fail_and_propagate 的语义）。
        for (size_t i = 0; i < cone_exec.size(); ++i) {
            if (cancelled_) {
                for (size_t j = i; j < cone_exec.size(); ++j) {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_[cone_exec[j]] = TaskResult{.status = TaskStatus::FAILED};
                }
                failed = true;
                break;
            }
            TaskResult r = execute_one(dag, cone_exec[i]);
            if (r.is_failed()) {
                TG_LOG_ERROR("stream frame " + std::to_string(frame_idx) +
                             ": task '" + cone_exec[i] + "' failed, aborting stream");
                for (size_t j = i + 1; j < cone_exec.size(); ++j) {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_[cone_exec[j]] = TaskResult{.status = TaskStatus::FAILED};
                }
                failed = true;
                break;
            }
        }
        if (failed) break;
        ++frame_idx;
    }

    // 5) 收尾 sink（关闭文件/写 trailer）；忽略收尾异常避免掩盖帧结果
    for (auto* k : cone_sinks) {
        try { k->on_stream_end(); }
        catch (...) { TG_LOG_ERROR("stream sink on_stream_end threw, ignoring"); }
    }

    TG_LOG_INFO("Stream mode done: " + std::to_string(frame_idx) +
                " frames processed" + (failed ? " (aborted on error)" : ""));
}

// DAG 执行核心：基于动态就绪队列的任务调度
// 维护每个任务的剩余依赖计数，任务完成时递减下游依赖计数，归零即加入就绪队列
void DAGExecutor::run(const DAG& dag) {
    try {
        {
            ExecutionEvent e;
            e.type = ExecutionEvent::Type::DagStarted;
            e.total_tasks = dag.num_tasks();
            e.timestamp = std::chrono::steady_clock::now();
            emit_event(e);
        }

        // 预初始化所有 task（如 GPU shader 预编译），异常不阻断执行
        for (const auto& [task_id, task] : dag.tasks()) {
            try {
                task->init();
            } catch (...) {
                TG_LOG_WARN("Task '" + task_id + "' init() threw exception, ignoring");
            }
        }

        // 流式模式检测：若 DAG 含 IStreamSource 且其下游 cone 内有 IStreamSink，
        // 进入逐帧循环（视频等时序数据），否则走下面原 one-shot 调度路径。
        if (try_run_stream(dag)) {
            size_t ok = 0, fail = 0;
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                for (const auto& [_, r] : results_) {
                    if (r.is_success()) ok++;
                    else if (r.is_failed()) fail++;
                }
            }
            ExecutionEvent e;
            e.type = ExecutionEvent::Type::DagCompleted;
            e.total_tasks = dag.num_tasks();
            e.completed_tasks = ok;
            e.failed_tasks = fail;
            e.timestamp = std::chrono::steady_clock::now();
            emit_event(e);
            TG_LOG_INFO("Stream-mode DAG execution completed: " + std::to_string(ok) +
                        " ok, " + std::to_string(fail) + " failed");
            running_ = false;
            return;
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
            emit_event(ExecutionEvent::Type::TaskFailed, tid, task_type, std::chrono::nanoseconds{0}, reason);
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
                    emit_event(ExecutionEvent::Type::TaskSkipped, tid,
                               task ? task->type() : std::string{});
                    return;
                }

                if (!task) {
                    TG_LOG_ERROR("Task '" + tid + "' not found in DAG");
                    return;
                }

                // READY：任务已被调度器取出，进入处理流程
                emit_event(ExecutionEvent::Type::TaskReady, tid, task->type());

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
                emit_event(ExecutionEvent::Type::TaskStarted, tid, task->type());
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
                if (result.is_success()) {
                    emit_event(ExecutionEvent::Type::TaskCompleted, tid, task->type(), result.duration);
                } else {
                    emit_event(ExecutionEvent::Type::TaskFailed, tid, task->type(), result.duration);
                }

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

        {
            size_t ok = 0, fail = 0;
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                for (const auto& [_, r] : results_) {
                    if (r.is_success()) ok++; else fail++;
                }
            }
            ExecutionEvent e;
            e.type = ExecutionEvent::Type::DagCompleted;
            e.total_tasks = dag.num_tasks();
            e.completed_tasks = ok;
            e.failed_tasks = fail;
            e.timestamp = std::chrono::steady_clock::now();
            emit_event(e);
        }

    } catch (const std::exception& e) {
        running_ = false;
        {
            ExecutionEvent ev;
            ev.type = ExecutionEvent::Type::DagCompleted;
            ev.total_tasks = dag.num_tasks();
            ev.exception_message = e.what();
            ev.timestamp = std::chrono::steady_clock::now();
            emit_event(ev);
        }
        TG_LOG_ERROR("DAG execution failed with exception: " + std::string(e.what()));
        throw;
    } catch (...) {
        running_ = false;
        {
            ExecutionEvent ev;
            ev.type = ExecutionEvent::Type::DagCompleted;
            ev.total_tasks = dag.num_tasks();
            ev.exception_message = "unknown exception";
            ev.timestamp = std::chrono::steady_clock::now();
            emit_event(ev);
        }
        TG_LOG_ERROR("DAG execution failed with unknown exception");
        throw;
    }

    running_ = false;
}

}
