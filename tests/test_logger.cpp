// 日志系统测试。校验日志级别 get/set roundtrip、级别过滤边界，
// 以及日志 API / 宏在各级别下可安全调用（不崩溃）。
#include <plugin_api.hpp>
#include <task_graph/task_graph.hpp>
#include "test_util.hpp"

using namespace task_graph;

TEST_CASE(log_level_get_set_roundtrip) {
    tg_set_log_level(LogLevel::DEBUG);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::DEBUG);

    tg_set_log_level(LogLevel::WARN);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::WARN);

    tg_set_log_level(LogLevel::ERROR);
    EXPECT_TRUE(tg_get_log_level() == LogLevel::ERROR);
}

TEST_CASE(log_level_ordering) {
    // 级别数值单调递增，保证过滤比较正确
    EXPECT_TRUE(static_cast<int>(LogLevel::TRACE) < static_cast<int>(LogLevel::DEBUG));
    EXPECT_TRUE(static_cast<int>(LogLevel::DEBUG) < static_cast<int>(LogLevel::INFO));
    EXPECT_TRUE(static_cast<int>(LogLevel::INFO) < static_cast<int>(LogLevel::WARN));
    EXPECT_TRUE(static_cast<int>(LogLevel::WARN) < static_cast<int>(LogLevel::ERROR));
    EXPECT_TRUE(static_cast<int>(LogLevel::ERROR) < static_cast<int>(LogLevel::FATAL));
}

TEST_CASE(log_api_all_levels_safe) {
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

TEST_CASE(log_macros_safe) {
    tg_set_log_level(LogLevel::TRACE);
    TG_LOG_TRACE("macro trace");
    TG_LOG_DEBUG("macro debug");
    TG_LOG_INFO("macro info");
    TG_LOG_WARN("macro warn");
    TG_LOG_ERROR("macro error");
    TG_LOG_FATAL("macro fatal");
    EXPECT_TRUE(true);  // 到此未崩溃即通过
}

TEST_MAIN("Logger Tests")
