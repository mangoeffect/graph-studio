// DAG 序列化测试。合并自 test_serializer.cpp 与 test_serializer_v2.cpp。
// 覆盖：v2.0 序列化输出、v1.0 反序列化迁移、roundtrip、TaskConfig 持久化、
//       端口/specs 持久化、v1.0 多输入迁移、缺省端口字段、_source_dir 注入/跳过。
#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/path_utils.hpp>
#include <task_graph/plugin.hpp>
#include <string>
#include <gtest/gtest.h>
#include "tg_test_helpers.hpp"

using namespace task_graph;

static TaskPtr noop(const std::string& id, TaskConfig cfg = {}) {
    return std::make_shared<Task>(id, [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }, std::move(cfg));
}

// 声明 specs 的任务，验证 specs 落盘
class ImageProducer : public Task {
public:
    ImageProducer(const std::string& id) : LambdaNode(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> output_specs() const override {
        return {make_port<Image>("out")};
    }
};

class DualInputTask : public Task {
public:
    DualInputTask(const std::string& id) : LambdaNode(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> input_specs() const override {
        return {make_port<Image>("image"), make_port<Image>("mask")};
    }
};

// ---- 基础 serialize / deserialize / roundtrip ----

TEST(Serializer, serialize_contains_tasks_and_edges) {
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

TEST(Serializer, deserialize_v1_single_edge) {
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

TEST(Serializer, roundtrip_preserves_structure) {
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

TEST(Serializer, roundtrip_preserves_task_config) {
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

TEST(Serializer, v2_ports_and_specs_roundtrip) {
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

TEST(Serializer, v1_multi_input_migration_no_throw) {
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

TEST(Serializer, v2_missing_port_fields_default) {
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

// ---- _source_dir 框架参数注入与序列化跳过 ----

TEST(Serializer, deserialize_without_base_dir_injects_empty_source_dir) {
    // base_dir 默认空串：仍然注入 _source_dir 占位（值是空串）
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "A"}]
    })";
    DAG dag = DAGSerializer::from_string(json);  // 不传 base_dir
    auto t = dag.get_task("A");
    EXPECT_TRUE(t->config().params.get_string(kSourceDirParam).has_value());
    EXPECT_EQ(t->config().params.get_string(kSourceDirParam).value_or("x"), std::string{});
}

TEST(Serializer, deserialize_with_base_dir_injects_source_dir) {
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "A"}, {"id": "B"}]
    })";
    const std::string base = "/home/u/graphs";
    DAG dag = DAGSerializer::from_string(json, base);
    EXPECT_EQ(dag.get_task("A")->config().params.get_string(kSourceDirParam).value_or(""),
              base);
    EXPECT_EQ(dag.get_task("B")->config().params.get_string(kSourceDirParam).value_or(""),
              base);
}

TEST(Serializer, serialize_skips_framework_reserved_param) {
    // _source_dir 不能写回 graph.json（避免泄露本机路径 / 文件移动后失效）
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "A"}]
    })";
    DAG dag = DAGSerializer::from_string(json, "/home/u/graphs");
    std::string out = DAGSerializer::to_string(dag);
    EXPECT_FALSE(out.find("_source_dir") != std::string::npos);
    // 普通业务参数仍然要保留：构造一个含业务参数的 DAG roundtrip 一下
    TaskConfig cfg;
    cfg.params.set_string("file_path", "assets/x.png");
    cfg.params.set_string(kSourceDirParam, "/home/u/graphs");
    DAG src;
    src.add_task(noop("A", cfg));
    std::string out2 = DAGSerializer::to_string(src);
    EXPECT_CONTAINS(out2, "file_path");
    EXPECT_FALSE(out2.find("_source_dir") != std::string::npos);
}

TEST(Serializer, source_dir_round_trip_via_base_dir) {
    // 模拟实际工作流：保存 graph.json -> 不含 _source_dir；重新加载时
    // 通过 base_dir 入参重新注入。
    TaskConfig cfg;
    cfg.params.set_string("file_path", "assets/x.png");
    DAG src;
    src.add_task(noop("A", cfg));
    std::string json = DAGSerializer::to_string(src);
    EXPECT_FALSE(json.find("_source_dir") != std::string::npos);

    DAG restored = DAGSerializer::from_string(json, "/home/u/graphs");
    EXPECT_EQ(restored.get_task("A")->config().params.get_string("file_path").value_or(""),
              "assets/x.png");
    EXPECT_EQ(restored.get_task("A")->config().params.get_string(kSourceDirParam).value_or(""),
              "/home/u/graphs");
}

