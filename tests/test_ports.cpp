// 端口契约测试（统一入口）。
// 合并自 test_check_input.cpp、test_check_input_json.cpp、test_validate.cpp，
// 以及 test_dependency_injection.cpp 的 TaskContext 单元测试、
// test_data_types.cpp 的 Image check_input 执行测试。
//
// 覆盖四个层面：
//   1) check_input 单元级：按 input_specs 校验必填端口 + 类型
//   2) 执行级：executor 按 Edge.to_port 绑定上游输出到下游端口
//   3) 构图期：DAGCompiler::validate() 的端口契约校验
//   4) TaskContext 数据注入：get_result_value / input / declare_dependency
#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/compiler.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/task_context.hpp>
#include <execution_context.hpp>
#include <plugin_api.hpp>
#include <any>
#include <string>
#include <unordered_map>
#include "test_util.hpp"

using namespace task_graph;

// ============================================================
// 通用测试 task
// ============================================================

// 声明两个必填 int 输入端口 "a"/"b"，输出 a+b
class SumTask : public Task {
public:
    SumTask(const std::string& id) : Task(id, [this](auto& ctx) { return run(ctx); }) {}
    std::vector<PortSpec> input_specs() const override {
        return {make_port<int>("a"), make_port<int>("b")};
    }
private:
    TaskResult run(TaskContext& ctx) {
        auto a = ctx.input<int>("a");
        auto b = ctx.input<int>("b");
        return (a && b) ? TaskResult{.status = TaskStatus::COMPLETED, .value = *a + *b}
                        : TaskResult{.status = TaskStatus::FAILED};
    }
};

class ImageProducer : public Task {
public:
    ImageProducer(const std::string& id) : Task(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> output_specs() const override {
        return {make_port<Image>("out")};
    }
};

class ImageConsumer : public Task {
public:
    ImageConsumer(const std::string& id) : Task(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> input_specs() const override {
        return {make_port<Image>("in")};
    }
};

class IntConsumer : public Task {
public:
    IntConsumer(const std::string& id) : Task(id, [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }) {}
    std::vector<PortSpec> input_specs() const override {
        return {make_port<int>("in")};
    }
};

static std::unordered_map<std::string, std::any> make_inputs(
    std::initializer_list<std::pair<std::string, std::any>> kv) {
    std::unordered_map<std::string, std::any> m;
    for (const auto& p : kv) m.emplace(p.first, p.second);
    return m;
}

static bool has_error(const std::vector<ValidationError>& errs) {
    for (const auto& e : errs)
        if (e.severity == ValidationError::Severity::ERROR) return true;
    return false;
}
static size_t count_warnings(const std::vector<ValidationError>& errs) {
    size_t n = 0;
    for (const auto& e : errs)
        if (e.severity == ValidationError::Severity::WARNING) ++n;
    return n;
}

// ============================================================
// 1) check_input 单元级
// ============================================================

TEST_CASE(check_input_success) {
    SumTask task("sum");
    auto r = task.check_input(make_inputs({{"a", 10}, {"b", 20}}));
    EXPECT_TRUE(r.success);
}

TEST_CASE(check_input_missing_required_port) {
    SumTask task("sum");
    auto r = task.check_input(make_inputs({{"a", 10}}));  // 缺 b
    EXPECT_FALSE(r.success);
    EXPECT_CONTAINS(r.error_message, "missing required input port");
    EXPECT_CONTAINS(r.error_message, "'b'");
}

TEST_CASE(check_input_wrong_type) {
    SumTask task("sum");
    auto r = task.check_input(make_inputs({{"a", 10}, {"b", std::string("x")}}));
    EXPECT_FALSE(r.success);
    EXPECT_CONTAINS(r.error_message, "expected int");
    EXPECT_CONTAINS(r.error_message, "got std::string");
}

TEST_CASE(check_input_extra_port_allowed) {
    SumTask task("sum");
    auto r = task.check_input(make_inputs({{"a", 10}, {"b", 20}, {"c", 99}}));
    EXPECT_TRUE(r.success);
}

// ============================================================
// 2) 执行级：port 绑定 + check_input 拦截
// ============================================================

// 两个 producer 分别喂 SumTask 的 "a"/"b" 端口，正常求和
TEST_CASE(exec_port_binding_success) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("pa", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 10};
    }));
    dag.add_task(std::make_shared<Task>("pb", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 20};
    }));
    dag.add_task(std::make_shared<SumTask>("sum"));
    dag.connect("pa", "out", "sum", "a");
    dag.connect("pb", "out", "sum", "b");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["sum"].is_success());
    EXPECT_EQ(std::any_cast<int>(results["sum"].value), 30);
}

// 上游类型错误：SumTask 的 "b" 端口收到 string -> check_input 拦截 -> FAILED
TEST_CASE(exec_check_input_rejects_wrong_type) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("pa", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = 10};
    }));
    dag.add_task(std::make_shared<Task>("pb", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = std::string("x")};
    }));
    dag.add_task(std::make_shared<SumTask>("sum"));
    dag.connect("pa", "out", "sum", "a");
    dag.connect("pb", "out", "sum", "b");

    DAGExecutor executor;
    executor.execute(dag).wait();

    EXPECT_TRUE(executor.get_results()["sum"].is_failed());
}

// Image 类型经端口传递到下游正常执行
TEST_CASE(exec_image_port_dataflow) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("src", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = Image(640, 480, 3)};
    }));
    dag.add_task(std::make_shared<ImageConsumer>("dst"));
    dag.connect("src", "dst");  // 默认 out->in

    DAGExecutor executor;
    executor.execute(dag).wait();

    EXPECT_TRUE(executor.get_results()["dst"].is_success());
}

// Image 端口收到 string -> check_input 拦截
TEST_CASE(exec_image_port_rejects_wrong_type) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("src", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED, .value = std::string("not_image")};
    }));
    dag.add_task(std::make_shared<ImageConsumer>("dst"));
    dag.connect("src", "dst");

    DAGExecutor executor;
    executor.execute(dag).wait();

    EXPECT_TRUE(executor.get_results()["dst"].is_failed());
}

// ============================================================
// 3) 构图期 validate()
// ============================================================

TEST_CASE(validate_clean_graph) {
    DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P"));
    dag.add_task(std::make_shared<ImageConsumer>("C"));
    dag.connect("P", "out", "C", "in");

    DAGCompiler c;
    auto errs = c.validate(dag);
    EXPECT_FALSE(has_error(errs));
}

TEST_CASE(validate_missing_required_port) {
    DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P"));
    dag.add_task(std::make_shared<ImageConsumer>("C"));
    // 不连接 -> C 的必填 "in" 端口缺失

    DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs)
        if (e.severity == ValidationError::Severity::ERROR &&
            e.task_id == "C" && e.port_name == "in" &&
            e.message.find("not connected") != std::string::npos)
            found = true;
    EXPECT_TRUE(found);
}

TEST_CASE(validate_type_mismatch) {
    DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P"));
    dag.add_task(std::make_shared<IntConsumer>("C"));
    dag.connect("P", "out", "C", "in");

    DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs)
        if (e.severity == ValidationError::Severity::ERROR &&
            e.message.find("type mismatch") != std::string::npos)
            found = true;
    EXPECT_TRUE(found);
}

TEST_CASE(validate_undeclared_port_is_warning) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("A", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<Task>("B", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.connect("A", "B");  // B 未声明 specs -> 连到未声明端口

    DAGCompiler c;
    auto errs = c.validate(dag);
    EXPECT_FALSE(has_error(errs));
    EXPECT_TRUE(count_warnings(errs) >= 1);
}

TEST_CASE(validate_diamond_multi_source_no_error) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("A", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<Task>("B", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<Task>("C", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.connect("A", "C");
    dag.connect("B", "C");  // 同 "in" 端口 -> WARNING 而非 ERROR

    DAGCompiler c;
    auto errs = c.validate(dag);
    EXPECT_FALSE(has_error(errs));
}

TEST_CASE(validate_cycle_is_error) {
    DAG dag;
    dag.add_task(std::make_shared<Task>("A", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<Task>("B", [](auto&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }));
    dag.connect("A", "B");
    dag.connect("B", "A");

    DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs)
        if (e.severity == ValidationError::Severity::ERROR &&
            e.message.find("cycles") != std::string::npos)
            found = true;
    EXPECT_TRUE(found);
}

// ============================================================
// 4) TaskContext 数据注入单元测试
// ============================================================

TEST_CASE(context_type_safe_get_result_value) {
    TaskContext ctx;
    ctx.set_result("task1", TaskResult{.status = TaskStatus::COMPLETED, .value = 42});

    auto int_r = ctx.get_result_value<int>("task1");
    EXPECT_TRUE(int_r.has_value());
    EXPECT_EQ(*int_r, 42);

    EXPECT_FALSE(ctx.get_result_value<std::string>("task1").has_value());  // 类型不符
    EXPECT_FALSE(ctx.get_result_value<int>("missing").has_value());       // 不存在
}

TEST_CASE(context_dependency_methods) {
    ExecutionContext ctx;
    ctx.declare_dependency("A");
    ctx.declare_dependency("B");
    ctx.declare_dependency("A");  // 去重
    EXPECT_EQ(ctx.dependencies().size(), size_t(2));

    EXPECT_FALSE(ctx.validate_dependencies());  // 无结果
    ctx.set_result("A", TaskResult{.status = TaskStatus::COMPLETED});
    ctx.set_result("B", TaskResult{.status = TaskStatus::COMPLETED});
    EXPECT_TRUE(ctx.validate_dependencies());

    ctx.clear_result("A");
    EXPECT_FALSE(ctx.validate_dependencies());
}

TEST_CASE(config_dependencies_metadata) {
    TaskConfig config;
    config.dependencies = {"A", "B"};
    auto task = std::make_shared<Task>("C", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }, config);
    EXPECT_EQ(task->config().dependencies.size(), size_t(2));
}

TEST_MAIN("Port Contract Tests")
