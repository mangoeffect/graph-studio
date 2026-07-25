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

class TG_EXPORT Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel get_level() const;

    void trace(const std::string& msg, const char* file = "", int line = 0);
    void debug(const std::string& msg, const char* file = "", int line = 0);
    void info(const std::string& msg, const char* file = "", int line = 0);
    void warn(const std::string& msg, const char* file = "", int line = 0);
    void error(const std::string& msg, const char* file = "", int line = 0);
    void fatal(const std::string& msg, const char* file = "", int line = 0);

    bool is_enabled(LogLevel level) const;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const std::string& msg, const char* file, int line);
    std::string get_level_name(LogLevel level) const;
    std::string get_timestamp() const;

    mutable std::mutex mutex_;
    LogLevel level_{LogLevel::INFO};
};

#define TG_LOG_TRACE(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::TRACE)) task_graph::Logger::instance().trace(msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_DEBUG(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::DEBUG)) task_graph::Logger::instance().debug(msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_INFO(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::INFO)) task_graph::Logger::instance().info(msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_WARN(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::WARN)) task_graph::Logger::instance().warn(msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_ERROR(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::ERROR)) task_graph::Logger::instance().error(msg, __FILE__, __LINE__); } while(0)
#define TG_LOG_FATAL(msg) do { if (task_graph::Logger::instance().is_enabled(task_graph::LogLevel::FATAL)) task_graph::Logger::instance().fatal(msg, __FILE__, __LINE__); } while(0)

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

struct TaskConfig {
    TaskPriority priority{TaskPriority::NORMAL};
    size_t max_retries{0};
    std::chrono::milliseconds timeout{0};
    bool skip_on_fail{false};
};

using TaskId = std::string;

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;

    virtual void set_result(const TaskId& task_id, const TaskResult& result) = 0;
    virtual std::optional<TaskResult> get_result(const TaskId& task_id) const = 0;

    virtual void set_value(const std::string& key, std::any value) = 0;
    virtual std::optional<std::any> get_value(const std::string& key) const = 0;

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
};

using ExecutionContextPtr = std::shared_ptr<IExecutionContext>;

class IPluginTask {
public:
    virtual ~IPluginTask() = default;
    virtual const std::string& id() const = 0;
    virtual TaskResult execute(IExecutionContext& ctx) = 0;
    virtual const TaskConfig& config() const = 0;
};

using PluginTaskPtr = std::shared_ptr<IPluginTask>;

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;

    virtual void register_task(const std::string& task_id,
                               std::function<PluginTaskPtr()> creator) = 0;

    virtual void unregister_task(const std::string& task_id) = 0;

    virtual bool has_task(const std::string& task_id) const = 0;

    virtual PluginTaskPtr create_task(const std::string& task_id) const = 0;

    virtual std::vector<std::string> available_tasks() const = 0;
};

class TG_EXPORT PluginRegistry : public IPluginRegistry {
public:
    static PluginRegistry& instance();

    void register_task(const std::string& task_id,
                       std::function<PluginTaskPtr()> creator) override;

    void unregister_task(const std::string& task_id) override;

    bool has_task(const std::string& task_id) const override;

    PluginTaskPtr create_task(const std::string& task_id) const override;

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
