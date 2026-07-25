#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <any>
#include <optional>
#include <chrono>
#include <mutex>
#include <shared_mutex>

#include <task_graph/data_types.hpp>

#ifdef _WIN32
#if defined(TASK_GRAPH_BUILD)
#define TG_EXPORT __declspec(dllexport)
#else
#define TG_EXPORT __declspec(dllimport)
#endif
#define TG_IMPORT __declspec(dllimport)
#else
#define TG_EXPORT __attribute__((visibility("default")))
#define TG_IMPORT __attribute__((visibility("default")))
#endif

namespace task_graph {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

extern "C" {
    TG_EXPORT void tg_log(LogLevel level, const char* msg, const char* file = "", int line = 0);
    TG_EXPORT void tg_set_log_level(LogLevel level);
    TG_EXPORT LogLevel tg_get_log_level();
}

inline void tg_log(LogLevel level, const std::string& msg, const char* file = "", int line = 0) {
    tg_log(level, msg.c_str(), file, line);
}

#define TG_LOG_TRACE(msg) do { task_graph::tg_log(task_graph::LogLevel::TRACE, msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_DEBUG(msg) do { task_graph::tg_log(task_graph::LogLevel::DEBUG, msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_INFO(msg) do { task_graph::tg_log(task_graph::LogLevel::INFO, msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_WARN(msg) do { task_graph::tg_log(task_graph::LogLevel::WARN, msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_ERROR(msg) do { task_graph::tg_log(task_graph::LogLevel::ERROR, msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_FATAL(msg) do { task_graph::tg_log(task_graph::LogLevel::FATAL, msg, __FILE__, __LINE__); } while(0)

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

struct CheckResult {
    bool success{false};
    std::string error_message;

    CheckResult() = default;
    CheckResult(bool s) : success(s) {}
    CheckResult(bool s, const std::string& msg) : success(s), error_message(msg) {}
};

using TaskId = std::string;

struct TaskConfig {
    TaskPriority priority{TaskPriority::NORMAL};
    size_t max_retries{0};
    std::chrono::milliseconds timeout{0};
    bool skip_on_fail{false};
    std::vector<TaskId> dependencies;
};

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;

    virtual void set_result(const TaskId& task_id, const TaskResult& result) = 0;
    virtual std::optional<TaskResult> get_result(const TaskId& task_id) const = 0;

    virtual void set_value(const std::string& key, std::any value) = 0;
    virtual std::optional<std::any> get_value(const std::string& key) const = 0;

    virtual void log(LogLevel level, const std::string& msg) = 0;

    virtual void declare_dependency(const TaskId& task_id) = 0;
    virtual bool validate_dependencies() const = 0;
    virtual std::vector<TaskId> dependencies() const = 0;

    virtual void clear_result(const TaskId& task_id) = 0;
    virtual void clear_all_results() = 0;

    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto opt = get_value(key);
        if (!opt) {
            return std::nullopt;
        }
        try {
            return std::any_cast<T>(*opt);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    template<typename T>
    std::optional<T> get_result_value(const TaskId& task_id) const {
        auto opt = get_result(task_id);
        if (!opt || !opt->value.has_value()) {
            return std::nullopt;
        }
        try {
            return std::any_cast<T>(opt->value);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
};

using ExecutionContextPtr = std::shared_ptr<IExecutionContext>;

class IPluginTask {
public:
    virtual ~IPluginTask() = default;
    virtual const std::string& id() const = 0;
    virtual const std::string& type() const = 0;
    virtual TaskResult execute(IExecutionContext& ctx) = 0;
    virtual const TaskConfig& config() const = 0;
    
    virtual CheckResult check_input(const std::vector<std::any>& inputs) const = 0;
};

using PluginTaskPtr = std::shared_ptr<IPluginTask>;

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;

    virtual void register_task(const std::string& task_type,
                               std::function<PluginTaskPtr()> creator) = 0;

    virtual void unregister_task(const std::string& task_type) = 0;

    virtual bool has_task(const std::string& task_type) const = 0;

    virtual PluginTaskPtr create_task(const std::string& task_type) const = 0;

    virtual std::vector<std::string> available_tasks() const = 0;
};

class TG_EXPORT PluginRegistry : public IPluginRegistry {
public:
    static PluginRegistry& instance();

    void register_task(const std::string& task_type,
                       std::function<PluginTaskPtr()> creator) override;

    void unregister_task(const std::string& task_type) override;

    bool has_task(const std::string& task_type) const override;

    PluginTaskPtr create_task(const std::string& task_type) const override;

    std::vector<std::string> available_tasks() const override;

private:
    PluginRegistry();
    ~PluginRegistry();
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    std::unordered_map<std::string, std::function<PluginTaskPtr()>> task_creators_;
    mutable std::mutex mutex_;
};

}

extern "C" {
    using RegisterPluginFunc = bool(*)();
    using UnregisterPluginFunc = void(*)();
}
