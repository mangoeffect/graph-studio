// 模型查找服务（ModelFinder）测试。校验全局槽的安装/覆盖/清除语义、
// 未安装与未命中返回空串、并发查询线程安全，以及 DAG 任务在
// on_init/execute 路径上经 find_model 解析模型名的基本用法
//（对齐 mediapipe 任务的"finder 优先、_source_dir 路径回退"解析顺序）。
#include <plugin_api.hpp>
#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/path_utils.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

using namespace task_graph;

// 全局槽测试需要干净起点，且不能把捕获已销毁局部变量的 finder 留给后续用例
class ModelFinderTest : public ::testing::Test {
protected:
    void SetUp() override { clear_model_finder(); }
    void TearDown() override { clear_model_finder(); }
};

TEST_F(ModelFinderTest, no_finder_returns_empty) {
    EXPECT_TRUE(find_model("face_landmarker.task").empty());
}

TEST_F(ModelFinderTest, set_and_find) {
    set_model_finder([](const std::string& name) {
        return name == "face_landmarker.task" ? "/models/face_landmarker.task"
                                              : std::string();
    });
    EXPECT_EQ(find_model("face_landmarker.task"), "/models/face_landmarker.task");
    // 未命中：回调返回空串
    EXPECT_TRUE(find_model("unknown.model").empty());
}

TEST_F(ModelFinderTest, overwrite_and_clear) {
    set_model_finder([](const std::string&) { return std::string("/v1"); });
    set_model_finder([](const std::string&) { return std::string("/v2"); });
    EXPECT_EQ(find_model("m"), "/v2");

    clear_model_finder();
    EXPECT_TRUE(find_model("m").empty());

    // 空 finder 等同清除
    set_model_finder([](const std::string&) { return std::string("/v3"); });
    set_model_finder(nullptr);
    EXPECT_TRUE(find_model("m").empty());
}

TEST_F(ModelFinderTest, empty_name_is_forwarded) {
    // 空名称照常进回调（由宿主决定语义），SDK 不做特殊处理
    set_model_finder([](const std::string& name) {
        return name.empty() ? std::string("<empty>") : std::string("/models/") + name;
    });
    EXPECT_EQ(find_model(""), "<empty>");
}

TEST_F(ModelFinderTest, concurrent_queries_with_finder_swapping) {
    // 并发压力：多线程查询 + 主线程换槽。锁内拷贝/锁外调用保证无死锁、
    // 无崩溃；命中计数精确（同一时刻槽内只有一个 finder）。
    constexpr int kThreads = 4;
    constexpr int kIters = 2000;
    std::atomic<int> hits{0};
    std::atomic<bool> stop{false};

    set_model_finder([](const std::string& name) {
        return name == "m" ? std::string("/models/m") : std::string();
    });

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                if (!find_model("m").empty()) hits++;
            }
        });
    }
    for (int i = 0; i < 200; ++i) {
        set_model_finder([](const std::string& name) {
            return name == "m" ? std::string("/models/m") : std::string();
        });
    }
    stop = true;
    for (auto& th : workers) th.join();

    // 换槽期间槽内始终有 finder，所有查询都应命中
    EXPECT_EQ(hits.load(), kThreads * kIters);
    (void)stop;
}

TEST_F(ModelFinderTest, finder_may_reenter_logging) {
    // 契约：回调在锁外执行，内部可安全再调 tg_log
    set_model_finder([](const std::string& name) {
        tg_log(LogLevel::DEBUG, "model finder queried: " + name);
        return std::string("/models/") + name;
    });
    EXPECT_EQ(find_model("m"), "/models/m");
}

// 复现 mediapipe 任务的解析顺序：finder 优先，未命中回退 _source_dir 相对路径。
// 该顺序由插件 MediaPipeVisionTaskBase::resolve_model_path 实现，这里在核心侧
// 把语义钉住（finder 语义变化时先在这里暴露）。
static std::string resolve_like_plugin(const std::string& raw,
                                       const std::string& source_dir) {
    if (const std::string hit = find_model(raw); !hit.empty()) return hit;
    return resolve_path(source_dir, raw);
}

// Windows 的 lexically_normal 会把分隔符规范成 '\'，期望值同样经 fs::path 构造
static std::string expect_path(const std::string& base, const std::string& rel) {
    return (std::filesystem::path(base) / rel).lexically_normal().string();
}

TEST_F(ModelFinderTest, plugin_resolution_order_finder_first_then_path) {
    // finder 未装：退化为路径解析
    EXPECT_EQ(resolve_like_plugin("models/face.task", "/graphs"),
              expect_path("/graphs", "models/face.task"));

    // finder 装了但未命中（返回空）：同样回退路径解析
    set_model_finder([](const std::string&) { return std::string(); });
    EXPECT_EQ(resolve_like_plugin("models/face.task", "/graphs"),
              expect_path("/graphs", "models/face.task"));

    // finder 命中：优先使用 finder 结果（即使值看起来像相对路径）
    set_model_finder([](const std::string& name) {
        return name == "face.task" ? std::string("/app/models/face.task")
                                   : std::string();
    });
    EXPECT_EQ(resolve_like_plugin("face.task", "/graphs"),
              "/app/models/face.task");
}

// 自定义 INode：on_init 阶段（无 TaskContext，只有 config params）解析模型，
// execute 把解析结果作为输出 —— 与 mediapipe 任务的调用形态一致
class ModelTaskNode : public INode {
public:
    ModelTaskNode(const std::string& id, const TaskConfig& cfg)
        : INode(id, cfg), resolved_("<unset>") {}

    const std::string& type() const override {
        static const std::string t("test_model_task");
        return t;
    }

    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override {
        return {PortSpec{"out", "", false}};
    }

protected:
    void on_init() override {
        const std::string raw = config().params.get_string("model_path").value_or("");
        const std::string base =
            config().params.get_string(kSourceDirParam).value_or("");
        resolved_ = resolve_like_plugin(raw, base);
    }

    TaskResult execute(TaskContext&) override {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = resolved_};
    }

private:
    std::string resolved_;
};

TEST_F(ModelFinderTest, dag_task_resolves_model_in_on_init) {
    set_model_finder([](const std::string& name) {
        return name == "face_landmarker.task"
                   ? std::string("/app/models/face_landmarker.task")
                   : std::string();
    });

    TaskConfig cfg;
    cfg.params.set_string("model_path", "face_landmarker.task");
    cfg.params.set_string(kSourceDirParam, "/graphs");
    DAG dag;
    dag.add_task(std::make_shared<ModelTaskNode>("model", cfg));

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    ASSERT_TRUE(results.count("model"));
    EXPECT_TRUE(results["model"].is_success());
    EXPECT_EQ(std::any_cast<std::string>(results["model"].value),
              "/app/models/face_landmarker.task");
}

TEST_F(ModelFinderTest, dag_task_falls_back_to_source_dir_when_miss) {
    set_model_finder([](const std::string&) { return std::string(); });

    TaskConfig cfg;
    cfg.params.set_string("model_path", "models/pose_landmarker.task");
    cfg.params.set_string(kSourceDirParam, "/graphs");
    DAG dag;
    dag.add_task(std::make_shared<ModelTaskNode>("model", cfg));

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    ASSERT_TRUE(results.count("model"));
    EXPECT_TRUE(results["model"].is_success());
    EXPECT_EQ(std::any_cast<std::string>(results["model"].value),
              expect_path("/graphs", "models/pose_landmarker.task"));
}
