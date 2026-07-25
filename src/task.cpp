#include <task_graph/task.hpp>
#include <task_graph/context.hpp>
#include <plugin_api.hpp>
#include <sstream>
#include <any>

namespace task_graph {

Task::Task(std::string id, TaskFunction func, TaskConfig config)
    : IPluginTask(std::move(id), std::move(config)), func_(std::move(func)) {
    type_ = this->id();
}

Task::Task(std::string id, std::string type, TaskFunction func, TaskConfig config)
    : IPluginTask(std::move(id), std::move(config)), type_(std::move(type)), func_(std::move(func)) {}

CheckResult Task::check_input(const std::vector<std::any>& inputs) const {
    return CheckResult(true);
}

TaskResult Task::execute(TaskContext& ctx) {
    TaskResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    TG_LOG_INFO("Starting execution of task '" + id() + "'");
    TG_LOG_DEBUG("Task '" + id() + "' priority: " + std::to_string(static_cast<int>(config().priority)) +
                 ", max_retries: " + std::to_string(config().max_retries) +
                 ", timeout: " + std::to_string(config().timeout.count()) + "ms");

    try {
        result = func_(ctx);
    } catch (const std::exception& e) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id() + "' failed with exception: " + e.what());
    } catch (...) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id() + "' failed with unknown exception");
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.duration = end_time - start_time;

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(result.duration).count();
    std::stringstream ss;
    ss << "Task '" << id() << "' completed with status ";
    
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