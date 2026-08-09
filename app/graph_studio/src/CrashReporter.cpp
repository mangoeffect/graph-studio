#include "CrashReporter.h"

#include <cstdlib>
#include <cstdio>
#include <string>
#include <filesystem>
#include <csignal>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    sentry_close();
    g_initialized = false;
}

bool IsCrashReportingEnabled() {
    return g_initialized;
}

[[noreturn]] void TriggerTestCrash() {
    if (g_initialized)
        std::fprintf(stderr, "[CrashReporter] 触发测试崩溃 (SIGSEGV)\n");
    else
        std::fprintf(stderr,
                     "[CrashReporter] 未配置崩溃上报，测试崩溃不会上报\n");
    std::raise(SIGSEGV);
    // unreachable
    std::abort();
}

}  // namespace graph_studio