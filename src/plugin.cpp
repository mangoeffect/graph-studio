#include <task_graph/plugin.hpp>
#include <mutex>
#include <stdexcept>

#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    // iOS 不支持运行时 dlopen 加载外部代码；插件通过静态链接 +
    // __attribute__((constructor)) 注册。定义空桩使编译通过，load() 直接返回 false。
    #define TASK_GRAPH_NO_DLOPEN 1
    #define dlopen(path, mode) ((void*)nullptr)
    #define dlclose(handle) ((void)0)
    #define dlsym(handle, symbol) ((void*)nullptr)
    #define dlerror() "dlopen not available on iOS"
    #define RTLD_LAZY 0
    #define RTLD_NODELETE 0
#elif defined(_WIN32)
#include <windows.h>
#define dlopen(path, mode) LoadLibraryA(path)
#define dlclose(handle) FreeLibrary((HMODULE)handle)
#define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
#define dlerror() "Windows error"
#define RTLD_LAZY 0
#define RTLD_NODELETE 0
#else
#include <dlfcn.h>
#endif

// 宿主框架导出的 SDK 版本：插件在加载时通过 tg_plugin_sdk_version() 与它比对。
extern "C" TG_EXPORT uint32_t tg_sdk_version() {
    return ::task_graph::TG_SDK_VERSION;
}

namespace task_graph {

struct PluginLoader::Handle {
    void* lib_handle{nullptr};
    RegisterPluginFunc register_func{nullptr};
    UnregisterPluginFunc unregister_func{nullptr};
    GetPluginInfoFunc info_func{nullptr};
    uint32_t sdk_version{0};
    std::string path;
};

PluginLoader::PluginLoader() {
    // 先触碰 PluginRegistry 单例，使其构造早于本 loader。Meyers 单例按构造
    // 完成的逆序析构，从而保证退出期 registry 晚于 loader 析构；否则
    // ~PluginLoader -> unload_all -> 插件 unregister_plugin -> 已销毁的
    // PluginRegistry（对已析构 mutex 加锁抛 std::system_error）→ 逃出
    // noexcept 析构 → std::terminate。
    (void)PluginRegistry::instance();
}

PluginLoader::~PluginLoader() {
    // 析构函数隐式 noexcept：任何异常逃逸都会触发 std::terminate。
    // 退出期插件卸载路径可能因静态析构顺序等触及已销毁对象，这里兜底吞掉。
    try {
        unload_all();
    } catch (...) {
    }
}

bool PluginLoader::load(const std::string& path) {
#ifdef TASK_GRAPH_NO_DLOPEN
    (void)path;
    return false;
#else
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

    // 可选的 ABI 校验：插件导出的 SDK 版本必须与宿主框架一致。
    // 未导出该符号的旧插件以警告方式放行。
    auto plugin_sdk_fn =
        reinterpret_cast<uint32_t(*)()>(dlsym(handle, "tg_plugin_sdk_version"));
    uint32_t plugin_sdk = plugin_sdk_fn ? plugin_sdk_fn() : 0;
    if (plugin_sdk != 0 && plugin_sdk != TG_SDK_VERSION) {
        TG_LOG_WARN("Plugin SDK version mismatch (" + std::to_string(plugin_sdk) +
                    " != " + std::to_string(TG_SDK_VERSION) + "): " + path);
        dlclose(handle);
        return false;
    }

    // 可选的插件元信息（未导出时忽略）
    GetPluginInfoFunc info_func =
        reinterpret_cast<GetPluginInfoFunc>(dlsym(handle, "get_plugin_info"));
    if (info_func) {
        if (const PluginInfo* info = info_func()) {
            TG_LOG_DEBUG("Plugin '" + info->name + "' v" + info->version +
                         ": " + info->description);
        }
    }

    if (!reg_func()) {
        dlclose(handle);
        return false;
    }

    Handle h;
    h.lib_handle = handle;
    h.register_func = reg_func;
    h.unregister_func = unreg_func;
    h.info_func = info_func;
    h.sdk_version = plugin_sdk;
    h.path = path;

    handles_[path] = std::move(h);
    return true;
#endif
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
