#pragma once

#include <plugin_api.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace task_graph {

class TaskManager {
public:
    TaskManager() = default;
    ~TaskManager() = default;
    
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;
    TaskManager(TaskManager&&) = delete;
    TaskManager& operator=(TaskManager&&) = delete;
    
    void add_task(PluginTaskPtr task);
    
    void add_task(const std::string& id, PluginTaskPtr task);
    
    PluginTaskPtr get_task(const std::string& id) const;
    
    bool has_task(const std::string& id) const;
    
    void remove_task(const std::string& id);
    
    void clear_all();
    
    std::size_t size() const;
    
    std::vector<std::string> task_ids() const;
    
    std::vector<PluginTaskPtr> all_tasks() const;
    
private:
    std::unordered_map<std::string, PluginTaskPtr> tasks_;
    mutable std::mutex mutex_;
};

}
