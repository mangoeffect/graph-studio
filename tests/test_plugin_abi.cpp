// 插件 ABI + 运行时动态加载测试。
//
// 覆盖：宿主框架导出的 SDK 版本、加载独立编译的插件动态库（scripts/build_sdk.py
// + scripts/build_plugin_standalone.py 产物）、register_plugin 注册到 PluginRegistry、
// DAGExecutor 真实运行插件任务。
//
// 插件路径优先取环境变量 TASK_GRAPH_DEMO_PLUGIN，否则用编译期默认
// <build>/standalone/plugins/demo/demo_plugin.{dylib,so,dll}。
// 文件缺失时 soft-SKIP（与 mediapipe 测试一致的处理），不视为失败。
#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/dag_serializer.hpp>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <gtest/gtest.h>

using namespace task_graph;

#ifndef TASK_GRAPH_DEMO_PLUGIN
#define TASK_GRAPH_DEMO_PLUGIN ""
#endif

static bool file_exists(const char* path) {
    if (!path || !*path) return false;
    struct stat st{};
    return stat(path, &st) == 0;
}

static std::string demo_plugin_path() {
    const char* env = std::getenv("TASK_GRAPH_DEMO_PLUGIN");
    if (env && *env) return env;
    return TASK_GRAPH_DEMO_PLUGIN;
}

TEST(PluginAbi, host_sdk_version_exported) {
    EXPECT_EQ(tg_sdk_version(), TG_SDK_VERSION);
}

TEST(PluginAbi, standalone_plugin_load_and_run) {
    const std::string path = demo_plugin_path();
    if (!file_exists(path.c_str())) {
        GTEST_SKIP() << "(soft-skip) 未找到独立插件: \"" << path << "\"\n"
                     << "运行: scripts/build_sdk.py\n"
                     << "&& scripts/build_plugin_standalone.py examples/plugins/demo";
    }

    PluginLoader loader;
    const bool loaded = loader.load(path);
    EXPECT_TRUE(loaded);
    if (!loaded) return;

auto& registry = PluginRegistry::instance();
    EXPECT_TRUE(registry.has_task("demo_add"));
    if (!registry.has_task("demo_add")) {
        loader.unload_all();
        return;
    }

    DAG dag;
    auto a = std::make_shared<Task>("a", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 2};
    });
    auto b = std::make_shared<Task>("b", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 3};
    });
    dag.add_task(a);
    dag.add_task(b);
    dag.add_plugin_task("add_node", "demo_add");
    dag.connect("a", "out", "add_node", "a");
    dag.connect("b", "out", "add_node", "b");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    auto it = results.find("add_node");
    EXPECT_TRUE(it != results.end());
    if (it != results.end()) {
        EXPECT_TRUE(it->second.is_success());
        if (it->second.is_success()) {
            EXPECT_EQ(std::any_cast<int>(it->second.value), 5);
        }
    }

    // 先释放持有的插件任务实例（dag 内 add_node），再卸载 DLL：
    // 否则 FreeLibrary 后析构插件类（虚表/析构函数位于已卸载的库内）会访问违例。
    dag = DAG{};

    loader.unload_all();
    EXPECT_FALSE(registry.has_task("demo_add"));
}

