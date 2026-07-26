#include "model/GraphModel.h"
#include <task_graph/dag.hpp>
#include <task_graph/task.hpp>

using namespace graph_studio;

GraphModel::GraphModel()
    : dag_(std::make_unique<task_graph::DAG>())
{
}

GraphModel::~GraphModel() = default;

bool GraphModel::add_task(const std::string& task_id, const std::string& task_type, const task_graph::TaskConfig& config)
{
    try {
        dag_->add_plugin_task(task_id, task_type, config);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool GraphModel::add_edge(const std::string& from_id, const std::string& to_id)
{
    try {
        dag_->add_dependency(from_id, to_id);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

size_t GraphModel::task_count() const
{
    return dag_->num_tasks();
}

size_t GraphModel::edge_count() const
{
    return dag_->num_edges();
}