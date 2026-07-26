#ifndef GRAPH_MODEL_H
#define GRAPH_MODEL_H

#include <memory>
#include <string>
#include <task_graph/task.hpp>
#include <task_graph/dag.hpp>

namespace graph_studio {

class GraphModel {
public:
    GraphModel();
    ~GraphModel();
    
    bool add_task(const std::string& task_id, const std::string& task_type, const task_graph::TaskConfig& config = {});
    bool add_edge(const std::string& from_id, const std::string& to_id);
    
    size_t task_count() const;
    size_t edge_count() const;
    
private:
    std::unique_ptr<task_graph::DAG> dag_;
};

} // namespace graph_studio

#endif // GRAPH_MODEL_H