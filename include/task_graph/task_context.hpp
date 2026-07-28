#pragma once

#include <plugin_api.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace task_graph {

// TaskContext：单个 task 执行期间的上下文。
//
// 数据来源分三类：
//   1) params_        —— 静态配置参数（构图期固定）
//   2) results_       —— 上游 task 的结果快照（按 task_id 查）
//   3) inputs_by_port_—— 按 port 绑定的上游输出（推荐入口，确定性）
//
// 跨 task 数据传递的唯一通道是 TaskResult.value/outputs，由 executor 在
// task 启动前按 Edge 的 to_port 填充到 inputs_by_port_。
class TaskContext : public IExecutionContext {
public:
    TaskContext() = default;

    TaskContext(const TaskParams& params, const std::vector<TaskId>& deps,
                const std::unordered_map<TaskId, TaskResult>& results)
        : params_(params), dependencies_(deps), results_(results) {}

    TaskContext(const TaskParams& params, const std::vector<TaskId>& deps,
                const std::unordered_map<TaskId, TaskResult>& results,
                std::unordered_map<std::string, std::any> inputs_by_port)
        : params_(params), dependencies_(deps), results_(results),
          inputs_by_port_(std::move(inputs_by_port)) {}

    // --- IExecutionContext pure virtual implementations ---

    void set_result(const TaskId& task_id, const TaskResult& result) override {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_[task_id] = result;
    }

    std::optional<TaskResult> get_result(const TaskId& task_id) const override {
        std::lock_guard<std::mutex> lock(results_mutex_);
        auto it = results_.find(task_id);
        if (it != results_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set_value(const std::string& key, std::any value) override {
        std::lock_guard<std::mutex> lock(values_mutex_);
        values_[key] = std::move(value);
    }

    std::optional<std::any> get_value(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(values_mutex_);
        auto it = values_.find(key);
        if (it != values_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void log(LogLevel level, const std::string& msg) override {
        tg_log(level, msg.c_str());
    }

    void declare_dependency(const TaskId& task_id) override {
        std::lock_guard<std::mutex> lock(dependencies_mutex_);
        auto it = std::find(dependencies_.begin(), dependencies_.end(), task_id);
        if (it == dependencies_.end()) {
            dependencies_.push_back(task_id);
        }
    }

    bool validate_dependencies() const override {
        std::lock_guard<std::mutex> lock(dependencies_mutex_);
        for (const auto& dep : dependencies_) {
            std::lock_guard<std::mutex> rlock(results_mutex_);
            auto it = results_.find(dep);
            if (it == results_.end() || it->second.status != TaskStatus::COMPLETED) {
                return false;
            }
        }
        return true;
    }

    std::vector<TaskId> dependencies() const override {
        std::lock_guard<std::mutex> lock(dependencies_mutex_);
        return dependencies_;
    }

    void clear_result(const TaskId& task_id) override {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.erase(task_id);
    }

    void clear_all_results() override {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.clear();
    }

    const TaskParams& params() const override {
        return params_;
    }

    void set_params(const TaskParams& params) { params_ = params; }

    // --- Convenience helpers ---

    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

    // ===== 推荐：按端口取上游输出（确定性） =====
    template <typename T>
    std::optional<T> input(const std::string& port_name) const {
        auto it = inputs_by_port_.find(port_name);
        if (it == inputs_by_port_.end() || !it->second.has_value()) {
            return std::nullopt;
        }
        // type-check-first：WASM -fno-exceptions 下 any_cast 失败会 abort，
        // 因此先用 type_info 比对，仅匹配时再 cast（桌面平台同样安全）。
        if (it->second.type() != typeid(T)) {
            return std::nullopt;
        }
        return std::any_cast<T>(it->second);
    }

    // 零拷贝只读版本：返回指向内部 any 的 const 指针，避免拷贝大型负载
    template <typename T>
    std::optional<const T*> input_ptr(const std::string& port_name) const {
        auto it = inputs_by_port_.find(port_name);
        if (it == inputs_by_port_.end() || !it->second.has_value()) {
            return std::nullopt;
        }
        if (it->second.type() != typeid(T)) {
            return std::nullopt;
        }
        return &std::any_cast<const T&>(it->second);
    }

    // ===== 备用：按上游 task_id 取其完整 TaskResult 中的 value =====
    // 仅在确需按 task_id 而非 port 取值时使用（少见，多输出场景应用 input<T>(port)）。
    template <typename T>
    std::optional<T> get_result_value(const TaskId& task_id) const {
        auto opt = get_result(task_id);
        if (!opt) {
            return std::nullopt;
        }
        // 优先 outputs["out"]，回退 value 字段
        const std::any* v = nullptr;
        if (!opt->outputs.empty()) {
            auto it = opt->outputs.find("out");
            if (it != opt->outputs.end()) v = &it->second;
        }
        if (!v && opt->value.has_value()) v = &opt->value;
        if (!v || !v->has_value() || v->type() != typeid(T)) return std::nullopt;
        return std::any_cast<T>(*v);
    }

    // ===== values_ 黑板（task 内部局部状态；不跨 task） =====
    template <typename T>
    std::optional<T> get(const std::string& key) const {
        auto opt = get_value(key);
        if (!opt || opt->type() != typeid(T)) {
            return std::nullopt;
        }
        return std::any_cast<T>(*opt);
    }

    std::optional<int> get_param_int(const std::string& key) const {
        return params().get_int(key);
    }

    std::optional<float> get_param_float(const std::string& key) const {
        return params().get_float(key);
    }

    std::optional<std::string> get_param_string(const std::string& key) const {
        return params().get_string(key);
    }

    std::optional<bool> get_param_bool(const std::string& key) const {
        return params().get_bool(key);
    }

private:
    TaskParams params_;
    std::vector<TaskId> dependencies_;

    mutable std::mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;

    mutable std::mutex dependencies_mutex_;

    std::unordered_map<std::string, std::any> inputs_by_port_;  // 由 executor 按 Edge.to_port 填充
};

}
