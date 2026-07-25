#include <task_graph/task.hpp>
#include <task_graph/context.hpp>

namespace task_graph {

Task::Task(std::string id, TaskFunction func, TaskConfig config)
    : id_(std::move(id)), func_(std::move(func)), config_(std::move(config)) {}

TaskResult Task::execute(ExecutionContext& ctx) {
    TaskResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        result = func_(ctx);
    } catch (...) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.duration = end_time - start_time;

    return result;
}

}
