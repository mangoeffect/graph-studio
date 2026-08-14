// DAG 构建、调度与执行的核心行为测试。
// 合并自 test_dag.cpp 与 test_dependency_injection.cpp 的执行相关用例。
#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/task_context.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <any>
#include <gtest/gtest.h>

using namespace task_graph;

static TaskPtr make_task(const std::string& id, TaskFunction fn, TaskConfig cfg = {}) {
    return std::make_shared<Task>(id, std::move(fn), std::move(cfg));
}

// 基础执行：菱形依赖 A,B -> C，全部完成
TEST(Dag, basic_dag_execution) {
    DAG dag;
    std::atomic<int> counter{0};
    auto body = [&](TaskContext&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        counter++;
        return TaskResult{.status = TaskStatus::COMPLETED};
    };
    dag.add_task(make_task("A", body));
    dag.add_task(make_task("B", body));
    dag.add_task(make_task("C", body));
    dag.connect("A", "C");
    dag.connect("B", "C");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_EQ(results.size(), size_t(3));
    EXPECT_EQ(counter.load(), 3);
}

// 环检测
TEST(Dag, cycle_detection) {
    DAG dag;
    dag.add_task(make_task("A", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.add_task(make_task("B", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.connect("A", "B");
    dag.connect("B", "A");

    DAGCompiler compiler;
    EXPECT_TRUE(compiler.has_cycle(dag));
}

// 并行执行：无依赖的三个任务应能并发运行
TEST(Dag, parallel_execution) {
    DAG dag;
    std::atomic<int> parallel_count{0};
    std::atomic<int> max_parallel{0};
    std::mutex mtx;
    auto body = [&](TaskContext&) {
        int count = parallel_count.fetch_add(1) + 1;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (count > max_parallel) max_parallel = count;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        parallel_count.fetch_sub(1);
        return TaskResult{.status = TaskStatus::COMPLETED};
    };
    dag.add_task(make_task("A", body));
    dag.add_task(make_task("B", body));
    dag.add_task(make_task("C", body));

    DAGExecutor executor;
    executor.execute(dag).wait();

#ifdef __EMSCRIPTEN__
    // WASM 单线程 build 下 ThreadPool 退化为 inline 执行，并行度恒为 1
    EXPECT_TRUE(max_parallel >= 1);
#else
    EXPECT_TRUE(max_parallel >= 2);
#endif
}

// 数据传递：A 输出 42，B 通过端口 "in" 读取并 *2
TEST(Dag, data_passing_between_tasks) {
    DAG dag;
    dag.add_task(make_task("A", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 42};
    }));
    dag.add_task(make_task("B", [](TaskContext& ctx) {
        auto v = ctx.input<int>("in");
        return v ? TaskResult{.status = TaskStatus::COMPLETED, .value = *v * 2}
                 : TaskResult{.status = TaskStatus::FAILED};
    }));
    dag.connect("A", "B");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["B"].is_success());
    EXPECT_EQ(std::any_cast<int>(results["B"].value), 84);
}

// 依赖失败传播：A 失败 -> B 应被标记失败
TEST(Dag, dependency_failure_propagation) {
    DAG dag;
    dag.add_task(make_task("A", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::FAILED};
    }));
    dag.add_task(make_task("B", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.connect("A", "B");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["A"].is_failed());
    EXPECT_TRUE(results["B"].is_failed());
}

// executor 端到端依赖：A 产出 10，B 读取 *2 = 20
TEST(Dag, executor_dependency_dataflow) {
    DAG dag;
    dag.add_task(make_task("A", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 10};
    }));
    dag.add_task(make_task("B", [](TaskContext& ctx) {
        auto a = ctx.input<int>("in");
        return a ? TaskResult{.status = TaskStatus::COMPLETED, .value = *a * 2}
                 : TaskResult{.status = TaskStatus::FAILED};
    }));
    dag.connect("A", "B");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["A"].is_success());
    EXPECT_TRUE(results["B"].is_success());
    EXPECT_EQ(std::any_cast<int>(results["B"].value), 20);
}

