#ifndef GRAPH_STUDIO_CRASH_REPORTER_H
#define GRAPH_STUDIO_CRASH_REPORTER_H

#include <string>

namespace graph_studio {

// 初始化崩溃上报（Sentry + crashpad，桌面端 macOS/Windows/Linux）。
// DSN 来自 SENTRY_DSN 环境变量（或编译期嵌入的 GRAPH_STUDIO_SENTRY_DSN）；
// 未配置 DSN 时保持 no-op，不影响应用运行。
// 应在创建 QApplication 之前尽早调用，以覆盖启动早期崩溃。
//
// 初始化成功后自动把框架日志（WARN 及以上）桥接为 sentry breadcrumb，
// 崩溃事件随之携带最近的告警/错误记录。
void InitCrashReporting();

// 进程退出前关闭崩溃上报，与 InitCrashReporting() 配对。
void ShutdownCrashReporting();

// 是否已成功接入崩溃上报后端（配置了 DSN 且 sentry_init 成功）。
bool IsCrashReportingEnabled();

// 更新崩溃时的图上下文（当前图文件名、节点数、边数），随崩溃事件附带，
// 用于定位"崩在哪个图/多大规模的图"。未启用崩溃上报时为 no-op。
// 建议在加载/保存图后从 UI 线程调用。
void SetGraphContext(const std::string& file, int node_count, int edge_count);

// 追加一条执行期 breadcrumb（如任务执行失败：task 名 + 失败原因），
// 崩溃时随最近 breadcrumb 链附带。未启用崩溃上报时为 no-op。
void AddExecutionBreadcrumb(const std::string& task, const std::string& message);

// 人为触发一次真实崩溃（Windows：空指针解引用产生 EXCEPTION_ACCESS_VIOLATION；
// POSIX：SIGSEGV），用于验证 crashpad minidump 能正常上报。注意 MSVC CRT 对
// raise(SIGSEGV) 的默认处理是 _exit(3)，不产生 SEH 异常，crashpad 捕获不到。
// 仅在崩溃上报已初始化后调用；未初始化时仍会崩溃但无上报。
[[noreturn]] void TriggerTestCrash();

}  // namespace graph_studio

#endif  // GRAPH_STUDIO_CRASH_REPORTER_H
