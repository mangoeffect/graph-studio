// 日志系统测试。校验日志级别 get/set roundtrip、级别过滤边界，
// 以及日志 API / 宏在各级别下可安全调用（不崩溃）。
#include <plugin_api.hpp>
#include <task_graph/task_graph.hpp>
#include <gtest/gtest.h>

using namespace task_graph;

TEST(Logger, log_level_get_set_roundtrip) {
    tg_set_log_level(LogLevel::DEBUG);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::DEBUG);

    tg_set_log_level(LogLevel::WARN);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::WARN);

    tg_set_log_level(LogLevel::ERROR);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::ERROR);
}

TEST(Logger, log_level_ordering) {
    // 级别数值单调递增，保证过滤比较正确
    EXPECT_TRUE(static_cast<int>(LogLevel::TRACE) < static_cast<int>(LogLevel::DEBUG));
    EXPECT_TRUE(static_cast<int>(LogLevel::DEBUG) < static_cast<int>(LogLevel::INFO));
    EXPECT_TRUE(static_cast<int>(LogLevel::INFO) < static_cast<int>(LogLevel::WARN));
    EXPECT_TRUE(static_cast<int>(LogLevel::WARN) < static_cast<int>(LogLevel::ERROR));
    EXPECT_TRUE(static_cast<int>(LogLevel::ERROR) < static_cast<int>(LogLevel::FATAL));
}

TEST(Logger, log_api_all_levels_safe) {
    tg_set_log_level(LogLevel::TRACE);
    tg_log(LogLevel::TRACE, "trace");
    tg_log(LogLevel::DEBUG, "debug");
    tg_log(LogLevel::INFO, "info");
    tg_log(LogLevel::WARN, "warn");
    tg_log(LogLevel::ERROR, "error");
    tg_log(LogLevel::FATAL, "fatal");
    // 恢复默认，确认设置生效
    tg_set_log_level(LogLevel::WARN);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::WARN);
}

TEST(Logger, log_macros_safe) {
    tg_set_log_level(LogLevel::TRACE);
    TG_LOG_TRACE("macro trace");
    TG_LOG_DEBUG("macro debug");
    TG_LOG_INFO("macro info");
    TG_LOG_WARN("macro warn");
    TG_LOG_ERROR("macro error");
    TG_LOG_FATAL("macro fatal");
    EXPECT_TRUE(true);  // 到此未崩溃即通过
}

TEST(Logger, add_log_sink_multiple_and_remove) {
    tg_set_log_level(LogLevel::TRACE);
    std::vector<std::string> got_a, got_b;
    LogSinkHandle ha = add_log_sink([&](const LogEntry& e) { got_a.push_back(e.msg); });
    LogSinkHandle hb = add_log_sink([&](const LogEntry& e) { got_b.push_back(e.msg); });
    EXPECT_NE(ha, 0u);
    EXPECT_NE(hb, 0u);
    EXPECT_NE(ha, hb);

    tg_log(LogLevel::INFO, "both");
    remove_log_sink(ha);
    tg_log(LogLevel::INFO, "only-b");
    remove_log_sink(hb);
    tg_log(LogLevel::INFO, "neither");
    // 重复注销是安全的 no-op
    remove_log_sink(ha);

    ASSERT_EQ(got_a.size(), 1u);
    EXPECT_EQ(got_a[0], "both");
    ASSERT_EQ(got_b.size(), 2u);
    EXPECT_EQ(got_b[0], "both");
    EXPECT_EQ(got_b[1], "only-b");
}

TEST(Logger, add_log_sink_coexists_with_set_log_sink) {
    tg_set_log_level(LogLevel::TRACE);
    std::vector<std::string> got_main, got_extra;
    set_log_sink([&](const LogEntry& e) { got_main.push_back(e.msg); });
    LogSinkHandle h = add_log_sink([&](const LogEntry& e) { got_extra.push_back(e.msg); });
    tg_log(LogLevel::WARN, "fanout");
    // 主槽被覆盖不影响附加观察者
    set_log_sink({});
    tg_log(LogLevel::WARN, "after-main-replaced");
    remove_log_sink(h);
    clear_log_sink();

    ASSERT_EQ(got_main.size(), 1u);
    EXPECT_EQ(got_main[0], "fanout");
    ASSERT_EQ(got_extra.size(), 2u);
    EXPECT_EQ(got_extra[1], "after-main-replaced");
}

TEST(Logger, add_log_sink_empty_returns_zero) {
    EXPECT_EQ(add_log_sink(nullptr), 0u);
    remove_log_sink(0);  // no-op，不崩溃
}

