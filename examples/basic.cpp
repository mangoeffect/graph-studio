#include <task_graph/task_graph.hpp>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== Basic DAG Example ===\n" << std::endl;

    task_graph::DAG dag;

    auto task_fetch = std::make_shared<task_graph::Task>(
        "fetch_data",
        [](task_graph::ExecutionContext& ctx) {
            std::cout << "[fetch_data] Fetching data from database...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ctx.set_value("raw_data", std::string("user_123_data"));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_process = std::make_shared<task_graph::Task>(
        "process_data",
        [](task_graph::ExecutionContext& ctx) {
            std::cout << "[process_data] Processing data...\n";
            auto data = ctx.get<std::string>("raw_data");
            if (data) {
                ctx.set_value("processed_data", *data + "_processed");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    auto task_save = std::make_shared<task_graph::Task>(
        "save_result",
        [](task_graph::ExecutionContext& ctx) {
            std::cout << "[save_result] Saving result...\n";
            auto data = ctx.get<std::string>("processed_data");
            if (data) {
                std::cout << "[save_result] Saved: " << *data << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }
    );

    dag.add_task(task_fetch);
    dag.add_task(task_process);
    dag.add_task(task_save);
    dag.add_dependency("fetch_data", "process_data");
    dag.add_dependency("process_data", "save_result");

    std::cout << "DAG structure:\n";
    std::cout << "  fetch_data -> process_data -> save_result\n\n";

    task_graph::DAGExecutor executor;
    auto future = executor.execute(dag);

    std::cout << "Executing DAG...\n";
    future.wait();

    auto results = executor.get_results();
    std::cout << "\nExecution results:\n";
    for (const auto& [id, result] : results) {
        std::cout << "  " << id << ": " 
                  << (result.is_success() ? "SUCCESS" : "FAILED") 
                  << " (" << result.duration.count() << " ns)\n";
    }

    std::cout << "\nDone!\n";
    return 0;
}
