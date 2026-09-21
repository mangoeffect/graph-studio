// 节点手册真值导出工具：dlopen build/submodules 下全部插件 dylib 后，
// 经 PluginRegistry 枚举每个 task type 的 param_specs/input_specs/output_specs，
// 输出 JSON 供 scripts/node_doc_gen/generate.py 生成官网节点手册页面
// （docs/research/node-doc-links-design.md）。
//
// 用法（macOS 桌面构建树）：
//   clang++ -std=c++20 -I<repo>/include scripts/node_doc_gen/probe_dump.cpp \
//     -L<repo>/build -ltask_graph -Wl,-rpath,<repo>/build -o /tmp/tg_node_probe
//   /tmp/tg_node_probe <repo>/build scripts/node_doc_gen/task_specs.json
//
// 重新生成文档前先增量重建 build/，避免导出过期的参数表。

#include <plugin_api.hpp>
#include <task_graph/data_types.hpp>

#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using task_graph::INode;
using task_graph::PluginRegistry;

static std::string jesc(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

static std::string typeName(const task_graph::ParamType& t)
{
    switch (t) {
    case task_graph::ParamType::Int:    return "int";
    case task_graph::ParamType::Float:  return "float";
    case task_graph::ParamType::String: return "string";
    case task_graph::ParamType::Bool:   return "bool";
    case task_graph::ParamType::Enum:   return "enum";
    }
    return "unknown";
}

static std::string defaultValue(const task_graph::ParamValue& v)
{
    // variant 序列：int, float, std::string, bool（data_types.hpp）
    if (auto* p = std::get_if<int>(&v))         return std::to_string(*p);
    if (auto* p = std::get_if<float>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(*p));
        return buf;
    }
    if (auto* p = std::get_if<std::string>(&v)) return "\"" + jesc(*p) + "\"";
    if (auto* p = std::get_if<bool>(&v))        return *p ? "true" : "false";
    return "null";
}

static void writeParam(std::ofstream& out, const task_graph::ParamSpec& s, bool first)
{
    out << (first ? "" : ",") << "\n    {\"name\":\"" << jesc(s.name)
        << "\",\"type\":\"" << typeName(s.type)
        << "\",\"description\":\"" << jesc(s.description) << "\""
        << ",\"default\":" << defaultValue(s.default_value);
    if (s.min_value) out << ",\"min\":" << *s.min_value;
    if (s.max_value) out << ",\"max\":" << *s.max_value;
    if (s.step)      out << ",\"step\":" << *s.step;
    if (!s.enum_values.empty()) {
        out << ",\"enum_values\":[";
        for (size_t i = 0; i < s.enum_values.size(); ++i) {
            out << (i ? "," : "") << "{\"label\":\"" << jesc(s.enum_values[i].first)
                << "\",\"value\":" << s.enum_values[i].second << "}";
        }
        out << "]";
    }
    if (!s.widget_hint.empty())   out << ",\"widget\":\"" << jesc(s.widget_hint) << "\"";
    if (!s.file_filter.empty())   out << ",\"file_filter\":\"" << jesc(s.file_filter) << "\"";
    if (s.required)               out << ",\"required\":true";
    if (s.hidden)                 out << ",\"hidden\":true";
    if (!s.visible_when.empty()) {
        out << ",\"visible_when\":\"" << jesc(s.visible_when) << "\",\"visible_when_values\":[";
        for (size_t i = 0; i < s.visible_when_values.size(); ++i)
            out << (i ? "," : "") << s.visible_when_values[i];
        out << "]";
        if (s.reset_on_visible_when_change) out << ",\"reset_on_change\":true";
    }
    out << "}";
}

static void writePorts(std::ofstream& out, const std::vector<task_graph::PortSpec>& ports)
{
    for (size_t i = 0; i < ports.size(); ++i) {
        out << (i ? "," : "") << "\n    {\"name\":\"" << jesc(ports[i].name)
            << "\",\"type_name\":\"" << jesc(ports[i].type_name)
            << "\",\"required\":" << (ports[i].required ? "true" : "false") << "}";
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <build_dir> <out_json>\n";
        return 2;
    }
    const fs::path subdirs = fs::path(argv[1]) / "submodules";
    if (!fs::exists(subdirs)) {
        std::cerr << "no build/submodules under " << argv[1] << "\n";
        return 2;
    }
    for (const auto& entry : fs::directory_iterator(subdirs)) {
        if (!entry.is_directory()) continue;
        for (const auto& f : fs::directory_iterator(entry.path())) {
            if (f.path().extension() != ".dylib") continue;
            if (!dlopen(f.path().c_str(), RTLD_NOW | RTLD_LOCAL)) {
                std::cerr << "dlopen failed: " << f.path() << " : " << dlerror() << "\n";
            }
        }
    }

    auto types = PluginRegistry::instance().available_tasks();
    std::sort(types.begin(), types.end());

    std::ofstream out(argv[2]);
    out << "[";
    bool firstTask = true;
    for (const auto& type : types) {
        auto node = PluginRegistry::instance().create_task("probe", type, {});
        if (!node) {
            std::cerr << "create_task failed: " << type << "\n";
            continue;
        }
        out << (firstTask ? "" : ",") << "\n{\"type\":\"" << type << "\"";
        out << ",\"params\":[";
        bool firstParam = true;
        for (const auto& s : node->param_specs()) {
            writeParam(out, s, firstParam);
            firstParam = false;
        }
        out << "]";
        out << ",\"inputs\":[";
        writePorts(out, node->input_specs());
        out << "]";
        out << ",\"outputs\":[";
        writePorts(out, node->output_specs());
        out << "]}";
        firstTask = false;
    }
    out << "\n]\n";
    std::cout << "exported " << types.size() << " task types -> " << argv[2] << "\n";
    return 0;
}
