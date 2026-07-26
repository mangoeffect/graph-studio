#include "model/GraphModel.h"
#include <task_graph/dag.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/task.hpp>
#include <task_graph/task_context.hpp>
#include <stdexcept>

using namespace graph_studio;

GraphModel::GraphModel()
    : dag_(std::make_unique<task_graph::DAG>())
{
}

GraphModel::~GraphModel() = default;

bool GraphModel::add_task(const std::string& task_id, const std::string& task_type, const task_graph::TaskConfig& config)
{
    try {
        // Prefer registered plugin task (real implementation available)
        dag_->add_plugin_task(task_id, task_type, config);
        return true;
    } catch (const std::exception&) {
        // Fallback: plugin not registered, create a placeholder task so the
        // editor can still model the graph. Execution will require the plugin
        // to be registered at runtime.
        try {
            auto placeholder = std::make_shared<task_graph::Task>(
                task_id, task_type,
                [](task_graph::TaskContext&) -> task_graph::TaskResult {
                    return {};
                },
                config);
            dag_->add_task(task_id, placeholder);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
}

bool GraphModel::add_edge(const std::string& from_id, const std::string& to_id)
{
    try {
        dag_->add_dependency(from_id, to_id);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void GraphModel::clear()
{
    dag_ = std::make_unique<task_graph::DAG>();
}

void GraphModel::rebuild(const std::vector<std::pair<std::string, std::string>>& tasks,
                         const std::vector<std::pair<std::string, std::string>>& edges)
{
    dag_ = std::make_unique<task_graph::DAG>();
    for (const auto& [id, type] : tasks) {
        add_task(id, type);
    }
    for (const auto& [from, to] : edges) {
        dag_->add_dependency(from, to);
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

bool GraphModel::has_task(const std::string& id) const
{
    return dag_->has_task(id);
}

bool GraphModel::has_edge(const std::string& from_id, const std::string& to_id) const
{
    auto it = dag_->adjacency().find(from_id);
    if (it == dag_->adjacency().end())
        return false;
    return it->second.find(to_id) != it->second.end();
}

std::string GraphModel::to_json_string() const
{
    try {
        return task_graph::DAGSerializer::to_string(*dag_);
    } catch (const std::exception&) {
        return {};
    }
}

bool GraphModel::from_json_string(const std::string& json)
{
    try {
        dag_ = std::make_unique<task_graph::DAG>(task_graph::DAGSerializer::from_string(json));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
