#ifndef GRAPH_STUDIO_CRASH_REPORTER_H
#define GRAPH_STUDIO_CRASH_REPORTER_H

namespace graph_studio {

// 初始化崩溃上报（Sentry + crashpad，桌面端 macOS/Windows）。
// DSN 来自 SENTRY_DSN 环境变量（或编译期嵌入的 GRAPH_STUDIO_SENTRY_DSN）；
// 未配置 DSN 时保持 no-op，不影响应用运行。
// 应在创建 QApplication 之前尽早调用，以覆盖启动早期崩溃。
void InitCrashReporting();

// 进程退出前关闭崩溃上报，与 InitCrashReporting() 配对。
void ShutdownCrashReporting();

// 是否已成功接入崩溃上报后端（配置了 DSN 且 sentry_init 成功）。
bool IsCrashReportingEnabled();

// 人为触发一次崩溃（SIGSEGV），用于验证 crashpad minidump 能正常上报。
// 仅在崩溃上报已初始化后调用；未初始化时仍会崩溃但无上报。
[[noreturn]] void TriggerTestCrash();

}  // namespace graph_studio

#endif  // GRAPH_STUDIO_CRASH_REPORTER_H