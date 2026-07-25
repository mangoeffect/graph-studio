#include <task_graph/plugin.hpp>
#include <mutex>
#include <stdexcept>
#include <dlfcn.h>

#ifdef _WIN32
#include <windows.h>
#define dlopen(path, mode) LoadLibraryA(path)
#define dlclose(handle) FreeLibrary((HMODULE)handle)
#define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
#define dlerror() "Windows error"
#endif

namespace task_graph {

struct PluginLoader::Handle {
    void* lib_handle{nullptr};
    RegisterPluginFunc register_func{nullptr};
    UnregisterPluginFunc unregister_func{nullptr};
    GetPluginInfoFunc info_func{nullptr};
    std::string path;
};

PluginLoader::PluginLoader() {}

PluginLoader::~PluginLoader() {
    unload_all();
}

bool PluginLoader::load(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        return false;
    }

    RegisterPluginFunc reg_func = 
        reinterpret_cast<RegisterPluginFunc>(dlsym(handle, "register_plugin"));
    
    UnregisterPluginFunc unreg_func = 
        reinterpret_cast<UnregisterPluginFunc>(dlsym(handle, "unregister_plugin"));

    if (!reg_func) {
        dlclose(handle);
        return false;
    }

    if (!reg_func()) {
        dlclose(handle);
        return false;
    }

    Handle h;
    h.lib_handle = handle;
    h.register_func = reg_func;
    h.unregister_func = unreg_func;
    h.path = path;

    handles_[path] = std::move(h);
    return true;
}

void PluginLoader::unload(const std::string& path) {
    auto it = handles_.find(path);
    if (it != handles_.end()) {
        if (it->second.unregister_func) {
            it->second.unregister_func();
        }
        if (it->second.lib_handle) {
            dlclose(it->second.lib_handle);
        }
        handles_.erase(it);
    }
}

void PluginLoader::unload_all() {
    for (auto& [path, handle] : handles_) {
        if (handle.unregister_func) {
            handle.unregister_func();
        }
        if (handle.lib_handle) {
            dlclose(handle.lib_handle);
        }
    }
    handles_.clear();
}

std::vector<std::string> PluginLoader::loaded_plugins() const {
    std::vector<std::string> result;
    for (const auto& [path, _] : handles_) {
        result.push_back(path);
    }
    return result;
}

bool PluginLoader::is_loaded(const std::string& name) const {
    return handles_.contains(name);
}

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::register_task(const std::string& task_id,
                                   std::function<PluginTaskPtr()> creator) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_[task_id] = std::move(creator);
}

void PluginRegistry::unregister_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_creators_.erase(task_id);
}

bool PluginRegistry::has_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_creators_.contains(task_id);
}

PluginTaskPtr PluginRegistry::create_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_creators_.find(task_id);
    if (it != task_creators_.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> PluginRegistry::available_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [id, _] : task_creators_) {
        result.push_back(id);
    }
    return result;
}

}
