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

bool GraphModel::add_edge(const std::string& from_id, const std::string& from_port,
                          const std::string& to_id, const std::string& to_port)
{
    try {
        dag_->connect(from_id, from_port, to_id, to_port);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool GraphModel::add_edge(const std::string& from_id, const std::string& to_id)
{
    return add_edge(from_id, "out", to_id, "in");
}

void GraphModel::clear()
{
    dag_->clear();
}

void GraphModel::rebuild(const std::vector<NodeSpec>& tasks,
                         const std::vector<std::pair<std::string, std::string>>& edges)
{
    dag_->clear();
    for (const auto& t : tasks) {
        add_task(t.id, t.type, t.config);
    }
    for (const auto& [from, to] : edges) {
        dag_->add_dependency(from, to);
    }
}

void GraphModel::rebuild(const std::vector<std::pair<std::string, std::string>>& tasks,
                         const std::vector<std::pair<std::string, std::string>>& edges)
{
    std::vector<NodeSpec> specs;
    specs.reserve(tasks.size());
    for (const auto& [id, type] : tasks) specs.push_back({id, type, {}});
    rebuild(specs, edges);
}

bool GraphModel::update_task_params(const std::string& task_id, const task_graph::TaskParams& params)
{
    try {
        dag_->update_task_params(task_id, params);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

task_graph::TaskParams GraphModel::task_params(const std::string& task_id) const
{
    auto task = dag_->get_task(task_id);
    if (!task) return {};
    return task->config().params;
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
    return dag_->has_edge(from_id, to_id);
}

bool GraphModel::remove_task(const std::string& task_id)
{
    try {
        dag_->remove_task(task_id);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool GraphModel::remove_edge(const std::string& from_id, const std::string& to_id)
{
    try {
        dag_->remove_edge(from_id, to_id);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string GraphModel::to_json_string() const
{
    try {
        return task_graph::DAGSerializer::to_string(*dag_);
    } catch (const std::exception&) {
        return {};
    }
}

std::string GraphModel::to_json_string(const std::string& metadata_json) const
{
    try {
        auto meta = nlohmann::json::parse(metadata_json);
        return task_graph::DAGSerializer::to_string(*dag_, meta);
    } catch (const std::exception&) {
        return {};
    }
}

bool GraphModel::from_json_string(const std::string& json)
{
    try {
        auto new_dag = task_graph::DAGSerializer::from_string(json);
        dag_->reset_from(std::move(new_dag));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string GraphModel::from_json_string_with_metadata(const std::string& json)
{
    try {
        auto result = task_graph::DAGSerializer::from_string_with_metadata(json);
        std::string meta = result.metadata.dump();
        dag_->reset_from(std::move(result.dag));
        return meta;
    } catch (const std::exception&) {
        return {};
    }
}
