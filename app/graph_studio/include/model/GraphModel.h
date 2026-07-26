#ifndef GRAPH_MODEL_H
#define GRAPH_MODEL_H

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <task_graph/task.hpp>
#include <task_graph/dag.hpp>

namespace graph_studio {

class GraphModel {
public:
    GraphModel();
    ~GraphModel();

    // Incremental building (forwarded to DAG)
    bool add_task(const std::string& task_id, const std::string& task_type, const task_graph::TaskConfig& config = {});
    bool add_edge(const std::string& from_id, const std::string& to_id);

    // DAG is immutable for deletion, so rebuild from scratch
    void clear();
    void rebuild(const std::vector<std::pair<std::string, std::string>>& tasks,
                 const std::vector<std::pair<std::string, std::string>>& edges);

    // Queries
    size_t task_count() const;
    size_t edge_count() const;
    bool has_task(const std::string& id) const;
    bool has_edge(const std::string& from_id, const std::string& to_id) const;

    // Serialization helpers (via DAGSerializer)
    std::string to_json_string() const;
    bool from_json_string(const std::string& json);

    const task_graph::DAG& dag() const { return *dag_; }

private:
    std::unique_ptr<task_graph::DAG> dag_;
};

} // namespace graph_studio

#endif // GRAPH_MODEL_H
