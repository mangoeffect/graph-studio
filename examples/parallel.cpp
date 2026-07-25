#include <task_graph/task_graph.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <task_graph/task_context.hpp>

int main() {
    std::cout << "=== Parallel DAG Example ===\n" << std::endl;

    task_graph::DAG dag;

    auto task_init = std::make_shared<task_graph::Task>(
        "init",
        [](task_graph::TaskContext& ctx) {
            std::cout << "[init] Initializing...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_health = std::make_shared<task_graph::Task>(
        "health_check",
        [](task_graph::TaskContext& ctx) {
            std::cout << "[health_check] Checking health...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 95};
        }
    );

    auto task_analysis = std::make_shared<task_graph::Task>(
        "data_analysis",
        [](task_graph::TaskContext& ctx) {
            std::cout << "[data_analysis] Analyzing data...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = std::string("positive")};
        }
    );

    auto task_report = std::make_shared<task_graph::Task>(
        "generate_report",
        [](task_graph::TaskContext& ctx) {
            std::cout << "[generate_report] Generating report...\n";
            auto health = ctx.get_input<int>("health_check");
            auto analysis = ctx.get_input<std::string>("data_analysis");
            if (health && analysis) {
                std::cout << "[generate_report] Report: Health=" << *health
                          << ", Analysis=" << *analysis << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    dag.add_task(task_init);
    dag.add_task(task_health);
    dag.add_task(task_analysis);
    dag.add_task(task_report);

    dag.add_dependency("init", "health_check");
    dag.add_dependency("init", "data_analysis");
    dag.add_dependency("health_check", "generate_report");
    dag.add_dependency("data_analysis", "generate_report");

    std::cout << "DAG structure:\n";
    std::cout << "        init\n";
    std::cout << "       /   \\\n";
    std::cout << "  health  analysis\n";
    std::cout << "       \\   /\n";
    std::cout << "    report\n\n";

    task_graph::DAGExecutor executor;
    auto start_time = std::chrono::high_resolution_clock::now();
    auto future = executor.execute(dag);
    future.wait();
    auto end_time = std::chrono::high_resolution_clock::now();

    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    auto results = executor.get_results();
    std::cout << "\nExecution results:\n";
    for (const auto& [id, result] : results) {
        std::cout << "  " << id << ": "
                  << (result.is_success() ? "SUCCESS" : "FAILED")
                  << " (" << std::chrono::duration_cast<std::chrono::milliseconds>(result.duration).count() << " ms)\n";
    }

    std::cout << "\nTotal time: " << total_time.count() << " ms\n";
    std::cout << "Expected serial time: ~500 ms\n";
    std::cout << "Expected parallel time: ~300 ms\n";

    std::cout << "\nDone!\n";
    return 0;
}
