#include <task_graph/task.hpp>
#include <task_graph/context.hpp>
#include <plugin_api.hpp>
#include <sstream>

namespace task_graph {

Task::Task(std::string id, TaskFunction func, TaskConfig config)
    : id_(std::move(id)), func_(std::move(func)), config_(std::move(config)) {}

TaskResult Task::execute(ExecutionContext& ctx) {
    TaskResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    TG_LOG_INFO("Starting execution of task '" + id_ + "'");
    TG_LOG_DEBUG("Task '" + id_ + "' priority: " + std::to_string(static_cast<int>(config_.priority)) +
                 ", max_retries: " + std::to_string(config_.max_retries) +
                 ", timeout: " + std::to_string(config_.timeout.count()) + "ms");

    try {
        result = func_(ctx);
    } catch (const std::exception& e) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id_ + "' failed with exception: " + e.what());
    } catch (...) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id_ + "' failed with unknown exception");
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.duration = end_time - start_time;

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(result.duration).count();
    std::stringstream ss;
    ss << "Task '" << id_ << "' completed with status ";
    
    switch (result.status) {
        case TaskStatus::COMPLETED:
            ss << "COMPLETED, duration: " << duration_ms << "ms";
            TG_LOG_INFO(ss.str());
            break;
        case TaskStatus::FAILED:
            ss << "FAILED, duration: " << duration_ms << "ms";
            TG_LOG_ERROR(ss.str());
            break;
        case TaskStatus::SKIPPED:
            ss << "SKIPPED";
            TG_LOG_INFO(ss.str());
            break;
        default:
            ss << "UNKNOWN";
            TG_LOG_WARN(ss.str());
            break;
    }

    return result;
}

}
