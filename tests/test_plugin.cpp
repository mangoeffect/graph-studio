// 插件系统测试。合并自 test_plugin.cpp、test_subnode.cpp、test_task_type_id.cpp。
// 覆盖：registry 注册/注销、动态库 loader、唯一 ID 生成、TaskManager 生命周期、
//       同类型多实例、type/id 分离、subnode 内置插件可用性与 DAG 执行。
#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_manager.hpp>
#include <task_graph/task_context.hpp>
#include <task_graph/dag_serializer.hpp>
#include <filesystem>
#include <set>
#include <algorithm>
#include <string>
#include "test_util.hpp"

using namespace task_graph;

class TestPluginTask : public IPluginTask {
public:
    using IPluginTask::IPluginTask;
    const std::string& type() const override {
        static const std::string t = "test_plugin_type";
        return t;
    }
    TaskResult execute(TaskContext&) override {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }
};

static std::filesystem::path find_example_plugin() {
    auto p = std::filesystem::current_path() / "task_plugin_example.dylib";
    if (!std::filesystem::exists(p))
        p = std::filesystem::current_path() / "task_plugin_example.so";
    return p;
}

// ============================================================
// registry / 唯一 ID / TaskManager
// ============================================================

TEST_CASE(registry_register_unregister) {
    auto& registry = PluginRegistry::instance();
    registry.register_task("test_task", [](const std::string& id, const TaskConfig& cfg) {
        return std::make_shared<TestPluginTask>(id, cfg);
    });
    EXPECT_TRUE(registry.has_task("test_task"));
    registry.unregister_task("test_task");
    EXPECT_FALSE(registry.has_task("test_task"));
}

TEST_CASE(unique_task_id_same_type) {
    auto& registry = PluginRegistry::instance();
    registry.register_task("test_plugin_type", [](const std::string& id, const TaskConfig& cfg) {
        return std::make_shared<TestPluginTask>(id, cfg);
    });

    auto t1 = registry.create_task("test_plugin_type");
    auto t2 = registry.create_task("test_plugin_type");
    auto t3 = registry.create_task("test_plugin_type");

    EXPECT_TRUE(t1->id() != t2->id());
    EXPECT_TRUE(t2->id() != t3->id());
    EXPECT_TRUE(t1->id() != t3->id());
    EXPECT_TRUE(t1->type() == "test_plugin_type");
    EXPECT_TRUE(t3->type() == "test_plugin_type");

    registry.unregister_task("test_plugin_type");
}

TEST_CASE(task_manager_lifecycle) {
    TaskManager manager;
    auto t1 = std::make_shared<TestPluginTask>("test1");
    auto t2 = std::make_shared<TestPluginTask>("test2");

    manager.add_task(t1);
    manager.add_task("custom_id", t2);

    EXPECT_TRUE(manager.has_task(t1->id()));
    EXPECT_TRUE(manager.has_task("custom_id"));
    EXPECT_EQ(manager.size(), size_t(2));
    EXPECT_TRUE(manager.get_task(t1->id()) == t1);
    EXPECT_TRUE(manager.get_task("custom_id") == t2);

    manager.remove_task(t1->id());
    EXPECT_FALSE(manager.has_task(t1->id()));
    EXPECT_EQ(manager.size(), size_t(1));

    manager.clear_all();
    EXPECT_EQ(manager.size(), size_t(0));
}

TEST_CASE(dag_multiple_same_type) {
    auto& registry = PluginRegistry::instance();
    registry.register_task("test_plugin_type", [](const std::string& id, const TaskConfig& cfg) {
        return std::make_shared<TestPluginTask>(id, cfg);
    });

    DAG dag;
    dag.add_plugin_task("test_plugin_type");
    dag.add_plugin_task("test_plugin_type");
    dag.add_plugin_task("test_plugin_type");

    EXPECT_EQ(dag.num_tasks(), size_t(3));
    std::set<std::string> ids, types;
    for (const auto& [id, task] : dag.tasks()) {
        ids.insert(id);
        types.insert(task->type());
    }
    EXPECT_EQ(ids.size(), size_t(3));       // id 唯一
    EXPECT_EQ(types.size(), size_t(1));     // type 相同

    registry.unregister_task("test_plugin_type");
}

TEST_CASE(json_type_id_separation) {
    auto& registry = PluginRegistry::instance();
    registry.register_task("test_plugin_type", [](const std::string& id, const TaskConfig& cfg) {
        return std::make_shared<TestPluginTask>(id, cfg);
    });

    std::string json = R"({
        "version": "1.0",
        "tasks": [
            {"id": "f1", "type": "test_plugin_type"},
            {"id": "f2", "type": "test_plugin_type"}
        ],
        "edges": [{"from": "f1", "to": "f2"}]
    })";
    DAG dag = DAGSerializer::from_string(json);

    EXPECT_TRUE(dag.has_task("f1"));
    EXPECT_TRUE(dag.has_task("f2"));
    EXPECT_TRUE(dag.get_task("f1")->type() == "test_plugin_type");
    EXPECT_TRUE(dag.get_task("f1")->id() == "f1");
    EXPECT_TRUE(dag.adjacency().at("f1").contains("f2"));

    registry.unregister_task("test_plugin_type");
}

// backward compat: id 即 type
TEST_CASE(json_id_only_backward_compat) {
    std::string json = R"({
        "version": "1.0",
        "tasks": [{"id": "simple"}, {"id": "another"}],
        "edges": [{"from": "simple", "to": "another"}]
    })";
    DAG dag = DAGSerializer::from_string(json);
    auto s = dag.get_task("simple");
    EXPECT_TRUE(s->type() == s->id());
}

// ============================================================
// subnode 内置插件（task1/task2/task_processor 编译期链接，进程启动时经
// __attribute__((constructor)) 注册）。
// 注意：这些测试必须在 loader 测试之前运行——loadable example plugin 与
// subnode 内置插件共用相同的 task 名（example_task_1/2、data_processor），
// loader.unload() 会 unregister 这些名字，破坏后续依赖内置注册的用例。
// ============================================================

TEST_CASE(subnode_tasks_available) {
    auto avail = PluginRegistry::instance().available_tasks();
    auto has = [&](const std::string& n) {
        return std::find(avail.begin(), avail.end(), n) != avail.end();
    };
    EXPECT_TRUE(has("example_task_1"));
    EXPECT_TRUE(has("example_task_2"));
    EXPECT_TRUE(has("data_processor"));
}

TEST_CASE(subnode_dag_execution) {
    DAG dag;
    dag.add_plugin_task("example_task_1", "example_task_1");
    dag.add_plugin_task("example_task_2", "example_task_2");
    dag.add_plugin_task("data_processor", "data_processor");
    dag.connect("example_task_1", "out", "data_processor", "a");
    dag.connect("example_task_2", "out", "data_processor", "b");

    DAGExecutor executor;
    executor.execute(dag).wait();
    auto results = executor.get_results();
    EXPECT_EQ(results.size(), size_t(3));
    EXPECT_TRUE(results["example_task_1"].is_success());
    EXPECT_TRUE(results["example_task_2"].is_success());
    EXPECT_TRUE(results["data_processor"].is_success());
}

// ============================================================
// 动态库 loader（依赖 task_plugin_example，缺失则跳过）。
// 放在最后：unload() 会 unregister 与 subnode 共名的 task。
// ============================================================

TEST_CASE(plugin_loader_load_unload) {
    auto path = find_example_plugin();
    if (!std::filesystem::exists(path)) {
        std::cout << "         (skipped: plugin not found)\n";
        return;
    }
    PluginLoader loader;
    EXPECT_TRUE(loader.load(path.string()));
    EXPECT_TRUE(PluginRegistry::instance().has_task("example_task_1"));
    loader.unload(path.string());
}

TEST_CASE(dag_with_loaded_plugin) {
    auto path = find_example_plugin();
    if (!std::filesystem::exists(path)) {
        std::cout << "         (skipped: plugin not found)\n";
        return;
    }
    PluginLoader loader;
    EXPECT_TRUE(loader.load(path.string()));

    DAG dag;
    dag.add_plugin_task("example_task_1", "example_task_1");
    dag.add_plugin_task("data_processor", "data_processor");
    dag.connect("example_task_1", "out", "data_processor", "in");

    DAGExecutor executor;
    executor.execute(dag).wait();
    auto results = executor.get_results();
    EXPECT_EQ(results.size(), size_t(2));
    EXPECT_TRUE(results["example_task_1"].is_success());

    loader.unload(path.string());
}

TEST_MAIN("Plugin Tests")
