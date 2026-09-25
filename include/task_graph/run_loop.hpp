#pragma once

// RunLoop：多轮运行会话层（单次 / N 次 / 循环），驱动 DAGExecutor 逐轮执行。
//
// 设计要点（对应运行模型改造方案）：
//  - 一次"运行"（run）的天然定义：one-shot 图 = 整图执行一遍；stream 图 =
//    源从 reset_stream() 到 STREAM_END 走完一遍。RunLoop 在 run 之外套驱动。
//  - 阻塞式 API：run_next()/run_all() 在调用线程阻塞执行，每轮结束返回
//    轻量摘要（RunSummary）。GUI 宿主自行开线程包一层 + 事件编组。
//  - 内存策略：循环模式下默认只保留摘要（ResultRetention::SummaryOnly），
//    重数据（TaskResult 的 std::any 图像等）不经过 RunLoop；LastRun 单槽
//    只保留最新一轮；AllRuns 显式 opt-in。
//  - 暂停/取消：协作式。轮间检查点在本层；任务间/帧间检查点在 DAGExecutor
//    （pause/resume 直接透传）。暂停最迟在当前任务完成时生效。

#include <task_graph/executor.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace task_graph {

// ====== 运行策略与结果模型 ======

struct RunPolicy {
    enum class Mode {
        Once,    // 单次运行
        NTimes,  // 连续 n 次
        Loop,    // 无限循环（直到 cancel）
    };
    Mode mode{Mode::Once};
    size_t count{1};             // NTimes 模式的轮数上限
    bool stop_on_failure{true};  // 某轮失败是否终止后续轮（Loop 模式同样生效）

    // 每轮结果保留策略（循环模式下避免重数据随轮数积累）
    enum class ResultRetention {
        SummaryOnly,  // 默认：只有 RunSummary，完整 TaskResult 快照直接丢弃
        LastRun,      // 只保留最新一轮 RunResult（单槽覆盖）
        AllRuns,      // 全部保留（Once/测试用；Loop 下不推荐，会无界增长）
    };
    ResultRetention retention{ResultRetention::SummaryOnly};
};

// 每轮轻量摘要（O(1) 大小，不含任务输出数据）
struct RunSummary {
    size_t run_index{0};                 // 会话内从 0 递增
    bool ok{false};                      // 全部任务成功（stream 图 = 整段流走完）
    size_t completed_tasks{0};
    size_t failed_tasks{0};
    std::chrono::nanoseconds duration{0};
    // 首个失败任务（便于日志/诊断；截断由调用方处理）
    std::string first_failure_task_id;
    std::string first_failure_reason;
};

// 完整单轮结果（仅 retention != SummaryOnly 时填充 results）
struct RunResult : RunSummary {
    std::unordered_map<TaskId, TaskResult> results;
};

// 整个会话的汇总（run_all 返回）
struct SessionReport {
    size_t runs_executed{0};
    size_t runs_ok{0};
    size_t runs_failed{0};
    // 会话终止原因（诊断用）
    std::string stop_reason;  // "completed" | "cancelled" | "paused-cancel" |
                              // "failure" | "max-runs"
};

// ====== RunLoop ======

class RunLoop {
public:
    // cfg.callback（若设置）会被包裹：DagCompleted 拦截为内部计数，其余事件
    // 原样转发。每轮 summary 另经 set_run_callback 交付。
    explicit RunLoop(RunPolicy policy, ExecutorConfig cfg = {});
    ~RunLoop();  // cancel() + 收尾

    // —— 阻塞式驱动 ——

    // 阻塞执行一轮；返回 nullopt 表示会话已终止（cancel / 轮数用尽 /
    // stop_on_failure 触发）。会话进行中被 pause 时，本轮开始前挂起等待。
    std::optional<RunSummary> run_next(const DAG& dag);

    // 阻塞跑完整个会话（Loop 模式直到 cancel），返回汇总。
    SessionReport run_all(const DAG& dag);

    // —— 控制（可从其他线程调用）——
    void pause();    // 轮间生效（任务间/帧间由 executor 透传支持）
    void resume();
    void cancel();   // 取消当前轮并终止会话；paused 状态下先自动 resume
    bool is_paused() const { return paused_; }
    bool is_cancelled() const { return cancelled_; }
    bool session_done() const { return session_done_; }

    // —— 结果与事件 ——

    // 每轮 summary 回调（在 run_next 的调用线程触发，即"本轮刚结束"）。
    // run_all 时每轮都会回调一次。
    void set_run_callback(std::function<void(const RunSummary&)> cb) {
        run_callback_ = std::move(cb);
    }

    // 透传给 executor 的事件回调（TaskStarted/Completed/Failed 等）
    void set_task_event_callback(ExecutionCallback cb) { task_event_callback_ = std::move(cb); }

    // 最新一轮完整结果（仅 retention == LastRun/AllRuns 时非空）
    std::optional<RunResult> last_result() const;

    // 所有保留轮的结果（retention == AllRuns 时增长；其余为空/单槽）
    std::vector<RunResult> retained_results() const;

    const RunPolicy& policy() const { return policy_; }
    DAGExecutor& executor() { return *executor_; }
    const DAGExecutor& executor() const { return *executor_; }

private:
    void wait_while_paused();
    // 包裹用户回调后的 executor 配置（拦截 DagCompleted 计数 + 首失败原因，
    // 其余转发）；executor_ 在构造函数里以此创建。
    ExecutorConfig config_passthrough_;

    RunPolicy policy_;
    std::unique_ptr<DAGExecutor> executor_;

    ExecutionCallback task_event_callback_;
    std::function<void(const RunSummary&)> run_callback_;

    // executor 事件拦截状态（executor 线程写，run_next 同线程读——execute()
    // 的回调在 run() 内同步触发，无跨线程竞争；加锁仅为 pause/cancel 语义）
    mutable std::mutex state_mutex_;
    size_t pending_completed_{0};
    size_t pending_failed_{0};
    std::string first_failure_task_id_;
    std::string first_failure_reason_;

    std::optional<RunResult> last_result_;       // LastRun 单槽
    std::vector<RunResult> all_results_;         // AllRuns（会话内）

    size_t runs_executed_{0};

    std::atomic<bool> paused_{false};
    std::atomic<bool> cancelled_{false};
    bool session_done_{false};  // 仅驱动线程（run_next/run_all 调用方）写

    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;
};

}  // namespace task_graph
