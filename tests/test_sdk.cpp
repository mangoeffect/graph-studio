// TaskGraph SDK 生命周期测试。
// 覆盖:生命周期状态机、graph 加载、输入/输出绑定(拉/推模式)、类型前置校验、
//       globals/env 上下文注入、diff 增量更新(热状态保留)、异步执行、
//       LogSink 注销安全、与裸 DAGExecutor 的等价性。
#include <task_graph/task_graph.hpp>
#include <task_graph/sdk.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <unordered_map>

using namespace task_graph;

// ====================== 测试用任务类型 ======================

namespace {

// 字符串拼接:param "suffix"。同时用实例地址计数验证热状态保留。
class AppendTask final : public INode {
public:
    AppendTask(const std::string& id, const TaskConfig& cfg) : INode(id, cfg) {}
    const std::string& type() const override {
        static const std::string t{"test_append"};
        return t;
    }
    std::vector<PortSpec> input_specs() const override {
        return {PortSpec{"in", "std::string", true}};
    }
    std::vector<PortSpec> output_specs() const override {
        return {PortSpec{"out", "std::string", true}};
    }
    std::vector<ParamSpec> param_specs() const override {
        ParamSpec s;
        s.name = "suffix";
        s.type = ParamType::String;
        s.default_value = std::string("");
        return {s};
    }
    TaskResult execute(TaskContext& ctx) override {
        exec_count(this);  // 记录实例执行(热状态验证)
        auto in = ctx.input<std::string>("in");
        if (!in) {
            return TaskResult{.status = TaskStatus::FAILED};
        }
        auto suffix = config().params.get_string("suffix").value_or("");
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = *in + suffix;
        return r;
    }

    // 实例执行计数(验证 diff 更新后 TaskPtr 是否保留):单一共享 map
    static std::unordered_map<const void*, int>& counters() {
        static std::unordered_map<const void*, int> counts;
        return counts;
    }
    static int exec_count(const void* inst) { return ++counters()[inst]; }
    // 出现过的不同实例数(params-only 更新后应不增长:同一实例继续服务)
    static size_t distinct_instances() { return counters().size(); }
    static void reset_counters() { counters().clear(); }
};

// 读执行上下文全局值:返回 "_env.<KEY>" + "|" + key 的拼接
class ReadGlobalTask final : public INode {
public:
    ReadGlobalTask(const std::string& id, const TaskConfig& cfg) : INode(id, cfg) {}
    const std::string& type() const override {
        static const std::string t{"test_read_global"};
        return t;
    }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override {
        return {PortSpec{"out", "std::string", true}};
    }
    std::vector<ParamSpec> param_specs() const override {
        ParamSpec s;
        s.name = "env_key";
        s.type = ParamType::String;
        s.default_value = std::string("DEVICE");
        return {s};
    }
    TaskResult execute(TaskContext& ctx) override {
        auto key = config().params.get_string("env_key").value_or("DEVICE");
        std::string env_val;
        if (auto v = ctx.get_value("_env." + key)) {
            env_val = std::any_cast<std::string>(*v);
        }
        std::string g_val;
        if (auto v = ctx.get_value("mode")) {
            g_val = std::any_cast<std::string>(*v);
        }
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = env_val + "|" + g_val;
        return r;
    }
};

void register_test_tasks_once() {
    static bool done = [] {
        auto& reg = PluginRegistry::instance();
        reg.register_task("test_append",
            [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                return std::make_shared<AppendTask>(id, cfg);
            });
        reg.register_task("test_read_global",
            [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                return std::make_shared<ReadGlobalTask>(id, cfg);
            });
        return true;
    }();
    (void)done;
}

const char* kPipeline = R"({
  "version": "2.0",
  "tasks": [
    { "id": "src",  "type": "io_input", "params": { "data_type": "std::string" } },
    { "id": "proc", "type": "test_append", "params": { "suffix": "-A" } },
    { "id": "dst",  "type": "io_output" }
  ],
  "edges": [
    { "from": "src",  "from_port": "out", "to": "proc", "to_port": "in" },
    { "from": "proc", "from_port": "out", "to": "dst",  "to_port": "in" }
  ]
})";

SdkConfig base_config() {
    SdkConfig cfg;
    cfg.thread_pool_size = 2;
    return cfg;
}

}  // namespace

// ====================== 生命周期 ======================

TEST(SdkLifecycle, StateMachine) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    EXPECT_FALSE(sdk->is_initialized());

    // 未 init
    EXPECT_EQ(sdk->execute(), SdkStatus::NOT_INITIALIZED);
    EXPECT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::NOT_INITIALIZED);

    EXPECT_EQ(sdk->init(base_config()), SdkStatus::OK);
    EXPECT_TRUE(sdk->is_initialized());
    // init 两次
    EXPECT_EQ(sdk->init(base_config()), SdkStatus::ALREADY_INITIALIZED);

    // shutdown 幂等;之后方法安全
    EXPECT_EQ(sdk->shutdown(), SdkStatus::OK);
    EXPECT_EQ(sdk->shutdown(), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::INTERNAL_ERROR);
    EXPECT_FALSE(sdk->is_initialized());
}

TEST(SdkLifecycle, LogSinkUnregisteredOnShutdown) {
    register_test_tasks_once();
    static std::atomic<int> sink_calls{0};
    {
        auto sdk = TaskGraphSdk::create();
        SdkConfig cfg = base_config();
        cfg.log_callback = [](const LogEntry&) { ++sink_calls; };
        ASSERT_EQ(sdk->init(cfg), SdkStatus::OK);
        ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
        ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("x")), SdkStatus::OK);
        ASSERT_EQ(sdk->execute(), SdkStatus::OK);
        sdk->shutdown();
    }
    // sdk 已销毁:再打日志不得触碰已注销的回调(悬垂即崩溃,asan 下可检出)
    tg_log(LogLevel::INFO, "after shutdown");
    EXPECT_GT(sink_calls.load(), 0);
}

// ====================== 加载 ======================

TEST(SdkLoad, InvalidGraphKeepsIssues) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    EXPECT_EQ(sdk->graph_config(), nullptr);

    EXPECT_EQ(sdk->load_graph_string("{ not json"), SdkStatus::GRAPH_INVALID);
    EXPECT_FALSE(sdk->last_load_issues().empty());
    EXPECT_EQ(sdk->last_load_issues().front().stage, DagConfigIssue::Stage::Parse);

    EXPECT_EQ(sdk->load_graph_string(
        R"({"version":"2.0","tasks":[{"id":"a"}],"edges":[{"from":"a","to":"zz"}]})"),
        SdkStatus::GRAPH_INVALID);
    EXPECT_EQ(sdk->last_load_issues().front().stage, DagConfigIssue::Stage::Semantics);

    // 拼错类型名 + require_known_types
    SdkConfig strict = base_config();
    strict.require_known_types = true;
    auto sdk2 = TaskGraphSdk::create();
    ASSERT_EQ(sdk2->init(strict), SdkStatus::OK);
    EXPECT_EQ(sdk2->load_graph_string(
        R"({"version":"2.0","tasks":[{"id":"x","type":"test_appand"}]})"),
        SdkStatus::GRAPH_INVALID);

    // 好图:快照 + 编译预检
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_NE(sdk->graph_config(), nullptr);
    EXPECT_EQ(sdk->graph_config()->tasks().size(), 3u);
    EXPECT_TRUE(sdk->validate_graph().empty());
    EXPECT_EQ(sdk->input_nodes(), (std::vector<std::string>{"src"}));
    EXPECT_EQ(sdk->output_nodes(), (std::vector<std::string>{"dst"}));
}

// ====================== 绑定与执行 ======================

TEST(SdkExecute, BindExecuteRebind) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);

    // 未绑定 → INVALID_ARGUMENT,消息含节点 id
    EXPECT_EQ(sdk->execute(), SdkStatus::INVALID_ARGUMENT);
    EXPECT_NE(sdk->last_error().find("'src'"), std::string::npos);

    // 绑定 + 执行 + 拉取
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("img")), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    auto out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "img-A");

    // 换绑复用同一张图
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("video-frame")), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "video-frame-A");

    // unbind 后回到未绑定失败
    ASSERT_EQ(sdk->unbind_input("src"), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::INVALID_ARGUMENT);
    EXPECT_EQ(sdk->unbind_input("src"), SdkStatus::INVALID_ARGUMENT);
}

TEST(SdkExecute, BindTypeMismatch) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);

    // data_type 声明 std::string,绑 int → TYPE_MISMATCH,消息含两侧类型
    EXPECT_EQ(sdk->bind_input("src", std::any(42)), SdkStatus::TYPE_MISMATCH);
    EXPECT_NE(sdk->last_error().find("std::string"), std::string::npos);

    // 非 io_input 节点 → INVALID_ARGUMENT
    EXPECT_EQ(sdk->bind_input<std::string>("proc", std::string("x")),
              SdkStatus::INVALID_ARGUMENT);
    EXPECT_EQ(sdk->bind_output("src"), SdkStatus::INVALID_ARGUMENT);
    // 输出绑到输入节点 / 输入绑到输出节点
    EXPECT_EQ(sdk->bind_output("src"), SdkStatus::INVALID_ARGUMENT);
    EXPECT_EQ(sdk->bind_input<std::string>("dst", std::string("x")),
              SdkStatus::INVALID_ARGUMENT);
}

TEST(SdkExecute, PushModeOutput) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("data")), SdkStatus::OK);

    std::string got_id, got_value;
    ASSERT_EQ(sdk->bind_output("dst",
                [&](const std::string& id, const std::any& value) {
                    got_id = id;
                    got_value = std::any_cast<std::string>(value);
                }),
              SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(got_id, "dst");
    EXPECT_EQ(got_value, "data-A");
    // 推拉并存
    auto out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "data-A");
    // 未绑定的输出节点拉取 → nullopt + last_error
    EXPECT_FALSE(sdk->get_output<std::string>("nonexistent").has_value());
    EXPECT_FALSE(sdk->last_error().empty());
}

TEST(SdkExecute, GlobalAndEnvContext) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    SdkConfig cfg = base_config();
    cfg.env = {{"DEVICE", "metal"}};
    cfg.globals = {{"mode", std::any(std::string("prod"))}};
    ASSERT_EQ(sdk->init(cfg), SdkStatus::OK);

    const char* j = R"({
      "version": "2.0",
      "tasks": [
        { "id": "g", "type": "test_read_global", "params": { "env_key": "DEVICE" } },
        { "id": "dst", "type": "io_output" }
      ],
      "edges": [ { "from": "g", "from_port": "out", "to": "dst", "to_port": "in" } ]
    })";
    ASSERT_EQ(sdk->load_graph_string(j), SdkStatus::OK);
    // 注意:g 无输入,不需要绑定
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    auto out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "metal|prod");

    // set_global 运行期更新,下次执行生效
    ASSERT_EQ(sdk->set_global("mode", std::any(std::string("debug"))), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "metal|debug");
}

TEST(SdkExecute, AsyncExecute) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("async")), SdkStatus::OK);

    auto fut = sdk->execute_async();
    ASSERT_EQ(fut.get(), SdkStatus::OK);
    auto out = sdk->get_output<std::string>("dst");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "async-A");
}

// ====================== diff 更新 ======================

TEST(SdkUpdate, ParamsOnlyKeepsHotState) {
    register_test_tasks_once();
    AppendTask::reset_counters();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("x")), SdkStatus::OK);
    ASSERT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(*sdk->get_output<std::string>("dst"), "x-A");
    EXPECT_EQ(AppendTask::distinct_instances(), 1u);

    // params-only 变化:suffix -A → -B
    std::string updated = R"({
      "version": "2.0",
      "tasks": [
        { "id": "src",  "type": "io_input", "params": { "data_type": "std::string" } },
        { "id": "proc", "type": "test_append", "params": { "suffix": "-B" } },
        { "id": "dst",  "type": "io_output" }
      ],
      "edges": [
        { "from": "src",  "from_port": "out", "to": "proc", "to_port": "in" },
        { "from": "proc", "from_port": "out", "to": "dst",  "to_port": "in" }
      ]
    })";
    EXPECT_EQ(sdk->update_graph_string(updated), SdkStatus::OK);
    const GraphDiff& diff = sdk->last_diff();
    EXPECT_EQ(diff.tasks_updated, (std::vector<std::string>{"proc"}));
    EXPECT_TRUE(diff.tasks_added.empty());
    EXPECT_TRUE(diff.tasks_removed.empty());
    EXPECT_FALSE(diff.topology_changed());

    // 输入绑定存活 + 新参数生效 + 任务实例保留(distinct_instances 不增长)
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(*sdk->get_output<std::string>("dst"), "x-B");
    EXPECT_EQ(AppendTask::distinct_instances(), 1u);
    // 再执行一次:同一实例继续服务
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(*sdk->get_output<std::string>("dst"), "x-B");
    EXPECT_EQ(AppendTask::distinct_instances(), 1u);
}

TEST(SdkUpdate, TopologyChangeAndDiffReport) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);

    // 删掉 proc,src 直连 dst;新增一个并行分支 extra
    std::string updated = R"({
      "version": "2.0",
      "tasks": [
        { "id": "src",  "type": "io_input", "params": { "data_type": "std::string" } },
        { "id": "dst",  "type": "io_output" },
        { "id": "extra", "type": "test_append", "params": { "suffix": "-E" } }
      ],
      "edges": [
        { "from": "src", "from_port": "out", "to": "dst", "to_port": "in" },
        { "from": "src", "from_port": "out", "to": "extra", "to_port": "in" }
      ]
    })";
    EXPECT_EQ(sdk->update_graph_string(updated), SdkStatus::OK);
    const GraphDiff& diff = sdk->last_diff();
    EXPECT_EQ(diff.tasks_removed, (std::vector<std::string>{"proc"}));
    EXPECT_EQ(diff.tasks_added, (std::vector<std::string>{"extra"}));
    ASSERT_EQ(diff.edges_removed.size(), 2u);
    ASSERT_EQ(diff.edges_added.size(), 2u);
    EXPECT_TRUE(diff.topology_changed());

    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("direct")), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(*sdk->get_output<std::string>("dst"), "direct");
}

TEST(SdkUpdate, InvalidUpdateKeepsOldGraph) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("x")), SdkStatus::OK);

    EXPECT_EQ(sdk->update_graph_string("{ broken"), SdkStatus::GRAPH_INVALID);
    // 旧图原样保留,继续可执行
    EXPECT_EQ(sdk->execute(), SdkStatus::OK);
    EXPECT_EQ(*sdk->get_output<std::string>("dst"), "x-A");
}

// ====================== 等价性 ======================

TEST(SdkEquivalence, MatchesBareExecutorPath) {
    register_test_tasks_once();
    // SDK 路径
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("golden")), SdkStatus::OK);
    ASSERT_EQ(sdk->execute(), SdkStatus::OK);
    auto via_sdk = *sdk->get_output<std::string>("dst");

    // 裸路径:程序化 DAG(lambda 源 → 同一 AppendTask)过 DAGExecutor
    DAG dag;
    dag.add_task(std::make_shared<Task>("src", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = std::string("golden")};
    }));
    TaskConfig cfg;
    cfg.params.set_string("suffix", "-A");
    dag.add_task(std::make_shared<AppendTask>("proc", cfg));
    dag.connect("src", "proc");
    DAGExecutor exec;
    auto fut = exec.execute(dag);
    fut.wait();
    auto results = exec.get_results();
    ASSERT_TRUE(results.count("proc"));
    EXPECT_TRUE(results["proc"].is_success());
    // SDK 路径与直连插件任务输出一致
    EXPECT_EQ(via_sdk, "golden-A");
    EXPECT_EQ(std::any_cast<std::string>(results["proc"].value), "golden-A");
}

// ====================== 按任意节点访问上次执行结果 ======================

TEST(SdkTaskResults, PerNodeStatusAndOutput) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    ASSERT_EQ(sdk->load_graph_string(kPipeline), SdkStatus::OK);

    // 未执行:无结果记录
    EXPECT_EQ(sdk->task_status("proc"), std::nullopt);
    EXPECT_EQ(sdk->task_output("proc"), std::nullopt);
    EXPECT_TRUE(sdk->executed_tasks().empty());

    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("x")), SdkStatus::OK);
    ASSERT_EQ(sdk->execute(), SdkStatus::OK);

    // 逐节点状态与输出(非 io_output 节点)
    EXPECT_EQ(sdk->task_status("src"), TaskStatus::COMPLETED);
    EXPECT_EQ(sdk->task_status("proc"), TaskStatus::COMPLETED);
    auto mid = sdk->task_output("proc");
    ASSERT_TRUE(mid.has_value());
    EXPECT_EQ(std::any_cast<std::string>(*mid), "x-A");
    // io_input 的输出 = 绑定值
    auto srcv = sdk->task_output("src");
    ASSERT_TRUE(srcv.has_value());
    EXPECT_EQ(std::any_cast<std::string>(*srcv), "x");
    // 节点不存在
    EXPECT_EQ(sdk->task_status("nope"), std::nullopt);
    EXPECT_EQ(sdk->executed_tasks().size(), 3u);

    // 下次 execute 清空并重建
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("y")), SdkStatus::OK);
    ASSERT_EQ(sdk->execute(), SdkStatus::OK);
    auto mid2 = sdk->task_output("proc");
    ASSERT_TRUE(mid2.has_value());
    EXPECT_EQ(std::any_cast<std::string>(*mid2), "y-A");
    EXPECT_EQ(sdk->executed_tasks().size(), 3u);
}

TEST(SdkTaskResults, FailedTaskHasStatusButNoOutput) {
    register_test_tasks_once();
    auto sdk = TaskGraphSdk::create();
    ASSERT_EQ(sdk->init(base_config()), SdkStatus::OK);
    // proc 无输入(直连 src→dst,proc 悬空)→ proc FAILED
    ASSERT_EQ(sdk->load_graph_string(R"({
      "version": "2.0",
      "tasks": [
        { "id": "src", "type": "io_input", "params": { "data_type": "std::string" } },
        { "id": "proc", "type": "test_append", "params": { "suffix": "-Z" } },
        { "id": "dst", "type": "io_output" }
      ],
      "edges": [ { "from": "src", "from_port": "out", "to": "dst", "to_port": "in" } ]
    })"), SdkStatus::OK);
    ASSERT_EQ(sdk->bind_input<std::string>("src", std::string("x")), SdkStatus::OK);
    EXPECT_EQ(sdk->execute(), SdkStatus::INTERNAL_ERROR);
    EXPECT_EQ(sdk->task_status("proc"), TaskStatus::FAILED);
    EXPECT_EQ(sdk->task_output("proc"), std::nullopt);   // 失败任务无输出
    EXPECT_EQ(sdk->task_status("src"), TaskStatus::COMPLETED);
}
