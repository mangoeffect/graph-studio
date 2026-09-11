#pragma once

// ============================================================================
// dag_config.hpp - 只读 DAG JSON 配置 API
//
// 设计文档:dev-docs/dag-config-api.md(已定稿)
//
// 定位:DAGSerializer 的"消费侧守门员"——
//   - 入口只有两种:JSON 文件路径 / JSON 字符串(不接受 nlohmann::json 构造);
//   - 全程只读零副作用:不触碰 PluginRegistry、不创建任务实例;
//   - 产物 DagConfig 是不可变快照(params 以原始 JSON 保留,类型化推迟到
//     to_dag()——单一权威实现在 DAGSerializer);
//   - 结构化错误 DagConfigIssue(severity/stage/json_pointer/行列号),
//     一次报全多条,公共 API 永不抛异常;
//   - 校验分层:Parse → Schema → Semantics(纯 JSON 平面)→ TypeCheck(可选)。
//
// 需要执行时显式调用 DagConfig::to_dag()(副作用边界:此时才实例化插件任务),
// 与 DAGSerializer::from_string 结果逐位等价(golden test 锁定)。
// ============================================================================

#include <task_graph/dag_serializer.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace task_graph {

// 单条加载诊断。severity=ERROR 会导致 load result ok()==false;
// WARNING 不阻止加载(与 DAGCompiler::validate 的划分一致)。
struct DagConfigIssue {
    enum class Severity { ERROR, WARNING };
    enum class Stage { Parse, Schema, Semantics, TypeCheck };

    Severity severity{Severity::ERROR};
    Stage stage{Stage::Parse};
    // JSON 指针路径,如 "/tasks/3/params/kernel_size";Parse 阶段为空串。
    std::string json_pointer;
    // 仅 Parse 阶段有效(1-based;由 parse_error 字节偏移换算)。
    size_t line{0};
    size_t column{0};
    // 人类可读消息,面向最终用户。
    std::string message;
};

// 快照条目:task 的只读描述(params 未类型化,原样保留)。
struct TaskConfigEntry {
    std::string id;
    std::string type;                       // JSON 未给出 type 时 = id
    int priority = 0;                       // TaskPriority 底层值
    size_t max_retries = 0;
    long long timeout_ms = 0;
    bool skip_on_fail = false;
    std::vector<std::string> dependencies;
    nlohmann::json params_raw;              // object;JSON 未写 params 时为 null
};

struct EdgeConfigEntry {
    std::string from;
    std::string from_port{"out"};
    std::string to;
    std::string to_port{"in"};
};

// 不可变 DAG 配置快照。全部访问器 const;唯一非 const 行为是 to_dag()
// (显式桥接到可执行 DAG)。
class DagConfig {
public:
    DagConfig() = default;

    const std::string& version() const { return version_; }        // "1.0" / "2.0"
    const std::string& base_dir() const { return base_dir_; }      // _source_dir 来源
    const std::vector<TaskConfigEntry>& tasks() const { return tasks_; }
    const std::vector<EdgeConfigEntry>& edges() const { return edges_; }
    // UI 元数据:优先 "metadata" 键,兼容旧的非标准 top-level 键(与
    // DAGSerializer::deserialize_with_metadata 同规则)。
    const nlohmann::json& metadata() const { return metadata_; }
    const TaskConfigEntry* find_task(const TaskId& id) const;

    // 显式桥接:快照 → 可执行 DAG。内部走 DAGSerializer::deserialize 同一
    // 代码路径(_source_dir 注入、按 ParamSpec 类型收敛 params 均在其中),
    // 保证与 from_string 等价。加载存在 ERROR 级 issue 时返回 nullopt。
    std::optional<DAG> to_dag() const;

    bool valid() const { return valid_; }

private:
    friend class DagConfigLoader;

    nlohmann::json root_;                   // 原始解析结果(to_dag 复用)
    std::string base_dir_;
    std::string version_;
    std::vector<TaskConfigEntry> tasks_;
    std::vector<EdgeConfigEntry> edges_;
    nlohmann::json metadata_;
    bool valid_{false};                     // 无 ERROR 级 issue
};

struct DagConfigLoadResult {
    bool ok() const;                        // 无 ERROR 级 issue(允许 WARNING)
    DagConfig config;                       // 失败时 best-effort 填充
    std::vector<DagConfigIssue> issues;
};

class DagConfigLoader {
public:
    struct Options {
        // 语义校验(环/悬垂 edge/重复写同 port/dependencies 引用),纯快照层。
        bool validate_semantics = true;
        // 未知 task type 升级为 ERROR(默认 WARNING)。TypeCheck 阶段只做
        // PluginRegistry::has_task 只读查询,不创建实例。
        bool require_known_types = false;
        // 接受 version "1.0"(与 DAGSerializer 对齐)。
        bool allow_legacy_v1 = true;
        // _source_dir 注入值;load_file 默认 = 文件所在目录,load_string 默认 ""。
        std::string base_dir;
    };

    // ===== 唯二入口:只消费 JSON 文件或 JSON 字符串 =====
    // (重载而非默认实参:nested Options 带 NSDMI,默认实参 {} 在类定义处
    //  需要 complete-type,clang 会报 default member initializer 错误)
    static DagConfigLoadResult load_file(const std::filesystem::path& path);
    static DagConfigLoadResult load_file(const std::filesystem::path& path,
                                         const Options& opts);
    static DagConfigLoadResult load_string(const std::string& json);
    static DagConfigLoadResult load_string(const std::string& json,
                                           const Options& opts);

private:
    // 分层校验 + 快照填充(实现在 src/dag_config.cpp;static 成员经由
    // friend 声明访问 DagConfig 私有成员)。
    static DagConfigLoadResult load_impl(const std::string& json,
                                         const std::string& base_dir,
                                         const Options& opts);
};

}  // namespace task_graph
