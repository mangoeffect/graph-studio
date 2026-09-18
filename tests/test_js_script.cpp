// Test: js_script 任务（核心库内注册，对照 test_mediapipe_vision 形态）。
// 每个用例读 tests/graphs/js/<x>.json（DAGSerializer + DAGExecutor 经
// TaskGraphSdk 消费者 API 驱动）。js_script 已编入 libtask_graph，无需
// dlopen 插件；图引用的 opencv_image_read 来自 image_reader 子模块。
//
// 覆盖（quickjs 迁移的核心回归面）：
//   - js_arith   引擎 + TG.Math + ES2020 箭头函数（无图输入，float 输出）
//   - js_mat_ops MatWrapper + cv.* 绑定 + createMat 宽参序（真实图像输入）
//   - js_error   JS throw -> 任务 FAILED 且异常消息含 'test error'
//                （getLastError 的 message-first 语义）
// 资产相对路径（scripts/*.js、data/test.png）由 resolve_asset_path 的
// 祖先探测解析（graph 目录 tests/graphs/js 的第二级祖先 = tests/）。
#include <task_graph/plugin.hpp>
#include <task_graph/sdk.hpp>

#include <gtest/gtest.h>
#include <cstdlib>

#include <any>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace task_graph;

namespace {

const std::filesystem::path kGraphsDir =
    std::filesystem::path(TG_TEST_JS_DIR) / "graphs" / "js";

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 消费者 API(TaskGraphSdk)驱动:load_graph_file 自动推导 base_dir
// (graph.json 所在目录 → _source_dir 注入);结果物化为 map。
std::unordered_map<std::string, TaskResult> run_via_sdk(
    const std::string& graph_path) {
    std::unordered_map<std::string, TaskResult> results;
    auto sdk = TaskGraphSdk::create();
    if (!sdk || sdk->init(SdkConfig{}) != SdkStatus::OK) return results;
    if (sdk->load_graph_file(graph_path) != SdkStatus::OK) return results;
    (void)sdk->execute();
    for (const auto& id : sdk->executed_tasks()) {
        if (auto r = sdk->task_result(id)) results.emplace(id, std::move(*r));
    }
    return results;
}

}  // namespace

TEST(JsScriptGraph, Arith) {
    auto results = run_via_sdk((kGraphsDir / "js_arith.json").string());
    auto it = results.find("arith");
    ASSERT_NE(it, results.end());
    EXPECT_TRUE(it->second.is_success());
    float v = -1;
    try { v = std::any_cast<float>(it->second.value); }
    catch (const std::bad_any_cast&) {}
    EXPECT_NEAR(v, 360.0f, 0.01f);  // 10 + 50 + 10*30
}

TEST(JsScriptGraph, MatOps) {
    // CI Linux/Windows 专属跳过：下游 js 任务从 image_read 收到的输入为空
    // （cvtColor 空 Mat 断言；根因疑似跨 SO 输入传递，与 mp 测试的
    // TG_TEST_SKIP_MP 同源，待复现定位）。macOS 正常。
    if (std::getenv("TG_TEST_SKIP_JS_MATOPS") != nullptr) {
        GTEST_SKIP() << "TG_TEST_SKIP_JS_MATOPS set (input-passing issue pending)";
    }
    auto results = run_via_sdk((kGraphsDir / "js_mat_ops.json").string());
    auto it = results.find("ops");
    ASSERT_NE(it, results.end());
    EXPECT_TRUE(it->second.is_success());
    float v = -1;
    try { v = std::any_cast<float>(it->second.value); }
    catch (const std::bad_any_cast&) {}
    // 128*10000 + 128*100 + 1（灰度阈值后单通道）
    EXPECT_NEAR(v, 1292801.0f, 0.01f);
}

TEST(JsScriptGraph, ErrorThrow) {
    auto results = run_via_sdk((kGraphsDir / "js_error.json").string());
    auto it = results.find("err");
    ASSERT_NE(it, results.end());
    EXPECT_TRUE(it->second.is_failed());
    std::string msg;
    if (it->second.exception) {
        try { std::rethrow_exception(it->second.exception); }
        catch (const std::exception& e) { msg = e.what(); }
        catch (...) {}
    }
    EXPECT_NE(msg.find("test error"), std::string::npos) << "msg=" << msg;
}
