// Test: RunLoop multi-run session (Once / NTimes / Loop + pause/cancel +
// ResultRetention policies). Mock int-source graph, no OpenCV dependency.
//
//  1) Once mode: run_all executes exactly one run, summary ok
//  2) NTimes=3: 3 runs, per-run summaries, run_index monotonic
//  3) stop_on_failure: failing task ends the session after 1 run
//  4) stop_on_failure=false: all N runs execute despite failures
//  5) Loop mode: runs until cancel() from another thread
//  6) Pause: run_next blocks while paused, resumes after resume()
//  7) ResultRetention::SummaryOnly never snapshots heavy results
//  8) ResultRetention::LastRun keeps only the newest run snapshot
//  9) run callbacks fire once per run with matching run_index
#include <task_graph/dag.hpp>
#include <task_graph/run_loop.hpp>
#include <plugin_api.hpp>
#include <gtest/gtest.h>

#include <any>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

using namespace task_graph;

// Constant int source (one-shot graph, no stream interfaces).
class ConstIntTask : public INode {
public:
    ConstIntTask(const std::string& id, int v, TaskConfig config = {})
        : INode(id, std::move(config)), v_(v) {}
    const std::string& type() const override { static const std::string t("mock_const"); return t; }
    TaskResult execute(TaskContext&) override {
        ++runs_;
        return TaskResult{.status = TaskStatus::COMPLETED, .value = v_};
    }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("val")}; }
    int v_;
    int runs_{0};
};

// Pass-through that fails every run (for stop_on_failure tests).
class AlwaysFailTask : public INode {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("mock_always_fail"); return t; }
    TaskResult execute(TaskContext&) override {
        ++runs_;
        return TaskResult{.status = TaskStatus::FAILED};
    }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int runs_{0};
};

namespace {

// cfg -> single-node DAG
DAG make_single_node_graph(std::shared_ptr<INode> node) {
    DAG dag;
    dag.add_task(node->id(), node);
    return dag;
}

}  // namespace

TEST(RunLoop, OnceModeRunsExactlyOneRun) {
    auto node = std::make_shared<ConstIntTask>("n", 7);
    DAG dag = make_single_node_graph(node);

    RunLoop loop(RunPolicy{});  // Once, SummaryOnly
    auto rep = loop.run_all(dag);

    EXPECT_EQ(rep.runs_executed, 1u);
    EXPECT_EQ(rep.runs_ok, 1u);
    EXPECT_EQ(rep.runs_failed, 0u);
    EXPECT_EQ(node->runs_, 1);
    EXPECT_TRUE(loop.session_done());
}

TEST(RunLoop, NTimesRunsExactlyN) {
    auto node = std::make_shared<ConstIntTask>("n", 1);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 3;
    RunLoop loop(p);
    auto rep = loop.run_all(dag);

    EXPECT_EQ(rep.runs_executed, 3u);
    EXPECT_EQ(node->runs_, 3);
    // 会话自然终止后 run_next 不再有产出
    EXPECT_FALSE(loop.run_next(dag).has_value());
}

TEST(RunLoop, StopOnFailureEndsSession) {
    auto node = std::make_shared<AlwaysFailTask>("bad");
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 5;
    p.stop_on_failure = true;  // default
    RunLoop loop(p);
    auto rep = loop.run_all(dag);

    EXPECT_EQ(rep.runs_executed, 1u);
    EXPECT_EQ(rep.runs_failed, 1u);
    EXPECT_EQ(rep.stop_reason, "failure");
    EXPECT_EQ(node->runs_, 1);
}

TEST(RunLoop, ContinueOnFailureRunsAllN) {
    auto node = std::make_shared<AlwaysFailTask>("bad");
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 4;
    p.stop_on_failure = false;
    RunLoop loop(p);
    auto rep = loop.run_all(dag);

    EXPECT_EQ(rep.runs_executed, 4u);
    EXPECT_EQ(rep.runs_failed, 4u);
    EXPECT_EQ(node->runs_, 4);
}

TEST(RunLoop, LoopModeRunsUntilCancel) {
    auto node = std::make_shared<ConstIntTask>("n", 0);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::Loop;
    RunLoop loop2(p);

    // 另一线程跑 50ms 后 cancel
    std::thread killer([&loop2] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        loop2.cancel();
    });
    auto rep = loop2.run_all(dag);
    killer.join();

    EXPECT_GE(rep.runs_executed, 1u);
    EXPECT_TRUE(loop2.is_cancelled());
    EXPECT_EQ(rep.stop_reason, "cancelled");
    EXPECT_EQ(node->runs_, static_cast<int>(rep.runs_executed));
}

TEST(RunLoop, PauseBlocksUntilResume) {
    auto node = std::make_shared<ConstIntTask>("n", 0);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 2;
    RunLoop loop(p);

    loop.pause();
    std::atomic<bool> finished{false};
    std::thread runner([&] {
        auto rep = loop.run_all(dag);
        (void)rep;
        finished = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    // 暂停中：第一轮都没跑（挂在轮间暂停点）
    EXPECT_FALSE(finished.load());
    EXPECT_EQ(node->runs_, 0);

    loop.resume();
    runner.join();
    EXPECT_TRUE(finished.load());
    EXPECT_EQ(node->runs_, 2);
}

TEST(RunLoop, CancelWhilePausedUnblocks) {
    auto node = std::make_shared<ConstIntTask>("n", 0);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 2;
    RunLoop loop(p);

    loop.pause();
    std::atomic<bool> finished{false};
    std::thread runner([&] {
        auto rep = loop.run_all(dag);
        (void)rep;
        finished = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_FALSE(finished.load());
    loop.cancel();  // 暂停态下 cancel 应解除挂起并终止
    runner.join();

    EXPECT_TRUE(finished.load());
    EXPECT_EQ(node->runs_, 0);  // 一轮都没跑
}

TEST(RunLoop, SummaryOnlyNeverSnapshotsResults) {
    auto node = std::make_shared<ConstIntTask>("n", 3);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 2;
    p.retention = RunPolicy::ResultRetention::SummaryOnly;
    RunLoop loop(p);

    std::vector<RunSummary> summaries;
    loop.set_run_callback([&](const RunSummary& s) { summaries.push_back(s); });

    auto rep = loop.run_all(dag);
    EXPECT_EQ(rep.runs_executed, 2u);
    ASSERT_EQ(summaries.size(), 2u);
    EXPECT_EQ(summaries[0].run_index, 0u);
    EXPECT_EQ(summaries[1].run_index, 1u);
    EXPECT_TRUE(summaries[0].ok);
    EXPECT_FALSE(loop.last_result().has_value());
    EXPECT_TRUE(loop.retained_results().empty());
}

TEST(RunLoop, LastRunKeepsOnlyNewestSnapshot) {
    auto node = std::make_shared<ConstIntTask>("n", 5);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 2;
    p.retention = RunPolicy::ResultRetention::LastRun;
    RunLoop loop(p);
    loop.run_all(dag);

    auto last = loop.last_result();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->run_index, 1u);          // 是最新一轮
    EXPECT_TRUE(loop.retained_results().empty());  // AllRuns 才积累
    // 快照里有该轮任务结果
    ASSERT_EQ(last->results.count("n"), 1u);
    EXPECT_TRUE(last->results.at("n").is_success());
}

TEST(RunLoop, AllRunsAccumulatesEverySnapshot) {
    auto node = std::make_shared<ConstIntTask>("n", 5);
    DAG dag = make_single_node_graph(node);

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 3;
    p.retention = RunPolicy::ResultRetention::AllRuns;
    RunLoop loop(p);
    loop.run_all(dag);

    auto all = loop.retained_results();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].run_index, 0u);
    EXPECT_EQ(all[2].run_index, 2u);
}

// ====== stream 图多轮会话：源/汇每轮 reset、收尾恰好一次 ======

// 3 帧后 STREAM_END 的 mock 源
class TinySource : public INode, public IStreamSource {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("tiny_source"); return t; }
    void reset_stream() override { ++resets_; idx_ = 0; }
    TaskResult next_frame(TaskContext&) override {
        if (idx_ >= 3) return TaskResult{.status = TaskStatus::STREAM_END};
        return TaskResult{.status = TaskStatus::COMPLETED, .value = idx_++};
    }
    TaskResult execute(TaskContext& ctx) override { reset_stream(); return next_frame(ctx); }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int resets_{0};
    int idx_{0};
};

// 计数 reset/end/帧数的 mock 汇
class CountingSink : public INode, public IStreamSink {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("counting_sink"); return t; }
    TaskResult execute(TaskContext& ctx) override {
        auto v = ctx.input<int>("in");
        if (!v) return TaskResult{.status = TaskStatus::FAILED};
        ++frames_;
        return TaskResult{.status = TaskStatus::COMPLETED, .value = *v};
    }
    void on_stream_end() override { ++ends_; }
    void reset_stream() override { ++resets_; }
    std::vector<PortSpec> input_specs() const override { return {PortSpec{"in", "", true}}; }
    std::vector<PortSpec> output_specs() const override { return {}; }
    int frames_{0};
    int ends_{0};
    int resets_{0};
};

TEST(RunLoop, StreamGraphRunsPerRunWithSinkReset) {
    auto src = std::make_shared<TinySource>("src");
    auto sink = std::make_shared<CountingSink>("sink");
    DAG dag;
    dag.add_task("src", src);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "sink", "in");

    RunPolicy p;
    p.mode = RunPolicy::Mode::NTimes;
    p.count = 2;
    RunLoop loop(p);
    auto rep = loop.run_all(dag);

    EXPECT_EQ(rep.runs_executed, 2u);
    EXPECT_EQ(rep.runs_ok, 2u);
    // 每轮：源 reset 一次、汇 reset 一次、收尾恰好一次
    EXPECT_EQ(src->resets_, 2);
    EXPECT_EQ(sink->resets_, 2);
    EXPECT_EQ(sink->ends_, 2);
    // 每轮 3 帧 × 2 轮
    EXPECT_EQ(sink->frames_, 6);
}


TEST(ExecutorPause, PauseHoldsTaskDispatchUntilResume) {
    auto node = std::make_shared<ConstIntTask>("n", 1);
    DAG dag = make_single_node_graph(node);

    DAGExecutor exec;
    exec.pause();
    auto fut = exec.execute(dag);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    // worker 停在派发前检查点，任务未执行
    EXPECT_TRUE(exec.is_paused());
    EXPECT_EQ(node->runs_, 0);

    exec.resume();
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(node->runs_, 1);
}

TEST(ExecutorPause, CancelWhilePausedMidRunUnblocks) {
    auto node = std::make_shared<ConstIntTask>("n", 1);
    DAG dag = make_single_node_graph(node);

    DAGExecutor exec;
    exec.pause();
    auto fut = exec.execute(dag);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    exec.cancel();  // 应唤醒检查点上的 worker 并终止调度
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(node->runs_, 0);
}
