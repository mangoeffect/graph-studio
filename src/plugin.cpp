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
#define RTLD_LAZY 0
#define RTLD_NODELETE 0
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
    // RTLD_NODELETE：dlclose 时不真正卸载代码段，避免插件内 C++ 静态对象
    // 的析构函数（__cxa_atexit 注册）在 dlclose 与进程退出时被重复调用，
    // 或注册到主库单例中的 std::function（其 vtable/代码位于插件地址空间）
    // 在插件卸载后悬空导致退出期 SIGSEGV。
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_NODELETE);
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
    return handles_.find(name) != handles_.end();
}

}
