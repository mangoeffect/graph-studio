#include <task_graph/compiler.hpp>
#include <task_graph/task.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/task_context.hpp>
#include <iostream>
#include <unordered_map>
#include <any>

namespace {

using task_graph::ValidationError;

bool has_error(const std::vector<ValidationError>& errs) {
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::ERROR) return true;
    }
    return false;
}

size_t count_warnings(const std::vector<ValidationError>& errs) {
    size_t n = 0;
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::WARNING) ++n;
    }
    return n;
}

bool test_validate_clean() {
    std::cout << "Test: clean graph validates without errors... ";

    task_graph::DAG dag;
    auto a = std::make_shared<task_graph::Task>("A", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    auto b = std::make_shared<task_graph::Task>("B", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    dag.add_task(a);
    dag.add_task(b);
    dag.connect("A", "B");

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    if (has_error(errs)) {
        std::cout << "FAIL (unexpected errors)\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

// 一个有 specs 的自定义 task：声明一个必填 int 输入端口
class TaskWithSpecs : public task_graph::Task {
public:
    TaskWithSpecs(const std::string& id)
        : Task(id, [this](auto& ctx) { return execute_impl(ctx); }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<int>("value")};
    }

private:
    task_graph::TaskResult execute_impl(task_graph::TaskContext& ctx) {
        (void)ctx;
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }
};

bool test_validate_missing_required_port() {
    std::cout << "Test: missing required port detected... ";

    task_graph::DAG dag;
    auto a = std::make_shared<task_graph::Task>("A", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    auto b = std::make_shared<TaskWithSpecs>("B");
    dag.add_task(a);
    dag.add_task(b);
    // 故意不连接 A->B

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::ERROR &&
            e.task_id == "B" && e.port_name == "value" &&
            e.message.find("not connected") != std::string::npos) {
            found = true;
        }
    }
    std::cout << (found ? "OK\n" : "FAIL\n");
    return found;
}

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

class ImageConsumer : public task_graph::Task {
public:
    ImageConsumer(const std::string& id)
        : Task(id, [](auto&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<task_graph::Image>("in")};
    }
};

class IntConsumer : public task_graph::Task {
public:
    IntConsumer(const std::string& id)
        : Task(id, [](auto&) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        }) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<int>("in")};
    }
};

bool test_validate_type_match_ok() {
    std::cout << "Test: type match between Image producer and consumer... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P"));
    dag.add_task(std::make_shared<ImageConsumer>("C"));
    dag.connect("P", "out", "C", "in");

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    if (has_error(errs)) {
        std::cout << "FAIL\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

bool test_validate_type_mismatch() {
    std::cout << "Test: type mismatch detected (Image vs int)... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<ImageProducer>("P"));
    dag.add_task(std::make_shared<IntConsumer>("C"));
    dag.connect("P", "out", "C", "in");

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::ERROR &&
            e.message.find("type mismatch") != std::string::npos) {
            found = true;
        }
    }
    std::cout << (found ? "OK\n" : "FAIL\n");
    return found;
}

bool test_validate_undeclared_port_warning() {
    std::cout << "Test: undeclared input port is a WARNING, not ERROR... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<task_graph::Task>("A", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<task_graph::Task>("B", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    // A,B 都没声明 specs，connect 到默认 'in' 端口 → 应是 WARNING
    dag.connect("A", "B");

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    bool ok = !has_error(errs) && count_warnings(errs) >= 1;
    std::cout << (ok ? "OK\n" : "FAIL\n");
    return ok;
}

bool test_validate_diamond_multi_source_warning() {
    std::cout << "Test: diamond dep (A->C, B->C on same port) is WARNING... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<task_graph::Task>("A", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<task_graph::Task>("B", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<task_graph::Task>("C", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.connect("A", "C");
    dag.connect("B", "C");  // 同 'in' 端口

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    // 不应有 ERROR（依赖调度合法），但应有 multi-source WARNING
    bool ok = !has_error(errs);
    std::cout << (ok ? "OK\n" : "FAIL\n");
    return ok;
}

bool test_validate_cycle_error() {
    std::cout << "Test: cycle is reported as ERROR... ";

    task_graph::DAG dag;
    dag.add_task(std::make_shared<task_graph::Task>("A", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.add_task(std::make_shared<task_graph::Task>("B", [](auto&) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }));
    dag.connect("A", "B");
    dag.connect("B", "A");

    task_graph::DAGCompiler c;
    auto errs = c.validate(dag);
    bool found = false;
    for (const auto& e : errs) {
        if (e.severity == ValidationError::Severity::ERROR &&
            e.message.find("cycles") != std::string::npos) {
            found = true;
        }
    }
    std::cout << (found ? "OK\n" : "FAIL\n");
    return found;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_validate_clean();
    ok &= test_validate_missing_required_port();
    ok &= test_validate_type_match_ok();
    ok &= test_validate_type_mismatch();
    ok &= test_validate_undeclared_port_warning();
    ok &= test_validate_diamond_multi_source_warning();
    ok &= test_validate_cycle_error();
    std::cout << (ok ? "\nAll validate tests passed.\n" : "\nSome validate tests FAILED.\n");
    return ok ? 0 : 1;
}
