#pragma once

#include <functional>
#include <string>
#include <any>
#include <stdexcept>
#include <chrono>
#include <memory>

namespace task_graph {

using TaskId = std::string;

enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    SKIPPED
};

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

struct TaskResult {
    TaskStatus status{TaskStatus::PENDING};
    std::any value;
    std::exception_ptr exception{nullptr};
    std::chrono::nanoseconds duration{0};

    bool is_success() const { return status == TaskStatus::COMPLETED; }
    bool is_failed() const { return status == TaskStatus::FAILED; }
};

class ExecutionContext;

using TaskFunction = std::function<TaskResult(ExecutionContext&)>;

struct TaskConfig {
    TaskPriority priority{TaskPriority::NORMAL};
    size_t max_retries{0};
    std::chrono::milliseconds timeout{0};
    bool skip_on_fail{false};
};

class Task {
public:
    Task(std::string id, TaskFunction func, TaskConfig config = {});

    const std::string& id() const { return id_; }
    const TaskFunction& func() const { return func_; }
    const TaskConfig& config() const { return config_; }

    TaskResult execute(ExecutionContext& ctx);

private:
    std::string id_;
    TaskFunction func_;
    TaskConfig config_;
};

using TaskPtr = std::shared_ptr<Task>;

}
