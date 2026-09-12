#include <task_graph/sdk.hpp>

#include <task_graph/plugin.hpp>
#include <task_graph/data_types.hpp>

#include <atomic>
#include <condition_variable>
#include <set>
#include <stdexcept>
#include <typeindex>
#include <unordered_set>
#include <utility>

namespace task_graph {

// ============================================================================
// 内置图边界任务:io_input / io_output
//
// 图 JSON 用这两个类型标记数据边界。SDK 在执行入口把绑定值写入 io_input,
// 执行结束后从 io_output 收集结果。二者由核心库注册(非子模块),注册方式
// 与 src/mnn/mnn_registry.cpp 相同(TG_PLUGIN_AUTOREG + pull 锚点)。
// ============================================================================

namespace {

class IoInputNode final : public INode {
public:
    IoInputNode(const std::string& id, const TaskConfig& cfg)
        : INode(id, cfg) {}

    const std::string& type() const override {
        static const std::string t{kIoInputType};
        return t;
    }

    std::vector<ParamSpec> param_specs() const override {
        ParamSpec spec;
        spec.name = "data_type";
        spec.type = ParamType::String;
        spec.description = "expected stable type name (e.g. task_graph::Image); "
                           "empty = no type check";
        spec.default_value = std::string("");
        return {spec};
    }

    std::vector<PortSpec> input_specs() const override { return {}; }

    std::vector<PortSpec> output_specs() const override {
        // type_name = data_type 声明(空则不约束),下游 check_input 自动比对。
        return {PortSpec{"out", data_type(), true}};
    }

    TaskResult execute(TaskContext& ctx) override {
        std::lock_guard<std::mutex> lock(value_mutex_);
        if (!has_value_) {
            TaskResult r;
            r.status = TaskStatus::FAILED;
            ctx.error("io_input '" + id() + "' executed without a bound value");
            return r;
        }
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = value_;  // any 拷贝;Image 等大负载内部走 shared_ptr,实际廉价
        return r;
    }

    // ---- SDK 侧接口(执行入口调用) ----
    void set_value(std::any v) {
        std::lock_guard<std::mutex> lock(value_mutex_);
        value_ = std::move(v);
        has_value_ = true;
    }
    void clear_value() {
        std::lock_guard<std::mutex> lock(value_mutex_);
        value_.reset();
        has_value_ = false;
    }
    bool has_value() const {
        std::lock_guard<std::mutex> lock(value_mutex_);
        return has_value_;
    }

    std::string data_type() const {
        auto dt = config().params.get_string("data_type");
        return dt.value_or("");
    }

private:
    mutable std::mutex value_mutex_;
    std::any value_;
    bool has_value_{false};
};

class IoOutputNode final : public INode {
public:
    IoOutputNode(const std::string& id, const TaskConfig& cfg)
        : INode(id, cfg) {}

    const std::string& type() const override {
        static const std::string t{kIoOutputType};
        return t;
    }

    std::vector<ParamSpec> param_specs() const override { return {}; }

    std::vector<PortSpec> input_specs() const override {
        return {PortSpec{"in", "", true}};
    }
    std::vector<PortSpec> output_specs() const override { return {}; }

    TaskResult execute(TaskContext& ctx) override {
        // 端口名经 DAG::connect 规范化后即声明端口 "in"
        const std::any* v = ctx.input_any("in");
        if (!v) {
            TaskResult r;
            r.status = TaskStatus::FAILED;
            ctx.error("io_output '" + id() + "' has no incoming value");
            return r;
        }
        std::lock_guard<std::mutex> lock(value_mutex_);
        value_ = *v;
        has_value_ = true;
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        return r;
    }

    // ---- SDK 侧接口(执行结束后收集) ----
    bool has_value() const {
        std::lock_guard<std::mutex> lock(value_mutex_);
        return has_value_;
    }
    std::any take_value() {
        std::lock_guard<std::mutex> lock(value_mutex_);
        has_value_ = false;
        return std::move(value_);
    }

private:
    mutable std::mutex value_mutex_;
    std::any value_;
    bool has_value_{false};
};

void do_register_io_nodes() {
    auto& reg = PluginRegistry::instance();
    reg.register_task(kIoInputType,
                      [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                          return std::make_shared<IoInputNode>(id, cfg);
                      });
    reg.register_task(kIoOutputType,
                      [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                          return std::make_shared<IoOutputNode>(id, cfg);
                      });
}

void do_unregister_io_nodes() {
    auto& reg = PluginRegistry::instance();
    reg.unregister_task(kIoInputType);
    reg.unregister_task(kIoOutputType);
}

}  // namespace

// 非 inline 锚点:data_types.cpp 的 TypeRegistry::instance() 调用本函数,
// 强制拉入本 TU 的注册初始化器(STATIC 核心库防裁剪,同 pull_mnn_tasks)。
namespace detail {
void pull_sdk_tasks() {}
}  // namespace detail

// 核心库内编译:无需 extern "C" register_plugin(WASM whole-archive 同名冲突);
// TG_PLUGIN_AUTOREG 覆盖 GCC/Clang(含静态链接)与 MSVC。
TG_PLUGIN_AUTOREG(do_register_io_nodes, do_unregister_io_nodes);

// ============================================================================
// TaskGraphSdk 实现
// ============================================================================

class TaskGraphSdk::Impl {
public:
    enum class State { Created, Inited, Shutdown };

    State state{State::Created};
    mutable std::mutex mutex;          // 状态/图/绑定互斥(执行本体持 exec_mutex)
    std::mutex exec_mutex;             // 执行串行化(排队语义)

    SdkConfig config;
    std::unique_ptr<DAGExecutor> executor;
    LogSinkHandle log_handle{0};

    std::optional<DAG> dag;            // 当前活跃 graph
    DagConfig snapshot;                // 当前 graph 只读快照
    std::vector<DagConfigIssue> last_issues;
    GraphDiff last_diff_{};
    bool has_graph{false};

    // 权威全局黑板(env + globals);每次执行入口快照成不可变 map 交给 executor
    std::unordered_map<std::string, std::any> context_map;

    // 绑定(按 node_id)
    std::unordered_map<std::string, std::any> input_values;
    std::unordered_set<std::string> output_bound;      // 拉模式注册
    std::unordered_map<std::string, OutputCallback> output_push;

    // per-run 输出槽(上次执行的结果,直到下次执行被清空)
    std::unordered_map<std::string, std::any> run_outputs;

    // 上次执行的逐任务结果快照(按任意节点访问;每次 execute 入口清空)
    std::unordered_map<std::string, TaskResult> last_results;

    std::string last_error_;

    // 异步 worker 跟踪(shutdown join)
    std::mutex workers_mutex;
    std::vector<std::thread> workers;

    void set_error(const std::string& msg) { last_error_ = msg; }

    // 构建 context_map 的不可变快照并交给 executor
    void publish_context_values() {
        auto snap = std::make_shared<const std::unordered_map<std::string, std::any>>(
            context_map);
        executor->set_context_values(std::move(snap));
    }

    std::vector<std::string> nodes_of_type(const std::string& type) const {
        std::vector<std::string> out;
        if (!dag.has_value()) return out;
        for (const auto& id : dag->task_ids()) {
            auto t = dag->get_task(id);
            if (t && t->type() == type) out.push_back(id);
        }
        return out;
    }

    // 快照层 diff + 热状态搬运应用。调用方持有 mutex 且 executor 空闲。
    // 失败返回 false(set_error 已填)。
    bool apply_update(DagConfigLoadResult&& loaded);
};

// ---- diff 应用 ----
bool TaskGraphSdk::Impl::apply_update(DagConfigLoadResult&& loaded) {
    if (!loaded.ok()) {
        last_issues = std::move(loaded.issues);
        set_error("updated graph JSON invalid: " +
                  (last_issues.empty() ? std::string("unknown error")
                                       : last_issues.front().message));
        return false;
    }

    DagConfig& next = loaded.config;
    GraphDiff diff;

    // ---- 快照层 diff ----
    std::unordered_map<std::string, const TaskConfigEntry*> old_tasks;
    for (const auto& t : snapshot.tasks()) old_tasks[t.id] = &t;
    std::unordered_map<std::string, const TaskConfigEntry*> new_tasks;
    for (const auto& t : next.tasks()) new_tasks[t.id] = &t;

    for (const auto& [id, entry] : new_tasks) {
        auto it = old_tasks.find(id);
        if (it == old_tasks.end()) {
            diff.tasks_added.push_back(id);
            continue;
        }
        const TaskConfigEntry* old = it->second;
        bool changed = old->type != entry->type ||
                       old->priority != entry->priority ||
                       old->max_retries != entry->max_retries ||
                       old->timeout_ms != entry->timeout_ms ||
                       old->skip_on_fail != entry->skip_on_fail ||
                       old->dependencies != entry->dependencies ||
                       !(old->params_raw == entry->params_raw);
        if (changed) diff.tasks_updated.push_back(id);
    }
    for (const auto& [id, entry] : old_tasks) {
        (void)entry;
        if (!new_tasks.contains(id)) diff.tasks_removed.push_back(id);
    }

    auto edge_key = [](const EdgeConfigEntry& e) {
        return e.from + ":" + e.from_port + "->" + e.to + ":" + e.to_port;
    };
    std::set<std::string> old_edges, new_edges;
    for (const auto& e : snapshot.edges()) old_edges.insert(edge_key(e));
    for (const auto& e : next.edges()) new_edges.insert(edge_key(e));
    for (const auto& k : new_edges) {
        if (!old_edges.contains(k)) diff.edges_added.push_back(k);
    }
    for (const auto& k : old_edges) {
        if (!new_edges.contains(k)) diff.edges_removed.push_back(k);
    }

    // ---- 应用:反序列化出新 DAG,再对 eligible 任务搬运旧 TaskPtr ----
    auto next_dag_opt = next.to_dag();
    if (!next_dag_opt.has_value()) {
        set_error("failed to build updated DAG");
        return false;
    }
    DAG next_dag = std::move(*next_dag_opt);

    // diff 报告里 updated 的 + 完全未变的同 type 任务都保留热状态:
    // type 未变即可复用实例(配置差异通过 set_config 原地更新,与
    // DAG::update_task_config 同语义);type 变了用反序列化的新实例。
    for (const auto& id : next_dag.task_ids()) {
        auto old_it = old_tasks.find(id);
        if (old_it == old_tasks.end()) continue;          // 新增:用新实例
        const TaskConfigEntry* old_entry = old_it->second;
        auto new_entry_it = new_tasks.find(id);
        if (new_entry_it == new_tasks.end()) continue;
        if (new_entry_it->second->type != old_entry->type) continue;  // 换类型:新实例

        TaskPtr old_ptr = dag->get_task(id);
        if (!old_ptr) continue;
        // 新实例的 config 已按新 JSON 解析(含 _source_dir/params 类型化),
        // 原地搬到旧实例上,保住运行期热状态(GPU 管线/MNN 会话等)。
        TaskPtr fresh_ptr = next_dag.get_task(id);
        if (fresh_ptr) old_ptr->set_config(fresh_ptr->config());
        next_dag.replace_task(id, std::move(old_ptr));
    }

    // ---- io 绑定按 node_id 存活:节点被删/换类型的绑定清理 ----
    {
        auto drop_if_gone = [&](std::unordered_map<std::string, std::any>& m) {
            for (auto it = m.begin(); it != m.end();) {
                auto entry = new_tasks.find(it->first);
                if (entry == new_tasks.end() ||
                    entry->second->type != kIoInputType) {
                    it = m.erase(it);
                } else {
                    ++it;
                }
            }
        };
        drop_if_gone(input_values);
        for (auto it = output_bound.begin(); it != output_bound.end();) {
            auto entry = new_tasks.find(*it);
            if (entry == new_tasks.end() || entry->second->type != kIoOutputType) {
                it = output_bound.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = output_push.begin(); it != output_push.end();) {
            auto entry = new_tasks.find(it->first);
            if (entry == new_tasks.end() || entry->second->type != kIoOutputType) {
                it = output_push.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = run_outputs.begin(); it != run_outputs.end();) {
            if (!new_tasks.contains(it->first)) {
                it = run_outputs.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- 原子替换 ----
    if (dag.has_value()) {
        dag->reset_from(std::move(next_dag));
    } else {
        dag = std::move(next_dag);
    }
    snapshot = std::move(next);
    last_issues = std::move(loaded.issues);
    last_diff_ = std::move(diff);
    has_graph = true;
    return true;
}

// ============================================================================
// 生命周期
// ============================================================================

std::shared_ptr<TaskGraphSdk> TaskGraphSdk::create() {
    // 私有构造 + enable_shared_from_this:标准两段式
    struct EnableCtor : public TaskGraphSdk {};
    auto sdk = std::make_shared<EnableCtor>();
    sdk->impl_ = std::make_unique<Impl>();
    return sdk;
}

TaskGraphSdk::~TaskGraphSdk() {
    shutdown();
}

SdkStatus TaskGraphSdk::init(const SdkConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == Impl::State::Shutdown) {
        impl_->set_error("sdk already shut down");
        return SdkStatus::INTERNAL_ERROR;
    }
    if (impl_->state == Impl::State::Inited) {
        impl_->set_error("sdk already initialized");
        return SdkStatus::ALREADY_INITIALIZED;
    }

    impl_->config = config;
    tg_set_log_level(config.log_level);
    if (config.log_callback) {
        impl_->log_handle = add_log_sink(config.log_callback);
    }

    ExecutorConfig ec;
    ec.thread_pool_size = config.thread_pool_size;
    ec.timeout = config.default_timeout;
    ec.enable_profiling = config.enable_profiling;
    ec.callback = config.event_callback;
    impl_->executor = std::make_unique<DAGExecutor>(std::move(ec));

    // env → "_env.<KEY>";globals 原样。构建权威 map,执行入口再快照发布。
    for (const auto& kv : config.env) {
        impl_->context_map["_env." + kv.first] = std::any(kv.second);
    }
    for (const auto& kv : config.globals) {
        impl_->context_map[kv.first] = kv.second;
    }
    impl_->publish_context_values();

    impl_->state = Impl::State::Inited;
    return SdkStatus::OK;
}

bool TaskGraphSdk::is_initialized() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state == Impl::State::Inited;
}

SdkStatus TaskGraphSdk::shutdown() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state == Impl::State::Shutdown) return SdkStatus::OK;  // 幂等
        impl_->state = Impl::State::Shutdown;
        if (impl_->executor) impl_->executor->cancel();
    }

    // 等 worker 前:worker 尾部还要拿 mutex 做后处理,故 join 必须在锁外。
    {
        std::lock_guard<std::mutex> wlock(impl_->workers_mutex);
        for (auto& t : impl_->workers) {
            if (t.joinable()) t.join();
        }
        impl_->workers.clear();
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    // 注销 LogSink(进程级单例;不注销则回调悬垂)
    if (impl_->log_handle != 0) {
        remove_log_sink(impl_->log_handle);
        impl_->log_handle = 0;
    }
    if (impl_->executor) {
        impl_->executor->wait();
    }
    impl_->executor.reset();
    if (impl_->dag.has_value()) impl_->dag->clear();
    impl_->dag.reset();
    impl_->snapshot = DagConfig{};
    impl_->has_graph = false;
    impl_->input_values.clear();
    impl_->output_bound.clear();
    impl_->output_push.clear();
    impl_->run_outputs.clear();
    impl_->context_map.clear();
    return SdkStatus::OK;
}

// ============================================================================
// graph 加载
// ============================================================================

SdkStatus TaskGraphSdk::load_graph_file(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> exec_lock(impl_->exec_mutex);  // 等在途执行结束
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    DagConfigLoader::Options opts;
    opts.require_known_types = impl_->config.require_known_types;
    auto loaded = DagConfigLoader::load_file(path, opts);
    if (!loaded.ok()) {
        impl_->last_issues = std::move(loaded.issues);
        impl_->set_error("graph invalid: " +
                         (impl_->last_issues.empty() ? std::string("unknown")
                                                     : impl_->last_issues.front().message));
        return SdkStatus::GRAPH_INVALID;
    }
    auto dag_opt = loaded.config.to_dag();
    if (!dag_opt.has_value()) {
        impl_->last_issues = std::move(loaded.issues);
        impl_->set_error("failed to build DAG from config");
        return SdkStatus::GRAPH_INVALID;
    }
    // 整图替换:绑定与输出槽作废(节点集合已变)
    impl_->input_values.clear();
    impl_->output_bound.clear();
    impl_->output_push.clear();
    impl_->run_outputs.clear();
    impl_->last_diff_ = GraphDiff{};
    if (impl_->dag.has_value()) {
        impl_->dag->reset_from(std::move(*dag_opt));
    } else {
        impl_->dag = std::move(*dag_opt);
    }
    impl_->snapshot = std::move(loaded.config);
    impl_->last_issues = std::move(loaded.issues);
    impl_->has_graph = true;
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::load_graph_string(const std::string& json) {
    std::lock_guard<std::mutex> exec_lock(impl_->exec_mutex);  // 等在途执行结束
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    DagConfigLoader::Options opts;
    opts.require_known_types = impl_->config.require_known_types;
    auto loaded = DagConfigLoader::load_string(json, opts);
    if (!loaded.ok()) {
        impl_->last_issues = std::move(loaded.issues);
        impl_->set_error("graph invalid: " +
                         (impl_->last_issues.empty() ? std::string("unknown")
                                                     : impl_->last_issues.front().message));
        return SdkStatus::GRAPH_INVALID;
    }
    auto dag_opt = loaded.config.to_dag();
    if (!dag_opt.has_value()) {
        impl_->last_issues = std::move(loaded.issues);
        impl_->set_error("failed to build DAG from config");
        return SdkStatus::GRAPH_INVALID;
    }
    impl_->input_values.clear();
    impl_->output_bound.clear();
    impl_->output_push.clear();
    impl_->run_outputs.clear();
    impl_->last_diff_ = GraphDiff{};
    if (impl_->dag.has_value()) {
        impl_->dag->reset_from(std::move(*dag_opt));
    } else {
        impl_->dag = std::move(*dag_opt);
    }
    impl_->snapshot = std::move(loaded.config);
    impl_->last_issues = std::move(loaded.issues);
    impl_->has_graph = true;
    return SdkStatus::OK;
}

const std::vector<DagConfigIssue>& TaskGraphSdk::last_load_issues() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_issues;
}

const DagConfig* TaskGraphSdk::graph_config() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->has_graph ? &impl_->snapshot : nullptr;
}

std::vector<ValidationError> TaskGraphSdk::validate_graph() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->has_graph || !impl_->dag.has_value()) return {};
    DAGCompiler compiler;
    return compiler.validate(*impl_->dag);
}

// ============================================================================
// 绑定
// ============================================================================

SdkStatus TaskGraphSdk::bind_input(const std::string& node_id, std::any value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (node_id.empty()) {
        impl_->set_error("bind_input: empty node id");
        return SdkStatus::INVALID_ARGUMENT;
    }
    if (!impl_->has_graph || !impl_->dag.has_value()) {
        impl_->set_error("bind_input: no graph loaded");
        return SdkStatus::GRAPH_NOT_LOADED;
    }
    TaskPtr task = impl_->dag->get_task(node_id);
    if (!task || task->type() != kIoInputType) {
        impl_->set_error("bind_input: node '" + node_id + "' is not an io_input node");
        return SdkStatus::INVALID_ARGUMENT;
    }
    // 绑定期类型前置校验:对照 data_type 声明(稳定类型名)
    auto input_node = std::static_pointer_cast<IoInputNode>(task);
    const std::string declared = input_node->data_type();
    if (!declared.empty() && value.has_value()) {
        const std::string actual = detail::TypeRegistry::instance().name_of(
            std::type_index(value.type()));
        if (actual != declared) {
            impl_->set_error("bind_input: node '" + node_id + "' expects type '" +
                             declared + "' but got '" +
                             (actual.empty() ? value.type().name() : actual) + "'");
            return SdkStatus::TYPE_MISMATCH;
        }
    }
    impl_->input_values[node_id] = std::move(value);
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::unbind_input(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->input_values.find(node_id);
    if (it == impl_->input_values.end()) {
        impl_->set_error("unbind_input: no binding on node '" + node_id + "'");
        return SdkStatus::INVALID_ARGUMENT;
    }
    // 同步清掉节点上的残留值:unbind 后下一次 execute 应回到"未绑定"失败
    if (impl_->dag.has_value()) {
        auto t = impl_->dag->get_task(node_id);
        if (t && t->type() == kIoInputType) {
            std::static_pointer_cast<IoInputNode>(t)->clear_value();
        }
    }
    impl_->input_values.erase(it);
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::bind_output(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (!impl_->has_graph || !impl_->dag.has_value()) {
        impl_->set_error("bind_output: no graph loaded");
        return SdkStatus::GRAPH_NOT_LOADED;
    }
    TaskPtr task = impl_->dag->get_task(node_id);
    if (!task || task->type() != kIoOutputType) {
        impl_->set_error("bind_output: node '" + node_id + "' is not an io_output node");
        return SdkStatus::INVALID_ARGUMENT;
    }
    impl_->output_bound.insert(node_id);
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::bind_output(const std::string& node_id, OutputCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (!impl_->has_graph || !impl_->dag.has_value()) {
        impl_->set_error("bind_output: no graph loaded");
        return SdkStatus::GRAPH_NOT_LOADED;
    }
    TaskPtr task = impl_->dag->get_task(node_id);
    if (!task || task->type() != kIoOutputType) {
        impl_->set_error("bind_output: node '" + node_id + "' is not an io_output node");
        return SdkStatus::INVALID_ARGUMENT;
    }
    if (!cb) {
        impl_->set_error("bind_output: null callback for node '" + node_id + "'");
        return SdkStatus::INVALID_ARGUMENT;
    }
    impl_->output_push[node_id] = std::move(cb);
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::unbind_output(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    bool erased = impl_->output_bound.erase(node_id) > 0;
    erased = impl_->output_push.erase(node_id) > 0 || erased;
    if (!erased) {
        impl_->set_error("unbind_output: no binding on node '" + node_id + "'");
        return SdkStatus::INVALID_ARGUMENT;
    }
    return SdkStatus::OK;
}

std::vector<std::string> TaskGraphSdk::input_nodes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->nodes_of_type(kIoInputType);
}

std::vector<std::string> TaskGraphSdk::output_nodes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->nodes_of_type(kIoOutputType);
}

std::optional<std::any> TaskGraphSdk::get_output_any(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->run_outputs.find(node_id);
    if (it == impl_->run_outputs.end()) {
        impl_->set_error("get_output: no output for node '" + node_id +
                         "' (not bound, not executed, or execution failed)");
        return std::nullopt;
    }
    return it->second;
}

std::optional<TaskStatus> TaskGraphSdk::task_status(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->last_results.find(node_id);
    if (it == impl_->last_results.end()) return std::nullopt;
    return it->second.status;
}

std::optional<std::any> TaskGraphSdk::task_output(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->last_results.find(node_id);
    if (it == impl_->last_results.end() || !it->second.is_success()) {
        return std::nullopt;
    }
    return it->second.value;
}

std::vector<std::string> TaskGraphSdk::executed_tasks() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> ids;
    ids.reserve(impl_->last_results.size());
    for (const auto& kv : impl_->last_results) ids.push_back(kv.first);
    return ids;
}

SdkStatus TaskGraphSdk::set_global(const std::string& key, std::any value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (key.empty()) {
        impl_->set_error("set_global: empty key");
        return SdkStatus::INVALID_ARGUMENT;
    }
    impl_->context_map[key] = std::move(value);
    impl_->publish_context_values();
    return SdkStatus::OK;
}

// ============================================================================
// 执行
// ============================================================================

SdkStatus TaskGraphSdk::execute() {
    // 执行串行化:并发 execute 在此排队(设计契约:单飞)
    std::lock_guard<std::mutex> exec_lock(impl_->exec_mutex);

    std::vector<std::pair<std::string, TaskGraphSdk::OutputCallback>> push_targets;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state == Impl::State::Created) {
            impl_->set_error("sdk not initialized");
            return SdkStatus::NOT_INITIALIZED;
        }
        if (impl_->state == Impl::State::Shutdown) {
            impl_->set_error("sdk already shut down");
            return SdkStatus::INTERNAL_ERROR;
        }
        if (!impl_->has_graph || !impl_->dag.has_value()) {
            impl_->set_error("execute: no graph loaded");
            return SdkStatus::GRAPH_NOT_LOADED;
        }

        // 1) 校验全部 io_input 均已绑定(失败前置,消息含节点 id)
        for (const auto& id : impl_->dag->task_ids()) {
            auto t = impl_->dag->get_task(id);
            if (!t || t->type() != kIoInputType) continue;
            auto node = std::static_pointer_cast<IoInputNode>(t);
            if (!impl_->input_values.count(id) && !node->has_value()) {
                impl_->set_error("io_input '" + id + "' is not bound; "
                                 "call bind_input before execute");
                return SdkStatus::INVALID_ARGUMENT;
            }
        }

        // 2) 绑定输入快照写入 io 节点(执行中途换绑不影响当次)
        for (auto& kv : impl_->input_values) {
            auto t = impl_->dag->get_task(kv.first);
            if (t && t->type() == kIoInputType) {
                std::static_pointer_cast<IoInputNode>(t)->set_value(kv.second);
            }
        }

        // 3) 清空 per-run 输出槽 + 旧 io_output 值 + 上次执行结果快照
        impl_->run_outputs.clear();
        impl_->last_results.clear();
        for (const auto& id : impl_->dag->task_ids()) {
            auto t = impl_->dag->get_task(id);
            if (t && t->type() == kIoOutputType) {
                std::static_pointer_cast<IoOutputNode>(t)->take_value();
            }
        }

        // 4) context 快照发布(env+globals 对本 run 生效)
        impl_->publish_context_values();

        for (auto& kv : impl_->output_push) push_targets.emplace_back(kv.first, kv.second);
    }

    // 执行本体(锁外:is_running/cancel 等查询不被阻塞)
    try {
        auto fut = impl_->executor->execute(*impl_->dag);
        fut.wait();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->set_error(std::string("execution exception: ") + e.what());
        return SdkStatus::INTERNAL_ERROR;
    } catch (...) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->set_error("execution exception: unknown");
        return SdkStatus::INTERNAL_ERROR;
    }

    // 后处理:收集输出、填输出槽(拉模式)、聚合失败状态
    std::vector<std::pair<std::string, std::any>> collected;
    SdkStatus status = SdkStatus::OK;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state == Impl::State::Shutdown) {
            return SdkStatus::INTERNAL_ERROR;
        }
        for (const auto& id : impl_->dag->task_ids()) {
            auto t = impl_->dag->get_task(id);
            if (!t || t->type() != kIoOutputType) continue;
            auto node = std::static_pointer_cast<IoOutputNode>(t);
            if (!node->has_value()) continue;
            collected.emplace_back(id, node->take_value());
        }
        for (auto& kv : collected) {
            impl_->run_outputs[kv.first] = kv.second;  // 拉模式输出槽
        }
        // 逐任务结果快照(按任意节点访问的缓存源)
        impl_->last_results = impl_->executor->get_results();
        for (const auto& kv : impl_->last_results) {
            if (kv.second.is_failed()) {
                status = SdkStatus::INTERNAL_ERROR;
                impl_->set_error("task '" + kv.first + "' failed");
                break;
            }
        }
    }
    // 推模式回调:锁外逐个触发(回调内调用 SDK 方法契约上禁止)
    for (auto& target : push_targets) {
        for (auto& kv : collected) {
            if (kv.first == target.first) {
                target.second(kv.first, kv.second);
                break;
            }
        }
    }
    return status;
}

std::future<SdkStatus> TaskGraphSdk::execute_async() {
#ifdef __EMSCRIPTEN__
    // 单线程 WASM:退化为立即执行
    std::promise<SdkStatus> p;
    p.set_value(execute());
    return p.get_future();
#else
    auto self = shared_from_this();
    std::promise<SdkStatus> promise;
    auto future = promise.get_future();
    std::thread worker([self, pm = std::move(promise)]() mutable {
        pm.set_value(self->execute());
    });
    {
        std::lock_guard<std::mutex> wlock(impl_->workers_mutex);
        impl_->workers.push_back(std::move(worker));
    }
    return future;
#endif
}

bool TaskGraphSdk::is_running() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->executor && impl_->executor->is_running();
}

SdkStatus TaskGraphSdk::cancel() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->executor) {
        impl_->set_error("cancel: sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    impl_->executor->cancel();
    return SdkStatus::OK;
}

const ProfileCollector& TaskGraphSdk::profiler() const {
    static const ProfileCollector null_collector;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->executor ? impl_->executor->profiler() : null_collector;
}

std::string TaskGraphSdk::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_error_;
}

// ============================================================================
// 增量更新
// ============================================================================

SdkStatus TaskGraphSdk::update_graph_file(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> exec_lock(impl_->exec_mutex);  // 等在途执行结束
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (!impl_->has_graph) {
        impl_->set_error("update_graph: no graph loaded");
        return SdkStatus::GRAPH_NOT_LOADED;
    }
    DagConfigLoader::Options opts;
    opts.require_known_types = impl_->config.require_known_types;
    auto loaded = DagConfigLoader::load_file(path, opts);
    if (!impl_->apply_update(std::move(loaded))) {
        return SdkStatus::GRAPH_INVALID;
    }
    return SdkStatus::OK;
}

SdkStatus TaskGraphSdk::update_graph_string(const std::string& json) {
    std::lock_guard<std::mutex> exec_lock(impl_->exec_mutex);  // 等在途执行结束
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != Impl::State::Inited) {
        impl_->set_error("sdk not initialized");
        return SdkStatus::NOT_INITIALIZED;
    }
    if (!impl_->has_graph) {
        impl_->set_error("update_graph: no graph loaded");
        return SdkStatus::GRAPH_NOT_LOADED;
    }
    DagConfigLoader::Options opts;
    opts.require_known_types = impl_->config.require_known_types;
    auto loaded = DagConfigLoader::load_string(json, opts);
    if (!impl_->apply_update(std::move(loaded))) {
        return SdkStatus::GRAPH_INVALID;
    }
    return SdkStatus::OK;
}

const GraphDiff& TaskGraphSdk::last_diff() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_diff_;
}

}  // namespace task_graph
