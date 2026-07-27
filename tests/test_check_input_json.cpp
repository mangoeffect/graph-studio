#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/dag_serializer.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <vector>
#include <string>
#include <task_graph/task_context.hpp>

class StrictSumTask : public task_graph::Task {
public:
    StrictSumTask(const std::string& id)
        : Task(id, [this](auto& ctx) { return execute_impl(ctx); }, create_config()) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {
            task_graph::make_port<int>("a"),
            task_graph::make_port<int>("b"),
        };
    }

private:
    static task_graph::TaskConfig create_config() {
        task_graph::TaskConfig cfg;
        cfg.dependencies = {"producer_a", "producer_b"};
        return cfg;
    }

    task_graph::TaskResult execute_impl(task_graph::TaskContext& ctx) {
        auto dep_a = ctx.template input<int>("a");
        auto dep_b = ctx.template input<int>("b");
        if (dep_a && dep_b) {
            int result = *dep_a + *dep_b;
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    }
};

template<typename TaskType>
void add_or_update_task(task_graph::DAG& dag, const std::shared_ptr<TaskType>& task) {
    auto existing = dag.get_task(task->id());
    if (existing) {
        const_cast<std::unordered_map<task_graph::TaskId, task_graph::TaskPtr>&>(dag.tasks())[task->id()] = task;
    } else {
        dag.add_task(task);
    }
}

// 多输入边由 v1.0 JSON 加载会触发端口冲突（v1.0 无 port 字段，默认都到 "in"）。
// 用 C++ 端口化 connect 直接构图，绕开此限制；v2.0 序列化支持见 commit 4。
static void build_two_input_dag(task_graph::DAG& dag,
                                const task_graph::TaskPtr& producer_a,
                                const task_graph::TaskPtr& producer_b,
                                const std::shared_ptr<StrictSumTask>& sum_task) {
    dag.add_task(producer_a);
    dag.add_task(producer_b);
    dag.add_task(sum_task);
    dag.connect("producer_a", "out", "sum_task", "a");
    dag.connect("producer_b", "out", "sum_task", "b");
}

bool test_check_input_success() {
    std::cout << "Test: check_input success case (port-based)... ";

    task_graph::DAG dag;

    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });

    auto producer_b = std::make_shared<task_graph::Task>("producer_b", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 20};
    });

    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    build_two_input_dag(dag, producer_a, producer_b, sum_task);

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (sum_task should be completed)" << std::endl;
        return false;
    }

    int sum_value = std::any_cast<int>(results["sum_task"].value);
    if (sum_value != 30) {
        std::cout << "FAILED (expected 30, got " << sum_value << ")" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_wrong_type() {
    std::cout << "Test: check_input wrong type... ";

    task_graph::DAG dag;

    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = std::string("wrong_type")};
    });

    auto producer_b = std::make_shared<task_graph::Task>("producer_b", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 20};
    });

    auto sum_task = std::make_shared<StrictSumTask>("sum_task");
    build_two_input_dag(dag, producer_a, producer_b, sum_task);

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (results["sum_task"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (sum_task should be failed due to wrong input type)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_json_single_edge() {
    // v1.0 单输入 JSON 加载仍可用（默认端口 "in"）
    std::cout << "Test: v1.0 JSON single-edge load... ";

    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "source"},
                {"id": "sink"}
            ],
            "edges": [
                {"from": "source", "to": "sink"}
            ]
        }
    )";

    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);

    auto source = std::make_shared<task_graph::Task>("source", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 42};
    });

    auto sink = std::make_shared<task_graph::Task>("sink", [](auto& ctx) {
        (void)ctx;
        auto v = ctx.template input<int>("in");
        return v ? task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *v}
                 : task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    });

    add_or_update_task(dag, source);
    add_or_update_task(dag, sink);

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (results["sink"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (sink should be completed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_json_roundtrip_single_edge() {
    std::cout << "Test: JSON roundtrip single-edge... ";

    task_graph::DAG original;

    auto producer_a = std::make_shared<task_graph::Task>("producer_a", [](auto& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 15};
    });

    auto sink = std::make_shared<task_graph::Task>("sink", [](auto& ctx) {
        (void)ctx;
        auto v = ctx.template input<int>("in");
        return v ? task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *v + 25}
                 : task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    });

    original.add_task(producer_a);
    original.add_task(sink);
    original.connect("producer_a", "sink");

    std::string json_str = task_graph::DAGSerializer::to_string(original);
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);

    add_or_update_task(restored, producer_a);
    add_or_update_task(restored, sink);

    task_graph::DAGExecutor executor;
    executor.execute(restored).wait();

    auto results = executor.get_results();
    if (results["sink"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (sink should be completed after roundtrip)" << std::endl;
        return false;
    }

    int v = std::any_cast<int>(results["sink"].value);
    if (v != 40) {
        std::cout << "FAILED (expected 40, got " << v << ")" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);
    
    std::cout << "=== Check Input JSON Tests ===\n" << std::endl;
    
    bool all_passed = true;
    all_passed &= test_check_input_success();
    all_passed &= test_check_input_wrong_type();
    all_passed &= test_json_single_edge();
    all_passed &= test_json_roundtrip_single_edge();
    
    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
