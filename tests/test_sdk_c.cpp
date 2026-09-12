// 纯 C API(tg_sdk_c.h)功能测试。调用方是 C++(gtest),但只经 C 接口操作。
// 覆盖:生命周期、加载诊断(行列号/缓冲拷贝语义)、类型化绑定/拉取、图像
//       句柄、env/globals、推模式通知回调、异步执行、diff 报告。
#include <task_graph/tg_sdk_c.h>
#include <task_graph/plugin.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace task_graph;

// ====================== 测试任务(注册进 PluginRegistry) ======================

namespace {

class CAppendTask final : public INode {
public:
    CAppendTask(const std::string& id, const TaskConfig& cfg) : INode(id, cfg) {}
    const std::string& type() const override {
        static const std::string t{"c_test_append"};
        return t;
    }
    std::vector<PortSpec> input_specs() const override {
        return {PortSpec{"in", "std::string", true}};
    }
    std::vector<PortSpec> output_specs() const override {
        return {PortSpec{"out", "std::string", true}};
    }
    std::vector<ParamSpec> param_specs() const override {
        ParamSpec s;
        s.name = "suffix";
        s.type = ParamType::String;
        s.default_value = std::string("");
        return {s};
    }
    TaskResult execute(TaskContext& ctx) override {
        auto in = ctx.input<std::string>("in");
        if (!in) return TaskResult{.status = TaskStatus::FAILED};
        auto suffix = config().params.get_string("suffix").value_or("");
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = *in + suffix;
        return r;
    }
};

class CImageInfoTask final : public INode {
public:
    CImageInfoTask(const std::string& id, const TaskConfig& cfg) : INode(id, cfg) {}
    const std::string& type() const override {
        static const std::string t{"c_test_image_info"};
        return t;
    }
    std::vector<PortSpec> input_specs() const override {
        return {PortSpec{"in", "task_graph::Image", true}};
    }
    std::vector<PortSpec> output_specs() const override {
        return {PortSpec{"out", "int", true}};
    }
    TaskResult execute(TaskContext& ctx) override {
        auto in = ctx.input<Image>("in");
        if (!in) return TaskResult{.status = TaskStatus::FAILED};
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = in->width * in->height;
        return r;
    }
};

void register_c_test_tasks_once() {
    static bool done = [] {
        auto& reg = PluginRegistry::instance();
        reg.register_task("c_test_append",
            [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                return std::make_shared<CAppendTask>(id, cfg);
            });
        reg.register_task("c_test_image_info",
            [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                return std::make_shared<CImageInfoTask>(id, cfg);
            });
        return true;
    }();
    (void)done;
}

const char* kCPipeline = R"({
  "version": "2.0",
  "tasks": [
    { "id": "src",  "type": "io_input", "params": { "data_type": "std::string" } },
    { "id": "proc", "type": "c_test_append", "params": { "suffix": "-C" } },
    { "id": "dst",  "type": "io_output" }
  ],
  "edges": [
    { "from": "src",  "from_port": "out", "to": "proc", "to_port": "in" },
    { "from": "proc", "from_port": "out", "to": "dst",  "to_port": "in" }
  ]
})";

}  // namespace

// ====================== 测试 ======================

TEST(CApi, Lifecycle) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_NE(sdk, nullptr);
    EXPECT_EQ(tg_sdk_is_initialized(sdk), 0);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_ERR_NOT_INITIALIZED);

    tg_sdk_config cfg;
    cfg.log_level = 2;  // INFO
    cfg.log_callback = nullptr;
    cfg.log_user_data = nullptr;
    cfg.thread_pool_size = 2;
    cfg.default_timeout_ms = 0;
    cfg.enable_profiling = 0;
    cfg.require_known_types = 0;
    ASSERT_EQ(tg_sdk_init(sdk, &cfg), TG_OK);
    EXPECT_EQ(tg_sdk_is_initialized(sdk), 1);
    EXPECT_EQ(tg_sdk_init(sdk, &cfg), TG_ERR_ALREADY_INITIALIZED);

    tg_sdk_destroy(sdk);  // = shutdown + free,幂等
}

TEST(CApi, LoadIssuesWithLineCol) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);

    EXPECT_EQ(tg_sdk_load_graph_json(sdk, "{ bad json"), TG_ERR_GRAPH_INVALID);
    int n = tg_sdk_issue_count(sdk);
    ASSERT_GE(n, 1);
    int severity = -1, stage = -1;
    int64_t line = 0, column = 0;
    char msg[256] = {0};
    ASSERT_EQ(tg_sdk_issue(sdk, 0, &severity, &stage, nullptr, 0,
                           &line, &column, msg, sizeof(msg)),
              TG_OK);
    EXPECT_EQ(stage, 0);  // Parse
    EXPECT_GT(line, 0);
    EXPECT_GT(column, 0);
    EXPECT_GT(std::string(msg).size(), 0u);
    // 越界
    EXPECT_EQ(tg_sdk_issue(sdk, n, nullptr, nullptr, nullptr, 0,
                           nullptr, nullptr, nullptr, 0),
              TG_ERR_INVALID_ARGUMENT);
    tg_sdk_destroy(sdk);
}

TEST(CApi, BindExecuteTypedOutputs) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, kCPipeline), TG_OK);

    // 边界枚举
    ASSERT_EQ(tg_sdk_input_node_count(sdk), 1);
    EXPECT_STREQ(tg_sdk_input_node(sdk, 0), "src");
    EXPECT_EQ(tg_sdk_input_node(sdk, 1), nullptr);
    ASSERT_EQ(tg_sdk_output_node_count(sdk), 1);
    EXPECT_STREQ(tg_sdk_output_node(sdk, 0), "dst");

    // 未绑定
    EXPECT_EQ(tg_sdk_execute(sdk), TG_ERR_INVALID_ARGUMENT);
    EXPECT_NE(std::string(tg_sdk_last_error(sdk)), "");

    // 类型不匹配(data_type 声明 std::string,绑 int)
    EXPECT_EQ(tg_sdk_bind_input_int(sdk, "src", 42), TG_ERR_TYPE_MISMATCH);

    // 字符串绑定 + 执行 + 拷贝式拉取
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "hello"), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);

    char buf[5] = {0};             // 故意不足:只能装 4 字符
    int needed = tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_EQ(needed, 8);          // "hello-C\0"
    EXPECT_STREQ(buf, "hell");     // 截断且 NUL 结尾
    std::vector<char> big(static_cast<size_t>(needed));
    EXPECT_EQ(tg_sdk_get_output_string(sdk, "dst", big.data(), needed), 8);
    EXPECT_STREQ(big.data(), "hello-C");

    // 类型不匹配的拉取
    int32_t iv = 0;
    EXPECT_EQ(tg_sdk_get_output_int(sdk, "dst", &iv), TG_ERR_TYPE_MISMATCH);

    // 换绑复用
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "again"), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);
    EXPECT_EQ(tg_sdk_get_output_string(sdk, "dst", big.data(),
                                       static_cast<int>(big.size())), 8);
    EXPECT_STREQ(big.data(), "again-C");
    tg_sdk_destroy(sdk);
}

TEST(CApi, ImageHandleRoundtrip) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, R"({
      "version": "2.0",
      "tasks": [
        { "id": "src", "type": "io_input", "params": { "data_type": "task_graph::Image" } },
        { "id": "info", "type": "c_test_image_info" },
        { "id": "dst", "type": "io_output" }
      ],
      "edges": [
        { "from": "src", "from_port": "out", "to": "info", "to_port": "in" },
        { "from": "info", "from_port": "out", "to": "dst", "to_port": "in" }
      ]
    })"), TG_OK);

    // 创建 4x8 RGBA 图像,首像素写魔数
    uint8_t pixels[4 * 8 * 4];
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = static_cast<uint8_t>(i);
    tg_image* img = tg_image_create(4, 8, 4, 4 /*RGBA*/, 0 /*UINT8*/, pixels);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(tg_image_width(img), 4);
    EXPECT_EQ(tg_image_height(img), 8);
    EXPECT_EQ(tg_image_channels(img), 4);
    EXPECT_EQ(tg_image_data_size(img), sizeof(pixels));
    ASSERT_NE(tg_image_data(img), nullptr);
    EXPECT_EQ(tg_image_data(img)[0], pixels[0]);      // 深拷贝,值相同
    EXPECT_NE(tg_image_data(img), pixels);            // 但不是原缓冲

    ASSERT_EQ(tg_sdk_bind_input_image(sdk, "src", img), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);

    int32_t out = 0;
    ASSERT_EQ(tg_sdk_get_output_int(sdk, "dst", &out), TG_OK);
    EXPECT_EQ(out, 32);  // 4*8

    tg_image_destroy(img);
    tg_sdk_destroy(sdk);
}

TEST(CApi, PushNotificationAndGlobals) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    tg_sdk_config cfg;
    cfg.log_level = 2;
    cfg.log_callback = nullptr;
    cfg.log_user_data = nullptr;
    cfg.thread_pool_size = 1;
    cfg.default_timeout_ms = 0;
    cfg.enable_profiling = 0;
    cfg.require_known_types = 0;
    ASSERT_EQ(tg_sdk_init(sdk, &cfg), TG_OK);

    // env 走 _env 前缀全局;graph 直接读 global 值的任务路径经 c_test_append
    ASSERT_EQ(tg_sdk_set_env(sdk, "DEVICE", "metal"), TG_OK);
    ASSERT_EQ(tg_sdk_set_global_string(sdk, "greeting", "hi"), TG_OK);
    ASSERT_EQ(tg_sdk_set_global_int(sdk, "answer", 42), TG_OK);

    ASSERT_EQ(tg_sdk_load_graph_json(sdk, kCPipeline), TG_OK);
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "go"), TG_OK);

    // 推模式:通知回调(数据另行拉取)
    static std::vector<std::string> notified;
    ASSERT_EQ(tg_sdk_bind_output(sdk, "dst",
                 [](void*, const char* node) { notified.emplace_back(node); }, nullptr),
              TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);
    ASSERT_EQ(notified.size(), 1u);
    EXPECT_EQ(notified[0], "dst");
    char buf[32];
    tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_STREQ(buf, "go-C");
    tg_sdk_destroy(sdk);
}

TEST(CApi, AsyncExecute) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, kCPipeline), TG_OK);
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "async"), TG_OK);

    ASSERT_EQ(tg_sdk_execute_async(sdk), TG_OK);
    // 未 wait 再发 → BUSY
    EXPECT_EQ(tg_sdk_execute_async(sdk), TG_ERR_BUSY);
    int st = -1;
    ASSERT_EQ(tg_sdk_execute_wait(sdk, &st), TG_OK);
    EXPECT_EQ(st, TG_OK);
    // wait 收割后再发允许
    ASSERT_EQ(tg_sdk_execute_async(sdk), TG_OK);
    ASSERT_EQ(tg_sdk_execute_wait(sdk, &st), TG_OK);
    EXPECT_EQ(st, TG_OK);
    // 无未决执行时 wait → INVALID_ARGUMENT
    EXPECT_EQ(tg_sdk_execute_wait(sdk, &st), TG_ERR_INVALID_ARGUMENT);

    char buf[32];
    tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_STREQ(buf, "async-C");
    tg_sdk_destroy(sdk);
}

TEST(CApi, DiffReport) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, kCPipeline), TG_OK);

    // params-only 更新
    std::string updated = R"({
      "version": "2.0",
      "tasks": [
        { "id": "src",  "type": "io_input", "params": { "data_type": "std::string" } },
        { "id": "proc", "type": "c_test_append", "params": { "suffix": "-D" } },
        { "id": "dst",  "type": "io_output" }
      ],
      "edges": [
        { "from": "src",  "from_port": "out", "to": "proc", "to_port": "in" },
        { "from": "proc", "from_port": "out", "to": "dst",  "to_port": "in" }
      ]
    })";
    ASSERT_EQ(tg_sdk_update_graph_json(sdk, updated.c_str()), TG_OK);
    EXPECT_EQ(tg_sdk_diff_count(sdk, 2), 1);                       // updated
    EXPECT_EQ(tg_sdk_diff_count(sdk, 0), 0);                       // added
    EXPECT_STREQ(tg_sdk_diff_item(sdk, 2, 0), "proc");
    EXPECT_EQ(tg_sdk_diff_topology_changed(sdk), 0);

    // 坏 JSON:旧图保留
    EXPECT_EQ(tg_sdk_update_graph_json(sdk, "{ nope"), TG_ERR_GRAPH_INVALID);
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "x"), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);
    char buf[32];
    tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_STREQ(buf, "x-D");
    tg_sdk_destroy(sdk);
}

// 头文件 C 编译冒烟(定义在 sdk_c_header_check.c)
extern "C" int tg_c_header_smoke(tg_sdk* sdk, tg_image* img);
TEST(CApi, HeaderIsPlainC) {
    register_c_test_tasks_once();
    tg_sdk* sdk = tg_sdk_create();
    tg_image* img = tg_image_create(1, 1, 1, 1, 0, nullptr);
    EXPECT_GE(tg_c_header_smoke(sdk, img), 0);
    tg_image_destroy(img);
    tg_sdk_destroy(sdk);
}

// ====================== C 自定义任务注册 ======================

namespace {
// 字符串加工 C 任务:param "suffix" + 输入 "in"
int c_greet_execute(tg_task_ctx* ctx) {
    const char* in = tg_task_ctx_input_string(ctx, "in");
    const char* suffix = tg_task_ctx_param_string(ctx, "suffix");
    if (!in) return -1;
    std::string out = std::string(in) + (suffix ? suffix : "");
    tg_task_ctx_set_output_string(ctx, out.c_str());
    return 0;
}
// 图像面积 C 任务:输入 Image → 输出 int
int c_area_execute(tg_task_ctx* ctx) {
    const tg_image* img = tg_task_ctx_input_image(ctx, "in");
    if (!img) return -1;
    tg_task_ctx_set_output_int(ctx, tg_image_width(img) * tg_image_height(img));
    return 0;
}
// 双端口输入 C 任务(parallel 形态):int + string → string
int c_report_execute(tg_task_ctx* ctx) {
    int32_t health = tg_task_ctx_input_int(ctx, "health");
    const char* analysis = tg_task_ctx_input_string(ctx, "analysis");
    std::string out = "health=" + std::to_string(health) +
                      " analysis=" + (analysis ? analysis : "?");
    tg_task_ctx_set_output_string(ctx, out.c_str());
    return 0;
}
}  // namespace

TEST(CApi, RegisterCTaskStringPipeline) {
    register_c_test_tasks_once();
    static const char* in_ports[] = {"in", nullptr};
    static const char* out_ports[] = {"out", nullptr};
    static const char* param_names[] = {"suffix"};
    static const int param_types[] = {2 /*string*/};
    ASSERT_EQ(tg_register_c_task("c_greet", c_greet_execute, in_ports, out_ports,
                                 param_names, param_types, 1),
              TG_OK);
    // 重复注册同名
    EXPECT_EQ(tg_register_c_task("c_greet", c_greet_execute, in_ports, out_ports,
                                 nullptr, nullptr, 0),
              TG_ERR_INVALID_ARGUMENT);

    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, R"({
      "version": "2.0",
      "tasks": [
        { "id": "src", "type": "io_input", "params": { "data_type": "std::string" } },
        { "id": "g", "type": "c_greet", "params": { "suffix": "!" } },
        { "id": "dst", "type": "io_output" }
      ],
      "edges": [
        { "from": "src", "from_port": "out", "to": "g", "to_port": "in" },
        { "from": "g", "from_port": "out", "to": "dst", "to_port": "in" }
      ]
    })"), TG_OK);
    ASSERT_EQ(tg_sdk_bind_input_string(sdk, "src", "hi"), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);
    char buf[32];
    tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_STREQ(buf, "hi!");

    tg_sdk_destroy(sdk);
    ASSERT_EQ(tg_unregister_c_task("c_greet"), TG_OK);
    EXPECT_EQ(tg_unregister_c_task("c_greet"), TG_ERR_INVALID_ARGUMENT);
}

TEST(CApi, RegisterCTaskImageAndPorts) {
    register_c_test_tasks_once();
    static const char* in_ports[] = {"in", nullptr};
    static const char* report_in[] = {"health", "analysis", nullptr};
    static const char* out_ports[] = {"out", nullptr};

    ASSERT_EQ(tg_register_c_task("c_area", c_area_execute, in_ports, out_ports,
                                 nullptr, nullptr, 0), TG_OK);
    ASSERT_EQ(tg_register_c_task("c_report", c_report_execute, report_in, out_ports,
                                 nullptr, nullptr, 0), TG_OK);

    tg_sdk* sdk = tg_sdk_create();
    ASSERT_EQ(tg_sdk_init(sdk, nullptr), TG_OK);
    ASSERT_EQ(tg_sdk_load_graph_json(sdk, R"({
      "version": "2.0",
      "tasks": [
        { "id": "img_src", "type": "io_input", "params": { "data_type": "task_graph::Image" } },
        { "id": "area", "type": "c_area" },
        { "id": "report", "type": "c_report" },
        { "id": "dst", "type": "io_output" }
      ],
      "edges": [
        { "from": "img_src", "from_port": "out", "to": "area", "to_port": "in" },
        { "from": "area", "from_port": "out", "to": "report", "to_port": "health" },
        { "from": "img_src", "from_port": "out", "to": "report", "to_port": "analysis" },
        { "from": "report", "from_port": "out", "to": "dst", "to_port": "in" }
      ]
    })"), TG_OK);

    // 注意:report 的 analysis 端口收到的是 Image(类型不符读出 NULL → "?")
    tg_image* img = tg_image_create(6, 7, 3, 3 /*RGB*/, 0, nullptr);
    ASSERT_NE(img, nullptr);
    ASSERT_EQ(tg_sdk_bind_input_image(sdk, "img_src", img), TG_OK);
    EXPECT_EQ(tg_sdk_execute(sdk), TG_OK);
    char buf[64];
    tg_sdk_get_output_string(sdk, "dst", buf, sizeof(buf));
    EXPECT_STREQ(buf, "health=42 analysis=?");
    tg_image_destroy(img);
    tg_sdk_destroy(sdk);

    ASSERT_EQ(tg_unregister_c_task("c_area"), TG_OK);
    ASSERT_EQ(tg_unregister_c_task("c_report"), TG_OK);
}
