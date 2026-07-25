#include <plugin_api.hpp>
#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <iostream>

bool test_logger_basic() {
    std::cout << "Test: Logger basic functionality... ";
    
    task_graph::tg_set_log_level(task_graph::LogLevel::DEBUG);
    
    task_graph::tg_log(task_graph::LogLevel::TRACE, "This is a TRACE message");
    task_graph::tg_log(task_graph::LogLevel::DEBUG, "This is a DEBUG message");
    task_graph::tg_log(task_graph::LogLevel::INFO, "This is an INFO message");
    task_graph::tg_log(task_graph::LogLevel::WARN, "This is a WARN message");
    task_graph::tg_log(task_graph::LogLevel::ERROR, "This is an ERROR message");
    task_graph::tg_log(task_graph::LogLevel::FATAL, "This is a FATAL message");
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_logger_level_filter() {
    std::cout << "Test: Logger level filtering... ";
    
    task_graph::tg_set_log_level(task_graph::LogLevel::WARN);
    
    task_graph::tg_log(task_graph::LogLevel::TRACE, "Should NOT be visible");
    task_graph::tg_log(task_graph::LogLevel::DEBUG, "Should NOT be visible");
    task_graph::tg_log(task_graph::LogLevel::INFO, "Should NOT be visible");
    task_graph::tg_log(task_graph::LogLevel::WARN, "Should be visible - WARN");
    task_graph::tg_log(task_graph::LogLevel::ERROR, "Should be visible - ERROR");
    task_graph::tg_log(task_graph::LogLevel::FATAL, "Should be visible - FATAL");
    
    task_graph::tg_set_log_level(task_graph::LogLevel::DEBUG);
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_logger_macros() {
    std::cout << "Test: Logger macros... ";
    
    TG_LOG_TRACE("Macro TRACE");
    TG_LOG_DEBUG("Macro DEBUG");
    TG_LOG_INFO("Macro INFO");
    TG_LOG_WARN("Macro WARN");
    TG_LOG_ERROR("Macro ERROR");
    TG_LOG_FATAL("Macro FATAL");
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_dag_logging() {
    std::cout << "Test: DAG logging... ";
    
    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_executor_logging() {
    std::cout << "Test: Executor logging... ";
    
    task_graph::DAG dag;
    
    auto task_a = std::make_shared<task_graph::Task>("A", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    auto task_b = std::make_shared<task_graph::Task>("B", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    });
    
    dag.add_task(task_a);
    dag.add_task(task_b);
    dag.add_dependency("A", "B");
    
    task_graph::DAGExecutor executor;
    auto future = executor.execute(dag);
    future.wait();
    
    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    task_graph::tg_set_log_level(task_graph::LogLevel::DEBUG);
    
    std::cout << "=== Logger Tests ===\n" << std::endl;
    
    test_logger_basic();
    test_logger_level_filter();
    test_logger_macros();
    test_dag_logging();
    test_executor_logging();
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    return 0;
}
