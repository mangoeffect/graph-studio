// 插件系统测试。覆盖：registry 注册/注销、唯一 ID 生成、TaskManager 生命周期、
// 同类型多实例、type/id 分离。
#include <task_graph/task_graph.hpp>
#include <task_graph/plugin.hpp>
#include <task_graph/task_manager.hpp>
#include <task_graph/task_context.hpp>
#include <task_graph/dag_serializer.hpp>
#include <set>
#include <string>
#include "test_util.hpp"

using namespace task_graph;

class TestPluginTask : public IPluginTask {
public:
    using INode::INode;
    const std::string& type() const override {
        static const std::string t = "test_plugin_type";
        return t;
    }
    TaskResult execute(TaskContext&) override {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }
};

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

TEST_MAIN("Plugin Tests")
