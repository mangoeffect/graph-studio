# 只读 DAG JSON 配置 API 设计(dag_config)

> 状态:**已确认定稿**(2025-06 评审通过,可实施;上层 SDK 见 `dev-docs/sdk-lifecycle-api.md`)
> 日期:2025-06
> 目标:提供一套**只消费 JSON 文件或 JSON 字符串**的只读 DAG 配置 API,与现有 `DAGSerializer`(读写)/`DAGCompiler`(校验)/`DAGExecutor`(执行)分层共存。

---

## 1. 现状调研

### 1.1 现有 JSON 消费路径

```
调用方(GraphModel / submodule tests / WASM 上传)
   │ 自己 std::ifstream 读文件、自己算 base_dir
   ▼
DAGSerializer::from_string(json_str, base_dir)      ← 唯一入口,只有 string,没有 from_file
   ├─ nlohmann::json::parse(s)                       ← 解析失败 throw parse_error
   ├─ deserialize(j):
   │    ├─ 缺 version/tasks → throw runtime_error
   │    ├─ 每个 task:PluginRegistry::create_task(type) 临时探针读 param_specs   ← 有副作用
   │    ├─ add_plugin_task() 真实实例化插件任务                                  ← 有副作用
   │    └─ 未知 type → 生成 no-op lambda Task,静默"成功"                        ← 陷阱
   └─ 返回可变 DAG(move-only)
调用方想校验 → DAGCompiler::validate(dag)  ← 必须先付出"构建整个可变 DAG"的代价
```

### 1.2 痛点清单

| # | 痛点 | 证据 |
|---|------|------|
| P1 | **没有 `from_file`** —— 每个消费方自己 `ifstream` + `parent_path` | `GraphModel::from_json_string(json, base_dir)`、各 submodule test main |
| P2 | **错误通道是异常,信息在边界被吞** —— GraphStudio 把异常压成 `bool`/空串,行号、任务 id、原因全部丢失 | `GraphModel.cpp:189-198` `catch (...) { return false; }` |
| P3 | **加载即实例化插件** —— 反序列化内部 `create_task(type)` 探针 + 真实实例化;只想"看看/校验一下"这张图也必须付出完整构建代价,且依赖 registry 已加载 | `dag_serializer.cpp:244-270` |
| P4 | **未知 task type 静默降级为 no-op** —— 拼错类型名照样"加载成功",执行时该节点空转,结果错误但无任何诊断 | `dag_serializer.cpp:261-270` |
| P5 | **校验无法前置** —— `DAGCompiler::validate` 只接受已构建的 `DAG`(且需要任务实例的 `input_specs`) | `compiler.hpp:41` |
| P6 | **解析错误信息不可编程获取** —— nlohmann `parse_error` 带 byte/行号,但异常一路 throw 到顶层被吞 | `dag_serializer.cpp:307` |
| P7 | **DAG 是可变结构** —— 只读消费方(预览器、校验器、WASM 上传检查)拿到的对象仍可被 `add_task/connect` 改坏,没有不可变视图 | `dag.hpp:56-99` |

### 1.3 现有可复用资产

- `parse_params_with_specs()`(dag_serializer.cpp 匿名命名空间):按 ParamSpec 严格收敛参数类型 —— 类型化参数解析的唯一权威实现,新 API 复用而非重写。
- `resolve_path()` / `_source_dir` 注入机制(path_utils.hpp):相对路径随图目录走。
- `DAGCompiler::validate()` 的校验语义(环/required port/类型匹配/重复写同 port):作为语义校验分层的语义基准。
- v1.0/v2.0 双版本反序列化逻辑:版本识别与 edge 默认端口规则照搬。

---

## 2. 设计目标与非目标

### 目标

1. **输入面只有两种**:JSON 文件路径、JSON 字符串。不接受 `nlohmann::json&`、不接受程序化构造(那是 `DAG` 类的事)。
2. **真·只读**:加载零副作用 —— 不触碰 PluginRegistry、不创建任务实例、不修改输入;产物为不可变快照(immutable snapshot)。
3. **结构化错误**:不抛异常的 `Result` 返回;每条 issue 带 severity / 阶段 / JSON pointer 路径 / 行列号 / 可读消息。
4. **校验分层前置**:语法 → schema → 语义,全部在 JSON/快照层面完成,不依赖插件加载。
5. **显式桥接**:需要执行时,一步显式 `to_dag()` 转成现有可执行 `DAG`(此处才实例化插件),与 `DAGSerializer::from_string` 结果**逐位等价**(golden test 保证)。
6. **全平台**:-fno-exceptions(WASM/mobile)下公共 API 依然不抛(见 §7)。

### 非目标

- 写出/保存(继续用 `DAGSerializer::serialize`)。
- 流式/增量更新。
- YAML/其他格式。
- 替换 `DAGSerializer`(保持不动,新 API 是消费侧 фасад,内部复用其实现)。

---

## 3. API 设计

新头文件 `include/task_graph/dag_config.hpp`,实现 `src/dag_config.cpp`,编入核心库 `task_graph`(随 `task_graph_api.hpp` 导出)。

### 3.1 入口:仅有的两个加载函数

```cpp
namespace task_graph {

struct DagConfigIssue {
    enum class Severity { ERROR, WARNING };
    enum class Stage { Parse, Schema, Semantics, TypeCheck };

    Severity severity{Severity::ERROR};
    Stage    stage{Stage::Parse};
    std::string json_pointer;   // "/tasks/3/params/kernel_size";Parse 阶段为 ""
    size_t line{0};             // 仅 Parse 阶段有效(1-based)
    size_t column{0};           // 仅 Parse 阶段有效(1-based)
    std::string message;        // 人类可读,面向最终用户
};

struct DagConfigLoadResult {
    bool ok() const;            // 无 ERROR 级 issue 即 true(允许带 WARNING 加载)
    DagConfig config;           // 失败时也是 best-effort 填充(便于编辑器标错定位)
    std::vector<DagConfigIssue> issues;
};

class DagConfigLoader {
public:
    struct Options {
        // 语义校验(环/悬垂 edge/重复写同 port),纯 JSON 层,无副作用。默认开。
        bool validate_semantics = true;
        // 未知 task type 升级为 ERROR(默认 WARNING;需 registry 已加载才判得出)。
        // P4 陷阱的解药:服务器侧/发布流水线设 true 拦截拼错的类型名。
        bool require_known_types = false;
        // 接受 version "1.0"(默认开,与 DAGSerializer 对齐)。
        bool allow_legacy_v1 = true;
        // 覆盖 _source_dir;load_file 默认 = 文件所在目录,load_string 默认 = ""。
        std::string base_dir;
    };

    // ===== 唯二入口:只消费 json 文件或 json 字符串 =====
    static DagConfigLoadResult load_file(const std::filesystem::path& path,
                                         const Options& opts = {});
    static DagConfigLoadResult load_string(const std::string& json,
                                           const Options& opts = {});
};

}  // namespace task_graph
```

### 3.2 不可变快照:`DagConfig`

```cpp
struct TaskConfigEntry {
    std::string id;
    std::string type;              // 缺省时 = id(与现有规则一致)
    // TaskConfig 非 params 字段(JSON 显式给出才填,否则默认值):
    int         priority = 0;      // TaskPriority 底层值
    size_t      max_retries = 0;
    long long   timeout_ms = 0;
    bool        skip_on_fail = false;
    std::vector<std::string> dependencies;
    // params 原样保留(未按 ParamSpec 收敛 —— 类型化在 to_dag() 时发生,
    // 保证"读"永远不依赖插件加载,单一权威实现在 parse_params_with_specs)。
    nlohmann::json params_raw;     // object;无 params 键时为 null
};

struct EdgeConfigEntry {           // 与 Edge 四元组同形
    std::string from, from_port{"out"},
                to,   to_port{"in"};
};

class DagConfig {
public:
    const std::string& version() const;               // "1.0" / "2.0"
    const std::string& base_dir() const;              // 供消费方再 resolve 相对路径
    const std::vector<TaskConfigEntry>& tasks() const;
    const std::vector<EdgeConfigEntry>& edges() const;
    const nlohmann::json& metadata() const;           // UI 元数据,round-trip 保留
    const TaskConfigEntry* find_task(const TaskId& id) const;

    // ===== 显式桥接:快照 -> 可执行 DAG(此时才触碰 PluginRegistry)=====
    // 内部走 DAGSerializer::deserialize 同一套代码路径,保证等价;
    // _source_dir = base_dir() 注入。ok()==false 时调用返回 nullopt。
    std::optional<DAG> to_dag() const;

private:
    /* 不可变数据 + friend DagConfigLoader;无任何 mutation 方法 */
};
```

要点:

- **`params_raw` 存原始 JSON 而非 `TaskParams`**:类型收敛需要 ParamSpec,而 ParamSpec 来自任务实例;只读层不实例化任务,故推迟到 `to_dag()`。这同时避免了两处类型解析逻辑漂移(单一权威实现)。
- **`to_dag()` 是副作用边界**:API 文档明示"调用即可能创建插件任务实例"。只读消费者(校验器/预览器)不调它就没有任何实例化。
- `DagConfig` 拷贝安全(全值类型),WASM 边界可整体传递。

### 3.3 错误报告语义

| 场景 | issue 示例 |
|---|---|
| JSON 语法错误 | `{ERROR, Parse, "", 12, 8, "expected ':' after object key"}`(行列由 parse_error::byte 反推) |
| 文件打不开/读失败 | `{ERROR, Parse, "", 0, 0, "cannot open file: /x/y.json"}` |
| 缺 version / version 非法 | `{ERROR, Schema, "", …, "unsupported DAG version: 3.0"}` |
| task 缺 id / id 非字符串 / tasks 非数组 | `{ERROR, Schema, "/tasks/3", …}` |
| **task id 重复** | `{ERROR, Schema, "/tasks/5/id", …, "duplicate task id 'blur'"}`(现状:add_task throw,但消息无位置) |
| edge 引用不存在的 task | `{ERROR, Semantics, "/edges/2/from", …, "edge references unknown task 'outp'"}`(现状:connect throw) |
| **图有环** | `{ERROR, Semantics, "", …, "cycle detected: a -> b -> c -> a"}` |
| 多条 edge 写同一 to_port | `{WARNING, Semantics, "/edges/4", …}`(与 compiler.validate 语义一致) |
| v1.0 同 (to,"in") 多源 | `{WARNING, Semantics, …, "multiple sources into v1.0 default port; upgrade to 2.0 named ports"}` |
| 未知 task type | WARNING;`require_known_types=true` 时 ERROR |
| params 中未声明 key | 不报(与现状一致,渐进迁移) |

**多个错误一次报全**:schema/semantics 层收集后统一返回(现状是 throw 第一个就停),编辑器可以一次标出所有红点。Parse 失败则止于 Parse(文本无法结构化)。

---

## 4. 校验分层

```
L0 Parse      nlohmann 解析;失败→行列号 issue,停止
L1 Schema     结构/字段/类型/唯一性:version、tasks[]、id、duplicate id、
              edges[] 四元组、params 为 object、TaskConfig 字段类型
              —— 纯 JSON,零依赖
L2 Semantics  快照图论检查:edge 端点存在、自环、环检测(DFS,报出环路径)、
              同 to_port 多写(v1.0 聚合到默认端口)、task dependencies
              引用存在性
              —— 纯快照,零依赖(默认开,可关)
L3 TypeCheck  (可选,默认关)require_known_types 时查询
              PluginRegistry::has_task(type)
              —— 唯一触碰 registry 的路径,只读查询、不创建实例
────── 以上为只读层 ──────
L4 Build      to_dag():复用 DAGSerializer::deserialize 路径,实例化任务,
              parse_params_with_specs 类型收敛,_source_dir 注入
```

L2 的检查刻意与 `DAGCompiler::validate()` 的 ERROR/WARNING 划分保持一致(环=ERROR、多写 port=WARNING),但实现独立:compiler 需要 `input_specs()`(即任务实例),L2 在 id/edge 平面即可完成 —— 这是"校验前置"的核心收益。

---

## 5. 与现有组件的关系

```
             ┌──────────── 只读层(新) ────────────┐
json file ──►│ DagConfigLoader                     │
json str  ──►│   ├─ parse + schema + semantics     │────► DagConfig(不可变快照)
             │   └─ issues[](结构化诊断)           │        │
             └─────────────────────────────────────┘        │ to_dag()(显式)
                                                              ▼
json str ──►│ DAGSerializer::from_string(现有,读写路径) │──► DAG(可变,编辑器用)
                                                              │
                                                    DAGCompiler::validate
                                                    DAGExecutor::execute
```

- `DAGSerializer` **不动**:编辑器的保存/加载继续用它;新 API 是消费侧(测试、服务端校验、WASM 上传检查、发布流水线)的入口。
- `to_dag()` 内部直接调用 `DAGSerializer::deserialize`(将其内部 `parse_params_with_specs`、v1/v2 edge 规则、`_source_dir` 注入全部复用),**不是第二套反序列化实现** —— 等价性由构造保证 + golden test 锁死。
- `load_file` 内部 = `ifstream` 读取 + `load_string(json, opts.base_dir = parent_path(path))`,`base_dir` 推导收敛进框架,消费方不再手算。

---

## 6. 使用示例

```cpp
// 1) 服务端/CI 校验一张图,不加载任何插件
auto r = DagConfigLoader::load_file("graphs/pipeline.json",
            {.require_known_types = false});
if (!r.ok()) {
    for (auto& iss : r.issues)
        if (iss.severity == DagConfigIssue::Severity::ERROR)
            std::cerr << iss.json_pointer << ": " << iss.message << "\n";
    return 1;
}
// 快照只读消费:
for (auto& t : r.config.tasks())
    std::cout << t.id << " (" << t.type << ") params=" << t.params_raw.dump() << "\n";

// 2) 校验通过后执行(此时才实例化插件)
if (auto dag = r.config.to_dag()) {
    DAGExecutor ex(*dag);
    ex.execute();
}

// 3) 拦截拼错的类型名(发布前检查)
auto strict = DagConfigLoader::load_string(json_str,
               {.require_known_types = true});
// "opencv_image_read" 拼成 "opencv_imag_read" → ERROR issue,不再静默 no-op

// 4) WASM 上传:字符串入口,不抛异常,错误码可跨 ABI
auto r2 = DagConfigLoader::load_string(uploaded_json);
return r2.ok() ? 0 : static_cast<int>(r2.issues.front().stage);
```

---

## 7. 异常安全与 -fno-exceptions

公共 API **永不抛出**:

- 实现 .cpp 内部用 try/catch 包住 `nlohmann::json::parse`(核心库当前带异常编译,`dag_serializer.cpp` 的 throw 是既有事实),把 `parse_error::byte` 换算成行列号(统计 `\n`)填入 issue。
- 文件读取用 `std::filesystem::filesystem_error` 捕获同理。
- 未来若核心库转 -fno-exceptions:降级用 `json::parse(s, nullptr, /*allow_exceptions=*/false)`,`is_discarded()` → 通用 "JSON syntax error" issue(丢行列号),API 面不变。此路径在文档中注明,不在本期实现。

---

## 8. 测试计划

新测试目标 `test_dag_config`(注册进根 CMakeLists,与 `test_dag` 同形态):

**加载与快照**
1. v2.0 合法图(带 metadata)→ ok,快照字段逐一断言(version/tasks/edges/metadata)。
2. v1.0 legacy 图 → ok,edge 默认端口补全为 out/in。
3. `allow_legacy_v1=false` + v1.0 → ERROR。
4. `load_file`:临时目录写 json + 相对路径资产 → `base_dir()` = 目录,`to_dag()` 后 `_source_dir` 注入正确。
5. 文件不存在 → Parse ERROR,消息含路径。

**诊断**
6. 语法错误 → line/column 断言(构造已知位置的坏 JSON)。
7. 缺 version、tasks 非数组、task 缺 id → Schema ERROR + json_pointer 断言。
8. duplicate id、悬垂 edge、3 节点环 → 各自 ERROR,**一次返回多条**(双错误用例)。
9. 同 to_port 双写 → WARNING 且 `ok()==true`。
10. 未知 type:默认 WARNING ok;`require_known_types` → ERROR(测试注册一个 dummy type 到 registry)。

**等价性(golden)**
11. 对 `tests/` 下既有全部合法 graph JSON:`to_dag()` 后 `DAGSerializer::to_string(dag)` == `from_string` 同路输出的字符串(逐字节)。
12. `to_dag()` 产物过 `DAGCompiler::validate` 无 ERROR,与直连路径一致。

---

## 9. 实施步骤

| 阶段 | 内容 | 交付 |
|---|---|---|
| A | `dag_config.hpp/.cpp`:`DagConfig`、`DagConfigLoader`(L0-L2)、issue 模型;`parse_params_with_specs` 从 dag_serializer.cpp 匿名命名空间提为共享内部符号(`detail::`)供复用 | 头/源/注册 CMake |
| B | `to_dag()` 桥接(复用 deserialize)+ golden 等价测试 | test_dag_config 目标 |
| C(可选) | GraphStudio `GraphModel::from_json_string` 内部换用 loader(外部 bool 签名不变,增加 issue 日志);submodule 测试公共 helper `load_graph_json(path)` 收敛各 test main 的手写 ifstream | 迁移 PR |

预估:A+B 一次提交可完成(`src/` 两个文件 + 头文件 + 测试),不动任何现有行为。
