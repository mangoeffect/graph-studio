#include <task_graph/profiler.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace task_graph {

namespace {

const char* phase_to_string(ProfilePhase phase) {
    switch (phase) {
        case ProfilePhase::READY:     return "READY";
        case ProfilePhase::STARTED:   return "STARTED";
        case ProfilePhase::COMPLETED: return "COMPLETED";
        case ProfilePhase::FAILED:    return "FAILED";
        case ProfilePhase::SKIPPED:   return "SKIPPED";
    }
    return "UNKNOWN";
}

const char* status_to_string(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING:   return "PENDING";
        case TaskStatus::RUNNING:   return "RUNNING";
        case TaskStatus::COMPLETED: return "COMPLETED";
        case TaskStatus::FAILED:    return "FAILED";
        case TaskStatus::SKIPPED:   return "SKIPPED";
    }
    return "UNKNOWN";
}

// 纳秒 → 毫秒（双精度），便于 JSON 可读
double ns_to_ms(std::chrono::nanoseconds ns) {
    return std::chrono::duration<double, std::milli>(ns).count();
}

}  // namespace

void ProfileCollector::on_task_event(const TaskProfileEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    raw_events_.push_back(event);
}

void ProfileCollector::on_dag_event(const DagProfileEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.phase == DagProfilePhase::STARTED) {
        dag_start_ = event;
    } else {
        dag_end_ = event;
    }
}

std::vector<TaskStats> ProfileCollector::compute_task_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 按 task_id 聚合事件
    std::unordered_map<std::string, TaskStats> stats_map;
    for (const auto& ev : raw_events_) {
        auto& s = stats_map[ev.task_id];
        s.task_id = ev.task_id;
        s.task_type = ev.task_type;
        switch (ev.phase) {
            case ProfilePhase::READY:
                s.ready_time = ev.timestamp;
                s.has_ready = true;
                break;
            case ProfilePhase::STARTED:
                s.start_time = ev.timestamp;
                s.has_start = true;
                break;
            case ProfilePhase::COMPLETED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::COMPLETED;
                s.exec_duration = ev.duration;
                break;
            case ProfilePhase::FAILED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::FAILED;
                s.exec_duration = ev.duration;
                break;
            case ProfilePhase::SKIPPED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::SKIPPED;
                break;
        }
    }

    // 计算等待耗时与总耗时
    std::vector<TaskStats> result;
    result.reserve(stats_map.size());
    for (auto& [id, s] : stats_map) {
        if (s.has_ready && s.has_start) {
            s.wait_duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(s.start_time - s.ready_time);
        }
        if (s.has_ready && s.has_end) {
            s.total_duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(s.end_time - s.ready_time);
        }
        result.push_back(std::move(s));
    }

    // 按 total_duration 降序排列，便于快速定位热点任务
    std::sort(result.begin(), result.end(),
              [](const TaskStats& a, const TaskStats& b) { return a.total_duration > b.total_duration; });
    return result;
}

DagStats ProfileCollector::compute_dag_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    DagStats stats;
    if (dag_start_) {
        stats.start_time = dag_start_->timestamp;
        stats.has_start = true;
        stats.total_tasks = dag_start_->total_tasks;
    }
    if (dag_end_) {
        stats.end_time = dag_end_->timestamp;
        stats.has_end = true;
    }
    if (stats.has_start && stats.has_end) {
        stats.total_duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stats.end_time - stats.start_time);
    }

    // 统计任务完成/失败/跳过数量
    for (const auto& ev : raw_events_) {
        if (ev.phase == ProfilePhase::COMPLETED)      stats.completed_tasks++;
        else if (ev.phase == ProfilePhase::FAILED)    stats.failed_tasks++;
        else if (ev.phase == ProfilePhase::SKIPPED)   stats.skipped_tasks++;
    }

    // 关键路径：所有任务中最大的 total_duration 近似（精确版本需依赖图 DP）
    auto task_stats = compute_task_stats_impl();
    for (const auto& ts : task_stats) {
        if (ts.total_duration > stats.critical_path) {
            stats.critical_path = ts.total_duration;
        }
    }

    return stats;
}

std::string ProfileCollector::to_json_string(bool pretty) const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json j;

    // DAG 概览
    nlohmann::json overview;
    overview["total_tasks"] = dag_start_ ? dag_start_->total_tasks : 0;
    if (dag_start_ && dag_end_) {
        overview["total_duration_ms"] = ns_to_ms(
            std::chrono::duration_cast<std::chrono::nanoseconds>(dag_end_->timestamp - dag_start_->timestamp));
    }

    size_t completed = 0, failed = 0, skipped = 0;
    for (const auto& ev : raw_events_) {
        if (ev.phase == ProfilePhase::COMPLETED)      completed++;
        else if (ev.phase == ProfilePhase::FAILED)    failed++;
        else if (ev.phase == ProfilePhase::SKIPPED)   skipped++;
    }
    overview["completed_tasks"] = completed;
    overview["failed_tasks"] = failed;
    overview["skipped_tasks"] = skipped;

    j["dag_overview"] = overview;

    // 每个任务的聚合统计
    auto task_stats = compute_task_stats_impl();
    nlohmann::json tasks_json = nlohmann::json::array();
    for (const auto& ts : task_stats) {
        nlohmann::json t;
        t["task_id"] = ts.task_id;
        t["task_type"] = ts.task_type;
        t["final_status"] = status_to_string(ts.final_status);
        t["wait_duration_ms"] = ns_to_ms(ts.wait_duration);
        t["exec_duration_ms"] = ns_to_ms(ts.exec_duration);
        t["total_duration_ms"] = ns_to_ms(ts.total_duration);
        tasks_json.push_back(std::move(t));
    }
    j["tasks"] = tasks_json;

    // 原始事件流（便于程序消费 / 可视化时间轴）
    nlohmann::json events_json = nlohmann::json::array();
    for (const auto& ev : raw_events_) {
        nlohmann::json e;
        e["task_id"] = ev.task_id;
        e["task_type"] = ev.task_type;
        e["phase"] = phase_to_string(ev.phase);
        e["timestamp_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
            ev.timestamp.time_since_epoch()).count();
        if (ev.phase == ProfilePhase::COMPLETED || ev.phase == ProfilePhase::FAILED) {
            e["duration_ms"] = ns_to_ms(ev.duration);
        }
        events_json.push_back(std::move(e));
    }
    j["events"] = events_json;

    return pretty ? j.dump(2) : j.dump();
}

// 内部实现：不加锁版本（已在外层加锁）
std::vector<TaskStats> ProfileCollector::compute_task_stats_impl() const {
    std::unordered_map<std::string, TaskStats> stats_map;
    for (const auto& ev : raw_events_) {
        auto& s = stats_map[ev.task_id];
        s.task_id = ev.task_id;
        s.task_type = ev.task_type;
        switch (ev.phase) {
            case ProfilePhase::READY:
                s.ready_time = ev.timestamp;
                s.has_ready = true;
                break;
            case ProfilePhase::STARTED:
                s.start_time = ev.timestamp;
                s.has_start = true;
                break;
            case ProfilePhase::COMPLETED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::COMPLETED;
                s.exec_duration = ev.duration;
                break;
            case ProfilePhase::FAILED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::FAILED;
                s.exec_duration = ev.duration;
                break;
            case ProfilePhase::SKIPPED:
                s.end_time = ev.timestamp;
                s.has_end = true;
                s.final_status = TaskStatus::SKIPPED;
                break;
        }
    }

    std::vector<TaskStats> result;
    result.reserve(stats_map.size());
    for (auto& [id, s] : stats_map) {
        if (s.has_ready && s.has_start) {
            s.wait_duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(s.start_time - s.ready_time);
        }
        if (s.has_ready && s.has_end) {
            s.total_duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(s.end_time - s.ready_time);
        }
        result.push_back(std::move(s));
    }

    std::sort(result.begin(), result.end(),
              [](const TaskStats& a, const TaskStats& b) { return a.total_duration > b.total_duration; });
    return result;
}

// ===== 文件落盘 =====
bool ProfileCollector::save_json_report(const std::string& filepath, bool pretty) const {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) {
        return false;
    }
    ofs << to_json_string(pretty);
    return ofs.good();
}

bool ProfileCollector::save_trace_report(const std::string& filepath) const {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) {
        return false;
    }
    ofs << to_trace_string(false);
    return ofs.good();
}

// ===== Chrome Trace Event 格式 =====
// 标准格式文档：https://docs.google.com/document/d/1CvAClvFfyA5R5-IOmNK-QJ0BcVxFyqywUBCiB4nVAAY
// 可在 chrome://tracing 或 Perfetto UI 加载查看时间轴/火焰图
std::string ProfileCollector::to_trace_string(bool pretty) const {
    auto task_stats = compute_task_stats();
    auto dag_stats = compute_dag_stats();

    // 时间锚点：DAG 起始时刻（trace 事件 ts 需相对该锚点，单位微秒）
    auto anchor = dag_stats.has_start ? dag_stats.start_time : std::chrono::steady_clock::now();
    auto rel_us = [&](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::microseconds>(t - anchor).count();
    };
    // duration → 微秒（double，避免短任务被截断为 0）
    auto dur_us = [](std::chrono::nanoseconds ns) {
        return std::chrono::duration<double, std::micro>(ns).count();
    };

    nlohmann::json events = nlohmann::json::array();

    // 进程元数据：标注 DAG 总览
    {
        nlohmann::json m;
        m["name"] = "process_name";
        m["ph"] = "M";  // Metadata
        m["pid"] = 1;
        m["args"] = nlohmann::json::object();
        m["args"]["name"] = "task_graph DAG";
        m["args"]["total_tasks"] = dag_stats.total_tasks;
        m["args"]["completed"] = dag_stats.completed_tasks;
        m["args"]["failed"] = dag_stats.failed_tasks;
        m["args"]["skipped"] = dag_stats.skipped_tasks;
        events.push_back(std::move(m));
    }

    // 每个任务用 tid = 字符串哈希（让相同/不同任务在独立轨道显示，类似甘特图）
    // 注意：Chrome trace 要求 tid 为数值；用 std::hash 折叠 task_id
    auto tid_of = [](const std::string& id) -> int64_t {
        return static_cast<int64_t>(std::hash<std::string>{}(id) & 0x3FFF);  // 限制在 0..16383
    };

    for (const auto& ts : task_stats) {
        int64_t tid = tid_of(ts.task_id);

        // 等待段（READY → STARTED）：浅色，类别 "wait"
        if (ts.has_ready && ts.has_start && ts.start_time > ts.ready_time) {
            nlohmann::json e;
            e["name"] = ts.task_id + " (wait)";
            e["cat"] = "wait";
            e["ph"] = "X";  // Complete event
            e["pid"] = 1;
            e["tid"] = tid;
            e["ts"] = rel_us(ts.ready_time);
            e["dur"] = dur_us(ts.start_time - ts.ready_time);
            nlohmann::json args;
            args["task_id"] = ts.task_id;
            args["task_type"] = ts.task_type;
            e["args"] = std::move(args);
            events.push_back(std::move(e));
        }

        // 执行段（STARTED → END）：深色，类别 "exec"，附带状态
        if (ts.has_start && ts.has_end && ts.end_time > ts.start_time) {
            nlohmann::json e;
            e["name"] = ts.task_id;
            e["cat"] = "exec";
            e["ph"] = "X";
            e["pid"] = 1;
            e["tid"] = tid;
            e["ts"] = rel_us(ts.start_time);
            e["dur"] = dur_us(ts.end_time - ts.start_time);
            nlohmann::json args;
            args["task_id"] = ts.task_id;
            args["task_type"] = ts.task_type;
            args["status"] = status_to_string(ts.final_status);
            args["wait_ms"] = ns_to_ms(ts.wait_duration);
            args["exec_ms"] = ns_to_ms(ts.exec_duration);
            e["args"] = std::move(args);
            events.push_back(std::move(e));
        } else if (ts.final_status == TaskStatus::FAILED || ts.final_status == TaskStatus::SKIPPED) {
            // 失败/跳过：无执行段，画一个极短的 instant 事件以可见
            nlohmann::json e;
            e["name"] = ts.task_id + " (" + status_to_string(ts.final_status) + ")";
            e["cat"] = status_to_string(ts.final_status);
            e["ph"] = "i";  // Instant event
            e["pid"] = 1;
            e["tid"] = tid;
            e["ts"] = ts.has_ready ? rel_us(ts.ready_time) : 0;
            e["s"] = "t";  // thread scope
            nlohmann::json args;
            args["task_id"] = ts.task_id;
            args["task_type"] = ts.task_type;
            args["status"] = status_to_string(ts.final_status);
            e["args"] = std::move(args);
            events.push_back(std::move(e));
        }
    }

    // 包装为标准 trace 格式：{"traceEvents": [...]}
    nlohmann::json doc;
    doc["traceEvents"] = std::move(events);
    // 顶层加 displayTimeUnit，提示 UI 以毫秒显示
    doc["displayTimeUnit"] = "ms";

    return pretty ? doc.dump(2) : doc.dump();
}

}  // namespace task_graph
