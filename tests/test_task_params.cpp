#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/task_context.hpp>
#include <plugin_api.hpp>
#include <gtest/gtest.h>
#include <string>

TEST(TaskParams, SetGet) {
    task_graph::TaskParams params;

    params.set_int("kernel_size", 5);
    params.set_float("sigma", 1.5f);
    params.set_string("mode", "RGB");

    EXPECT_EQ(params.get_int("kernel_size"), 5);
    EXPECT_EQ(params.get_float("sigma"), 1.5f);
    EXPECT_EQ(params.get_string("mode"), "RGB");

    EXPECT_FALSE(params.get_int("missing_key").has_value());
    EXPECT_FALSE(params.get_float("missing_key").has_value());
    EXPECT_FALSE(params.get_string("missing_key").has_value());

    EXPECT_TRUE(params.has_param("kernel_size"));
    EXPECT_FALSE(params.has_param("nonexistent"));
}

TEST(TaskParams, Clear) {
    task_graph::TaskParams params;

    params.set_int("value1", 10);
    params.set_string("value2", "test");

    params.clear();

    EXPECT_FALSE(params.has_param("value1"));
    EXPECT_FALSE(params.has_param("value2"));
}

TEST(TaskParams, ConfigWithParams) {
    task_graph::TaskConfig config;
    config.params.set_int("threshold", 128);
    config.params.set_float("scale", 0.5f);
    config.params.set_string("algorithm", "canny");

    EXPECT_EQ(config.params.get_int("threshold"), 128);
    EXPECT_EQ(config.params.get_float("scale"), 0.5f);
    EXPECT_EQ(config.params.get_string("algorithm"), "canny");
}

TEST(TaskParams, ExecutionWithParams) {
    task_graph::DAG dag;

    auto input_task = std::make_shared<task_graph::Task>("input", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 42};
    });
    dag.add_task(input_task);

    task_graph::TaskConfig config;
    config.params.set_int("multiplier", 2);
    config.params.set_float("factor", 0.5f);
    config.params.set_string("operation", "multiply");

    auto process_task = std::make_shared<task_graph::Task>(
        "process",
        [](task_graph::TaskContext& ctx) {
            auto input = ctx.get_result_value<int>("input");
            auto multiplier = ctx.get_param_int("multiplier");
            auto factor = ctx.get_param_float("factor");
            auto operation = ctx.get_param_string("operation");

            if (input && multiplier && factor && operation && *operation == "multiply") {
                int result = *input * *multiplier * static_cast<int>(*factor * 10);
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config
    );
    dag.add_task(process_task);
    dag.add_dependency("input", "process");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["input"].is_success());
    EXPECT_TRUE(results["process"].is_success());
}

TEST(TaskParams, Serialization) {
    task_graph::DAG dag;

    task_graph::TaskConfig config;
    config.params.set_int("width", 640);
    config.params.set_float("ratio", 1.777f);
    config.params.set_string("format", "JPEG");

    auto task = std::make_shared<task_graph::Task>(
        "encoder",
        [](auto& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        },
        config
    );
    dag.add_task(task);

    std::string json_str = task_graph::DAGSerializer::to_string(dag);

    EXPECT_NE(json_str.find("\"width\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"ratio\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"format\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"params\""), std::string::npos);
}

TEST(TaskParams, Deserialization) {
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {
                    "id": "filter",
                    "params": {
                        "kernel_size": 7,
                        "sigma": 2.0,
                        "mode": "GRAY"
                    }
                }
            ],
            "edges": []
        }
    )";

    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);

    auto task = dag.get_task("filter");
    ASSERT_TRUE(task) << "task 'filter' missing after deserialization";

    const auto& params = task->config().params;
    EXPECT_EQ(params.get_int("kernel_size"), 7);
    EXPECT_EQ(params.get_float("sigma"), 2.0f);
    EXPECT_EQ(params.get_string("mode"), "GRAY");
}

TEST(TaskParams, Roundtrip) {
    task_graph::DAG original;

    task_graph::TaskConfig config;
    config.params.set_int("count", 100);
    config.params.set_float("accuracy", 0.95f);
    config.params.set_string("model", "resnet50");

    auto task = std::make_shared<task_graph::Task>(
        "inference",
        [](auto& ctx) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
        },
        config
    );
    original.add_task(task);

    std::string json_str = task_graph::DAGSerializer::to_string(original);
    task_graph::DAG restored = task_graph::DAGSerializer::from_string(json_str);

    auto restored_task = restored.get_task("inference");
    ASSERT_TRUE(restored_task) << "task 'inference' missing after roundtrip";

    const auto& params = restored_task->config().params;
    EXPECT_EQ(params.get_int("count"), 100);
    EXPECT_EQ(params.get_float("accuracy"), 0.95f);
    EXPECT_EQ(params.get_string("model"), "resnet50");
}

TEST(TaskParams, CascadeWithParams) {
    task_graph::DAG dag;

    auto input_task = std::make_shared<task_graph::Task>("input", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = 10};
    });
    dag.add_task(input_task);

    task_graph::TaskConfig config1;
    config1.params.set_int("add_value", 5);
    auto add_task = std::make_shared<task_graph::Task>(
        "add",
        [](task_graph::TaskContext& ctx) {
            auto input = ctx.get_result_value<int>("input");
            auto add_val = ctx.get_param_int("add_value");
            if (input && add_val) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *input + *add_val};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config1
    );
    dag.add_task(add_task);

    task_graph::TaskConfig config2;
    config2.params.set_int("multiply_value", 2);
    auto multiply_task = std::make_shared<task_graph::Task>(
        "multiply",
        [](task_graph::TaskContext& ctx) {
            auto input = ctx.get_result_value<int>("add");
            auto mul_val = ctx.get_param_int("multiply_value");
            if (input && mul_val) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = *input * *mul_val};
            }
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        },
        config2
    );
    dag.add_task(multiply_task);

    dag.add_dependency("input", "add");
    dag.add_dependency("add", "multiply");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    EXPECT_TRUE(results["input"].is_success());
    EXPECT_TRUE(results["add"].is_success());
    EXPECT_TRUE(results["multiply"].is_success());
}
