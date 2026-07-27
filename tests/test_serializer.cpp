// DAG 序列化测试。合并自 test_serializer.cpp 与 test_serializer_v2.cpp。
// 覆盖：v2.0 序列化输出、v1.0 反序列化迁移、roundtrip、TaskConfig 持久化、
//       端口/specs 持久化、v1.0 多输入迁移、缺省端口字段。
#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/plugin.hpp>
#include <string>
#include "test_util.hpp"

using namespace task_graph;

static TaskPtr noop(const std::string& id, TaskConfig cfg = {}) {
    return std::make_shared<Task>(id, [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }, std::move(cfg));
}

// 声明 specs 的任务，验证 specs 落盘
class ImageProducer : public Task {
public:
    ImageProducer(const std::string& id) : Task(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> output_specs() const override {
        return {make_port<Image>("out")};
    }
};

class DualInputTask : public Task {
public:
    DualInputTask(const std::string& id) : Task(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> input_specs() const override {
        return {make_port<Image>("image"), make_port<Image>("mask")};
    }
};

// ---- 基础 serialize / deserialize / roundtrip ----

TEST_CASE(serialize_contains_tasks_and_edges) {
    DAG dag;
    dag.add_task(noop("A"));
    dag.add_task(noop("B"));
    dag.connect("A", "B");

    std::string json = DAGSerializer::to_string(dag);
    EXPECT_CONTAINS(json, "tasks");
    EXPECT_CONTAINS(json, "edges");
    EXPECT_CONTAINS(json, "\"A\"");
    EXPECT_CONTAINS(json, "\"B\"");
    EXPECT_CONTAINS(json, "\"version\": \"2.0\"");
}

TEST_CASE(deserialize_v1_single_edge) {
    std::string json = R"({
        "version": "1.0",
        "tasks": [{"id": "A"}, {"id": "B"}],
        "edges": [{"from": "A", "to": "B"}]
    })";
    DAG dag = DAGSerializer::from_string(json);
    EXPECT_TRUE(dag.has_task("A"));
    EXPECT_TRUE(dag.has_task("B"));
    EXPECT_TRUE(dag.adjacency().at("A").contains("B"));
}

TEST_CASE(roundtrip_preserves_structure) {
    DAG original;
    original.add_task(noop("A"));
    original.add_task(noop("B"));
    original.add_task(noop("C"));
    original.connect("A", "B");
    original.connect("B", "C");

    std::string json = DAGSerializer::to_string(original);
    DAG restored = DAGSerializer::from_string(json);

    EXPECT_EQ(original.num_tasks(), restored.num_tasks());
    EXPECT_EQ(original.num_edges(), restored.num_edges());
    EXPECT_TRUE(restored.has_task("A"));
    EXPECT_TRUE(restored.has_task("B"));
    EXPECT_TRUE(restored.has_task("C"));
}

TEST_CASE(roundtrip_preserves_task_config) {
    TaskConfig config;
    config.priority = TaskPriority::HIGH;
    config.max_retries = 3;
    config.timeout = std::chrono::milliseconds(5000);
    config.skip_on_fail = true;

    DAG original;
    original.add_task(noop("A", config));
    std::string json = DAGSerializer::to_string(original);
    DAG restored = DAGSerializer::from_string(json);

    auto t = restored.get_task("A");
    EXPECT_TRUE(t->config().priority == TaskPriority::HIGH);
    EXPECT_EQ(t->config().max_retries, size_t(3));
    EXPECT_TRUE(t->config().timeout == std::chrono::milliseconds(5000));
    EXPECT_TRUE(t->config().skip_on_fail);
}

// ---- v2.0 端口与 specs ----

TEST_CASE(v2_ports_and_specs_roundtrip) {
    DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P1"));
    dag.add_task(std::make_shared<ImageProducer>("P2"));
    dag.add_task(std::make_shared<DualInputTask>("C"));
    dag.connect("P1", "out", "C", "image");
    dag.connect("P2", "out", "C", "mask");

    std::string json = DAGSerializer::to_string(dag);
    EXPECT_CONTAINS(json, "from_port");
    EXPECT_CONTAINS(json, "to_port");
    EXPECT_CONTAINS(json, "input_specs");
    EXPECT_CONTAINS(json, "output_specs");

    DAG restored = DAGSerializer::from_string(json);
    EXPECT_EQ(restored.num_edges(), size_t(2));

    auto c_in = restored.incoming_edges("C");
    EXPECT_EQ(c_in.size(), size_t(2));
    bool has_image = false, has_mask = false;
    for (const auto& e : c_in) {
        if (e.to_port == "image") has_image = true;
        if (e.to_port == "mask") has_mask = true;
    }
    EXPECT_TRUE(has_image);
    EXPECT_TRUE(has_mask);
}

TEST_CASE(v1_multi_input_migration_no_throw) {
    std::string json = R"({
        "version": "1.0",
        "tasks": [{"id": "A"}, {"id": "B"}, {"id": "C"}],
        "edges": [{"from": "A", "to": "C"}, {"from": "B", "to": "C"}]
    })";
    bool threw = false;
    DAG dag;
    try {
        dag = DAGSerializer::from_string(json);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw);
    EXPECT_EQ(dag.num_tasks(), size_t(3));
}

TEST_CASE(v2_missing_port_fields_default) {
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "A"}, {"id": "B"}],
        "edges": [{"from": "A", "to": "B"}]
    })";
    DAG dag = DAGSerializer::from_string(json);
    auto edges = dag.edges();
    EXPECT_EQ(edges.size(), size_t(1));
    EXPECT_TRUE(edges[0].from_port == "out");
    EXPECT_TRUE(edges[0].to_port == "in");
}

TEST_MAIN("Serializer Tests")
