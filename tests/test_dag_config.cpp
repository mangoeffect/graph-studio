// 只读 DAG JSON 配置 API 测试(DagConfigLoader/DagConfig)。
// 覆盖:加载与快照、v1.0 legacy、诊断(语法行列号/schema/语义)、TypeCheck、
//       metadata、load_file/_source_dir、to_dag 与 DAGSerializer 的 golden 等价。
#include <task_graph/task_graph.hpp>
#include <task_graph/dag_config.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/plugin.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace task_graph;

namespace {
const char* kValidV2 = R"({
  "version": "2.0",
  "metadata": { "positions": { "a": { "x": 1, "y": 2 } } },
  "tasks": [
    { "id": "src", "type": "io_input", "params": { "data_type": "task_graph::Image" } },
    { "id": "blur", "type": "gpu_box_blur", "params": { "kernel_size": 3, "sigma": 1.5 } }
  ],
  "edges": [
    { "from": "src", "from_port": "out", "to": "blur", "to_port": "in" }
  ]
})";
}  // namespace

TEST(DagConfigLoad, ValidV2Snapshot) {
    auto r = DagConfigLoader::load_string(kValidV2);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.config.version(), "2.0");
    ASSERT_EQ(r.config.tasks().size(), 2u);
    EXPECT_EQ(r.config.tasks()[0].id, "src");
    EXPECT_EQ(r.config.tasks()[0].type, "io_input");
    EXPECT_EQ(r.config.tasks()[1].params_raw["kernel_size"], 3);
    ASSERT_EQ(r.config.edges().size(), 1u);
    EXPECT_EQ(r.config.edges()[0].from, "src");
    EXPECT_EQ(r.config.edges()[0].to_port, "in");
    EXPECT_EQ(r.config.metadata()["positions"]["a"]["x"], 1);
    EXPECT_NE(r.config.find_task("blur"), nullptr);
    EXPECT_EQ(r.config.find_task("missing"), nullptr);
    // 快照只读:默认 type 缺省规则
    auto r2 = DagConfigLoader::load_string(
        R"({"version":"2.0","tasks":[{"id":"plain"}]})");
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.config.tasks()[0].type, "plain");
}

TEST(DagConfigLoad, LegacyV1Edges) {
    auto r = DagConfigLoader::load_string(
        R"({"version":"1.0","tasks":[{"id":"a"},{"id":"b"}],
            "edges":[{"from":"a","to":"b"}]})");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.config.edges().size(), 1u);
    EXPECT_EQ(r.config.edges()[0].from_port, "out");
    EXPECT_EQ(r.config.edges()[0].to_port, "in");

    DagConfigLoader::Options opts;
    opts.allow_legacy_v1 = false;
    auto r2 = DagConfigLoader::load_string(
        R"({"version":"1.0","tasks":[{"id":"a"}]})", opts);
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.issues.front().stage, DagConfigIssue::Stage::Schema);
}

TEST(DagConfigLoad, SyntaxErrorHasLineCol) {
    // 第 3 行缺逗号 → parse error
    auto r = DagConfigLoader::load_string("{\n  \"version\": \"2.0\"\n  \"tasks\": []\n}");
    ASSERT_FALSE(r.ok());
    ASSERT_EQ(r.issues.size(), 1u);
    const auto& iss = r.issues.front();
    EXPECT_EQ(iss.stage, DagConfigIssue::Stage::Parse);
    EXPECT_GT(iss.line, 0u);
    EXPECT_GT(iss.column, 0u);
    EXPECT_FALSE(iss.message.empty());
}

TEST(DagConfigLoad, SchemaErrorsCollected) {
    // 缺 version + tasks 非数组 + edges 非对象条目:一次报全
    auto r = DagConfigLoader::load_string(
        R"({"tasks": "nope", "edges": [{"from":1}]})");
    EXPECT_FALSE(r.ok());
    EXPECT_GE(r.issues.size(), 2u);
    bool saw_version = false, saw_tasks = false;
    for (const auto& iss : r.issues) {
        EXPECT_EQ(iss.stage, DagConfigIssue::Stage::Schema);
        if (iss.json_pointer == "/version") saw_version = true;
        if (iss.json_pointer == "/tasks") saw_tasks = true;
    }
    EXPECT_TRUE(saw_version);
    EXPECT_TRUE(saw_tasks);

    // duplicate id
    auto r2 = DagConfigLoader::load_string(
        R"({"version":"2.0","tasks":[{"id":"a"},{"id":"a"}]})");
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.issues.front().json_pointer, "/tasks/1/id");

    // 不支持的版本
    auto r3 = DagConfigLoader::load_string(R"({"version":"3.0","tasks":[]})");
    EXPECT_FALSE(r3.ok());
}

TEST(DagConfigLoad, SemanticsErrors) {
    // 悬垂 edge + 3 节点环
    auto r = DagConfigLoader::load_string(
        R"({"version":"2.0","tasks":[{"id":"a"},{"id":"b"},{"id":"c"}],
            "edges":[{"from":"a","to":"zz"},
                     {"from":"a","to":"b"},{"from":"b","to":"c"},{"from":"c","to":"a"}]})");
    EXPECT_FALSE(r.ok());
    bool saw_dangling = false, saw_cycle = false;
    size_t semantics_errors = 0;
    for (const auto& iss : r.issues) {
        // 裸 id 任务同时会产生 unknown-type WARNING(TypeCheck),此处只断言语义层
        if (iss.stage != DagConfigIssue::Stage::Semantics) {
            EXPECT_EQ(iss.severity, DagConfigIssue::Severity::WARNING);
            continue;
        }
        ++semantics_errors;
        EXPECT_EQ(iss.severity, DagConfigIssue::Severity::ERROR);
        if (iss.message.find("unknown task 'zz'") != std::string::npos) saw_dangling = true;
        if (iss.message.find("cycle detected") != std::string::npos) saw_cycle = true;
    }
    EXPECT_TRUE(saw_dangling);
    EXPECT_TRUE(saw_cycle);
    EXPECT_GE(semantics_errors, 2u);
    EXPECT_EQ(r.config.to_dag(), std::nullopt);  // 存在 ERROR → to_dag 拒绝

    // 同 to_port 多写:WARNING,不阻止加载(只看 Semantics 层)
    auto count_semantics_warnings = [](const DagConfigLoadResult& res) {
        size_t n = 0;
        for (const auto& iss : res.issues) {
            if (iss.stage == DagConfigIssue::Stage::Semantics &&
                iss.severity == DagConfigIssue::Severity::WARNING) {
                ++n;
            }
        }
        return n;
    };
    auto r2 = DagConfigLoader::load_string(
        R"({"version":"2.0","tasks":[{"id":"a"},{"id":"b"},{"id":"c"}],
            "edges":[{"from":"a","to":"c"},{"from":"b","to":"c"}]})");
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(count_semantics_warnings(r2), 1u);
    // v1.0 同 (to,"in") 多源:WARNING 且提示升级
    auto r3 = DagConfigLoader::load_string(
        R"({"version":"1.0","tasks":[{"id":"a"},{"id":"b"},{"id":"c"}],
            "edges":[{"from":"a","to":"c"},{"from":"b","to":"c"}]})");
    EXPECT_TRUE(r3.ok());
    ASSERT_EQ(count_semantics_warnings(r3), 1u);
    bool found_upgrade_hint = false;
    for (const auto& iss : r3.issues) {
        if (iss.stage == DagConfigIssue::Stage::Semantics &&
            iss.message.find("upgrade to 2.0") != std::string::npos) {
            found_upgrade_hint = true;
        }
    }
    EXPECT_TRUE(found_upgrade_hint);

    // 语义校验关闭
    DagConfigLoader::Options off;
    off.validate_semantics = false;
    auto r4 = DagConfigLoader::load_string(
        R"({"version":"2.0","tasks":[{"id":"a"}],"edges":[{"from":"a","to":"zz"}]})", off);
    EXPECT_TRUE(r4.ok());
}

TEST(DagConfigLoad, UnknownTypeCheck) {
    const char* j = R"({"version":"2.0","tasks":[{"id":"x","type":"no_such_type"}]})";
    // 默认 WARNING(io_input 等核心类型已注册,未知类型才是 WARNING)
    auto r = DagConfigLoader::load_string(j);
    EXPECT_TRUE(r.ok());
    ASSERT_FALSE(r.issues.empty());
    EXPECT_EQ(r.issues[0].severity, DagConfigIssue::Severity::WARNING);
    EXPECT_EQ(r.issues[0].stage, DagConfigIssue::Stage::TypeCheck);

    // require_known_types → ERROR
    DagConfigLoader::Options opts;
    opts.require_known_types = true;
    auto r2 = DagConfigLoader::load_string(j, opts);
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.issues[0].severity, DagConfigIssue::Severity::ERROR);
}

TEST(DagConfigLoad, FileMissing) {
    auto r = DagConfigLoader::load_file("/nonexistent/path/graph.json");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.issues.front().stage, DagConfigIssue::Stage::Parse);
    EXPECT_NE(r.issues.front().message.find("cannot open"), std::string::npos);
}

TEST(DagConfigLoad, FileBaseDirAndEquivalence) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() /
                   ("tg_dag_config_test_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    fs::path file = dir / "graph.json";
    { std::ofstream out(file); out << kValidV2; }

    auto r = DagConfigLoader::load_file(file);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.config.base_dir(), dir.string());

    // golden 等价:to_dag 与 DAGSerializer::from_string 输出一致
    ASSERT_NE(r.config.to_dag(), std::nullopt);
    auto via_config = DAGSerializer::to_string(*r.config.to_dag());
    auto direct = DAGSerializer::to_string(
        DAGSerializer::from_string(kValidV2, dir.string()));
    EXPECT_EQ(via_config, direct);

    // _source_dir 注入(经 to_dag 生效)
    auto dag = *r.config.to_dag();
    auto src_params = dag.get_task("src")->config().params.get_string("_source_dir");
    ASSERT_TRUE(src_params.has_value());
    EXPECT_EQ(*src_params, dir.string());

    fs::remove_all(dir);
}
