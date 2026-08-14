#include <task_graph/dag_serializer.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/path_utils.hpp>
#include <stdexcept>
#include <any>
#include <variant>
#include <type_traits>
#include <typeinfo>
#include <cctype>
#include <algorithm>
#include <set>

namespace task_graph {

namespace {

// 公共：从 task_json 读取 TaskConfig 非 params 字段（priority/max_retries/timeout/
// skip_on_fail/dependencies）。params 单独按声明的 param_specs 解析（见下），
// 以避免 v1.0 时代按 JSON 字面量猜类型导致的隐蔽 bug（如把 float "sigma":2
// 存成 int，使后续 get_param_float 静默失败）。
TaskConfig parse_task_config(const nlohmann::json& task_json) {
    TaskConfig config;
    if (task_json.contains("priority")) {
        config.priority = static_cast<TaskPriority>(task_json["priority"].get<int>());
    }
    if (task_json.contains("max_retries")) {
        config.max_retries = task_json["max_retries"].get<size_t>();
    }
    if (task_json.contains("timeout_ms")) {
        config.timeout = std::chrono::milliseconds(task_json["timeout_ms"].get<int>());
    }
    if (task_json.contains("skip_on_fail")) {
        config.skip_on_fail = task_json["skip_on_fail"].get<bool>();
    }
    if (task_json.contains("dependencies")) {
        for (const auto& dep : task_json["dependencies"]) {
            config.dependencies.push_back(dep.get<std::string>());
        }
    }
    return config;
}

// 按声明的 ParamSpec 将 JSON params 解析到 TaskParams，类型严格遵循声明：
//   - Int/Enum: 接受任意数字，转为 int
//   - Float:    接受任意数字，转为 float
//   - String:   接受字符串
//   - Bool:     接受 bool；兼容数字（非 0 为 true）与 "true"/"1" 字符串
// 未在 specs 中声明的 key 按旧逻辑按 JSON 字面量类型回退（保持渐进迁移友好）。
TaskParams parse_params_with_specs(const nlohmann::json& params_json,
                                   const std::vector<ParamSpec>& specs) {
    TaskParams params;
    std::unordered_map<std::string, const ParamSpec*> spec_by_name;
    for (const auto& s : specs) spec_by_name[s.name] = &s;

    for (const auto& [key, value] : params_json.get<nlohmann::json::object_t>()) {
        auto it = spec_by_name.find(key);
        if (it != spec_by_name.end()) {
            const ParamSpec* spec = it->second;
            try {
                switch (spec->type) {
                    case ParamType::Int:
                    case ParamType::Enum:
                        if (value.is_number())
                            params.set_int(key, static_cast<int>(value.get<double>()));
                        // Enum 额外支持字符串 label —— 与 param_specs() 声明的
                        // enum_values 匹配（大小写不敏感）。匹配不到则忽略，
                        // 由 task 内部回退默认值。
                        else if (value.is_string() && spec->type == ParamType::Enum) {
                            const std::string label = value.get<std::string>();
                            for (const auto& [lbl, v] : spec->enum_values) {
                                std::string a = lbl, b = label;
                                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                                if (a == b) {
                                    params.set_int(key, v);
                                    break;
                                }
                            }
                        }
                        break;
                    case ParamType::Float:
                        if (value.is_number())
                            params.set_float(key, static_cast<float>(value.get<double>()));
                        break;
                    case ParamType::String:
                        if (value.is_string())
                            params.set_string(key, value.get<std::string>());
                        break;
                    case ParamType::Bool:
                        if (value.is_boolean())
                            params.set_bool(key, value.get<bool>());
                        else if (value.is_number())
                            params.set_bool(key, value.get<double>() != 0.0);
                        else if (value.is_string()) {
                            const auto& s = value.get<std::string>();
                            params.set_bool(key, s == "true" || s == "1");
                        }
                        break;
                }
            } catch (const std::exception&) {
                // 转换失败：跳过该 key，task 内部会回退到声明默认值
            }
        } else {
            // 未声明的 key：按 JSON 字面量类型回退
            if (value.is_number_integer())
                params.set_int(key, static_cast<int>(value.get<int64_t>()));
            else if (value.is_number_float())
                params.set_float(key, static_cast<float>(value.get<double>()));
            else if (value.is_string())
                params.set_string(key, value.get<std::string>());
            else if (value.is_boolean())
                params.set_bool(key, value.get<bool>());
        }
    }
    return params;
}

nlohmann::json serialize_specs(const std::vector<PortSpec>& specs) {
    auto arr = nlohmann::json::array();
    for (const auto& s : specs) {
        nlohmann::json o;
        o["name"] = s.name;
        if (!s.type_name.empty()) o["type"] = s.type_name;
        o["required"] = s.required;
        arr.push_back(std::move(o));
    }
    return arr;
}

// 框架注入到 params 里的保留 key，序列化时必须跳过：
//   - 这些是运行时上下文（_source_dir = graph.json 所在目录），不属于用户数据；
//   - 写回 graph.json 会泄露作者本机路径，且文件移动后立刻失效。
// 命名约定：所有保留 key 以 '_' 开头，业务参数请勿使用此命名。
bool is_framework_reserved_param(const std::string& key) {
    return !key.empty() && key[0] == '_';
}

}  // namespace

// ====================== v2.0 序列化 ======================
// 与 v1.0 的差异：
//   - version: "2.0"
//   - edges: {from, from_port, to, to_port}
//   - tasks: 新增 input_specs / output_specs（仅当非空时写入）
nlohmann::json DAGSerializer::serialize(const DAG& dag) {
    nlohmann::json j;

    j["version"] = "2.0";
    j["tasks"] = nlohmann::json::array();

    for (const auto& [id, task] : dag.tasks()) {
        nlohmann::json task_json;
        task_json["id"] = id;

        if (!task->type().empty() && task->type() != id) {
            task_json["type"] = task->type();
        }

        const auto& config = task->config();
        task_json["priority"]    = static_cast<int>(config.priority);
        task_json["max_retries"] = config.max_retries;
        task_json["timeout_ms"]  = config.timeout.count();
        task_json["skip_on_fail"] = config.skip_on_fail;

        nlohmann::json deps_json = nlohmann::json::array();
        for (const auto& dep : config.dependencies) deps_json.push_back(dep);
        task_json["dependencies"] = deps_json;

        nlohmann::json params_json = nlohmann::json::object();
        // 不用结构化绑定：Xcode 15 的 clang 不允许在 std::visit 的 lambda 里
        // 捕获结构化绑定（P1091），key/value 改为普通引用。
        for (const auto& kv : config.params.params()) {
            const std::string& key = kv.first;
            const auto& value = kv.second;
            // 跳过框架保留参数（_source_dir 等）：不写入 graph.json
            if (is_framework_reserved_param(key)) continue;
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int>) {
                    params_json[key] = v;
                } else if constexpr (std::is_same_v<T, float>) {
                    params_json[key] = static_cast<double>(v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    params_json[key] = v;
                } else if constexpr (std::is_same_v<T, bool>) {
                    params_json[key] = v;
                }
            }, value);
        }
        if (!params_json.empty()) task_json["params"] = params_json;

        // 端口契约：仅当 task 声明了 specs 时写入，便于 GUI 渲染端口锚点
        auto in_specs  = task->input_specs();
        auto out_specs = task->output_specs();
        if (!in_specs.empty())  task_json["input_specs"]  = serialize_specs(in_specs);
        if (!out_specs.empty()) task_json["output_specs"] = serialize_specs(out_specs);

        j["tasks"].push_back(std::move(task_json));
    }

    j["edges"] = nlohmann::json::array();
    for (const auto& e : dag.edges()) {
        nlohmann::json edge_json;
        edge_json["from"]       = e.from;
        edge_json["from_port"]  = e.from_port;
        edge_json["to"]         = e.to;
        edge_json["to_port"]    = e.to_port;
        j["edges"].push_back(std::move(edge_json));
    }

    return j;
}

// ====================== 反序列化：自动识别 v1.0 / v2.0 ======================
DAG DAGSerializer::deserialize(const nlohmann::json& j, const std::string& base_dir) {
    if (!j.contains("version")) {
        throw std::runtime_error("Missing version in DAG JSON");
    }
    std::string version = j["version"].get<std::string>();

    DAG dag;

    if (!j.contains("tasks")) {
        throw std::runtime_error("Missing tasks in DAG JSON");
    }

    // ---- tasks：v1.0/v2.0 共用 ----
    for (const auto& task_json : j["tasks"]) {
        std::string id = task_json["id"].get<std::string>();
        std::string type;
        if (task_json.contains("type")) {
            type = task_json["type"].get<std::string>();
        } else {
            type = id;
        }

        TaskConfig config = parse_task_config(task_json);

        // 按 task 声明的 param_specs 解析 params（修复 v1.0 按字面量猜类型的 bug）
        if (task_json.contains("params")) {
            std::vector<ParamSpec> specs;
            if (PluginRegistry::instance().has_task(type)) {
                // 实例化一个临时 task 读取声明的 param_specs
                if (auto probe = PluginRegistry::instance().create_task(type)) {
                    specs = probe->param_specs();
                }
            }
            config.params = parse_params_with_specs(task_json["params"], specs);
        }

        // 框架参数注入：graph.json 所在目录，供节点 resolve 相对路径使用。
        // base_dir 为空时仍然写入空串占位（保持 params 形态一致；节点侧
        // resolve_path 会在 base_dir 空时直接返回原值，零回归）。
        config.params.set_string(kSourceDirParam, base_dir);

        bool is_plugin_task = PluginRegistry::instance().has_task(type);
        if (is_plugin_task) {
            dag.add_plugin_task(id, type, config);
        } else {
            auto task = std::make_shared<Task>(
                id, type,
                [id](TaskContext&) {
                    (void)id;
                    return TaskResult{.status = TaskStatus::COMPLETED};
                },
                config
            );
            dag.add_task(task);
        }
    }

    // ---- edges：v1.0/v2.0 格式不同 ----
    if (j.contains("edges")) {
        if (version == "1.0") {
            // v1.0 迁移：默认端口 "out"/"in"。同一 (to, "in") 多源会被 connect()
            // 打 warning（last-write-wins），用户应升级到 v2.0 用命名端口。
            for (const auto& edge_json : j["edges"]) {
                std::string from = edge_json["from"].get<std::string>();
                std::string to   = edge_json["to"].get<std::string>();
                dag.connect(from, "out", to, "in");
            }
        } else if (version == "2.0") {
            for (const auto& edge_json : j["edges"]) {
                std::string from      = edge_json["from"].get<std::string>();
                std::string to        = edge_json["to"].get<std::string>();
                std::string from_port = edge_json.contains("from_port")
                                        ? edge_json["from_port"].get<std::string>() : "out";
                std::string to_port   = edge_json.contains("to_port")
                                        ? edge_json["to_port"].get<std::string>()   : "in";
                dag.connect(from, from_port, to, to_port);
            }
        } else {
            throw std::runtime_error("Unsupported DAG version: " + version);
        }
    }

    return dag;
}

std::string DAGSerializer::to_string(const DAG& dag, int indent) {
    return serialize(dag).dump(indent);
}

DAG DAGSerializer::from_string(const std::string& s, const std::string& base_dir) {
    return deserialize(nlohmann::json::parse(s), base_dir);
}

// ====================== 带元数据的序列化 ======================
nlohmann::json DAGSerializer::serialize(const DAG& dag, const nlohmann::json& metadata) {
    nlohmann::json j = serialize(dag);
    if (!metadata.empty()) {
        j["metadata"] = metadata;
    }
    return j;
}

std::string DAGSerializer::to_string(const DAG& dag, const nlohmann::json& metadata, int indent) {
    return serialize(dag, metadata).dump(indent);
}

DAGSerializer::DeserializeResult DAGSerializer::deserialize_with_metadata(
        const nlohmann::json& j, const std::string& base_dir) {
    DeserializeResult result;
    result.dag = deserialize(j, base_dir);

    // 优先读 "metadata" 键
    if (j.contains("metadata") && j["metadata"].is_object()) {
        result.metadata = j["metadata"];
    }

    // 兼容旧格式：收集非标准 top-level keys（如 graph_studio 旧的 "positions"）
    static const std::set<std::string> known_keys = {"version", "tasks", "edges", "metadata"};
    for (const auto& [key, value] : j.get<nlohmann::json::object_t>()) {
        if (!known_keys.contains(key)) {
            result.metadata[key] = value;
        }
    }
    return result;
}

DAGSerializer::DeserializeResult DAGSerializer::from_string_with_metadata(
        const std::string& s, const std::string& base_dir) {
    return deserialize_with_metadata(nlohmann::json::parse(s), base_dir);
}

}
