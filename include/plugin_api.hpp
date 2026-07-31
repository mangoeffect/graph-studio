#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <any>
#include <variant>
#include <optional>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <atomic>

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
    std::exception_ptr exception{nullptr};
    std::chrono::nanoseconds duration{0};

    // 单输出快捷字段（90% 场景）。executor 视为端口名 "out"。
    std::any value;
    // 多输出命名端口（opt-in）。非空时优先于 value 使用。
    std::unordered_map<std::string, std::any> outputs;

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

// ParamValue is defined in data_types.hpp (needed by ParamSpec)

class TaskParams {
public:
    void set_int(const std::string& key, int value);
    void set_float(const std::string& key, float value);
    void set_string(const std::string& key, const std::string& value);
    void set_bool(const std::string& key, bool value);
    
    std::optional<int> get_int(const std::string& key) const;
    std::optional<float> get_float(const std::string& key) const;
    std::optional<std::string> get_string(const std::string& key) const;
    std::optional<bool> get_bool(const std::string& key) const;
    
    bool has_param(const std::string& key) const;
    void clear();
    
    const std::unordered_map<std::string, ParamValue>& params() const { return params_; }
    
private:
    std::unordered_map<std::string, ParamValue> params_;
};

struct TaskConfig {
    TaskPriority priority{TaskPriority::NORMAL};
    size_t max_retries{0};
    std::chrono::milliseconds timeout{0};
    bool skip_on_fail{false};
    std::vector<TaskId> dependencies;
    TaskParams params;
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

    virtual const TaskParams& params() const = 0;
};

class TaskContext;

using ExecutionContextPtr = std::shared_ptr<IExecutionContext>;

class INode {
public:
    INode(const std::string& id, const TaskConfig& config = TaskConfig())
        : id_(id), config_(config) {}

    virtual ~INode() noexcept = default;

    const std::string& id() const { return id_; }
    virtual const std::string& type() const = 0;
    virtual TaskResult execute(TaskContext& ctx) = 0;
    const TaskConfig& config() const { return config_; }

    // 就地更新 config（params/priority/timeout 等）。用于 DAG::update_task_config。
    // 若子类持有 spec_delegate_（如 Task 包装 IPluginTask），应重写以同步委托对象。
    virtual void set_config(const TaskConfig& config) { config_ = config; }

    // 端口契约声明（默认空，等价于"无约束"）。子类应重写以参与构图期校验。
    virtual std::vector<PortSpec> input_specs()  const { return {}; }
    virtual std::vector<PortSpec> output_specs() const { return {}; }

    // 参数契约声明（默认空）。子类重写以声明可配置参数的类型/默认值/范围/枚举，
    // 供 UI / 工具链自动发现（对齐 input_specs/output_specs 的做法）。
    virtual std::vector<ParamSpec> param_specs() const { return {}; }

    // 预初始化：在 task 执行前调用，只依赖构造时传入的 config（不依赖运行时 context）。
    // 用于 GPU shader 预编译等提前准备工作。线程安全，保证每个实例只执行一次。
    // 失败不应阻止后续 execute（execute 内有 fallback 逻辑）。
    // 子类重写 on_init() 实现具体逻辑，不要重写 init()。
    virtual void init() final {
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true)) return;
        on_init();
    }

    // 输入校验：按端口名取值的 map。默认实现基于 input_specs() 自动校验
    // （必填端口存在 + 类型名匹配）。子类一般无需重写。
    virtual CheckResult check_input(
        const std::unordered_map<std::string, std::any>& inputs_by_port) const;

protected:
    // 子类重写此方法实现预初始化逻辑（如 GPU shader 预编译）。保证只被调用一次。
    virtual void on_init() {}

    std::string id_;
    TaskConfig config_;

private:
    std::atomic<bool> initialized_{false};
};

using NodePtr = std::shared_ptr<INode>;
// 向后兼容别名
using IPluginTask = INode;
using PluginTaskPtr = NodePtr;

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;

    virtual void register_task(const std::string& task_type,
                               std::function<NodePtr(const std::string&, const TaskConfig&)> creator) = 0;

    virtual void unregister_task(const std::string& task_type) = 0;

    virtual bool has_task(const std::string& task_type) const = 0;

    virtual NodePtr create_task(const std::string& task_type) const = 0;
    virtual NodePtr create_task(const std::string& task_type, const TaskConfig& config) const = 0;
    virtual NodePtr create_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) const = 0;

    virtual std::vector<std::string> available_tasks() const = 0;
};

class TG_EXPORT PluginRegistry : public IPluginRegistry {
public:
    static PluginRegistry& instance();

    void register_task(const std::string& task_type,
                       std::function<NodePtr(const std::string&, const TaskConfig&)> creator) override;

    void unregister_task(const std::string& task_type) override;

    bool has_task(const std::string& task_type) const override;

    NodePtr create_task(const std::string& task_type) const override;
    NodePtr create_task(const std::string& task_type, const TaskConfig& config) const override;
    NodePtr create_task(const std::string& task_id, const std::string& task_type, const TaskConfig& config) const override;

    std::vector<std::string> available_tasks() const override;

private:
    PluginRegistry();
    ~PluginRegistry();
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    std::unordered_map<std::string, std::function<PluginTaskPtr(const std::string&, const TaskConfig&)>> task_creators_;
    mutable std::mutex mutex_;
};

}

extern "C" {
    using RegisterPluginFunc = bool(*)();
    using UnregisterPluginFunc = void(*)();
}
