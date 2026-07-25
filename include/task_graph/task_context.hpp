#pragma once

#include <plugin_api.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace task_graph {

class TaskContext : public IExecutionContext {
public:
    TaskContext() = default;

    TaskContext(const TaskParams& params, const std::vector<TaskId>& deps,
                const std::unordered_map<TaskId, TaskResult>& results)
        : params_(params), dependencies_(deps), results_(results) {}

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

    // --- Convenience helpers (moved from IExecutionContext) ---

    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto opt = get_value(key);
        if (!opt) {
            return std::nullopt;
        }
        try {
            return std::any_cast<T>(*opt);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    template<typename T>
    std::optional<T> get_result_value(const TaskId& task_id) const {
        auto opt = get_result(task_id);
        if (!opt || !opt->value.has_value()) {
            return std::nullopt;
        }
        try {
            return std::any_cast<T>(opt->value);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    template<typename T>
    std::optional<T> get_input(const std::string& data_name = "") const {
        std::vector<std::pair<TaskId, std::any>> candidates;

        for (const auto& dep : dependencies()) {
            auto opt = get_result(dep);
            if (opt && opt->status == TaskStatus::COMPLETED && opt->value.has_value()) {
                try {
                    std::any_cast<T>(opt->value);
                    candidates.emplace_back(dep, opt->value);
                } catch (const std::bad_any_cast&) {
                    continue;
                }
            }
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        if (!data_name.empty()) {
            for (const auto& [task_id, value] : candidates) {
                if (task_id == data_name) {
                    try {
                        return std::any_cast<T>(value);
                    } catch (const std::bad_any_cast&) {
                        continue;
                    }
                }
            }
        }

        try {
            return std::any_cast<T>(candidates[0].second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
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

private:
    TaskParams params_;
    std::vector<TaskId> dependencies_;

    mutable std::mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;

    mutable std::mutex dependencies_mutex_;
};

}
