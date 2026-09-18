#pragma once

// webgpu.h 版本兼容垫片：wgpu-native（v29 新命名）与 emscripten 旧头
// （emsdk 3.1.37 钉死版本，dawn-2022 时代形态）共用一套后端代码。
// 原生构建直接用 wgpu-native v29 的新名；emsdk 3.1.37 的 webgpu.h 没有：
//   - WGPUStringView（消息是 char const*）
//   - 异步 CallbackInfo 形态（request*/mapAsync/workDone/popErrorScope
//     都是裸回调 + 单 userdata）
//   - wgpuInstanceProcessEvents / WGPUFuture
//   - WGPUShaderSourceWGSL（是 WGPUShaderModuleWGSLDescriptor{chain, source}）
//   - WGPUTexelCopy* 命名（是 WGPUImageCopy* / WGPUTextureDataLayout）
// 升级 emsdk 到携带新 webgpu.h 的版本后，本文件的 __EMSCRIPTEN__ 分支可删。
//
// 运行时语义：旧 emscripten 的回调经浏览器 JS 事件循环异步触发；wasm 上
// spin_until 立即返回（不能阻塞主线程），init 在回调到达前观察到 null 即
// 优雅失败——浏览器内真实初始化是独立里程碑。

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>

#include <webgpu/webgpu.h>

#if defined(__EMSCRIPTEN__)
// ---- 旧 emscripten webgpu.h（<= 3.1.4x 时代）兼容层 ----
// 注意：不能按枚举常量探测（defined() 只对宏生效；emsdk 升级换新头后本
// 分支整段删除即可）。

// 基础类型：StringView / Future / 回调模式 / 新名枚举
struct WGPUStringView {
    char const* data;
    size_t length;
};
struct WGPUFuture { uint64_t id; };
typedef enum WGPUCallbackMode {
    WGPUCallbackMode_WaitAnyOnly = 0x1,
    WGPUCallbackMode_AllowProcessEvents = 0x2,
    WGPUCallbackMode_AllowSpontaneous = 0x3,
} WGPUCallbackMode;
typedef enum WGPUPopErrorScopeStatus {
    WGPUPopErrorScopeStatus_Success = 0x1,
    WGPUPopErrorScopeStatus_Error = 0x2,
} WGPUPopErrorScopeStatus;
using WGPUMapAsyncStatus = WGPUBufferMapAsyncStatus;
constexpr WGPUMapAsyncStatus WGPUMapAsyncStatus_Success =
    WGPUBufferMapAsyncStatus_Success;

// 新名结构/枚举对齐
using WGPUShaderSourceWGSL = WGPUShaderModuleWGSLDescriptor;
#define WGPUSType_ShaderSourceWGSL WGPUSType_ShaderModuleWGSLDescriptor
using WGPUTexelCopyBufferInfo = WGPUImageCopyBuffer;
using WGPUTexelCopyTextureInfo = WGPUImageCopyTexture;
using WGPUTexelCopyBufferLayout = WGPUTextureDataLayout;
using WGPUTexelCopyBufferDataLayout = WGPUTextureDataLayout;

// 新形态回调 typedef（多 StringView + 双 userdata）
typedef void (*WGPURequestAdapterCallbackCompat)(
    WGPURequestAdapterStatus, WGPUAdapter, WGPUStringView, void*, void*);
typedef void (*WGPURequestDeviceCallbackCompat)(
    WGPURequestDeviceStatus, WGPUDevice, WGPUStringView, void*, void*);
typedef void (*WGPUQueueWorkDoneCallbackCompat)(
    WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*);
typedef void (*WGPUBufferMapCallbackCompat)(
    WGPUMapAsyncStatus, WGPUStringView, void*, void*);
typedef void (*WGPUPopErrorScopeCallbackCompat)(
    WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*);
typedef void (*WGPUUncapturedErrorCallbackCompat)(
    WGPUDevice, WGPUErrorType, WGPUStringView, void*, void*);

struct WGPURequestAdapterCallbackInfo {
    void* nextInChain;
    WGPUCallbackMode mode;
    WGPURequestAdapterCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};
struct WGPURequestDeviceCallbackInfo {
    void* nextInChain;
    WGPUCallbackMode mode;
    WGPURequestDeviceCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};
struct WGPUQueueWorkDoneCallbackInfo {
    void* nextInChain;
    WGPUCallbackMode mode;
    WGPUQueueWorkDoneCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};
struct WGPUBufferMapCallbackInfo {
    void* nextInChain;
    WGPUCallbackMode mode;
    WGPUBufferMapCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};
struct WGPUPopErrorScopeCallbackInfo {
    void* nextInChain;
    WGPUCallbackMode mode;
    WGPUPopErrorScopeCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};
struct WGPUUncapturedErrorCallbackInfo {
    void* nextInChain;
    WGPUUncapturedErrorCallbackCompat callback;
    void* userdata1;
    void* userdata2;
};

#define TG_WGPU_OLD_WGSL_DESCRIPTOR 1
// 旧头的 WGSL code 字段名是 source（新头是 code）
#define tg_wgsl_code_field source

namespace task_graph {
namespace wgpu_compat {

// 旧头异步 API 的裸回调形态 → CallbackInfo 形态包装（名字换新，避免与
// extern "C" 函数重载冲突；回调经 JS 事件循环异步触发，包装后行为不变）
inline WGPUFuture tg_request_adapter(WGPUInstance instance,
                                     WGPURequestAdapterOptions const* options,
                                     WGPURequestAdapterCallbackInfo const& cb) {
    wgpuInstanceRequestAdapter(
        instance, options,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
           char const* message, void* userdata) {
            auto* cbi = static_cast<WGPURequestAdapterCallbackInfo*>(userdata);
            WGPUStringView sv{message, message ? std::strlen(message) : 0};
            cbi->callback(status, adapter, sv, cbi->userdata1, cbi->userdata2);
            delete cbi;
        }, new WGPURequestAdapterCallbackInfo(cb));
    return WGPUFuture{0};
}

inline WGPUFuture tg_request_device(WGPUAdapter adapter,
                                    WGPUDeviceDescriptor const* desc,
                                    WGPURequestDeviceCallbackInfo const& cb) {
    wgpuAdapterRequestDevice(
        adapter, desc,
        [](WGPURequestDeviceStatus status, WGPUDevice device,
           char const* message, void* userdata) {
            auto* cbi = static_cast<WGPURequestDeviceCallbackInfo*>(userdata);
            WGPUStringView sv{message, message ? std::strlen(message) : 0};
            cbi->callback(status, device, sv, cbi->userdata1, cbi->userdata2);
            delete cbi;
        }, new WGPURequestDeviceCallbackInfo(cb));
    return WGPUFuture{0};
}

inline WGPUFuture tg_queue_work_done(WGPUQueue queue,
                                     WGPUQueueWorkDoneCallbackInfo const& cb) {
    wgpuQueueOnSubmittedWorkDone(
        queue, 0,  // 旧 dawn 形态多一个 fence signalValue
        [](WGPUQueueWorkDoneStatus status, void* userdata) {
            auto* cbi = static_cast<WGPUQueueWorkDoneCallbackInfo*>(userdata);
            cbi->callback(status, WGPUStringView{nullptr, 0},
                          cbi->userdata1, cbi->userdata2);
            delete cbi;
        }, new WGPUQueueWorkDoneCallbackInfo(cb));
    return WGPUFuture{0};
}

inline WGPUFuture tg_buffer_map_async(WGPUBuffer buffer, WGPUMapModeFlags mode,
                                      size_t offset, size_t size,
                                      WGPUBufferMapCallbackInfo const& cb) {
    wgpuBufferMapAsync(
        buffer, mode, offset, size,
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            auto* cbi = static_cast<WGPUBufferMapCallbackInfo*>(userdata);
            cbi->callback(static_cast<WGPUMapAsyncStatus>(status),
                          WGPUStringView{nullptr, 0},
                          cbi->userdata1, cbi->userdata2);
            delete cbi;
        }, new WGPUBufferMapCallbackInfo(cb));
    return WGPUFuture{0};
}

inline WGPUFuture tg_pop_error_scope(WGPUDevice device,
                                     WGPUPopErrorScopeCallbackInfo const& cb) {
    wgpuDevicePopErrorScope(
        device,
        [](WGPUErrorType type, char const* message, void* userdata) {
            auto* cbi = static_cast<WGPUPopErrorScopeCallbackInfo*>(userdata);
            WGPUStringView sv{message, message ? std::strlen(message) : 0};
            cbi->callback(WGPUPopErrorScopeStatus_Success, type, sv,
                          cbi->userdata1, cbi->userdata2);
            delete cbi;
        }, new WGPUPopErrorScopeCallbackInfo(cb));
    return WGPUFuture{0};
}

// 旧头无 ProcessEvents：回调依赖 JS 事件循环，wasm 上不能阻塞等待——
// 空实现让 spin_until 立即返回（init 观察到 null 即优雅失败）。
// 3.1.46 起头文件已声明同名函数——声明与 inline 定义签名一致时合法共存，
// 编译器取 inline 定义（行为相同），无需版本分支。
inline void wgpuInstanceProcessEvents(WGPUInstance) {}

// WGSL 描述：3.1.37 字段名 source，3.1.46 起 code。__EMSCRIPTEN_minor__
// 等版本宏由 <emscripten.h> 定义（本头不包含它，恒未定义），故用成员探测。
namespace tg_wgpu_compat {
template <typename T, typename = void>
struct has_wgsl_code : std::false_type {};
template <typename T>
struct has_wgsl_code<T, std::void_t<decltype(std::declval<T&>().code)>>
    : std::true_type {};
}  // namespace tg_wgpu_compat

inline WGPUShaderModuleWGSLDescriptor make_wgsl_source(const char* data, size_t) {
    WGPUShaderModuleWGSLDescriptor s{};
    s.chain.next = nullptr;
    s.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    if constexpr (tg_wgpu_compat::has_wgsl_code<WGPUShaderModuleWGSLDescriptor>::value) {
        s.code = data;
    } else {
        s.source = data;
    }
    return s;
}

inline WGPUShaderModuleWGSLDescriptor make_wgsl_source(const std::string& code) {
    return make_wgsl_source(code.c_str(), code.size());
}

inline WGPUStringView str_view(const std::string& s) {
    WGPUStringView v;
    v.data = s.c_str();
    v.length = s.size();
    return v;
}

// 字面量/静态串重载：注意 std::string 临时量的 c_str 会在表达式结束悬垂，
// 固定入口名（vs_main/fs_main/kernel 名）必须走本重载
inline WGPUStringView str_view(const char* s) {
    WGPUStringView v;
    v.data = s;
    v.length = std::char_traits<char>::length(s);
    return v;
}

}  // namespace wgpu_compat
}  // namespace task_graph

#else  // ---- wgpu-native v29（新命名）直通 + 薄包装 ----

namespace task_graph {
namespace wgpu_compat {

inline WGPUFuture tg_request_adapter(WGPUInstance instance,
                                     WGPURequestAdapterOptions const* options,
                                     WGPURequestAdapterCallbackInfo const& cb) {
    return wgpuInstanceRequestAdapter(instance, options, cb);
}

inline WGPUFuture tg_request_device(WGPUAdapter adapter,
                                    WGPUDeviceDescriptor const* desc,
                                    WGPURequestDeviceCallbackInfo const& cb) {
    return wgpuAdapterRequestDevice(adapter, desc, cb);
}

inline WGPUFuture tg_queue_work_done(WGPUQueue queue,
                                     WGPUQueueWorkDoneCallbackInfo const& cb) {
    return wgpuQueueOnSubmittedWorkDone(queue, cb);
}

inline WGPUFuture tg_buffer_map_async(WGPUBuffer buffer, WGPUMapMode mode,
                                      size_t offset, size_t size,
                                      WGPUBufferMapCallbackInfo const& cb) {
    return wgpuBufferMapAsync(buffer, mode, offset, size, cb);
}

inline WGPUFuture tg_pop_error_scope(WGPUDevice device,
                                     WGPUPopErrorScopeCallbackInfo const& cb) {
    return wgpuDevicePopErrorScope(device, cb);
}

// WGSL 描述：新头 code 是 WGPUStringView
inline WGPUShaderSourceWGSL make_wgsl_source(const char* data, size_t len) {
    WGPUShaderSourceWGSL s{};
    s.chain.next = nullptr;
    s.chain.sType = WGPUSType_ShaderSourceWGSL;
    s.code.data = data;
    s.code.length = len;
    return s;
}

inline WGPUShaderSourceWGSL make_wgsl_source(const std::string& code) {
    return make_wgsl_source(code.c_str(), code.size());
}

inline WGPUStringView str_view(const std::string& s) {
    WGPUStringView v;
    v.data = s.c_str();
    v.length = s.size();
    return v;
}

// 字面量/静态串重载：注意 std::string 临时量的 c_str 会在表达式结束悬垂，
// 固定入口名（vs_main/fs_main/kernel 名）必须走本重载
inline WGPUStringView str_view(const char* s) {
    WGPUStringView v;
    v.data = s;
    v.length = std::char_traits<char>::length(s);
    return v;
}

}  // namespace wgpu_compat
}  // namespace task_graph
#endif
