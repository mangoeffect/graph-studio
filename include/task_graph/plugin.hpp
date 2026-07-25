#pragma once

#include <plugin_api.hpp>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

namespace task_graph {

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

using PluginRegistryPtr = std::shared_ptr<IPluginRegistry>;

}

extern "C" {
    using GetPluginInfoFunc = const task_graph::PluginInfo*(*)();
}
