#include <task_graph/plugin.hpp>
#include <task_graph/task.hpp>
#include <task_graph/context.hpp>
#include <task_graph/task_context.hpp>
#include <string>
#include <memory>
#include <vector>
#include <any>

namespace task_graph {

class ExamplePluginTask : public IPluginTask {
public:
    using IPluginTask::IPluginTask;

    const std::string& type() const override {
        static std::string type = "example_plugin_task";
        return type;
    }

    TaskResult execute(TaskContext& ctx) override {
        (void)ctx;
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 42};
    }
};

// DataProcessorTask: 声明一个 int 输入端口 "in"，输出 *10 后从默认 "out" 端口返回。
// 跨 task 数据通过 TaskResult.value 流转；不再用 set_value 误导跨 task 黑板。
class DataProcessorTask : public IPluginTask {
public:
    using IPluginTask::IPluginTask;

    const std::string& type() const override {
        static std::string type = "data_processor";
        return type;
    }

    std::vector<PortSpec> input_specs() const override {
        return {make_port<int>("in")};
    }

    TaskResult execute(TaskContext& ctx) override {
        auto input = ctx.input<int>("in");
        if (input) {
            int result = *input * 10;
            return TaskResult{.status = TaskStatus::COMPLETED, .value = result};
        }
        return TaskResult{.status = TaskStatus::FAILED};
    }
};

static PluginInfo plugin_info = {
    .name = "example_plugin",
    .version = "1.0.0",
    .description = "Example plugin for task_graph"
};

extern "C" TG_EXPORT bool register_plugin() {
    PluginRegistry::instance().register_task(
        "example_task_1",
        [](const std::string& id, const TaskConfig& config) { return std::make_shared<ExamplePluginTask>(id, config); }
    );

    PluginRegistry::instance().register_task(
        "example_task_2",
        [](const std::string& id, const TaskConfig& config) { return std::make_shared<ExamplePluginTask>(id, config); }
    );

    PluginRegistry::instance().register_task(
        "data_processor",
        [](const std::string& id, const TaskConfig& config) { return std::make_shared<DataProcessorTask>(id, config); }
    );

    return true;
}

extern "C" TG_EXPORT void unregister_plugin() {
    PluginRegistry::instance().unregister_task("example_task_1");
    PluginRegistry::instance().unregister_task("example_task_2");
    PluginRegistry::instance().unregister_task("data_processor");
}

extern "C" TG_EXPORT const PluginInfo* get_plugin_info() {
    return &plugin_info;
}

}
