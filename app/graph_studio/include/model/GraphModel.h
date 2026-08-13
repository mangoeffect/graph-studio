#ifndef GRAPH_MODEL_H
#define GRAPH_MODEL_H

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <task_graph_api.hpp>

namespace graph_studio {

class GraphModel {
public:
    GraphModel();
    ~GraphModel();

    // Incremental building (forwarded to DAG)
    bool add_task(const std::string& task_id, const std::string& task_type, const task_graph::TaskConfig& config = {});
    // 端口化连线：from_port 输出 -> to 的 to_port 输入。
    bool add_edge(const std::string& from_id, const std::string& from_port,
                  const std::string& to_id, const std::string& to_port = "in");
    // 便捷重载（默认 out -> in 端口，仅用于无端口诉求的旧调用方）
    bool add_edge(const std::string& from_id, const std::string& to_id);
    bool remove_task(const std::string& task_id);
    bool remove_edge(const std::string& from_id, const std::string& to_id);
    // 端口级删除/查询（多端口边支持）
    bool remove_edge(const std::string& from_id, const std::string& from_port,
                     const std::string& to_id, const std::string& to_port);
    bool has_edge(const std::string& from_id, const std::string& from_port,
                  const std::string& to_id, const std::string& to_port) const;
    // to_id 的 to_port 输入口是否已被任何上游连接
    bool input_port_filled(const std::string& to_id, const std::string& to_port) const;
    // Update params of an existing task in-place (delegates to DAG::update_task_params).
    bool update_task_params(const std::string& task_id, const task_graph::TaskParams& params);
    task_graph::TaskParams task_params(const std::string& task_id) const;

    // 节点结构：rebuild 时携带 id/type/config（含 params），避免删除重建丢参数
    struct NodeSpec {
        std::string id;
        std::string type;
        task_graph::TaskConfig config;
    };

    // DAG is immutable for deletion, so rebuild from scratch
    void clear();
    void rebuild(const std::vector<NodeSpec>& tasks,
                 const std::vector<std::pair<std::string, std::string>>& edges);
    // 旧签名（不带 config）保留为 wrapper，便于过渡
    void rebuild(const std::vector<std::pair<std::string, std::string>>& tasks,
                 const std::vector<std::pair<std::string, std::string>>& edges);

    // Queries
    size_t task_count() const;
    size_t edge_count() const;
    bool has_task(const std::string& id) const;
    bool has_edge(const std::string& from_id, const std::string& to_id) const;

    // Serialization helpers (via DAGSerializer)
    std::string to_json_string() const;
    std::string to_json_string(const std::string& metadata_json) const;
    // base_dir = graph.json 所在目录的绝对路径，会被注入到每个 task 的
    // _source_dir 参数（详见 <task_graph/path_utils.hpp>），用于运行时把
    // 相对路径解析为绝对路径。默认空串=不启用相对路径解析（保持旧行为）。
    bool from_json_string(const std::string& json, const std::string& base_dir = "");
    std::string from_json_string_with_metadata(const std::string& json,
                                               const std::string& base_dir = "");

    const task_graph::DAG& dag() const { return *dag_; }

private:
    std::unique_ptr<task_graph::DAG> dag_;
};

} // namespace graph_studio

#endif // GRAPH_MODEL_H
