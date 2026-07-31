#include <task_graph/task_graph.hpp>
#include <task_graph/profiler.hpp>
#include <task_graph/task_context.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <any>

// 测试 1：启用 profiler 后，JSON 导出包含正确的 DAG 概览与任务统计
bool test_profiler_json_export() {
    std::cout << "Test: Profiler JSON export... ";

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
    bool duration_filled = results["A"].duration.count() > 0 && results["B"].duration.count() > 0;

    // 验证 JSON 导出可解析
    std::string json_str = executor.profiler().to_json_string(true);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);
    } catch (...) {
        std::cout << "FAILED (json parse error)" << std::endl;
        return false;
    }

    bool has_overview = j.contains("dag_overview") &&
                        j["dag_overview"]["total_tasks"].get<int>() == 2 &&
                        j["dag_overview"]["completed_tasks"].get<int>() == 2;
    bool has_tasks = j.contains("tasks") && j["tasks"].size() == 2;
    bool has_events = j.contains("events") && j["events"].size() >= 4;  // 每个 task 至少 READY+STARTED+COMPLETED

    // 验证任务统计字段完整
    bool task_fields_ok = true;
    if (has_tasks) {
        for (const auto& t : j["tasks"]) {
            if (!t.contains("task_id") || !t.contains("task_type") ||
                !t.contains("final_status") || !t.contains("exec_duration_ms")) {
                task_fields_ok = false;
                break;
            }
        }
    }

    bool success = duration_filled && has_overview && has_tasks && has_events && task_fields_ok;
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

// 测试 2：自定义回调能收到任务生命周期事件
bool test_profiler_callback() {
    std::cout << "Test: Profiler custom callback... ";

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

    bool success = ready_count == 1 && started_count == 1 && completed_count == 1;
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

// 测试 3：TaskStats 聚合计算正确（等待/执行/总耗时）
bool test_profiler_task_stats() {
    std::cout << "Test: Profiler task stats aggregation... ";

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

    bool size_ok = stats.size() == 2;
    // B 的执行耗时（~15ms）应小于 A 的执行耗时（~30ms）
    auto find_task = [&](const std::string& id) -> const task_graph::TaskStats* {
        for (const auto& s : stats) if (s.task_id == id) return &s;
        return nullptr;
    };
    const auto* sa = find_task("A");
    const auto* sb = find_task("B");

    bool exec_ok = sa && sb &&
                   sa->exec_duration.count() > 0 && sb->exec_duration.count() > 0 &&
                   sa->exec_duration > sb->exec_duration;
    bool status_ok = sa && sb &&
                     sa->final_status == task_graph::TaskStatus::COMPLETED &&
                     sb->final_status == task_graph::TaskStatus::COMPLETED;
    bool dag_ok = dag_stats.total_tasks == 2 && dag_stats.completed_tasks == 2 &&
                  dag_stats.total_duration.count() > 0;

    bool success = size_ok && exec_ok && status_ok && dag_ok;
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

// 测试 4：未启用 profiler 时无开销且不采集
bool test_profiler_disabled() {
    std::cout << "Test: Profiler disabled by default... ";

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
    bool success = executor.profiler().raw_events().empty();
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

// 测试 5：Chrome Trace Event 导出与文件落盘
bool test_profiler_trace_export() {
    std::cout << "Test: Profiler Chrome Trace Event export... ";

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
    try {
        trace = nlohmann::json::parse(trace_str);
    } catch (...) {
        std::cout << "FAILED (json parse error)" << std::endl;
        return false;
    }

    bool has_trace_events = trace.contains("traceEvents") && trace["traceEvents"].size() >= 3;
    // 至少存在一个进程元数据事件 + 两个任务的执行事件
    bool has_metadata = false;
    bool has_complete_event = false;
    if (has_trace_events) {
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
    }

    // 验证文件落盘（写到当前工作目录，避免硬编码 build/ 路径导致 CWD 依赖）
    bool save_trace_ok = executor.profiler().save_trace_report("profiler_trace.json");

    bool success = has_trace_events && has_metadata && has_complete_event && save_trace_ok;
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    if (save_trace_ok) {
        std::cout << "  -> Trace 已生成: profiler_trace.json (用 chrome://tracing 打开)" << std::endl;
    }
    return success;
}

int main() {
    std::vector<bool> results = {
        test_profiler_json_export(),
        test_profiler_callback(),
        test_profiler_task_stats(),
        test_profiler_disabled(),
        test_profiler_trace_export(),
    };

    size_t passed = 0;
    for (bool r : results) if (r) passed++;

    std::cout << "\n" << passed << "/" << results.size() << " tests passed" << std::endl;
    return passed == results.size() ? 0 : 1;
}
