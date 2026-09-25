#include <task_graph/run_loop.hpp>

#include <algorithm>

namespace task_graph {

RunLoop::RunLoop(RunPolicy policy, ExecutorConfig cfg)
    : policy_(policy) {
    // 包裹用户回调：DagCompleted 拦截为本轮计数（SummaryOnly 路径靠它避免
    // 整份 results_ 拷贝），TaskFailed 记录首失败原因，其余原样转发。
    ExecutionCallback user_cb = std::move(cfg.callback);
    config_passthrough_ = std::move(cfg);
    config_passthrough_.callback = [this, user_cb](const ExecutionEvent& e) {
        if (e.type == ExecutionEvent::Type::DagCompleted) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            pending_completed_ = e.completed_tasks;
            pending_failed_ = e.failed_tasks;
        } else if (e.type == ExecutionEvent::Type::TaskFailed) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (first_failure_task_id_.empty()) {
                first_failure_task_id_ = e.task_id;
                first_failure_reason_ = e.failure_reason;
            }
        }
        if (task_event_callback_) task_event_callback_(e);
        if (user_cb) user_cb(e);
    };
    executor_ = std::make_unique<DAGExecutor>(config_passthrough_);
}

RunLoop::~RunLoop() {
    cancel();
}

void RunLoop::wait_while_paused() {
    std::unique_lock<std::mutex> lock(pause_mutex_);
    pause_cv_.wait(lock, [this] { return !paused_.load() || cancelled_.load(); });
}

void RunLoop::pause() {
    paused_ = true;
    // 透传 executor：任务间/帧间检查点同步挂起（轮间挂起在本层）
    if (executor_) executor_->pause();
}

void RunLoop::resume() {
    paused_ = false;
    pause_cv_.notify_all();
    if (executor_) executor_->resume();
}

void RunLoop::cancel() {
    // paused 下 cancel：先解除挂起，等待者才能观察到 cancelled_ 退出。
    paused_ = false;
    pause_cv_.notify_all();
    cancelled_ = true;
    if (executor_) executor_->cancel();
}

std::optional<RunSummary> RunLoop::run_next(const DAG& dag) {
    if (session_done_) return std::nullopt;

    // 轮数终止检查（Once 首轮后终止；NTimes 计数用尽终止；Loop 只认 cancel）
    if (policy_.mode == RunPolicy::Mode::Once && runs_executed_ >= 1) {
        session_done_ = true;
        return std::nullopt;
    }
    if (policy_.mode == RunPolicy::Mode::NTimes && runs_executed_ >= policy_.count) {
        session_done_ = true;
        return std::nullopt;
    }

    // 轮间暂停点：挂起直到 resume / cancel
    wait_while_paused();
    if (cancelled_) {
        session_done_ = true;
        return std::nullopt;
    }

    // 清空上一轮的拦截状态
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_completed_ = 0;
        pending_failed_ = 0;
        first_failure_task_id_.clear();
        first_failure_reason_.clear();
    }

    const auto t0 = std::chrono::steady_clock::now();
    const bool wasCancelledBefore = cancelled_.load();
    // 单轮执行 = 现有 DAGExecutor 语义（one-shot 或 stream 全在内）。
    // execute() 返回 future；wait() 阻塞到本轮结束——天然的每轮串行化。
    executor_->execute(dag);
    executor_->wait();

    RunSummary s;
    s.run_index = runs_executed_;
    s.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        s.completed_tasks = pending_completed_;
        s.failed_tasks = pending_failed_;
        s.first_failure_task_id = first_failure_task_id_;
        s.first_failure_reason = first_failure_reason_;
    }
    s.ok = (s.failed_tasks == 0);

    // 本轮中途被 cancel：视为"被打断的半轮"——不计入 runs_executed、不回调，
    // 直接终止会话。判据 = cancel 发生在本轮执行期间且本轮不完整
    // （有失败任务，或有任务根本没跑完：completed < total，被打断的任务
    // 以 FAILED/SKIPPED 收场）。若 cancel 恰好落在整轮完成后，本轮正常计入。
    const size_t total_tasks = dag.num_tasks();
    if (cancelled_.load() && !wasCancelledBefore &&
        (s.failed_tasks > 0 || s.completed_tasks < total_tasks)) {
        session_done_ = true;
        return std::nullopt;
    }

    // 重数据快照按 retention 策略（SummaryOnly 全程不触碰 get_results()）
    RunResult full;
    static_cast<RunSummary&>(full) = s;  // 摘要字段
    if (policy_.retention == RunPolicy::ResultRetention::LastRun ||
        policy_.retention == RunPolicy::ResultRetention::AllRuns) {
        full.results = executor_->get_results();
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_result_ = std::move(full);
    }
    if (policy_.retention == RunPolicy::ResultRetention::AllRuns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (last_result_) all_results_.push_back(*last_result_);
    }

    ++runs_executed_;
    if (run_callback_) run_callback_(s);

    // 会话终止判定
    if (cancelled_) {
        session_done_ = true;
    } else if (policy_.stop_on_failure && !s.ok) {
        session_done_ = true;
    } else if (policy_.mode == RunPolicy::Mode::Once) {
        session_done_ = true;
    } else if (policy_.mode == RunPolicy::Mode::NTimes &&
               runs_executed_ >= policy_.count) {
        session_done_ = true;
    }
    return s;
}

SessionReport RunLoop::run_all(const DAG& dag) {
    SessionReport rep;
    while (true) {
        auto s = run_next(dag);
        if (!s) break;
        rep.runs_executed++;
        if (s->ok) rep.runs_ok++; else rep.runs_failed++;
    }
    if (cancelled_) rep.stop_reason = "cancelled";
    else if (session_done_ && rep.runs_failed > 0 && policy_.stop_on_failure) rep.stop_reason = "failure";
    else rep.stop_reason = (policy_.mode == RunPolicy::Mode::Loop) ? "cancelled" : "completed";
    // Loop 模式退出必经 cancel；上面分支已覆盖（Loop 且未 cancel 不可能退出）
    return rep;
}

std::optional<RunResult> RunLoop::last_result() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_result_;
}

std::vector<RunResult> RunLoop::retained_results() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return all_results_;
}

}  // namespace task_graph
