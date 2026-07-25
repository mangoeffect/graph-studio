#include <task_graph/task_graph.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <any>

bool test_basic_dag() {
    std::cout << "Test: Basic DAG execution... ";

    task_graph::DAG dag;

    std::atomic<int> counter{0};

    auto task_a = std::make_shared<task_graph::Task>(
        "A",
        [&](task_graph::IExecutionContext& ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 1};
        }
    );

    auto task_b = std::make_shared<task_graph::Task>(
        "B",
        [&](task_graph::IExecutionContext& ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 2};
        }
    );

    auto task_c = std::make_shared<task_graph::Task>(
        "C",
        [&](task_graph::IExecutionContext& ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 3};
        }
    );

    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_task(task_c);
    dag.add_dependency("A", "C");
    dag.add_dependency("B", "C");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    bool success = results.size() == 3 && counter == 3;

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_cycle_detection() {
    std::cout << "Test: Cycle detection... ";

    task_graph::DAG dag;

    auto task_a = std::make_shared<task_graph::Task>(
        "A",
        [](task_graph::IExecutionContext& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_b = std::make_shared<task_graph::Task>(
        "B",
        [](task_graph::IExecutionContext& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    dag.add_dependency("B", "A");

    task_graph::DAGCompiler compiler;
    bool has_cycle = compiler.has_cycle(dag);

    std::cout << (has_cycle ? "PASSED" : "FAILED") << std::endl;
    return has_cycle;
}

bool test_parallel_execution() {
    std::cout << "Test: Parallel execution... ";

    task_graph::DAG dag;

    std::atomic<int> parallel_count{0};
    std::atomic<int> max_parallel{0};
    std::mutex mtx;

    auto task_a = std::make_shared<task_graph::Task>(
        "A",
        [&](task_graph::IExecutionContext& ctx) {
            int count = parallel_count.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (count + 1 > max_parallel) {
                    max_parallel = count + 1;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            parallel_count.fetch_sub(1);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_b = std::make_shared<task_graph::Task>(
        "B",
        [&](task_graph::IExecutionContext& ctx) {
            int count = parallel_count.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (count + 1 > max_parallel) {
                    max_parallel = count + 1;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            parallel_count.fetch_sub(1);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_c = std::make_shared<task_graph::Task>(
        "C",
        [&](task_graph::IExecutionContext& ctx) {
            int count = parallel_count.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (count + 1 > max_parallel) {
                    max_parallel = count + 1;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            parallel_count.fetch_sub(1);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_task(task_c);

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    bool success = max_parallel >= 2;

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_data_passing() {
    std::cout << "Test: Data passing between tasks... ";

    task_graph::DAG dag;

    auto task_a = std::make_shared<task_graph::Task>(
        "A",
        [](task_graph::IExecutionContext& ctx) {
            ctx.set_value("shared_key", 42);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
        }
    );

    auto task_b = std::make_shared<task_graph::Task>(
        "B",
        [](task_graph::IExecutionContext& ctx) {
            auto value = ctx.get<int>("shared_key");
            if (value && *value == 42) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *value * 2};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        }
    );

    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    bool success = results["B"].is_success();
    auto result_value = std::any_cast<int>(results["B"].value);
    success &= (result_value == 84);

    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;

    results.push_back(test_basic_dag());
    results.push_back(test_cycle_detection());
    results.push_back(test_parallel_execution());
    results.push_back(test_data_passing());

    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;

    return passed == results.size() ? 0 : 1;
}
