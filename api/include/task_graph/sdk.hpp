#pragma once

// ============================================================================
// sdk.hpp - TaskGraph SDK 生命周期 API
//
// 设计文档:dev-docs/sdk-lifecycle-api.md(已定稿)
//
// 面向宿主 App 的完整生命周期:
//   1. TaskGraphSdk::create()            创建实例(零副作用)
//   2. init(SdkConfig)                   初始化(日志回调/环境变量/全局参数/线程数)
//   3. load_graph_file / load_graph_string  加载 graph(只吃 JSON 文件/字符串)
//   4. bind_input / bind_output          绑定真实输入输出(复用已配置的图)
//   5. update_graph_file / update_graph_string  增量更新(diff 由 SDK 处理)
//   6. execute / execute_async / cancel  执行
//   7. shutdown / 析构                   销毁释放(幂等)
//
// 关键契约:
//   - 所有方法永不抛异常,统一 SdkStatus + last_error() / last_load_issues();
//   - 单活跃 graph:load_graph 即替换,多图场景建多个 SDK 实例;
//   - 图边界用内置任务类型 io_input / io_output 标记(核心库注册),
//     io_input 可声明 data_type 参数做绑定期类型前置校验;
//   - 绑定 API = std::any 底座 + typed template 便捷封装(头文件内联转发);
//   - 绑定值入口快照:execute() 入口写入 io 节点,执行中途换绑不影响当次;
//   - 同一实例同时只有一次执行(execute 期间其他操作内部排队);
//   - log_callback / event_callback / OutputCallback 在执行线程触发,
//     调用方自行 marshal 回 UI 线程;回调内禁止调用本 SDK 方法(死锁)。
// ============================================================================

#include <task_graph/dag_config.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/profiler.hpp>

#include <any>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace task_graph {

enum class SdkStatus {
    OK = 0,
    NOT_INITIALIZED = 1,     // 未 init 即调用
    ALREADY_INITIALIZED = 2, // init 两次
    GRAPH_NOT_LOADED = 3,    // 未加载 graph 即 execute/bind
    INVALID_ARGUMENT = 4,    // 空 id / 图中无此节点 / 节点不是 io 节点
    TYPE_MISMATCH = 5,       // 绑定值类型与 data_type 声明不符
    GRAPH_INVALID = 6,       // graph JSON 校验失败(见 last_load_issues)
    BUSY = 7,                // 保留码(v1 内部排队,暂不返回)
    INTERNAL_ERROR = 8,      // 执行期任务失败等(见 last_error)
};

struct SdkConfig {
    // ---- 日志 ----
    LogLevel log_level{LogLevel::INFO};
    // 进程级 Logger 的 LogSink 回调;SDK 在 init 注册、shutdown 注销,
    // 销毁后不会再被回调(悬垂安全)。可能在任意线程触发。
    LogSink log_callback;

    // ---- 环境变量 / 全局参数 ----
    // env 以 "_env.<KEY>" 前缀写入执行期上下文(字符串形态);
    // globals 原样(set_global 可在运行期追加/覆盖,下次执行生效)。
    std::unordered_map<std::string, std::string> env;
    std::unordered_map<std::string, std::any> globals;

    // ---- 资产查找 ----
    // 汇入相对路径解析的搜索根(配合 graph 的 _source_dir 机制)。
    std::vector<std::filesystem::path> asset_search_paths;

    // ---- 执行 ----
    size_t thread_pool_size{std::thread::hardware_concurrency()};
    std::chrono::milliseconds default_timeout{0};
    bool enable_profiling{false};
    // 任务级 ExecutionEvent(耗时/失败/完成);执行线程触发。
    ExecutionCallback event_callback;

    // ---- graph 加载 ----
    // 透传 DagConfigLoader:未知 task type 视为 ERROR(拦截拼错的类型名)。
    bool require_known_types{false};
};

// update_graph 的 diff 报告。
struct GraphDiff {
    std::vector<std::string> tasks_added;
    std::vector<std::string> tasks_removed;
    std::vector<std::string> tasks_updated;   // 配置变化(含 params-only)
    std::vector<std::string> edges_added;     // "a:out->b:in"
    std::vector<std::string> edges_removed;
    bool topology_changed() const {
        return !tasks_added.empty() || !tasks_removed.empty() ||
               !edges_added.empty() || !edges_removed.empty();
    }
};

class TaskGraphSdk : public std::enable_shared_from_this<TaskGraphSdk> {
public:
    // 输出推模式回调:输出就绪时携带节点 id 与值(执行线程触发,
    // 回调内禁止调用本 SDK 方法)。
    using OutputCallback = std::function<void(const std::string& node_id,
                                              const std::any& value)>;

    // ===== 步骤 1:创建实例(零副作用,不触碰任何单例)=====
    static std::shared_ptr<TaskGraphSdk> create();

    // ===== 步骤 7:销毁(= shutdown(),幂等)=====
    ~TaskGraphSdk();

    TaskGraphSdk(const TaskGraphSdk&) = delete;
    TaskGraphSdk& operator=(const TaskGraphSdk&) = delete;

    // ===== 步骤 2:初始化(恰好一次)=====
    SdkStatus init(const SdkConfig& config);
    bool is_initialized() const;

    // ===== 步骤 3:加载 graph(只消费 JSON 文件/字符串;重复 load = 整图替换)=====
    SdkStatus load_graph_file(const std::filesystem::path& path);
    SdkStatus load_graph_string(const std::string& json);
    // base_dir 显式版(字符串来源无目录可推导时用,如"改参后重注入"的
    // JSON:_source_dir 注入与文件路径等价)
    SdkStatus load_graph_string(const std::string& json, const std::string& base_dir);
    // GRAPH_INVALID 时取回结构化诊断(行列号/JSON pointer)。
    const std::vector<DagConfigIssue>& last_load_issues() const;
    // 当前图的只读快照(未加载时为 nullptr;UI 预览用)。
    const DagConfig* graph_config() const;
    // 编译预检:暴露 DAGCompiler::validate 的结果(不执行)。
    std::vector<ValidationError> validate_graph() const;

    // ===== 步骤 4:绑定真实输入/输出 =====
    //
    // 图 JSON 用内置任务类型标记边界:
    //   { "id": "src", "type": "io_input",  "params": { "data_type": "task_graph::Image" } }
    //   { "id": "dst", "type": "io_output" }
    //
    // 核心 any 底座(ABI/WASM 友好);typed 便捷封装见下方 template。
    SdkStatus bind_input(const std::string& node_id, std::any value);
    template <typename T>
    SdkStatus bind_input(const std::string& node_id, T value) {
        return bind_input(node_id, std::any(std::move(value)));
    }
    SdkStatus unbind_input(const std::string& node_id);

    // 输出两种消费模式(可并存,按节点):
    SdkStatus bind_output(const std::string& node_id);                       // 拉模式
    SdkStatus bind_output(const std::string& node_id, OutputCallback cb);    // 推模式
    SdkStatus unbind_output(const std::string& node_id);

    // 图中全部 io_input / io_output 的 id(便于调用方枚举边界)。
    std::vector<std::string> input_nodes() const;
    std::vector<std::string> output_nodes() const;

    // execute 后拉取(拉模式)。类型不匹配返回 nullopt 并记录 last_error。
    std::optional<std::any> get_output_any(const std::string& node_id) const;
    template <typename T>
    std::optional<T> get_output(const std::string& node_id) const {
        auto v = get_output_any(node_id);
        if (!v.has_value()) return std::nullopt;
        if (v->type() != typeid(T)) {
            return std::nullopt;  // 类型不符;详情见 last_error()
        }
        return std::any_cast<T>(std::move(*v));
    }

    // ===== 按任意节点访问上次执行的结果(检查/测试用消费面) =====
    // 与 get_output_* 不同,不要求节点是 io_output——返回上次 execute 中
    // 该任务的 TaskResult 快照(每次 execute 入口清空):
    //   task_status: 任务状态;nullopt = 无结果记录(未执行/节点不存在)
    //   task_output: 任务 COMPLETED 时的 TaskResult.value(可能为空 any);
    //                否则 nullopt
    //   executed_tasks: 上次执行涉及的全部节点 id
    //   task_result: 完整 TaskResult 快照(含 status/value/exception/duration,
    //                失败诊断用——exception 指针可 rethrow 取消息)
    std::optional<TaskResult> task_result(const std::string& node_id) const;
    std::optional<TaskStatus> task_status(const std::string& node_id) const;
    std::optional<std::any> task_output(const std::string& node_id) const;
    std::vector<std::string> executed_tasks() const;

    // 全局参数运行期更新(下次执行生效;env 不可变)。
    SdkStatus set_global(const std::string& key, std::any value);

    // ===== 步骤 6:执行 =====
    // 同步:阻塞到完成。执行前 SDK 自动完成:globals/env 灌入执行上下文、
    // 绑定输入快照写入 io 节点、清空输出槽、(拓扑有变时)重编译执行计划。
    SdkStatus execute();
    // 异步:future 就绪即完成;get() 不会抛(SDK 内部已捕获异常)。
    // WASM 单线程构建下退化为立即完成。
    std::future<SdkStatus> execute_async();
    bool is_running() const;
    SdkStatus cancel();  // 协作式取消(委托 DAGExecutor::cancel)

    // 性能数据(enable_profiling 时有内容)
    const ProfileCollector& profiler() const;
    // 最近一次非 OK 的可读原因。
    std::string last_error() const;

    // ===== 步骤 5:增量更新(diff 由 SDK 处理)=====
    // 新 JSON 经同一套校验,失败则旧图原样保留(GRAPH_INVALID + issues)。
    // 未变化/same-type 任务复用原 TaskPtr(热状态保留:GPU 管线/MNN 会话等),
    // params-only 变化走 set_config 原地更新(与 DAG::update_task_config 同语义)。
    SdkStatus update_graph_file(const std::filesystem::path& path);
    SdkStatus update_graph_string(const std::string& json);
    const GraphDiff& last_diff() const;

    // ===== 步骤 7:销毁 =====
    // cancel + 等待在途执行 + 注销 LogSink + 释放全部;幂等。
    SdkStatus shutdown();

private:
    TaskGraphSdk() = default;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

// 图边界内置任务类型名(核心库注册,非子模块)。
inline constexpr const char* kIoInputType = "io_input";
inline constexpr const char* kIoOutputType = "io_output";

}  // namespace task_graph
