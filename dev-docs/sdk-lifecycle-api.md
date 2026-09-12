# TaskGraph SDK 生命周期 API 设计(sdk)

> 状态:**已确认定稿**(2025-06 评审通过,可实施)
> 已确认的三个设计分叉:
>   1. **单活跃 graph** —— load_graph 即替换,多图场景建多个 SDK 实例;
>   2. **边界节点类型名 `io_input` / `io_output`** —— 与 bind_input/bind_output 对仗;
>   3. **绑定 API = std::any 底座 + template 便捷封装** —— 核心 ABI 只有 any 签名,
>      typed 版本头文件内联转发(跨语言/WASM 绑定只包 any 层)。
> 前置文档:`dev-docs/dag-config-api.md`(只读 DAG JSON 配置 API —— 本文的 graph 加载层)
>
> **实现状态**(全部落地,211/211 测试通过):
>   - C++ 层:`include/task_graph/dag_config.hpp` / `sdk.hpp` + `src/dag_config.cpp` /
>     `sdk.cpp`(含 io_input/io_output 内置任务、DAGExecutor context_values)
>   - **纯 C 层**:`include/task_graph/tg_sdk_c.h` + `src/sdk_c.cpp` —— C++ API 的
>     extern "C" 包装,完整生命周期;约定见头文件注释(不透明句柄、
>     tg_status 码、拷贝式字符串出参(snprintf 语义)、类型化 bind/getter
>     (int32/double/string/bool/tg_image)、推模式回调只通知+拉取取数、
>     异步经 execute_async/execute_wait 的 future 存储)、diff/env 访问器;
>     `tests/sdk_c_header_check.c` 以纯 C 编译验证头文件合法性。
>
> **消费者迁移(已落地)**:
>   - `TaskGraphSdk` 补 `task_result/task_status/task_output/executed_tasks`
>     (execute 后缓存 executor 结果,按任意节点访问,含 exception 失败诊断);
>   - C 侧补 `tg_register_c_task` + `tg_task_ctx_*`(纯 C 自定义任务);
>   - **examples 三个(basic/parallel/multi_output)为纯 C**(`.c`:JSON graph +
>     io 边界 + C 任务 + tg_sdk_* 完整生命周期);
>   - **submodule graph 测试迁移到 C++ 消费 API**:image_filtering、
>     image_color/grading/geometry、image_reader/writer、gpu_image_graph、
>     js_script_graph、render_graph——图生命周期(load_graph_file 自动 base_dir
>     → execute → task_result 断言)全部经 TaskGraphSdk,插件加载/GPU 后端
>     setup/像素参考比较等测试自身逻辑保持;video_io/stream、render_bench、
>     mediapipe(流式/计时/双设备矩阵)不迁移。
> 目标:在"只读配置加载"之上,提供一套**面向宿主 App 的完整 SDK 生命周期**:
> 创建 → 初始化 → 加载 graph → 绑定输入/输出 → 更新 graph → 执行 → 销毁。

---

## 1. 设计总览

### 1.1 分层

```
┌──────────────────────────────────────────────────────┐
│ TaskGraphSdk(新,src/sdk.cpp)                        │
│  生命周期/多graph/IO绑定/diff更新/执行编排           │
├──────────────────────────────────────────────────────┤
│ DagConfigLoader(dag-config-api.md,只读加载+校验)    │
│ DAGSerializer / DAG / DAGCompiler / DAGExecutor(现有)│
│ ExecutionContext(全局参数) Logger(日志回调,现有)    │
│ io_input / io_output 内置任务(新,placeholder 节点)  │
└──────────────────────────────────────────────────────┘
```

SDK 是**编排层**,不重复实现任何执行/校验逻辑;每个环节落到现有组件上(见 §8 映射表)。

### 1.2 状态机

```
Created ──init(cfg)──► Inited ──load_graph(json)──► Ready
   │                                                │  ▲
   │                     bind_input/bind_output     │  │ update_graph(仅 Ready)
   │                     (可多次,执行间随时换绑)     │  │
   │                                                ▼  │
   │                                            Running ──结束──► Ready
   └────────────────────────────────── 任何状态 ──shutdown() ──► Destroyed
```

规则:
- `init()` 恰好一次;未 init 调用其他方法返回 `NOT_INITIALIZED`。
- 同一实例**同时只有一次执行**(与 `DAGExecutor::running_` 语义一致);Running 期间 `update_graph`/`shutdown` 内部排队/取消等待,不抛异常。
- `load_graph` 可再次调用=整体替换(等价全量 diff);`update_graph` 是增量路径。

---

## 2. API 设计

新头文件 `include/task_graph/sdk.hpp`(随 `task_graph_api.hpp` 导出),实现 `src/sdk.cpp`。

### 2.1 创建与初始化(步骤 1、2)

```cpp
namespace task_graph {

// 统一状态码:所有 SDK 方法永不抛异常
enum class SdkStatus {
    OK = 0,
    NOT_INITIALIZED,        // 未 init 即调用
    ALREADY_INITIALIZED,    // init 两次
    GRAPH_NOT_LOADED,       // 未加载 graph 即 execute/bind
    INVALID_ARGUMENT,       // 空 id / 未知节点 / 端口不匹配
    TYPE_MISMATCH,          // 绑定的数据类型与端口声明不符
    GRAPH_INVALID,          // graph JSON 校验失败(issues 附带)
    BUSY,                   // 执行中拒绝操作(仅显式 non-blocking 变体返回)
    INTERNAL_ERROR,         // 框架内部错误(附日志)
};

struct SdkConfig {
    // ---- 日志 ----
    LogLevel log_level{LogLevel::INFO};
    LogSink log_callback;              // 现有 LogSink(LogEntry 回调),进程级;
                                       // sdk 在 init 注册、shutdown 注销(§6 安全)
    // ---- 环境变量 / 全局参数 ----
    std::unordered_map<std::string, std::string> env;        // 环境变量风格 K-V
    std::unordered_map<std::string, std::any>    globals;    // 强类型全局参数
    // 两个 map 都注入 ExecutionContext:globals 原样 set_value;
    // env 以 "_env.<KEY>" 前缀 set_value,任务侧 get_value 查询。
    // (ExecutionContext 是每 DAGExecutor 的执行期黑板,SDK 负责在每次执行前灌入)

    // ---- 资产查找 ----
    std::vector<std::filesystem::path> asset_search_paths;   // 喂 ModelFinder/
                                                             // 相对路径解析

    // ---- 执行 ----
    size_t thread_pool_size{std::thread::hardware_concurrency()};
    std::chrono::milliseconds default_timeout{0};
    bool enable_profiling{false};
    ExecutionCallback event_callback;  // ExecutionEvent(任务级耗时/失败/完成),
                                       // 执行线程触发,调用方自行 marshal 回 UI
    // ---- graph 加载 ----
    bool require_known_types{false};   // 透传 DagConfigLoader(拦截拼错类型名)
};

class TaskGraphSdk {
public:
    // 步骤 1:创建实例(无副作用,不触碰任何单例)
    static std::shared_ptr<TaskGraphSdk> create();

    // 步骤 2:初始化(恰好一次)
    SdkStatus init(const SdkConfig& config);
    bool is_initialized() const;
    ...
};
```

### 2.2 加载 graph(步骤 3)

```cpp
    // 只消费 JSON 文件或 JSON 字符串(委托 DagConfigLoader → to_dag):
    SdkStatus load_graph_file(const std::filesystem::path& path);
    SdkStatus load_graph_string(const std::string& json);

    // 失败诊断:GRAPH_INVALID 时此处取回结构化 issue 列表
    // (severity / stage / json_pointer / line / column / message,复用 DagConfigIssue)
    const std::vector<DagConfigIssue>& last_load_issues() const;

    // 只读快照(不解实例):给 UI 预览图结构
    const DagConfig* graph_config() const;

    // 编译预检(暴露 DAGCompiler::validate 的结果,不执行)
    std::vector<ValidationError> validate_graph() const;
```

graph JSON 约定(复用 v2.0 格式,零扩展成本):**图的边界节点**用两个内置任务类型标记:

```json
{ "version": "2.0",
  "tasks": [
    { "id": "src",  "type": "io_input",  "params": { "data_type": "image" } },
    { "id": "blur", "type": "gpu_box_blur", "params": { "kernel_size": 3 } },
    { "id": "dst",  "type": "io_output" }
  ],
  "edges": [
    { "from": "src", "from_port": "out", "to": "blur", "to_port": "in" },
    { "from": "blur", "from_port": "out", "to": "dst", "to_port": "in" } ] }
```

`io_input`/`io_output` 是核心库内置注册的任务(同 `src/mnn/mnn_registry.cpp` 的核心注册模式,非子模块):

- **`io_input`**:持有 `std::any bound_value_`;`execute()` 把它作为默认端口 `"out"` 的 TaskResult 返回。`data_type` 参数声明期望类型(注册过的稳定类型名,如 `"image"`);未绑定就执行 → 任务 FAILED 并带可读消息。
- **`io_output`**:`execute()` 把上游输入存进 SDK 的 per-run 输出槽,`TaskResult{COMPLETED}`。单端口 `"in"`,可多实例(多输出图)。
- 二者 `param_specs` 声明 data_type/描述,GraphStudio 属性面板免费获得渲染。

### 2.3 绑定输入/输出(步骤 4)

```cpp
    // 绑定输入:把真实数据喂给 io_input 节点(可反复换绑,batch/视频帧场景)
    template <typename T>
    SdkStatus bind_input(const std::string& node_id, T value);   // any 包装 + 类型检查

    // 绑定输出:两种消费模式(可并存,按节点)
    //   拉模式:execute 后 get_output 拉取
    //   推模式:输出就绪即回调(执行线程触发)
    SdkStatus bind_output(const std::string& node_id);                       // 拉模式
    SdkStatus bind_output(const std::string& node_id,
                          std::function<void(const std::string&, std::any)>); // 推模式

    // 执行后拉取(std::any;类型不匹配返回 nullopt 并置 last error)
    template <typename T>
    std::optional<T> get_output(const std::string& node_id) const;

    // 便捷:全局参数运行中更新(下一次 execute 生效;env 不可变,globals 可)
    SdkStatus set_global(const std::string& key, std::any value);
```

设计要点:

- **类型检查前置**:`bind_input` 时即对照 `data_type` 声明校验(`TypeRegistry` 稳定类型名),不等到执行才发现错;`TYPE_MISMATCH` 消息两侧类型名都给出。
- **复用语义正是需求 4**:同一张配置好的 graph,换一组输入/输出绑定即可服务不同数据源;图本身(含 MNN 会话等热状态)不动。
- 绑定的值在 `execute()` 入口**快照**(内部 `std::any` 值拷贝/共享指针语义由数据类型自身决定 —— `Image` 走 shared_ptr 共享,零拷贝),执行期间换绑不影响进行中的 run。
- 除 io 节点外,也允许对**任意任务**的 `bind_input(node_id, port, value)` 做"边改写"吗?—— v1 **不做**(魔法太隐蔽,边结构变了 diff/序列化都混乱);v2 若有需求再以显式 `redirect_edge` API 形式加。

### 2.4 更新 graph(步骤 5)

```cpp
    struct GraphDiff {
        std::vector<std::string> tasks_added, tasks_removed, tasks_updated;
        std::vector<std::string> edges_added, edges_removed;   // "a:out->b:in"
        bool topology_changed() const;   // edges/tasks 增删 → 需重编译执行计划
    };

    // 增量更新:json 与当前图的 diff 应用到现有 DAG
    SdkStatus update_graph_file(const std::filesystem::path& path);
    SdkStatus update_graph_string(const std::string& json);
    const GraphDiff& last_diff() const;
```

diff/应用算法(在 `DagConfig` 快照层算 diff,在 `DAG` 层应用):

1. `DagConfigLoader` 加载新 JSON(同一套校验,失败则旧图原样保留,`GRAPH_INVALID` + issues)。
2. 快照 diff:task 以 `(id)` 为键 —— type 或非 params 配置变了 → `updated`(重建);仅 params 变了 → `updated`(优先 `DAG::update_task_config` 原地改,保住任务热状态 —— MNN/MediaPipe 的会话/句柄);id 消失 → `removed`;新 id → `added`。edge 以四元组精确匹配增删。
3. 应用:复用/新建 `TaskPtr` 装配出新 DAG,`reset_from(std::move(new_dag))` 原子替换(订阅者收到一次 `GraphReset`,与现有 DAG 事件模型一致)。
4. **热状态保留规则**:未变化任务**原样搬运 TaskPtr**(从旧 DAG `get_task` 取出放入新 DAG)—— 这是 diff 更新相对整体重载的全部意义;params-only 变化的任务若 `update_task_config` 成功同样保留实例(它对 plugin task 会重建 IPluginTask 以刷新 spec delegate —— 该语义已存在)。
5. io 绑定按 node_id 存活:updated 且类型未变的 io 节点保留绑定;节点被删则绑定一并丢弃(diff 里报 WARNING)。
6. Running 期间调用:内部等待当前 run 结束再加锁应用(简单正确优先;并发热更新留给 v2)。

### 2.5 执行(步骤 6)

```cpp
    // 同步执行:阻塞到完成;返回聚合状态
    SdkStatus execute();

    // 异步执行:future 就绪即完成;get() 不会抛(SDK 内部已吞异常→INTERNAL_ERROR)
    std::future<SdkStatus> execute_async();
    bool is_running() const;
    SdkStatus cancel();     // 委托 DAGExecutor::cancel(协作式)

    // 每次 execute 前 SDK 自动完成(调用方无感):
    //   1) globals/env 灌入 ExecutionContext
    //   2) 绑定输入快照写入各 io_input
    //   3) 清空 per-run 输出槽
    //   4) DAGCompiler 编译执行计划(拓扑有变才重编译,缓存 ExecutionPlan)

    // 性能数据(enable_profiling 时)
    const ProfileCollector& profiler() const;
```

### 2.6 销毁(步骤 7)

```cpp
    // 显式销毁(也可走析构,逻辑等价):
    //   1) cancel() + 等待在途执行真正结束(join)
    //   2) 注销 init 时注册的 LogSink(关键!Logger 是进程级单例,
    //      不注销则回调悬垂 —— 这是本 API 必须管生命周期的根本原因之一)
    //   3) 释放 graph / 绑定 / 执行计划 / 线程池
    //   之后所有方法返回 INTERNAL_ERROR/无操作;幂等。
    SdkStatus shutdown();
```

线程安全:所有公开方法可从任意线程调用(内部一把实例互斥锁;execute 期间长持锁的是执行本身,其余操作排队)。回调(log/event/output push)在执行线程触发,文档明示 marshal 责任在调用方(与现有 ExecutorConfig::callback 注释一致)。

---

## 3. 典型调用流(对应需求 1-7)

```cpp
// 1. 创建
auto sdk = TaskGraphSdk::create();

// 2. 初始化(全局配置:日志回调/环境变量/全局参数/线程数)
sdk->init(SdkConfig{
    .log_level = LogLevel::WARN,
    .log_callback = [](const LogEntry& e) { my_log_sink(e.msg); },
    .env = {{"DEVICE", "metal"}},
    .globals = {{"threshold", 0.5f}},
    .thread_pool_size = 4,
});

// 3. 加载 graph(JSON 文件或字符串)
if (sdk->load_graph_file("graphs/pipeline.json") != SdkStatus::OK) {
    for (auto& iss : sdk->last_load_issues()) /* 展示行列号+消息 */;
    return 1;
}

// 4. 绑定真实输入/输出 —— 复用同一张图处理自己的数据
sdk->bind_input<Image>("src", img);
sdk->bind_output("dst");                       // 拉模式
sdk->bind_output("preview", [&](auto&, std::any v) { push_preview(any_cast<Image>(v)); });

// 6'. 执行
if (sdk->execute() == SdkStatus::OK) {
    auto out = sdk->get_output<Image>("dst");
}

// 5. 图变了 → 增量更新(diff 由 SDK 处理,未变任务热状态保留)
sdk->update_graph_string(new_json);
// 换一组输入再跑(batch / 新相机帧)
sdk->bind_input<Image>("src", img2);
sdk->execute();

// 7. 销毁(注销日志回调、join 执行线程、释放全部)
sdk->shutdown();
```

---

## 4. 与只读配置 API(dag-config)的关系

| SDK 步骤 | 委托 |
|---|---|
| load_graph_file/string | `DagConfigLoader::load_*`(L0-L3 校验)→ `DagConfig::to_dag()` |
| last_load_issues / graph_config | 直接透传 `DagConfigIssue` / `DagConfig` |
| update_graph | 新快照经 `DagConfigLoader` 校验 → 快照层 diff → DAG 层应用 |
| validate_graph | `DAGCompiler::validate`(构建后语义,含端口契约) |

即:**dag-config 是 SDK 的"入口守门员"**(语法/schema/语义校验、不可变快照),SDK 负责"门后的一切"(生命周期、绑定、diff、执行、清理)。两份文档合起来是完整方案;实现顺序上 dag-config(阶段 A/B)先行,SDK 建立在其上。

---

## 5. 内置 io 任务契约

```cpp
// src/sdk_io_nodes.cpp,核心库内注册(仿 mnn_registry 的 TG_PLUGIN_AUTOREG
// + detail::pull_sdk_io_tasks() 强制链接锚点,STATIC/WASM 不丢注册)

class IoInputNode final : public INode {
    // type() = "io_input";单输出端口 "out"(type_name 取 params.data_type)
    // execute(): bound_value_ 为空 → FAILED("io_input 'src' not bound");
    //            否则 TaskResult{COMPLETED, bound_value_}
    // 线程安全:execute 读的值由 SDK 在执行入口写入(互斥),run 中途换绑不影响
};

class IoOutputNode final : public INode {
    // type() = "io_output";单输入端口 "in"
    // execute(): TaskContext::read 上游 → SDK 输出槽(拉模式)→ 推模式回调
};
```

`data_type` 校验用 `TypeRegistry` 的稳定类型名(`TG_REGISTER_TYPE`,如 Image 的注册名);`bind_input` 与 `execute` 双重检查(绑定期早失败,执行期兜底)。

---

## 6. 生命周期安全要点

1. **LogSink 进程级**:SDK 必须在 `shutdown`/析构注销自己注册的 sink —— 否则销毁后任意插件打日志即回调悬垂(悬垂进 `LoggerImpl`,进程级崩溃)。init 返回前注册、shutdown 第一步注销。
2. **执行线程归属**:线程池由 SDK 拥有(经 `DAGExecutor`);shutdown 必须 `cancel + wait`,不得 detach 逃逸。
3. **`DAG` 是 move-only 且非线程安全**:SDK 用单一实例锁串行化全部状态访问;`DAGExecutor::execute(const DAG&)` 只读访问,execute 期间锁保护写侧。
4. **回调重入**:输出推模式回调里调用 SDK 方法(execute/bind)→ 内部锁是 `std::mutex`,文档明示"回调内不得调用 SDK 方法"(死锁);如需,投递到调用方队列。
5. **WASM**:`execute_async` 在单线程 WASM 上退化为立即执行的 `std::future`(packing 语义),API 面不变。

---

## 7. 测试计划(新目标 `test_sdk`)

**生命周期**
1. 未 init 调 execute → `NOT_INITIALIZED`;init 两次 → `ALREADY_INITIALIZED`;shutdown 幂等,shutdown 后方法安全无崩溃。
2. shutdown 注销日志 sink:销毁后触发 `TG_LOG_INFO` 无崩溃(asan 干净)。

**加载**
3. 坏 JSON → `GRAPH_INVALID` + issues 行列号断言;好图 → `graph_config()` 快照字段正确。
4. `require_known_types=true` 拼错类型名 → `GRAPH_INVALID`。

**绑定与执行**
5. io_input 未绑定执行 → 该任务 FAILED、execute 返回错误、消息含节点 id。
6. bind_input 换绑两次 → 第二次 execute 用新值(值快照语义:run 中途换绑不影响当次)。
7. 类型不匹配 bind(Image 绑到声明 float 的 data_type)→ `TYPE_MISMATCH`,消息含两侧类型名。
8. 多输出图:两个 io_output,`get_output` 各自正确;推模式回调收到且顺序正确。
9. globals/env:任务内 `get_value` 读到 init 与 `set_global` 更新后的值。
10. 与裸 DAGExecutor 等价性:同一 JSON,SDK 路径输出 == 手搭 `from_string + execute` 输出(golden)。

**diff 更新**
11. params-only 变化:任务实例保留(以可观察副作用验证:任务计数器/会话指针不变)且新参数生效。
12. 增删任务/边:diff 报告正确,执行计划重编译,结果正确;被删 io 节点的绑定清理并有 WARNING。
13. 坏 JSON update:旧图保留可继续执行。
14. 执行中 update_graph:当前 run 完成后应用,无数据竞争(tsan 干净)。

---

## 8. 组件映射表(实现不新造轮子)

| SDK 职责 | 复用的现有组件 | 新增 |
|---|---|---|
| JSON 校验/快照 | `DagConfigLoader`/`DagConfig`(前一文阶段 A/B) | — |
| 实例化/执行图 | `DAG`/`DAGCompiler`/`DAGExecutor` | ExecutionPlan 缓存 |
| 日志回调 | `Logger`(`add_log_sink`/`remove_log_sink`) | 注册/注销编排 |
| 全局参数/env | `ExecutionContext::set_value/get_value` | 执行前灌入逻辑 |
| 执行事件 | `ExecutorConfig::callback`(`ExecutionEvent`) | 透传 |
| diff 应用 | `DAG::reset_from`/`update_task_config`/`get_task`(热状态搬运) | 快照层 diff 算法 |
| io 节点 | `INode`/`TaskContext`/`TypeRegistry` | `io_input`/`io_output` 两个内置任务 |
| 资产查找 | `ModelFinder`/`resolve_path` | search_paths 汇入 |

## 9. 实施步骤

| 阶段 | 内容 | 依赖 |
|---|---|---|
| S1 | `sdk.hpp/.cpp` 骨架:状态机、init/shutdown(含 sink 注销)、load_graph(委托 dag-config)、execute/get_output | dag-config 阶段 A/B |
| S2 | `io_input`/`io_output` 内置任务 + 绑定 API + 类型检查 | S1 |
| S3 | diff 更新(快照 diff + 热状态搬运)+ `last_diff` | S2 |
| S4 | `execute_async`/cancel/plan 缓存/tsan 与全量测试 | S3 |

预估 S1+S2 一个提交,S3 一个提交,S4 收尾。
