#pragma once

#include <task_graph/task.hpp>
#include <task_graph/dag.hpp>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

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

class IPluginTask {
public:
    virtual ~IPluginTask() = default;
    virtual const std::string& id() const = 0;
    virtual TaskResult execute(ExecutionContext& ctx) = 0;
    virtual const TaskConfig& config() const = 0;
};

using PluginTaskPtr = std::shared_ptr<IPluginTask>;

struct PluginInfo {
    std::string name;
    std::string version;
    std::string description;
};

class PluginLoader {
public:
    struct Handle;

    PluginLoader();
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    bool load(const std::string& path);
    void unload(const std::string& path);
    void unload_all();

    std::vector<std::string> loaded_plugins() const;
    bool is_loaded(const std::string& name) const;

private:
    std::unordered_map<std::string, Handle> handles_;
};

class PluginRegistry {
public:
    static PluginRegistry& instance();

    void register_task(const std::string& task_id, 
                       std::function<PluginTaskPtr()> creator);
    
    void unregister_task(const std::string& task_id);
    bool has_task(const std::string& task_id) const;
    
    PluginTaskPtr create_task(const std::string& task_id) const;
    std::vector<std::string> available_tasks() const;

private:
    PluginRegistry() = default;
    ~PluginRegistry() = default;
    
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    std::unordered_map<std::string, std::function<PluginTaskPtr()>> task_creators_;
    mutable std::mutex mutex_;
};

using PluginRegistryPtr = std::shared_ptr<PluginRegistry>;

}

extern "C" {
    using RegisterPluginFunc = bool(*)();
    using UnregisterPluginFunc = void(*)();
    using GetPluginInfoFunc = const task_graph::PluginInfo*(*)();
}
