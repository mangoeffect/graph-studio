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

// SDK/插件 ABI 版本。插件在构建期捕获 tg_sdk_version()，宿主框架在
// PluginLoader::load() 时校验一致性，不匹配的插件会被拒绝加载。
inline constexpr uint32_t TG_SDK_VERSION = 1;

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
    // 宿主框架导出的 SDK 版本（见 src/plugin.cpp）。
    TG_EXPORT uint32_t tg_sdk_version();
    // 可选：插件实现的通用 ABI 运行时，参见 TG_DEFINE_PLUGIN_SDK_VERSION。
    TG_EXPORT uint32_t tg_plugin_sdk_version();
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

// 日志条目：包含日志的全部上下文信息，供 sink 接收方格式化/显示。
struct LogEntry {
    LogLevel level;
    std::string msg;
    const char* file;        // 原始源文件路径（可能为 nullptr/空）
    int line;
    std::string filename;    // file 的文件名部分（已提取）
    std::string timestamp;   // 格式化后的时间戳
    std::string thread_name; // 线程名（可能为空）
    std::string thread_id;   // 线程 ID 字符串
};

// 日志 sink：收到完整日志条目（含级别、消息、源位置、时间戳、线程信息），
// 调用方自行决定如何格式化/显示。
// 在 LoggerImpl::log() 锁外调用，可安全重入（sink 内部再调 tg_log 不会死锁）。
// 可能在任意线程触发，调用方需自行做线程编组。
using LogSink = std::function<void(const LogEntry& entry)>;

// 注册/注销全局日志 sink。sink 附加到 stdout 输出（不替代），headless/CLI 仍可见日志。
// 同一时刻仅支持一个 sink；重复注册会覆盖前一个。
TG_EXPORT void set_log_sink(LogSink sink);
TG_EXPORT void clear_log_sink();

enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    SKIPPED,
    // 流式源流尽信号：IStreamSource::next_frame() 返回此状态表示再无更多帧。
    // 仅在 stream 模式下由 executor 识别，不计入 is_success()/is_failed()。
    STREAM_END
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

    // 端口契约声明（纯虚：每个 task 子类必须声明自己的输入/输出端口，
    // 供构图期校验 + 编辑器按端口连线）。通用包装类（如 LambdaNode）视为
    // 无固定契约，override 返回空即可。
    virtual std::vector<PortSpec> input_specs()  const = 0;
    virtual std::vector<PortSpec> output_specs() const = 0;

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

// 逐帧流式源/汇接口（视频等时序数据）。
// task 同时继承 INode + IStreamSource/IStreamSink 即可让 DAGExecutor 进入
// stream 模式：executor 检测到 IStreamSource 后循环调用 next_frame()，每帧
// 驱动其下游 cone（按拓扑序）重跑一次，帧以常规端口值（如 cv::Mat）流动，
// 因此中间 task 无需任何改动即可逐帧复用（例：image_filtering 作用于视频帧）。
// 判定方式为 dynamic_cast，未实现这些接口的图完全走原有 one-shot 路径。
class IStreamSource {
public:
    virtual ~IStreamSource() = default;
    // 图开始前调一次：打开资源、游标归零。实现从 config().params 读路径参数。
    // 失败应记录内部状态，next_frame() 随即返回 STREAM_END/FAILED。
    virtual void reset_stream() = 0;
    // 取下一帧。有帧返回 COMPLETED + value（端口 "out"）；流尽返回 STREAM_END；
    // 出错返回 FAILED。不得抛异常（-fno-exceptions 安全）。
    virtual TaskResult next_frame(TaskContext& ctx) = 0;
};

// 流式汇：跨帧累积（如视频写文件）。每帧照常被 execute() 调用写一帧，
// 全部帧处理完后 executor 调 on_stream_end() 收尾（关闭文件/写 trailer）。
class IStreamSink {
public:
    virtual ~IStreamSink() = default;
    virtual void on_stream_end() = 0;
};

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

// 插件作者辅助宏：在插件实现中调用一次，导出 SDK 版本供宿主校验。
//   TG_DEFINE_PLUGIN_SDK_VERSION;
// 若插件未导出该符号，PluginLoader 会以警告方式放行（兼容旧插件）。
#define TG_DEFINE_PLUGIN_SDK_VERSION                                         \
    extern "C" TG_EXPORT uint32_t tg_plugin_sdk_version() {                  \
        return ::task_graph::TG_SDK_VERSION;                                  \
    }

// 插件自动注册的跨平台宏（每个插件翻译单元内、anonymous namespace 中调用一次）：
//   TG_PLUGIN_AUTOREG(do_register, do_unregister);
//  - GCC/Clang（含 iOS/Android/WASM 静态链接）：__attribute__((constructor/destructor))，
//    库加载/静态链接时自动注册，卸载/退出时自动注销；
//  - MSVC (cl.exe)：不识别 GNU attributes，改用文件作用域静态对象的构造/析构函数，
//    在 DLL 加载/卸载（或 EXE 启动/退出）时执行，语义等价。
#if defined(_MSC_VER) && !defined(__clang__)
#define TG_PLUGIN_AUTOREG(init_fn, cleanup_fn)                               \
    static struct TgPluginAutoReg {                                          \
        TgPluginAutoReg() { init_fn(); }                                     \
        ~TgPluginAutoReg() { cleanup_fn(); }                                 \
    } g_tg_plugin_auto_reg;
#else
#define TG_PLUGIN_AUTOREG(init_fn, cleanup_fn)                               \
    __attribute__((constructor)) static void tg_plugin_constructor() {       \
        init_fn();                                                           \
    }                                                                        \
    __attribute__((destructor)) static void tg_plugin_destructor() {         \
        cleanup_fn();                                                        \
    }
#endif

// 自包含：子模块只需 #include <plugin_api.hpp> 即可获得 INode + TaskContext 完整定义
#include <task_graph/task_context.hpp>
