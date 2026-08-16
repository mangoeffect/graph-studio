#include "CrashReporter.h"

#include <cstdlib>
#include <cstdio>
#include <string>
#include <csignal>

#ifdef GRAPH_STUDIO_HAS_SENTRY

#include <plugin_api.hpp>

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// windows.h 会定义宏 ERROR(0)，破坏 task_graph::LogLevel::ERROR（同 logger.cpp）
#undef ERROR
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/param.h>
#else
#include <unistd.h>
#endif

#include <sentry.h>

namespace graph_studio {

namespace {

bool g_initialized = false;
// 日志→breadcrumb 桥接的注销句柄（Shutdown 时移除）。
task_graph::LogSinkHandle g_log_bridge = 0;

std::string get_exe_dir() {
    try {
        std::filesystem::path p;

#if defined(_WIN32)
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH)
            return "";
        p = std::filesystem::path(buf).remove_filename();
#elif defined(__APPLE__)
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0)
            return "";
        // macOS .app bundle：可执行文件位于 Contents/MacOS/，崩溃 handler
        // 会被拷贝到同一目录，紧邻可执行文件。
        p = std::filesystem::path(buf).remove_filename();
#else
        p = std::filesystem::path("/proc/self/exe").parent_path();
#endif

        return p.string();
    } catch (...) {
        return "";
    }
}

std::string get_database_path() {
    try {
        std::filesystem::path dir;
#if defined(_WIN32)
        const char* local = std::getenv("LOCALAPPDATA");
        if (local && *local)
            dir = local;
        else
            dir = std::filesystem::temp_directory_path();
        dir /= "GraphStudio";
#else
        const char* home = std::getenv("HOME");
        if (home && *home)
            dir = home;
        else
            dir = std::filesystem::temp_directory_path();
        dir /= ".graph_studio";
#endif
        dir /= "sentry_db";
        return dir.string();
    } catch (...) {
        return "";
    }
}

std::string get_dsn() {
    if (const char* dsn = std::getenv("SENTRY_DSN"))
        if (*dsn)
            return dsn;
#ifdef GRAPH_STUDIO_SENTRY_DSN
    return GRAPH_STUDIO_SENTRY_DSN;
#else
    return "";
#endif
}

std::string get_release() {
    std::string release = "task_graph";
#ifdef GRAPH_STUDIO_SENTRY_VERSION
    release += "@" GRAPH_STUDIO_SENTRY_VERSION;
#else
    release += "@0.0.0";
#endif
#ifdef GRAPH_STUDIO_SENTRY_BUILD_HASH
    release += std::string("#") + GRAPH_STUDIO_SENTRY_BUILD_HASH;
#endif
    return release;
}

std::string get_environment() {
#ifdef GRAPH_STUDIO_SENTRY_ENVIRONMENT
    return GRAPH_STUDIO_SENTRY_ENVIRONMENT;
#else
    return "development";
#endif
}

std::string get_handler_path() {
    std::string exe_dir = get_exe_dir();
    if (exe_dir.empty())
        return "";
    std::string handler = "crashpad_handler";
#ifdef _WIN32
    handler += ".exe";
#endif
    return (std::filesystem::path(exe_dir) / handler).string();
}

// 日志级别 → sentry breadcrumb level 字符串。
// breadcrumb 的 ring buffer 有配额，只桥接 WARN 及以上，避免刷掉有效线索。
const char* breadcrumb_level(task_graph::LogLevel level) {
    switch (level) {
        case task_graph::LogLevel::WARN:  return "warning";
        case task_graph::LogLevel::ERROR: return "error";
        case task_graph::LogLevel::FATAL: return "fatal";
        default:                          return "info";
    }
}

// 把框架日志桥接为 sentry breadcrumb（附加观察者 sink，不占用 GUI 主槽）。
// 仅在普通执行路径回调（非信号上下文），sentry_add_breadcrumb 内部加锁安全。
void bridge_logs_to_breadcrumbs() {
    g_log_bridge = task_graph::add_log_sink([](const task_graph::LogEntry& entry) {
        if (entry.level < task_graph::LogLevel::WARN)
            return;
        sentry_value_t crumb = sentry_value_new_object();
        sentry_value_set_by_key(crumb, "category",
                                sentry_value_new_string("log"));
        sentry_value_set_by_key(crumb, "level",
                                sentry_value_new_string(breadcrumb_level(entry.level)));
        if (!entry.filename.empty()) {
            std::string loc = entry.filename + ":" + std::to_string(entry.line);
            sentry_value_set_by_key(crumb, "data",
                                    sentry_value_new_string(loc.c_str()));
        }
        sentry_value_set_by_key(crumb, "message",
                                sentry_value_new_string(entry.msg.c_str()));
        sentry_add_breadcrumb(crumb);
    });
}

}  // namespace

void InitCrashReporting() {
    if (g_initialized)
        return;

    std::string dsn = get_dsn();
    if (dsn.empty()) {
        std::fprintf(stderr,
                     "[CrashReporter] SENTRY_DSN 未设置，崩溃上报关闭 (no-op)\n");
        return;
    }

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, dsn.c_str());
    sentry_options_set_environment(options, get_environment().c_str());
    sentry_options_set_release(options, get_release().c_str());

    std::string handler_path = get_handler_path();
    if (!handler_path.empty())
        sentry_options_set_handler_path(options, handler_path.c_str());

    std::string db_path = get_database_path();
    if (!db_path.empty())
        sentry_options_set_database_path(options, db_path.c_str());

    if (std::getenv("SENTRY_DEBUG"))
        sentry_options_set_debug(options, 1);

    // 与 GpuBootstrap 一致：init 失败仅告警不阻断 GUI。
    if (sentry_init(options) == 0) {
        g_initialized = true;
        bridge_logs_to_breadcrumbs();
        std::fprintf(stderr,
                     "[CrashReporter] initialized (%s; handler=%s; db=%s)\n",
                     get_release().c_str(), handler_path.c_str(), db_path.c_str());
    } else {
        std::fprintf(stderr, "[CrashReporter] sentry_init 失败，崩溃上报不可用\n");
    }
}

void ShutdownCrashReporting() {
    if (!g_initialized)
        return;
    if (g_log_bridge != 0) {
        task_graph::remove_log_sink(g_log_bridge);
        g_log_bridge = 0;
    }
    sentry_close();
    g_initialized = false;
}

bool IsCrashReportingEnabled() {
    return g_initialized;
}

void SetGraphContext(const std::string& file, int node_count, int edge_count) {
    if (!g_initialized)
        return;
    sentry_value_t ctx = sentry_value_new_object();
    // 只放文件名：完整路径会把用户目录结构上报到 Sentry（隐私）。
    sentry_value_set_by_key(ctx, "file", sentry_value_new_string(file.c_str()));
    sentry_value_set_by_key(ctx, "node_count", sentry_value_new_int32(node_count));
    sentry_value_set_by_key(ctx, "edge_count", sentry_value_new_int32(edge_count));
    // sentry_set_context 拷贝进全局 scope，重复调用直接替换旧值。
    sentry_set_context("graph", ctx);
}

void AddExecutionBreadcrumb(const std::string& task, const std::string& message) {
    if (!g_initialized)
        return;
    sentry_value_t crumb = sentry_value_new_object();
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string("execution"));
    sentry_value_set_by_key(crumb, "level", sentry_value_new_string("error"));
    std::string msg = task + ": " + message;
    sentry_value_set_by_key(crumb, "message", sentry_value_new_string(msg.c_str()));
    sentry_add_breadcrumb(crumb);
}

[[noreturn]] void TriggerTestCrash() {
    if (g_initialized)
        std::fprintf(stderr, "[CrashReporter] 触发测试崩溃 (SIGSEGV)\n");
    else
        std::fprintf(stderr,
                     "[CrashReporter] 未配置崩溃上报，测试崩溃不会上报\n");
#ifdef _WIN32
    // MSVC CRT 对 raise(SIGSEGV) 的默认处理是直接 _exit(3)，不会产生真正的
    // SEH 异常，crashpad 捕获不到（无 minidump）。用空指针解引用触发真实
    // EXCEPTION_ACCESS_VIOLATION；volatile 防止优化器删掉该解引用。
    {
        volatile int* p = nullptr;
        *p = 42;
    }
#else
    std::raise(SIGSEGV);
#endif
    // unreachable
    std::abort();
}

}  // namespace graph_studio

#else  // !GRAPH_STUDIO_HAS_SENTRY

// 未拉取 sentry-native 时的 no-op 桩：保持调用方（entry.cpp / GraphViewModel）
// 无需 #ifdef 分支。TriggerTestCrash 仍保证"确实崩"以便脚本测试退化为 abort 路径。
namespace graph_studio {

void InitCrashReporting() {}
void ShutdownCrashReporting() {}
bool IsCrashReportingEnabled() { return false; }
void SetGraphContext(const std::string&, int, int) {}
void AddExecutionBreadcrumb(const std::string&, const std::string&) {}

[[noreturn]] void TriggerTestCrash() {
    std::fprintf(stderr, "[CrashReporter] 未构建崩溃上报（sentry-native 缺失），直接 abort\n");
    std::abort();
}

}  // namespace graph_studio

#endif  // GRAPH_STUDIO_HAS_SENTRY
