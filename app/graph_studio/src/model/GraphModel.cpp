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

void GraphModel::rebuild(const std::vector<NodeSpec>& tasks,
                         const std::vector<std::pair<std::string, std::string>>& edges)
{
    dag_ = std::make_unique<task_graph::DAG>();
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
    auto task = dag_->get_task(task_id);
    if (!task) return false;
    task_graph::TaskConfig new_config = task->config();
    new_config.params = params;
    try {
        // 重新创建并替换实例，使新 config 生效（DAG::replace_task）
        if (!task->type().empty() && task->type() != task_id) {
            // plugin task：通过 type 重建以保留 spec delegate 与 execute 行为
            auto wrapper = std::make_shared<task_graph::Task>(
                task_id, task->type(),
                [plugin_ptr = std::shared_ptr<task_graph::IPluginTask>(
                     task_graph::PluginRegistry::instance().create_task(task_id, task->type(), new_config))]
                    (task_graph::TaskContext& ctx) { return plugin_ptr->execute(ctx); },
                new_config);
            wrapper->set_spec_delegate(task_graph::PluginRegistry::instance().create_task(task_id, task->type(), new_config));
            dag_->replace_task(task_id, wrapper);
        } else {
            // 普通 lambda Task：保留原 lambda 不可能（封装在闭包里），用一个空实现替换
            auto placeholder = std::make_shared<task_graph::Task>(
                task_id, task_id,
                [](task_graph::TaskContext&) -> task_graph::TaskResult { return {}; },
                new_config);
            dag_->replace_task(task_id, placeholder);
        }
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
