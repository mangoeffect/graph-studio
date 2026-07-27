#include <task_graph/task.hpp>
#include <task_graph/context.hpp>
#include <plugin_api.hpp>
#include <sstream>
#include <any>
#include <typeindex>

namespace task_graph {

Task::Task(std::string id, TaskFunction func, TaskConfig config)
    : IPluginTask(std::move(id), std::move(config)), func_(std::move(func)) {
    type_ = this->id();
}

Task::Task(std::string id, std::string type, TaskFunction func, TaskConfig config)
    : IPluginTask(std::move(id), std::move(config)), type_(std::move(type)), func_(std::move(func)) {}

std::vector<PortSpec> Task::input_specs() const {
    return spec_delegate_ ? spec_delegate_->input_specs() : IPluginTask::input_specs();
}

std::vector<PortSpec> Task::output_specs() const {
    return spec_delegate_ ? spec_delegate_->output_specs() : IPluginTask::output_specs();
}

// IPluginTask::check_input(map) 默认实现：
//  1) 每个声明的 required input port 必须存在
//  2) 已注册的类型名必须与实际 any 中的类型名匹配
// 子类通常无需重写，只需正确实现 input_specs()。
CheckResult IPluginTask::check_input(
    const std::unordered_map<std::string, std::any>& inputs_by_port) const {
    for (const auto& spec : input_specs()) {
        auto it = inputs_by_port.find(spec.name);
        if (it == inputs_by_port.end()) {
            if (spec.required) {
                return {false, "missing required input port '" + spec.name + "'"};
            }
            continue;
        }
        // 类型名校验：仅当 spec.type_name 非空（即 T 已通过 TG_REGISTER_TYPE 注册）
        // 且实际类型也已注册时才严格比对。未注册类型视为"不校验类型"。
        if (!spec.type_name.empty()) {
            const std::string actual = detail::TypeRegistry::instance().name_of(
                std::type_index(it->second.type()));
            if (!actual.empty() && actual != spec.type_name) {
                return {false, "port '" + spec.name + "': expected " + spec.type_name
                               + ", got " + actual};
            }
        }
    }
    return CheckResult(true);
}

TaskResult Task::execute(TaskContext& ctx) {
    TaskResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    TG_LOG_INFO("Starting execution of task '" + id() + "'");
    TG_LOG_DEBUG("Task '" + id() + "' priority: " + std::to_string(static_cast<int>(config().priority)) +
                 ", max_retries: " + std::to_string(config().max_retries) +
                 ", timeout: " + std::to_string(config().timeout.count()) + "ms");

    try {
        result = func_(ctx);
    } catch (const std::exception& e) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id() + "' failed with exception: " + e.what());
    } catch (...) {
        result.status = TaskStatus::FAILED;
        result.exception = std::current_exception();
        TG_LOG_ERROR("Task '" + id() + "' failed with unknown exception");
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.duration = end_time - start_time;

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(result.duration).count();
    std::stringstream ss;
    ss << "Task '" << id() << "' completed with status ";
    
    switch (result.status) {
        case TaskStatus::COMPLETED:
            ss << "COMPLETED, duration: " << duration_ms << "ms";
            TG_LOG_INFO(ss.str());
            break;
        case TaskStatus::FAILED:
            ss << "FAILED, duration: " << duration_ms << "ms";
            TG_LOG_ERROR(ss.str());
            break;
        case TaskStatus::SKIPPED:
            ss << "SKIPPED";
            TG_LOG_INFO(ss.str());
            break;
        default:
            ss << "UNKNOWN";
            TG_LOG_WARN(ss.str());
            break;
    }

    return result;
}

}