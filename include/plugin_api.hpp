#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <any>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <chrono>

#ifdef _WIN32
#if defined(TASK_GRAPH_BUILD)
#define TG_EXPORT __declspec(dllexport)
#else
#define TG_EXPORT __declspec(dllimport)
#endif
#define TG_IMPORT __declspec(dllimport)
#else
#define TG_EXPORT __attribute__((visibility("default")))
#define TG_IMPORT __attribute__((visibility("default")))
#endif

namespace task_graph {

enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    SKIPPED
};

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

struct TaskResult {
    TaskStatus status{TaskStatus::PENDING};
    std::any value;
    std::exception_ptr exception{nullptr};
    std::chrono::nanoseconds duration{0};

    bool is_success() const { return status == TaskStatus::COMPLETED; }
    bool is_failed() const { return status == TaskStatus::FAILED; }
};

struct TaskConfig {
    TaskPriority priority{TaskPriority::NORMAL};
    size_t max_retries{0};
    std::chrono::milliseconds timeout{0};
    bool skip_on_fail{false};
};

using TaskId = std::string;

class ExecutionContext {
public:
    ExecutionContext() = default;
    virtual ~ExecutionContext() = default;

    void set_result(const TaskId& task_id, const TaskResult& result) {
        std::unique_lock lock(results_mutex_);
        results_[task_id] = result;
    }

    std::optional<TaskResult> get_result(const TaskId& task_id) const {
        std::shared_lock lock(results_mutex_);
        auto it = results_.find(task_id);
        if (it != results_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set_value(const std::string& key, std::any value) {
        std::unique_lock lock(values_mutex_);
        values_[key] = std::move(value);
    }

    std::optional<std::any> get_value(const std::string& key) const {
        std::shared_lock lock(values_mutex_);
        auto it = values_.find(key);
        if (it != values_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

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

private:
    mutable std::shared_mutex results_mutex_;
    std::unordered_map<TaskId, TaskResult> results_;

    mutable std::shared_mutex values_mutex_;
    std::unordered_map<std::string, std::any> values_;
};

class IPluginTask {
public:
    virtual ~IPluginTask() = default;
    virtual const std::string& id() const = 0;
    virtual TaskResult execute(ExecutionContext& ctx) = 0;
    virtual const TaskConfig& config() const = 0;
};

using PluginTaskPtr = std::shared_ptr<IPluginTask>;

class PluginRegistry {
public:
    static PluginRegistry& instance() {
        static PluginRegistry instance;
        return instance;
    }

    void register_task(const std::string& task_id,
                       std::function<PluginTaskPtr()> creator) {
        std::lock_guard<std::mutex> lock(mutex_);
        task_creators_[task_id] = std::move(creator);
    }

    void unregister_task(const std::string& task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        task_creators_.erase(task_id);
    }

    bool has_task(const std::string& task_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return task_creators_.contains(task_id);
    }

    PluginTaskPtr create_task(const std::string& task_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_creators_.find(task_id);
        if (it != task_creators_.end()) {
            return it->second();
        }
        return nullptr;
    }

    std::vector<std::string> available_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [id, _] : task_creators_) {
            result.push_back(id);
        }
        return result;
    }

private:
    PluginRegistry() = default;
    ~PluginRegistry() = default;

    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    std::unordered_map<std::string, std::function<PluginTaskPtr()>> task_creators_;
    mutable std::mutex mutex_;
};

}

extern "C" {
    using RegisterPluginFunc = bool(*)();
    using UnregisterPluginFunc = void(*)();
}
