// tg_e2e_runner — Android 无头 graph E2E runner（GraphStudio 自动化测试的
// Android 轨道，对齐 macOS/WASM E2E 的图执行通道与日志断言契约）。
//
// 为什么是 runner 而不是 app：Android 侧仓库只产出 dist/android/ 静态库 SDK
// （无 Qt for Android 壳，见 dev-docs/e2e-android.md），e2e 的"执行子模块
// 单测图"一侧由本 console 程序承担——与 macOS 的
// `graph_studio --open <json> --run` 同语义：每张图一个干净进程，执行完把
// 结果写进 [gs] 日志行，由 adb 侧的驱动脚本（scripts/run_e2e_android.py）断言。
//
// 输出契约（与桌面 app 的 qInfo "[gs]" 镜像逐字一致——MainWindow::onLogMessage
// / GraphViewModel；驱动侧 FINISHED_RE 三端共用）：
//   [gs] Graph loaded: <N> nodes, <M> edges
//   [gs] <task_id>  (<ms> ms)            # 完成任务（对齐 onExecutionEvent）
//   [gs] <task_id>: <failure_reason>     # 失败任务（对齐 onExecutionEvent）
//   [gs] Run 0 finished: <N> ok, <M> failed (<ms> ms)   # 对齐 onRunSummary
//                                                     # （ff18697 运行模型
//                                                     # 扩展后的新契约）
//
// 退出码：0 = 全部成功；1 = 有任务失败；2 = 图加载失败（软跳过，对齐 ctest
// SKIP_RETURN_CODE 2 的既有惯例）。
//
// 静态注册保活：Android 是 STATIC 核心库 + 静态子模块，__attribute__((constructor))
// 注册的 TU 会被链接器裁剪，CMakeLists 用 --whole-archive 全量拉入（WASM app
// 同款先例）。--selfcheck 把注册结果暴露给驱动做 boot 断言——注册静默失效
// 是本方案最需要防的失败模式。
//
// 用法（设备侧 /data/local/tmp 下）:
//   ./tg_e2e_runner <graph.json> [--threads N]  # 执行一张图（相对资产按图目录解析）
//   ./tg_e2e_runner --selfcheck                 # 打印 ABI + 已注册任务清单
//   ./tg_e2e_runner --test-crash                # SIGSEGV（crash 场景断言）
//

#include <task_graph/data_types.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/executor.hpp>
#include <plugin_api.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 单行输出 + flush：设备侧由驱动重定向到文件再 pull 回主机（adb shell 的
// 管道/pty 缓冲行为不一致，不做行缓冲假设）。
void gs_line(const std::string& s) {
    std::fputs("[gs] ", stdout);
    std::fputs(s.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// 本二进制的 ABI（编译期判定，不依赖设备侧 getprop）——驱动用它核对与设备
// CPU 架构一致（x86_64 模拟器需 build_android.py --also-x86-64）。
std::string abi_name() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

// 已注册任务类型清单：驱动侧比对 subnode.json 里 Android 可跑子模块的任务
// 集合（whole-archive 注册失效时此处缺项）。
int selfcheck() {
    task_graph::detail::TypeRegistry::instance();   // 强制拉入核心库注册 TU
    const auto tasks = task_graph::PluginRegistry::instance().available_tasks();
    gs_line("selfcheck: abi=" + abi_name() + " tasks=" + std::to_string(tasks.size()));
    for (const auto& t : tasks) {
        gs_line("task: " + t);
    }
    return 0;
}

// crash 场景：与桌面 --test-crash 同形（CrashReporter 未构建时的桩路径也是
// std::raise(SIGSEGV)，见 app/graph_studio/src/CrashReporter.cpp）。
int test_crash() {
    std::fflush(stdout);
    std::raise(SIGSEGV);
    return 0;   // 信号默认终止；防御性返回
}

int run_graph(const std::string& path, unsigned threads) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        gs_line("--open failed: " + path + "（文件不存在）");
        return 2;
    }
    std::ostringstream buf;
    buf << in.rdbuf();

    // base_dir = 图所在目录：框架据此注入 _source_dir，图内相对路径
    // （data/test.png、scripts/*.js）由 resolve_asset_path 按图目录 + 两级
    // 祖先解析——与 GraphStudio 打开源码树图的行为一致（驱动侧 stage_copy
    // 保持"图与资产同级"的布局，设备上原样复现）。
    const std::string base_dir = std::filesystem::path(path).parent_path().string();

    task_graph::DAG dag;
    try {
        dag = task_graph::DAGSerializer::from_string(buf.str(), base_dir);
    } catch (const std::exception& e) {
        // 未注册任务类型（插件没链进来）也会走到这里（dag.cpp add_plugin_task
        // throw）——退出码 2 让驱动侧记为 skip 而非 fail。
        gs_line(std::string("--open failed: ") + e.what());
        return 2;
    }

    gs_line("Graph loaded: " + std::to_string(dag.num_tasks()) + " nodes, "
            + std::to_string(dag.num_edges()) + " edges");

    task_graph::ExecutorConfig cfg;
    if (threads > 0) {
        cfg.thread_pool_size = threads;
    }
    cfg.callback = [](const task_graph::ExecutionEvent& e) {
        using Type = task_graph::ExecutionEvent::Type;
        if (e.type == Type::TaskFailed) {
            gs_line(e.task_id + (e.failure_reason.empty()
                                     ? std::string()
                                     : ": " + e.failure_reason));
        } else if (e.type == Type::TaskCompleted) {
            const double ms =
                std::chrono::duration<double, std::milli>(e.duration).count();
            char line[256];
            // 与 GraphViewModel 的 "%1  (%2 ms)" 同形（两个空格、两位小数）
            std::snprintf(line, sizeof(line), "%s  (%.2f ms)", e.task_id.c_str(), ms);
            gs_line(line);
        }
    };

    task_graph::DAGExecutor executor(cfg);
    const auto t0 = std::chrono::steady_clock::now();
    executor.execute(dag);
    executor.wait();
    const auto t1 = std::chrono::steady_clock::now();

    // 统计口径与 GraphViewModel::onRunSummary 一致：is_success() 计 ok，
    // 其余（FAILED/SKIPPED/PENDING）计 failed。
    int ok = 0, failed = 0;
    for (const auto& [id, r] : executor.get_results()) {
        if (r.is_success()) {
            ++ok;
            continue;
        }
        ++failed;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        // 附加诊断行（超出 app 日志契约的增量，不影响 FINISHED_RE）：任务异常
        // 消息只存在 TaskResult::exception 里——executor 的 TaskFailed 事件对
        // "execute() 返回 FAILED" 这条路径不带 reason，app 侧同样看不到。
        // Android 端没有 AX 现场快照/截图，这条是失败定位的主要依据。
        if (r.exception) {
            try {
                std::rethrow_exception(r.exception);
            } catch (const std::exception& e) {
                gs_line("task '" + id + "' failed: " + e.what());
            } catch (...) {
                gs_line("task '" + id + "' failed: (非 std::exception 异常)");
            }
        }
#endif
    }
    // 与 GraphViewModel::onRunSummary 的 "Run %1 finished: %2 ok, %3 failed
    // (%4 ms)" 同形（单次执行 run_index 恒为 0，两位小数）。
    const double total_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    char done_line[128];
    std::snprintf(done_line, sizeof(done_line),
                  "Run 0 finished: %d ok, %d failed (%.2f ms)", ok, failed,
                  total_ms);
    gs_line(done_line);
    return failed == 0 ? 0 : 1;
}

void usage() {
    std::fputs("usage: tg_e2e_runner <graph.json> [--threads N]\n"
               "       tg_e2e_runner --selfcheck\n"
               "       tg_e2e_runner --test-crash\n", stderr);
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 2;
    }
    if (args[0] == "--selfcheck") {
        return selfcheck();
    }
    if (args[0] == "--test-crash") {
        return test_crash();
    }

    unsigned threads = 0;   // 0 = executor 默认（hardware_concurrency）
    std::string graph;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--threads" && i + 1 < args.size()) {
            threads = static_cast<unsigned>(std::stoul(args[++i]));
        } else if (graph.empty()) {
            graph = args[i];
        }
    }
    if (graph.empty()) {
        usage();
        return 2;
    }
    return run_graph(graph, threads);
}
