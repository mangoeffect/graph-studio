#include <task_graph/task_graph.hpp>
#include <task_graph/profiler.hpp>
#include <task_graph/task_context.hpp>
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <any>

// 测试 1：启用 profiler 后，JSON 导出包含正确的 DAG 概览与任务统计
TEST(Profiler, JsonExport) {
    task_graph::DAG dag;

    auto task_a = std::make_shared<task_graph::Task>(
        "A", "producer",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 1};
        }
    );
    auto task_b = std::make_shared<task_graph::Task>(
        "B", "consumer",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 2};
        }
    );

    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");

    task_graph::ExecutorConfig cfg;
    cfg.enable_profiling = true;
    task_graph::DAGExecutor executor(cfg);
    executor.execute(dag).wait();

    // 验证 TaskResult.duration 已被填充（此前始终为 0）
    auto results = executor.get_results();
    EXPECT_GT(results["A"].duration.count(), 0);
    EXPECT_GT(results["B"].duration.count(), 0);

    // 验证 JSON 导出可解析
    std::string json_str = executor.profiler().to_json_string(true);
    nlohmann::json j;
    ASSERT_NO_THROW(j = nlohmann::json::parse(json_str)) << "profiler json parse error";

    ASSERT_TRUE(j.contains("dag_overview"));
    EXPECT_EQ(j["dag_overview"]["total_tasks"].get<int>(), 2);
    EXPECT_EQ(j["dag_overview"]["completed_tasks"].get<int>(), 2);

    ASSERT_TRUE(j.contains("tasks"));
    EXPECT_EQ(j["tasks"].size(), size_t(2));
    for (const auto& t : j["tasks"]) {
        EXPECT_TRUE(t.contains("task_id"));
        EXPECT_TRUE(t.contains("task_type"));
        EXPECT_TRUE(t.contains("final_status"));
        EXPECT_TRUE(t.contains("exec_duration_ms"));
    }

    // 每个 task 至少 READY+STARTED+COMPLETED
    ASSERT_TRUE(j.contains("events"));
    EXPECT_GE(j["events"].size(), size_t(4));
}

// 测试 2：自定义回调能收到任务生命周期事件
TEST(Profiler, CustomCallback) {
    task_graph::DAG dag;

    std::atomic<int> ready_count{0};
    std::atomic<int> started_count{0};
    std::atomic<int> completed_count{0};

    auto task = std::make_shared<task_graph::Task>(
        "T", "worker",
        [](task_graph::TaskContext&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    dag.add_task(task);

    task_graph::ExecutorConfig cfg;
    cfg.callback = [&](const task_graph::ExecutionEvent& ev) {
        if (ev.task_id != "T") return;
        switch (ev.type) {
            case task_graph::ExecutionEvent::Type::TaskReady:     ready_count++; break;
            case task_graph::ExecutionEvent::Type::TaskStarted:   started_count++; break;
            case task_graph::ExecutionEvent::Type::TaskCompleted: completed_count++; break;
            default: break;
        }
    };
    task_graph::DAGExecutor executor(cfg);
    executor.execute(dag).wait();

    EXPECT_EQ(ready_count.load(), 1);
    EXPECT_EQ(started_count.load(), 1);
    EXPECT_EQ(completed_count.load(), 1);
}

// 测试 3：TaskStats 聚合计算正确（等待/执行/总耗时）
TEST(Profiler, TaskStatsAggregation) {
    task_graph::DAG dag;

    auto task_a = std::make_shared<task_graph::Task>(
        "A", "src",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    auto task_b = std::make_shared<task_graph::Task>(
        "B", "dst",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");

    task_graph::ExecutorConfig cfg;
    cfg.enable_profiling = true;
    task_graph::DAGExecutor executor(cfg);
    executor.execute(dag).wait();

    auto stats = executor.profiler().compute_task_stats();
    auto dag_stats = executor.profiler().compute_dag_stats();

    ASSERT_EQ(stats.size(), size_t(2));
    // B 的执行耗时（~15ms）应小于 A 的执行耗时（~30ms）
    auto find_task = [&](const std::string& id) -> const task_graph::TaskStats* {
        for (const auto& s : stats) if (s.task_id == id) return &s;
        return nullptr;
    };
    const auto* sa = find_task("A");
    const auto* sb = find_task("B");
    ASSERT_NE(sa, nullptr);
    ASSERT_NE(sb, nullptr);

    EXPECT_GT(sa->exec_duration.count(), 0);
    EXPECT_GT(sb->exec_duration.count(), 0);
    EXPECT_GT(sa->exec_duration, sb->exec_duration);
    EXPECT_EQ(sa->final_status, task_graph::TaskStatus::COMPLETED);
    EXPECT_EQ(sb->final_status, task_graph::TaskStatus::COMPLETED);

    EXPECT_EQ(dag_stats.total_tasks, 2);
    EXPECT_EQ(dag_stats.completed_tasks, 2);
    EXPECT_GT(dag_stats.total_duration.count(), 0);
}

// 测试 4：未启用 profiler 时无开销且不采集
TEST(Profiler, DisabledByDefault) {
    task_graph::DAG dag;
    auto task = std::make_shared<task_graph::Task>(
        "X", "noop",
        [](task_graph::TaskContext&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    dag.add_task(task);

    task_graph::DAGExecutor executor;  // 默认 enable_profiling=false
    executor.execute(dag).wait();

    // 未启用时应无事件采集
    EXPECT_TRUE(executor.profiler().raw_events().empty());
}

// 测试 5：Chrome Trace Event 导出与文件落盘
TEST(Profiler, ChromeTraceExport) {
    task_graph::DAG dag;

    auto task_a = std::make_shared<task_graph::Task>(
        "producer", "src",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    auto task_b = std::make_shared<task_graph::Task>(
        "consumer", "dst",
        [](task_graph::TaskContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("producer", "consumer");

    task_graph::ExecutorConfig cfg;
    cfg.enable_profiling = true;
    task_graph::DAGExecutor executor(cfg);
    executor.execute(dag).wait();

    // 验证 trace 字符串可解析且包含必需字段
    std::string trace_str = executor.profiler().to_trace_string(true);
    nlohmann::json trace;
    ASSERT_NO_THROW(trace = nlohmann::json::parse(trace_str)) << "trace json parse error";

    // 至少存在一个进程元数据事件 + 两个任务的执行事件
    ASSERT_TRUE(trace.contains("traceEvents"));
    EXPECT_GE(trace["traceEvents"].size(), size_t(3));
    bool has_metadata = false;
    bool has_complete_event = false;
    for (const auto& ev : trace["traceEvents"]) {
        if (ev["ph"].get<std::string>() == "M" && ev["name"].get<std::string>() == "process_name") {
            has_metadata = true;
        }
        if (ev["ph"].get<std::string>() == "X" && ev["cat"].get<std::string>() == "exec") {
            has_complete_event = true;
            // 校验完整事件必备字段
            if (!ev.contains("ts") || !ev.contains("dur") || !ev.contains("pid") || !ev.contains("tid")) {
                has_complete_event = false;
                break;
            }
        }
    }
    EXPECT_TRUE(has_metadata);
    EXPECT_TRUE(has_complete_event);

    // 验证文件落盘（写到当前工作目录，避免硬编码 build/ 路径导致 CWD 依赖）
    EXPECT_TRUE(executor.profiler().save_trace_report("profiler_trace.json"));
}
