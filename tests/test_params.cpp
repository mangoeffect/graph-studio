// 参数声明 (ParamSpec) 与参数存取 (TaskParams) 测试。
// 覆盖：make_*_param 工厂、int/float/string/bool/enum 存取、序列化按声明类型往返。
#include <task_graph/data_types.hpp>
#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <plugin_api.hpp>
#include <task_graph/task_context.hpp>
#include "test_util.hpp"

using namespace task_graph;

// ---- ParamSpec 工厂 ----

TEST_CASE(int_param_factory_carries_range) {
    auto s = make_int_param("kernel_size", 5, 1, 31, 2);
    EXPECT_TRUE(s.name == "kernel_size");
    EXPECT_TRUE(s.type == ParamType::Int);
    EXPECT_EQ(std::get<int>(s.default_value), 5);
    EXPECT_TRUE(s.min_value.has_value() && *s.min_value == 1.0);
    EXPECT_TRUE(s.max_value.has_value() && *s.max_value == 31.0);
    EXPECT_TRUE(s.step.has_value() && *s.step == 2.0);
}

TEST_CASE(float_param_factory) {
    auto s = make_float_param("sigma", 1.5f, 0.0, 100.0);
    EXPECT_TRUE(s.type == ParamType::Float);
    EXPECT_EQ(std::get<float>(s.default_value), 1.5f);
    EXPECT_TRUE(s.min_value.has_value() && *s.min_value == 0.0);
}

TEST_CASE(string_param_factory) {
    auto s = make_string_param("mode", "RGB");
    EXPECT_TRUE(s.type == ParamType::String);
    EXPECT_TRUE(std::get<std::string>(s.default_value) == "RGB");
    EXPECT_FALSE(s.min_value.has_value());
}

TEST_CASE(bool_param_factory) {
    auto s = make_bool_param("normalize", true);
    EXPECT_TRUE(s.type == ParamType::Bool);
    EXPECT_EQ(std::get<bool>(s.default_value), true);
}

TEST_CASE(enum_param_factory_carries_values) {
    auto s = make_enum_param("operation", 2, {{"OPEN", 2}, {"CLOSE", 3}});
    EXPECT_TRUE(s.type == ParamType::Enum);
    EXPECT_EQ(std::get<int>(s.default_value), 2);
    EXPECT_EQ(s.enum_values.size(), size_t(2));
    EXPECT_TRUE(s.enum_values[0].first == "OPEN");
    EXPECT_EQ(s.enum_values[0].second, 2);
}

// ---- TaskParams int/float/string/bool 存取 ----

TEST_CASE(task_params_int_float_string_roundtrip) {
    TaskParams p;
    p.set_int("k", 5);
    p.set_float("sigma", 1.5f);
    p.set_string("mode", "RGB");
    EXPECT_EQ(*p.get_int("k"), 5);
    EXPECT_EQ(*p.get_float("sigma"), 1.5f);
    EXPECT_TRUE(*p.get_string("mode") == "RGB");
}

TEST_CASE(task_params_bool_roundtrip) {
    TaskParams p;
    p.set_bool("flag", true);
    EXPECT_TRUE(p.get_bool("flag").has_value());
    EXPECT_EQ(*p.get_bool("flag"), true);
    p.set_bool("flag", false);
    EXPECT_EQ(*p.get_bool("flag"), false);
}

TEST_CASE(task_params_strict_type_mismatch_returns_nullopt) {
    TaskParams p;
    p.set_int("k", 5);
    // int 存入，float 取出应失败（不隐式转换）
    EXPECT_FALSE(p.get_float("k").has_value());
    EXPECT_FALSE(p.get_bool("k").has_value());
}

TEST_CASE(task_params_has_and_clear) {
    TaskParams p;
    p.set_int("k", 1);
    EXPECT_TRUE(p.has_param("k"));
    EXPECT_FALSE(p.has_param("missing"));
    p.clear();
    EXPECT_FALSE(p.has_param("k"));
}

TEST_CASE(task_context_get_param_bool) {
    TaskConfig cfg;
    cfg.params.set_bool("flag", true);
    TaskContext ctx(cfg.params, {}, {});
    EXPECT_EQ(*ctx.get_param_bool("flag"), true);
    EXPECT_FALSE(ctx.get_param_bool("missing").has_value());
}

// ---- param_specs() 委托（Task 经 spec_delegate 转发） ----

class TaskWithParams : public IPluginTask {
public:
    using IPluginTask::IPluginTask;
    const std::string& type() const override { static std::string t = "with_params"; return t; }
    TaskResult execute(TaskContext&) override {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }
    std::vector<ParamSpec> param_specs() const override {
        return {make_int_param("k", 5, 1, 10)};
    }
};

TEST_CASE(task_delegates_param_specs) {
    auto plugin = std::make_shared<TaskWithParams>("p");
    auto wrapper = std::make_shared<Task>("w", "with_params",
        [plugin](TaskContext& ctx) { return plugin->execute(ctx); });
    wrapper->set_spec_delegate(plugin);
    auto specs = wrapper->param_specs();
    EXPECT_EQ(specs.size(), size_t(1));
    EXPECT_TRUE(specs[0].name == "k");
}

// ---- 序列化：按声明类型解析 params（核心修复点） ----

TEST_CASE(serialize_params_includes_bool) {
    DAG dag;
    TaskConfig cfg;
    cfg.params.set_bool("flag", true);
    cfg.params.set_int("k", 7);
    dag.add_task(std::make_shared<Task>("A", [](TaskContext&) {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }, cfg));
    std::string json = DAGSerializer::to_string(dag);
    EXPECT_CONTAINS(json, "\"flag\"");
    EXPECT_CONTAINS(json, "true");
}

TEST_CASE(deserialize_params_by_declared_type) {
    // 模拟一个 task type（不依赖具体插件），用 JSON 加载：
    // JSON 里 sigma 写成整数 2（无小数点），按字面量会变 int；
    // 但这里 task 未注册，无 param_specs，回退到字面量 -> int。
    // 重点验证：声明了 specs 的插件 task，float 字段即使写成整数也能正确转 float。
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "A", "params": {"k": 5, "name": "x"}}],
        "edges": []
    })";
    DAG dag = DAGSerializer::from_string(json);
    auto t = dag.get_task("A");
    EXPECT_EQ(*t->config().params.get_int("k"), 5);
    EXPECT_TRUE(*t->config().params.get_string("name") == "x");
}

// 完整的 plugin + param_specs 往返测试（使用 image_filtering 的 GaussianBlurTask）
// 该测试依赖 image_filtering 已注册，仅在链接了 image_filtering 时有意义；
// 这里用 IPluginTask 直接构造一个本地声明 specs 的 task 验证机制。

class FloatParamTask : public IPluginTask {
public:
    using IPluginTask::IPluginTask;
    const std::string& type() const override { static std::string t = "float_param_task"; return t; }
    TaskResult execute(TaskContext&) override {
        return TaskResult{.status = TaskStatus::COMPLETED};
    }
    std::vector<ParamSpec> param_specs() const override {
        return {make_float_param("sigma", 0.0f, 0.0, 100.0)};
    }
};

TEST_CASE(deserialize_float_param_with_declared_spec) {
    // 注册 task 类型，让反序列化能 probe 到 param_specs
    PluginRegistry::instance().register_task(
        "float_param_task",
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<FloatParamTask>(id, cfg);
        });

    // JSON 把 sigma 写成整数 2（无小数点）。有声明 spec 时应转成 float，
    // 而非按字面量当 int（导致 get_float 失败）。
    std::string json = R"({
        "version": "2.0",
        "tasks": [{"id": "fp", "type": "float_param_task", "params": {"sigma": 2}}],
        "edges": []
    })";
    DAG dag = DAGSerializer::from_string(json);
    auto t = dag.get_task("fp");
    auto sigma = t->config().params.get_float("sigma");
    EXPECT_TRUE(sigma.has_value());  // 声明为 float，应能取出
    EXPECT_EQ(*sigma, 2.0f);

    PluginRegistry::instance().unregister_task("float_param_task");
}

TEST_MAIN("Parameter Tests")
