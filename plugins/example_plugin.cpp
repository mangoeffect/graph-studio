#include <task_graph/plugin.hpp>
#include <task_graph/task.hpp>
#include <task_graph/context.hpp>
#include <string>
#include <memory>
#include <vector>
#include <any>

namespace task_graph {

class ExamplePluginTask : public IPluginTask {
public:
    ExamplePluginTask(std::string id, int value, const TaskConfig& config = TaskConfig()) 
        : id_(std::move(id)), value_(value), config_(config) {}

    const std::string& id() const override { return id_; }
    
    const std::string& type() const override { 
        static std::string type = "example_plugin_task";
        return type;
    }
    
    TaskResult execute(IExecutionContext& ctx) override {
        std::string key = id_ + "_output";
        ctx.set_value(key, value_ * 2);
        return TaskResult{.status = TaskStatus::COMPLETED, .value = value_};
    }
    
    const TaskConfig& config() const override {
        return config_;
    }

    CheckResult check_input(const std::vector<std::any>& inputs) const override {
        return CheckResult(true);
    }

private:
    std::string id_;
    int value_;
    TaskConfig config_;
};

class DataProcessorTask : public IPluginTask {
public:
    DataProcessorTask(const TaskConfig& config = TaskConfig()) : id_("data_processor"), config_(config) {}
    
    const std::string& id() const override { return id_; }
    
    const std::string& type() const override { 
        static std::string type = "data_processor";
        return type;
    }
    
    TaskResult execute(IExecutionContext& ctx) override {
        auto input = ctx.get<int>("input_data");
        if (input) {
            int result = *input * 10;
            ctx.set_value("processed_data", result);
            return TaskResult{.status = TaskStatus::COMPLETED, .value = result};
        }
        return TaskResult{.status = TaskStatus::FAILED};
    }
    
    const TaskConfig& config() const override {
        return config_;
    }

    CheckResult check_input(const std::vector<std::any>& inputs) const override {
        return CheckResult(true);
    }

private:
    std::string id_;
    TaskConfig config_;
};

static PluginInfo plugin_info = {
    .name = "example_plugin",
    .version = "1.0.0",
    .description = "Example plugin for task_graph"
};

extern "C" TG_EXPORT bool register_plugin() {
    PluginRegistry::instance().register_task(
        "example_task_1",
        [](const TaskConfig& config) { return std::make_shared<ExamplePluginTask>("example_task_1", 10, config); }
    );
    
    PluginRegistry::instance().register_task(
        "example_task_2",
        [](const TaskConfig& config) { return std::make_shared<ExamplePluginTask>("example_task_2", 20, config); }
    );
    
    PluginRegistry::instance().register_task(
        "data_processor",
        [](const TaskConfig& config) { return std::make_shared<DataProcessorTask>(config); }
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