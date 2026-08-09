// demo_plugin：task_graph SDK 独立编译演示插件。
//
// 与主仓库完全解耦：仅依赖已安装的 SDK 头文件与 libtask_graph 动态库，
// 不引用主仓库任何源码。构建方式见 scripts/build_plugin_standalone.sh，
// 运行时由 PluginLoader::load() dlopen，注册的任务类型为 "demo_add"。
//
// 遵循与内置子模块一致的插件最佳实践：const char* const 类型名常量、
// type() 返回静态 std::string、constructor 自动注册 + extern "C" register_plugin
// 双通道、TG_DEFINE_PLUGIN_SDK_VERSION 导出 SDK 版本。
#include <plugin_api.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_context.hpp>

namespace {

const char* const kDemoAddType = "demo_add";

task_graph::PluginInfo g_plugin_info{"demo_plugin", "1.0.0",
    "Standalone demo plugin: sums two int inputs (a + b)"};

class DemoAddTask : public task_graph::INode {
public:
    using INode::INode;

    const std::string& type() const override {
        static const std::string t(kDemoAddType);
        return t;
    }

    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override {
        auto a = ctx.template input<int>("a");
        auto b = ctx.template input<int>("b");
        int sum = (a ? *a : 0) + (b ? *b : 0);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED,
                                      .value = sum};
    }

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<int>("a"), task_graph::make_port<int>("b")};
    }
    std::vector<task_graph::PortSpec> output_specs() const override {
        return {task_graph::make_port<int>("out")};
    }
};

bool do_register() {
    task_graph::PluginRegistry::instance().register_task(
        kDemoAddType,
        [](const std::string& id, const task_graph::TaskConfig& config) {
            return std::make_shared<DemoAddTask>(id, config);
        });
    return true;
}

void do_unregister() {
    task_graph::PluginRegistry::instance().unregister_task(kDemoAddType);
}

TG_PLUGIN_AUTOREG(do_register, do_unregister);

}  // namespace

extern "C" TG_EXPORT bool register_plugin() {
    return do_register();
}

extern "C" TG_EXPORT void unregister_plugin() {
    do_unregister();
}

extern "C" TG_EXPORT const task_graph::PluginInfo* get_plugin_info() {
    return &g_plugin_info;
}

TG_DEFINE_PLUGIN_SDK_VERSION;