#pragma once

#include <plugin_api.hpp>
#include <cstdint>
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

using GetPluginInfoFunc = const PluginInfo*(*)();

class PluginLoader {
public:
    // 完整定义而非前向声明：handles_ 的 unordered_map<string, Handle> 在
    // libstdc++ 11（ubuntu-22.04）下要求 mapped type 在声明处完整。
    struct Handle {
        void* lib_handle{nullptr};
        RegisterPluginFunc register_func{nullptr};
        UnregisterPluginFunc unregister_func{nullptr};
        GetPluginInfoFunc info_func{nullptr};
        uint32_t sdk_version{0};
        std::string path;
    };

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
