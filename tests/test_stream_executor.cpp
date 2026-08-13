// Test: DAGExecutor stream mode (IStreamSource / IStreamSink).
// Mock source/sink (no OpenCV dependency) verifies:
//  1) full stream: src -> middle -> sink, cone re-runs per frame, sink collects
//     N frames + finalizes exactly once
//  2) preamble runs once: tasks outside the cone are cached and reused per frame
//  3) one-shot fallback: a source with no sink falls back to one-shot path,
//     source execute() returns the first frame
//  4) STREAM_END terminates the loop cleanly
#include <task_graph/dag.hpp>
#include <task_graph/executor.hpp>
#include <plugin_api.hpp>

#include <any>
#include <cstdio>
#include <string>
#include <vector>

using namespace task_graph;

// Mock stream source: emits 0..N-1 as frames, then STREAM_END.
class MockSource : public INode, public IStreamSource {
public:
    MockSource(const std::string& id, int frames, TaskConfig config = {})
        : INode(id, std::move(config)), total_(frames) {}
    const std::string& type() const override { static const std::string t("mock_source"); return t; }
    void reset_stream() override { idx_ = 0; }
    TaskResult next_frame(TaskContext&) override {
        if (idx_ >= total_) return TaskResult{.status = TaskStatus::STREAM_END};
        return TaskResult{.status = TaskStatus::COMPLETED, .value = idx_++};
    }
    // one-shot fallback: reset and return first frame
    TaskResult execute(TaskContext& ctx) override {
        reset_stream();
        return next_frame(ctx);
    }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int total_;
    int idx_{0};
};

// Middle task: double the input int. Counts runs_ for observation.
class DoubleTask : public INode {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("mock_double"); return t; }
    TaskResult execute(TaskContext& ctx) override {
        ++runs_;
        auto v = ctx.input<int>("in");
        if (!v) return TaskResult{.status = TaskStatus::FAILED};
        return TaskResult{.status = TaskStatus::COMPLETED, .value = (*v) * 2};
    }
    std::vector<PortSpec> input_specs() const override { return {PortSpec{"in", "", true}}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int runs_{0};
};

// Constant int source (preamble candidate): returns a fixed value. Counts runs_.
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

// Adder task: two inputs a + b (mixes stream frame with preamble constant). Counts runs_.
class AddTask : public INode {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("mock_add"); return t; }
    TaskResult execute(TaskContext& ctx) override {
        ++runs_;
        auto a = ctx.input<int>("a");
        auto b = ctx.input<int>("b");
        if (!a || !b) return TaskResult{.status = TaskStatus::FAILED};
        return TaskResult{.status = TaskStatus::COMPLETED, .value = *a + *b};
    }
    std::vector<PortSpec> input_specs() const override {
        return {PortSpec{"a", "", true}, PortSpec{"b", "", true}};
    }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int runs_{0};
};

// Mock sink: collects each frame int; on_stream_end records finalization.
class MockSink : public INode, public IStreamSink {
public:
    using INode::INode;
    const std::string& type() const override { static const std::string t("mock_sink"); return t; }
    TaskResult execute(TaskContext& ctx) override {
        auto v = ctx.input<int>("in");
        if (!v) return TaskResult{.status = TaskStatus::FAILED};
        collected_.push_back(*v);
        return TaskResult{.status = TaskStatus::COMPLETED, .value = *v};
    }
    void on_stream_end() override { ++finalized_; }
    std::vector<PortSpec> input_specs() const override { return {PortSpec{"in", "", true}}; }
    std::vector<PortSpec> output_specs() const override { return {}; }
    std::vector<int> collected_;
    int finalized_{0};
};

// Pass-through task that fails when the input int equals target_ (simulates a
// per-frame decode/processing error). Used to verify mid-stream abort semantics.
class FailIfEqTask : public INode {
public:
    FailIfEqTask(const std::string& id, int target, TaskConfig config = {})
        : INode(id, std::move(config)), target_(target) {}
    const std::string& type() const override { static const std::string t("mock_fail_if"); return t; }
    TaskResult execute(TaskContext& ctx) override {
        ++runs_;
        auto v = ctx.input<int>("in");
        if (!v) return TaskResult{.status = TaskStatus::FAILED};
        if (*v == target_) return TaskResult{.status = TaskStatus::FAILED};
        return TaskResult{.status = TaskStatus::COMPLETED, .value = *v};
    }
    std::vector<PortSpec> input_specs() const override { return {PortSpec{"in", "", true}}; }
    std::vector<PortSpec> output_specs() const override { return {make_port<int>("out")}; }
    int target_;
    int runs_{0};
};

// Task that always fails (preamble failure simulation).
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

static int g_fails = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { printf("  [FAIL] %s\n", msg); ++g_fails; }
    else        printf("  [ok]   %s\n", msg);
}

static void test_full_stream() {
    printf("[test] full stream: src(5) -> double -> sink\n");
    DAG dag;
    auto src  = std::make_shared<MockSource>("src", 5);
    auto dbl  = std::make_shared<DoubleTask>("dbl");
    auto sink = std::make_shared<MockSink>("sink");
    dag.add_task("src", src);
    dag.add_task("dbl", dbl);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "dbl", "in");
    dag.connect("dbl", "out", "sink", "in");

    DAGExecutor exec;
    exec.execute(dag).wait();

    check(sink->collected_.size() == 5, "sink collected 5 frames");
    check(sink->collected_ == std::vector<int>({0, 2, 4, 6, 8}),
          "collected values == {0,2,4,6,8} (doubled frames)");
    check(sink->finalized_ == 1, "sink finalized exactly once");
    check(dbl->runs_ == 5, "middle task ran once per frame (5)");
    check(src->idx_ == 5, "source cursor advanced to 5 (exhausted)");
}

static void test_preamble_once() {
    printf("[test] preamble runs once: const(100) + src(5) -> add -> sink\n");
    DAG dag;
    auto cfg  = std::make_shared<ConstIntTask>("cfg", 100);
    auto src  = std::make_shared<MockSource>("src", 5);
    auto add  = std::make_shared<AddTask>("add");
    auto sink = std::make_shared<MockSink>("sink");
    dag.add_task("cfg", cfg);
    dag.add_task("src", src);
    dag.add_task("add", add);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "add", "a");   // stream frame
    dag.connect("cfg", "val", "add", "b");   // preamble constant
    dag.connect("add", "out", "sink", "in");

    DAGExecutor exec;
    exec.execute(dag).wait();

    check(cfg->runs_ == 1, "preamble task (cfg) ran exactly once");
    check(add->runs_ == 5, "cone task (add) ran once per frame (5)");
    check(sink->collected_.size() == 5, "sink collected 5 frames");
    check(sink->collected_ == std::vector<int>({100, 101, 102, 103, 104}),
          "collected == frame+100 each");
    check(sink->finalized_ == 1, "sink finalized once");
}

static void test_one_shot_fallback() {
    printf("[test] one-shot fallback: src alone (no sink) returns first frame\n");
    DAG dag;
    auto src = std::make_shared<MockSource>("src", 5);
    dag.add_task("src", src);

    DAGExecutor exec;
    exec.execute(dag).wait();
    auto results = exec.get_results();

    auto it = results.find("src");
    check(it != results.end(), "src result present");
    if (it != results.end()) {
        check(it->second.is_success(), "src completed (one-shot path)");
        try {
            int v = std::any_cast<int>(it->second.value);
            check(v == 0, "one-shot src returned first frame (0)");
        } catch (const std::bad_any_cast&) {
            check(false, "src value is int (bad_any_cast)");
        }
    }
    // one-shot consumed exactly one frame (idx==1, not exhausted to 5)
    check(src->idx_ == 1, "one-shot consumed exactly one frame (idx==1)");
}

static void test_zero_frames() {
    printf("[test] zero-frame stream: src(0) -> sink, still finalizes\n");
    DAG dag;
    auto src  = std::make_shared<MockSource>("src", 0);
    auto dbl  = std::make_shared<DoubleTask>("dbl");
    auto sink = std::make_shared<MockSink>("sink");
    dag.add_task("src", src);
    dag.add_task("dbl", dbl);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "dbl", "in");
    dag.connect("dbl", "out", "sink", "in");

    DAGExecutor exec;
    exec.execute(dag).wait();

    check(sink->collected_.empty(), "no frames collected");
    check(sink->finalized_ == 1, "sink still finalized once (empty stream)");
    check(dbl->runs_ == 0, "middle task never ran");
}

static void test_mid_stream_abort() {
    printf("[test] mid-stream abort: src(5) -> fail_if(2) -> after -> sink\n");
    DAG dag;
    auto src  = std::make_shared<MockSource>("src", 5);
    auto fail = std::make_shared<FailIfEqTask>("fail", 2);
    auto aft  = std::make_shared<DoubleTask>("after");
    auto sink = std::make_shared<MockSink>("sink");
    dag.add_task("src", src);
    dag.add_task("fail", fail);
    dag.add_task("after", aft);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "fail", "in");
    dag.connect("fail", "out", "after", "in");
    dag.connect("after", "out", "sink", "in");

    DAGExecutor exec;
    exec.execute(dag).wait();
    auto results = exec.get_results();

    // Frames 0,1 processed; frame 2 fails at `fail`. `after`/`sink` must not
    // retain a stale COMPLETED from frame 1 (regression guard).
    check(fail->runs_ == 3, "fail_if ran 3 times (frames 0,1,2)");
    check(aft->runs_ == 2, "after ran only 2 times (frames 0,1; aborted before frame 2)");
    check(sink->collected_.size() == 2, "sink collected 2 frames before abort");
    check(sink->finalized_ == 1, "sink still finalized once after abort");

    auto aft_it = results.find("after");
    check(aft_it != results.end() && aft_it->second.is_failed(),
          "after task result is FAILED (no stale COMPLETED leak)");
    auto sink_it = results.find("sink");
    check(sink_it != results.end() && sink_it->second.is_failed(),
          "sink task result is FAILED (no stale COMPLETED leak)");
    auto fail_it = results.find("fail");
    check(fail_it != results.end() && fail_it->second.is_failed(),
          "fail task result is FAILED");
}

static void test_preamble_failure() {
    printf("[test] preamble failure: failing preamble aborts before frame loop\n");
    DAG dag;
    auto bad  = std::make_shared<AlwaysFailTask>("bad");   // preamble (not in cone)
    auto src  = std::make_shared<MockSource>("src", 3);
    auto add  = std::make_shared<AddTask>("add");
    auto sink = std::make_shared<MockSink>("sink");
    dag.add_task("bad", bad);
    dag.add_task("src", src);
    dag.add_task("add", add);
    dag.add_task("sink", sink);
    dag.connect("src", "out", "add", "a");
    dag.connect("bad", "out", "add", "b");
    dag.connect("add", "out", "sink", "in");

    DAGExecutor exec;
    exec.execute(dag).wait();
    auto results = exec.get_results();

    check(bad->runs_ == 1, "preamble failing task ran once");
    check(src->idx_ == 0, "frame loop never started (source not advanced)");
    check(add->runs_ == 0, "cone task never ran");
    check(sink->collected_.empty(), "no frames collected");
    check(sink->finalized_ == 1, "sink still finalized once");
    auto bad_it = results.find("bad");
    check(bad_it != results.end() && bad_it->second.is_failed(),
          "preamble result is FAILED");
}

int main() {
    test_full_stream();
    test_preamble_once();
    test_one_shot_fallback();
    test_zero_frames();
    test_mid_stream_abort();
    test_preamble_failure();

    if (g_fails == 0) {
        printf("[PASS] test_stream_executor: all checks passed\n");
        return 0;
    }
    printf("[FAIL] test_stream_executor: %d check(s) failed\n", g_fails);
    return 1;
}
