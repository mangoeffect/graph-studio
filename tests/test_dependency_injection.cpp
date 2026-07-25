#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <execution_context.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <task_graph/task_context.hpp>

bool test_dependency_declaration() {
    std::cout << "Test: Dependency declaration in TaskConfig... ";
    
    task_graph::TaskConfig config;
    config.dependencies = {"A", "B"};
    
    auto task = std::make_shared<task_graph::Task>(
        "C",
        [](task_graph::TaskContext& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        },
        config
    );
    
    if (task->config().dependencies.size() != 2) {
        std::cout << "FAILED (expected 2 dependencies, got " << task->config().dependencies.size() << ")" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_context_dependency_methods() {
    std::cout << "Test: Context dependency methods... ";
    
    auto ctx = std::make_shared<task_graph::ExecutionContext>();
    
    ctx->declare_dependency("A");
    ctx->declare_dependency("B");
    ctx->declare_dependency("A");
    
    auto deps = ctx->dependencies();
    if (deps.size() != 2) {
        std::cout << "FAILED (expected 2 unique dependencies, got " << deps.size() << ")" << std::endl;
        return false;
    }
    
    if (ctx->validate_dependencies()) {
        std::cout << "FAILED (should not validate without results)" << std::endl;
        return false;
    }
    
    ctx->set_result("A", task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED});
    ctx->set_result("B", task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED});
    
    if (!ctx->validate_dependencies()) {
        std::cout << "FAILED (should validate with completed dependencies)" << std::endl;
        return false;
    }
    
    ctx->clear_result("A");
    if (ctx->validate_dependencies()) {
        std::cout << "FAILED (should not validate after clearing result)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_type_safe_get_result() {
    std::cout << "Test: Type-safe get_result_value... ";
    
    auto ctx = std::make_shared<task_graph::TaskContext>();
    task_graph::TaskContext* ctx_ptr = ctx.get();
    
    ctx->set_result("task1", task_graph::TaskResult{
        .status = task_graph::TaskStatus::COMPLETED,
        .value = 42
    });
    
    auto int_result = ctx_ptr->get_result_value<int>("task1");
    if (!int_result || *int_result != 42) {
        std::cout << "FAILED (expected 42)" << std::endl;
        return false;
    }
    
    auto str_result = ctx_ptr->get_result_value<std::string>("task1");
    if (str_result) {
        std::cout << "FAILED (expected nullopt for wrong type)" << std::endl;
        return false;
    }
    
    auto missing_result = ctx_ptr->get_result_value<int>("missing");
    if (missing_result) {
        std::cout << "FAILED (expected nullopt for missing task)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_executor_dependency_validation() {
    std::cout << "Test: Executor dependency validation... ";
    
    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        ctx.set_value("output", 10);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });
    
    task_graph::TaskConfig b_config;
    b_config.dependencies = {"A"};
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        auto a_value = ctx.template get_result_value<int>("A");
        if (!a_value) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *a_value * 2};
    }, b_config);
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    
    task_graph::DAGExecutor executor;
    auto future = executor.execute(dag);
    future.wait();
    
    auto results = executor.get_results();
    if (results["A"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (task A should be completed)" << std::endl;
        return false;
    }
    
    if (results["B"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (task B should be completed)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_dependency_failure_propagation() {
    std::cout << "Test: Dependency failure propagation... ";
    
    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
    });
    
    task_graph::TaskConfig b_config;
    b_config.dependencies = {"A"};
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }, b_config);
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    
    task_graph::DAGExecutor executor;
    auto future = executor.execute(dag);
    future.wait();
    
    auto results = executor.get_results();
    if (results["A"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (task A should be failed)" << std::endl;
        return false;
    }
    
    if (results["B"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (task B should be failed due to dependency failure)" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_data_passing_between_tasks() {
    std::cout << "Test: Data passing between tasks... ";
    
    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 100};
    });
    
    task_graph::TaskConfig b_config;
    b_config.dependencies = {"A"};
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        auto a_value = ctx.template get_result_value<int>("A");
        if (!a_value) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        }
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *a_value + 50};
    }, b_config);
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    
    task_graph::DAGExecutor executor;
    auto future = executor.execute(dag);
    future.wait();
    
    auto results = executor.get_results();
    auto b_value = std::any_cast<int>(results["B"].value);
    if (b_value != 150) {
        std::cout << "FAILED (expected 150, got " << b_value << ")" << std::endl;
        return false;
    }
    
    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);
    
    std::cout << "=== Dependency Injection Tests ===\n" << std::endl;
    
    test_dependency_declaration();
    test_context_dependency_methods();
    test_type_safe_get_result();
    test_executor_dependency_validation();
    test_dependency_failure_propagation();
    test_data_passing_between_tasks();
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    return 0;
}
