#include <task_graph/dag_config.hpp>

#include <task_graph/plugin.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace task_graph {

namespace {

using IssueSeverity = DagConfigIssue::Severity;
using IssueStage = DagConfigIssue::Stage;

void add_issue(std::vector<DagConfigIssue>& issues, IssueSeverity sev, IssueStage stage,
               std::string pointer, std::string message,
               size_t line = 0, size_t column = 0) {
    DagConfigIssue iss;
    iss.severity = sev;
    iss.stage = stage;
    iss.json_pointer = std::move(pointer);
    iss.line = line;
    iss.column = column;
    iss.message = std::move(message);
    issues.push_back(std::move(iss));
}

// 由 parse_error 的字节偏移换算 1-based 行列号。
void line_col_from_byte(const std::string& src, size_t byte,
                        size_t& line, size_t& column) {
    line = 1;
    column = 1;
    const size_t limit = std::min(byte, src.size());
    for (size_t i = 0; i < limit; ++i) {
        if (src[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}

bool has_error(const std::vector<DagConfigIssue>& issues) {
    for (const auto& iss : issues) {
        if (iss.severity == IssueSeverity::ERROR) return true;
    }
    return false;
}

// ---- L1 Schema:结构/字段/类型/唯一性(纯 JSON,零依赖) ----
// 返回是否通过(无 ERROR);tasks_/edges_ 等快照数据 best-effort 填充。
bool validate_schema(const nlohmann::json& j,
                     const DagConfigLoader::Options& opts,
                     std::vector<TaskConfigEntry>& tasks,
                     std::vector<EdgeConfigEntry>& edges,
                     std::string& version,
                     nlohmann::json& metadata,
                     std::vector<DagConfigIssue>& issues) {
    if (!j.is_object()) {
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "",
                  "DAG JSON root must be an object");
        return false;
    }

    // ---- version ----
    bool version_ok = false;
    if (!j.contains("version") || !j["version"].is_string()) {
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "/version",
                  "missing or non-string 'version' in DAG JSON");
    } else {
        version = j["version"].get<std::string>();
        if (version == "2.0") {
            version_ok = true;
        } else if (version == "1.0") {
            if (opts.allow_legacy_v1) {
                version_ok = true;
            } else {
                add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "/version",
                          "legacy DAG version 1.0 rejected (allow_legacy_v1=false)");
            }
        } else {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "/version",
                      "unsupported DAG version: " + version);
        }
    }

    // ---- tasks ----
    if (!j.contains("tasks")) {
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "",
                  "missing 'tasks' in DAG JSON");
        return false;
    }
    if (!j["tasks"].is_array()) {
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "/tasks",
                  "'tasks' must be an array");
        return false;
    }

    std::set<std::string> task_ids;
    size_t task_index = 0;
    for (const auto& tj : j["tasks"]) {
        std::string pointer = "/tasks/" + std::to_string(task_index);
        ++task_index;
        if (!tj.is_object()) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, pointer,
                      "task entry must be an object");
            continue;
        }
        if (!tj.contains("id") || !tj["id"].is_string() || tj["id"].get<std::string>().empty()) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, pointer + "/id",
                      "task entry requires a non-empty string 'id'");
            continue;
        }

        TaskConfigEntry entry;
        entry.id = tj["id"].get<std::string>();
        if (task_ids.contains(entry.id)) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                      pointer + "/id", "duplicate task id '" + entry.id + "'");
            continue;
        }
        task_ids.insert(entry.id);

        if (tj.contains("type")) {
            if (!tj["type"].is_string()) {
                add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                          pointer + "/type", "'type' must be a string");
                continue;
            }
            entry.type = tj["type"].get<std::string>();
        } else {
            entry.type = entry.id;  // 与 DAGSerializer 规则一致
        }

        if (tj.contains("params")) {
            if (!tj["params"].is_object()) {
                add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                          pointer + "/params", "'params' must be an object");
                continue;
            }
            entry.params_raw = tj["params"];
        }

        // TaskConfig 字段类型检查(存在且类型不符 → ERROR,与反序列化抛错对齐)
        bool fields_ok = true;
        auto check_number = [&](const char* key) {
            if (tj.contains(key) && !tj[key].is_number()) {
                add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                          pointer + "/" + key,
                          std::string("'") + key + "' must be a number");
                fields_ok = false;
            }
        };
        check_number("priority");
        check_number("max_retries");
        check_number("timeout_ms");
        if (tj.contains("skip_on_fail") && !tj["skip_on_fail"].is_boolean()) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                      pointer + "/skip_on_fail", "'skip_on_fail' must be a boolean");
            fields_ok = false;
        }
        if (tj.contains("dependencies")) {
            if (!tj["dependencies"].is_array()) {
                add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                          pointer + "/dependencies", "'dependencies' must be an array");
                fields_ok = false;
            } else {
                for (const auto& dep : tj["dependencies"]) {
                    if (!dep.is_string()) {
                        add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                                  pointer + "/dependencies",
                                  "'dependencies' entries must be strings");
                        fields_ok = false;
                        break;
                    }
                    entry.dependencies.push_back(dep.get<std::string>());
                }
            }
        }
        if (!fields_ok) continue;

        if (tj.contains("priority")) entry.priority = tj["priority"].get<int>();
        if (tj.contains("max_retries")) entry.max_retries = tj["max_retries"].get<size_t>();
        if (tj.contains("timeout_ms")) entry.timeout_ms = tj["timeout_ms"].get<long long>();
        if (tj.contains("skip_on_fail")) entry.skip_on_fail = tj["skip_on_fail"].get<bool>();

        tasks.push_back(std::move(entry));
    }

    // ---- edges(可选;格式随 version) ----
    if (j.contains("edges")) {
        if (!j["edges"].is_array()) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, "/edges",
                      "'edges' must be an array");
        } else {
            size_t edge_index = 0;
            for (const auto& ej : j["edges"]) {
                std::string pointer = "/edges/" + std::to_string(edge_index);
                ++edge_index;
                if (!ej.is_object()) {
                    add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, pointer,
                              "edge entry must be an object");
                    continue;
                }
                if (!ej.contains("from") || !ej["from"].is_string() ||
                    !ej.contains("to") || !ej["to"].is_string()) {
                    add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema, pointer,
                              "edge requires string 'from' and 'to'");
                    continue;
                }
                EdgeConfigEntry e;
                e.from = ej["from"].get<std::string>();
                e.to = ej["to"].get<std::string>();
                if (version == "1.0") {
                    e.from_port = "out";
                    e.to_port = "in";
                } else {
                    if (ej.contains("from_port")) {
                        if (!ej["from_port"].is_string()) {
                            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                                      pointer + "/from_port", "'from_port' must be a string");
                            continue;
                        }
                        e.from_port = ej["from_port"].get<std::string>();
                    }
                    if (ej.contains("to_port")) {
                        if (!ej["to_port"].is_string()) {
                            add_issue(issues, IssueSeverity::ERROR, IssueStage::Schema,
                                      pointer + "/to_port", "'to_port' must be a string");
                            continue;
                        }
                        e.to_port = ej["to_port"].get<std::string>();
                    }
                }
                edges.push_back(std::move(e));
            }
        }
    }

    // ---- metadata(与 deserialize_with_metadata 同规则) ----
    if (j.contains("metadata") && j["metadata"].is_object()) {
        metadata = j["metadata"];
    }
    static const std::set<std::string> known_keys = {"version", "tasks", "edges", "metadata"};
    for (const auto& kv : j.get<nlohmann::json::object_t>()) {
        if (!known_keys.contains(kv.first)) {
            metadata[kv.first] = kv.second;
        }
    }

    (void)version_ok;
    return !has_error(issues);
}

// ---- L2 Semantics:快照图论检查(纯 id/edge 平面) ----
void validate_semantics(const std::vector<TaskConfigEntry>& tasks,
                        const std::vector<EdgeConfigEntry>& edges,
                        const std::string& version,
                        std::vector<DagConfigIssue>& issues) {
    std::unordered_set<std::string> ids;
    for (const auto& t : tasks) ids.insert(t.id);

    // 悬垂 edge / 自环
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        std::string pointer = "/edges/" + std::to_string(i);
        if (!ids.contains(e.from)) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Semantics, pointer + "/from",
                      "edge references unknown task '" + e.from + "'");
        }
        if (!ids.contains(e.to)) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Semantics, pointer + "/to",
                      "edge references unknown task '" + e.to + "'");
        }
        if (e.from == e.to) {
            add_issue(issues, IssueSeverity::ERROR, IssueStage::Semantics, pointer,
                      "self loop on task '" + e.from + "'");
        }
    }

    // 环检测(迭代 DFS,三色标记;发现回边时沿 parent 链还原环路径)
    {
        std::unordered_map<std::string, std::vector<size_t>> adj;
        for (size_t i = 0; i < edges.size(); ++i) adj[edges[i].from].push_back(i);
        std::unordered_map<std::string, std::string> parent;
        std::unordered_map<std::string, int> color;  // 0=white 1=gray 2=black
        bool cycle_found = false;
        for (const auto& t : tasks) {
            if (cycle_found) break;
            if (color[t.id] != 0) continue;
            // 迭代 DFS:栈元素 = (节点, 已访问的邻边下标)
            std::vector<std::pair<std::string, size_t>> stack;
            stack.emplace_back(t.id, 0);
            color[t.id] = 1;
            while (!stack.empty() && !cycle_found) {
                auto& [node, idx] = stack.back();
                const auto& out = adj[node];
                if (idx < out.size()) {
                    const std::string& next = edges[out[idx]].to;
                    ++idx;
                    if (color[next] == 1) {
                        // 回边:还原环 next ... node -> next
                        std::string path = next;
                        std::string cur = node;
                        while (cur != next) {
                            path = cur + " -> " + path;
                            cur = parent[cur];
                        }
                        add_issue(issues, IssueSeverity::ERROR, IssueStage::Semantics, "",
                                  "cycle detected: " + path + " -> " + next);
                        cycle_found = true;
                    } else if (color[next] == 0) {
                        parent[next] = node;
                        color[next] = 1;
                        stack.emplace_back(next, 0);
                    }
                } else {
                    color[node] = 2;
                    stack.pop_back();
                }
            }
        }
    }

    // 重复写同一 to_port(WARNING,与 DAGCompiler::validate 语义一致)
    {
        // key: to + ":" + to_port -> 不同 from 集合
        std::map<std::pair<std::string, std::string>, std::set<std::string>> writers;
        std::map<std::string, int> exact_dup;
        for (const auto& e : edges) {
            writers[{e.to, e.to_port}].insert(e.from);
            const std::string quad = e.from + ":" + e.from_port + "->" + e.to + ":" + e.to_port;
            ++exact_dup[quad];
        }
        for (const auto& kv : writers) {
            if (kv.second.size() > 1) {
                std::string msg = "multiple edges write port '" + kv.first.second +
                                  "' of task '" + kv.first.first + "' (last-write-wins)";
                if (version == "1.0") {
                    msg += "; upgrade to 2.0 named ports";
                }
                add_issue(issues, IssueSeverity::WARNING, IssueStage::Semantics, "", msg);
            }
        }
        for (const auto& kv : exact_dup) {
            if (kv.second > 1) {
                add_issue(issues, IssueSeverity::WARNING, IssueStage::Semantics, "",
                          "duplicate edge " + kv.first + " (ignored on load)");
            }
        }
    }

    // config.dependencies 引用存在性(WARNING:依赖由 executor 另行处理)
    for (const auto& t : tasks) {
        for (const auto& dep : t.dependencies) {
            if (!ids.contains(dep)) {
                add_issue(issues, IssueSeverity::WARNING, IssueStage::Semantics, "",
                          "task '" + t.id + "' depends on unknown task '" + dep + "'");
            }
        }
    }
}

// ---- L3 TypeCheck:未知 task type(只读查询,不创建实例) ----
void validate_types(const std::vector<TaskConfigEntry>& tasks,
                    bool as_error,
                    std::vector<DagConfigIssue>& issues) {
    for (const auto& t : tasks) {
        if (!PluginRegistry::instance().has_task(t.type)) {
            add_issue(issues,
                      as_error ? IssueSeverity::ERROR : IssueSeverity::WARNING,
                      IssueStage::TypeCheck, "",
                      "unknown task type '" + t.type +
                      "' (node '" + t.id + "' will become a no-op)");
        }
    }
}

// 分层校验的结果数据(由 load_impl 搬进 DagConfig 私有成员)
struct LoadData {
    nlohmann::json root;
    std::string base_dir;
    std::string version;
    std::vector<TaskConfigEntry> tasks;
    std::vector<EdgeConfigEntry> edges;
    nlohmann::json metadata;
    bool valid{false};
};

LoadData load_config_entry(const std::string& json,
                           const std::string& base_dir,
                           const DagConfigLoader::Options& opts,
                           std::vector<DagConfigIssue>& issues) {
    LoadData data;

    // ---- L0 Parse(本仓库 mini json:异常即失败,parse_error 带字节偏移) ----
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error& e) {
        size_t line = 0, column = 0;
        line_col_from_byte(json, e.byte, line, column);
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Parse, "",
                  e.what(), line, column);
        return data;
    } catch (const std::exception& e) {
        add_issue(issues, IssueSeverity::ERROR, IssueStage::Parse, "", e.what());
        return data;
    }

    // ---- L1 Schema ----
    bool schema_ok = validate_schema(j, opts, data.tasks, data.edges,
                                     data.version, data.metadata, issues);

    // ---- L2 Semantics ----
    if (schema_ok && opts.validate_semantics) {
        validate_semantics(data.tasks, data.edges, data.version, issues);
    }

    // ---- L3 TypeCheck ----
    if (schema_ok && opts.require_known_types) {
        validate_types(data.tasks, true, issues);
    } else if (schema_ok) {
        // 默认 WARNING:提示 no-op 陷阱但不阻止加载
        validate_types(data.tasks, false, issues);
    }

    data.root = std::move(j);
    data.base_dir = base_dir;
    data.valid = !has_error(issues);
    return data;
}

}  // namespace

bool DagConfigLoadResult::ok() const {
    return !has_error(issues);
}

const TaskConfigEntry* DagConfig::find_task(const TaskId& id) const {
    for (const auto& t : tasks_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::optional<DAG> DagConfig::to_dag() const {
    if (!valid_) return std::nullopt;
    try {
        return DAGSerializer::deserialize(root_, base_dir_);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

DagConfigLoadResult DagConfigLoader::load_impl(const std::string& json,
                                               const std::string& base_dir,
                                               const Options& opts) {
    DagConfigLoadResult result;
    LoadData data = load_config_entry(json, base_dir, opts, result.issues);
    result.config.root_ = std::move(data.root);
    result.config.base_dir_ = std::move(data.base_dir);
    result.config.version_ = std::move(data.version);
    result.config.tasks_ = std::move(data.tasks);
    result.config.edges_ = std::move(data.edges);
    result.config.metadata_ = std::move(data.metadata);
    result.config.valid_ = data.valid;
    return result;
}

DagConfigLoadResult DagConfigLoader::load_file(const std::filesystem::path& path) {
    return load_file(path, Options{});
}

DagConfigLoadResult DagConfigLoader::load_file(const std::filesystem::path& path,
                                               const Options& opts) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        DagConfigLoadResult result;
        add_issue(result.issues, IssueSeverity::ERROR, IssueStage::Parse, "",
                  "cannot open file: " + path.string());
        return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        DagConfigLoadResult result;
        add_issue(result.issues, IssueSeverity::ERROR, IssueStage::Parse, "",
                  "failed reading file: " + path.string());
        return result;
    }
    std::string base_dir = opts.base_dir;
    if (base_dir.empty()) {
        auto parent = path.parent_path();
        if (!parent.empty()) base_dir = parent.string();
    }
    return load_impl(ss.str(), base_dir, opts);
}

DagConfigLoadResult DagConfigLoader::load_string(const std::string& json) {
    return load_string(json, Options{});
}

DagConfigLoadResult DagConfigLoader::load_string(const std::string& json,
                                                 const Options& opts) {
    return load_impl(json, opts.base_dir, opts);
}

}  // namespace task_graph
