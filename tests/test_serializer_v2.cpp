#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/plugin.hpp>
#include <iostream>
#include <string>

// 声明 specs 的 producer/consumer，验证 v2.0 序列化保留端口契约
class ImageProducer : public task_graph::Task {
public:
    ImageProducer(const std::string& id)
        : Task(id, [](auto&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }) {}

    std::vector<task_graph::PortSpec> output_specs() const override {
        return {task_graph::make_port<task_graph::Image>("out")};
    }
};

class DualInputTask : public task_graph::Task {
public:
    DualInputTask(const std::string& id)
        : Task(id, [](auto&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {
            task_graph::make_port<task_graph::Image>("image"),
            task_graph::make_port<task_graph::Image>("mask"),
        };
    }
};

bool test_v2_ports_roundtripped() {
    std::cout << "Test: v2.0 ports are preserved across roundtrip... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P1"));
    dag.add_task(std::make_shared<ImageProducer>("P2"));
    dag.add_task(std::make_shared<DualInputTask>("C"));
    dag.connect("P1", "out", "C", "image");
    dag.connect("P2", "out", "C", "mask");

    std::string json_str = task_graph::DAGSerializer::to_string(dag);

    // 验证 JSON 包含 v2.0 字段
    bool has_version = json_str.find("\"version\": \"2.0\"") != std::string::npos;
    bool has_from_port = json_str.find("from_port") != std::string::npos;
    bool has_to_port   = json_str.find("to_port")   != std::string::npos;
    bool has_input_specs  = json_str.find("input_specs")  != std::string::npos;
    bool has_output_specs = json_str.find("output_specs") != std::string::npos;

    if (!(has_version && has_from_port && has_to_port && has_input_specs && has_output_specs)) {
        std::cout << "FAIL (missing v2.0 fields)\n";
        std::cout << "  version=" << has_version << " from_port=" << has_from_port
                  << " to_port=" << has_to_port << " in_specs=" << has_input_specs
                  << " out_specs=" << has_output_specs << "\n";
        return false;
    }

    // 反序列化回来，验证边数和端口
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);
    if (restored.num_edges() != 2) {
        std::cout << "FAIL (expected 2 edges, got " << restored.num_edges() << ")\n";
        return false;
    }

    // 检查 C 的 incoming_edges 含 "image" 和 "mask" 端口
    auto c_in = restored.incoming_edges("C");
    if (c_in.size() != 2) {
        std::cout << "FAIL (C expected 2 incoming, got " << c_in.size() << ")\n";
        return false;
    }
    bool has_image = false, has_mask = false;
    for (const auto& e : c_in) {
        if (e.to_port == "image") has_image = true;
        if (e.to_port == "mask")  has_mask  = true;
    }
    if (!(has_image && has_mask)) {
        std::cout << "FAIL (C missing image/mask ports)\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

bool test_v1_migration_multi_input() {
    // v1.0 多输入边应被迁移（warning 而非 throw），last-write-wins
    std::cout << "Test: v1.0 multi-input migration does not throw... ";

    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "A"}, {"id": "B"}, {"id": "C"}
            ],
            "edges": [
                {"from": "A", "to": "C"},
                {"from": "B", "to": "C"}
            ]
        }
    )";

    task_graph::DAG dag;
    try {
        dag = task_graph::DAGSerializer::from_string(json_str);
    } catch (const std::exception& e) {
        std::cout << "FAIL (threw: " << e.what() << ")\n";
        return false;
    }

    if (dag.num_tasks() != 3) {
        std::cout << "FAIL (expected 3 tasks, got " << dag.num_tasks() << ")\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

bool test_v2_partial_port_fields() {
    // v2.0 但省略 from_port/to_port：默认 "out"/"in"
    std::cout << "Test: v2.0 edge with missing port fields uses defaults... ";

    std::string json_str = R"(
        {
            "version": "2.0",
            "tasks": [{"id": "A"}, {"id": "B"}],
            "edges": [{"from": "A", "to": "B"}]
        }
    )";

    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    auto edges = dag.edges();
    if (edges.size() != 1 ||
        edges[0].from_port != "out" || edges[0].to_port != "in") {
        std::cout << "FAIL\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

int main() {
    bool ok = true;
    ok &= test_v2_ports_roundtripped();
    ok &= test_v1_migration_multi_input();
    ok &= test_v2_partial_port_fields();
    std::cout << (ok ? "\nAll v2.0 serializer tests passed.\n"
                     : "\nSome v2.0 serializer tests FAILED.\n");
    return ok ? 0 : 1;
}
