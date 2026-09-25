#include "GpuBootstrap.h"

#include <task_graph_api.hpp>
#include <task_graph/gpu_image_ops.hpp>

#include <cstdlib>
#include <string>

// 后端可用性由 CMake 判定（TG_APP_HAS_*），而非平台硬编码：
// 例如 Linux 没装 libvulkan-dev 时这里编译为无后端空实现。
#if defined(__APPLE__)
    #include <task_graph/gpu_backends/metal_backend.hpp>
    #define TG_HAS_METAL 1
#elif defined(TG_APP_HAS_VULKAN)
    #include <task_graph/gpu_backends/vulkan_backend.hpp>
    #define TG_HAS_VULKAN 1
#endif
// wgpu 统一后端（WGSL）：三端可用，默认后端（Metal/Vulkan 回退）。
// app 侧宏由 app CMakeLists 定义为 TG_APP_HAS_WGPU（核心库直编路径下
// 也会带 TASK_GRAPH_ENABLE_WGPU，两个都认）。
#if defined(TG_APP_HAS_WGPU) || defined(TASK_GRAPH_ENABLE_WGPU)
    #include <task_graph/gpu_backends/wgpu_backend.hpp>
    #define TG_HAS_WGPU 1
#endif

namespace graph_studio {

namespace {

task_graph::GpuBackendPtr g_gpu_backend;

bool try_init_metal() {
#ifdef TG_HAS_METAL
    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (backend->init()) {
        task_graph::set_gpu_backend(backend);
        g_gpu_backend = backend;
        TG_LOG_INFO(("GPU backend: " + backend->get_backend_name() + " initialized").c_str());
        return true;
    }
    backend->shutdown();
    TG_LOG_WARN("MetalGpuBackend init failed; gpu tasks will fail at runtime");
#endif
    return false;
}

#if defined(TG_HAS_WGPU)
bool try_init_wgpu() {
    auto backend = std::make_shared<task_graph::WgpuGpuBackend>();
    if (backend->init()) {
        task_graph::set_gpu_backend(backend);
        g_gpu_backend = backend;
        TG_LOG_INFO(("GPU backend: " + backend->get_backend_name() + " initialized").c_str());
        return true;
    }
    backend->shutdown();
    TG_LOG_WARN("WgpuGpuBackend init failed; gpu tasks will fail at runtime");
    return false;
}
#endif

bool try_init_vulkan() {
#ifdef TG_HAS_VULKAN
    auto backend = std::make_shared<task_graph::VulkanGpuBackend>();
    if (backend->init()) {
        task_graph::set_gpu_backend(backend);
        g_gpu_backend = backend;
        TG_LOG_INFO(("GPU backend: " + backend->get_backend_name() + " initialized").c_str());
        return true;
    }
    backend->shutdown();
    TG_LOG_WARN("VulkanGpuBackend init failed; gpu tasks will fail at runtime");
#endif
    return false;
}

}  // namespace

void InitGpuBackend() {
    // TG_GPU_BACKEND=wgpu|metal|vulkan 显式强制指定后端（回归对照用）；
    // 未指定时默认顺序：wgpu 统一后端（WGSL 单源）→ Metal → Vulkan，
    // 前者 init 失败自动回退原生后端。
    const char* pref = std::getenv("TG_GPU_BACKEND");
    const std::string pref_str = pref ? pref : "";
    if (pref_str == "wgpu") {
#if defined(TG_HAS_WGPU)
        if (try_init_wgpu()) {
            return;
        }
        TG_LOG_WARN("TG_GPU_BACKEND=wgpu requested but init failed; falling back to default");
#else
        TG_LOG_WARN("TG_GPU_BACKEND=wgpu requested but wgpu backend not compiled in "
                    "(build with -DTASK_GRAPH_ENABLE_WGPU=ON and run scripts/fetch_wgpu.py)");
#endif
    } else if (pref_str == "metal") {
#if defined(TG_HAS_METAL)
        if (try_init_metal()) {
            return;
        }
        TG_LOG_WARN("TG_GPU_BACKEND=metal requested but init failed; falling back to default");
#else
        TG_LOG_WARN("TG_GPU_BACKEND=metal requested but not available on this platform");
#endif
    } else if (pref_str == "vulkan") {
#if defined(TG_HAS_VULKAN)
        if (try_init_vulkan()) {
            return;
        }
        TG_LOG_WARN("TG_GPU_BACKEND=vulkan requested but init failed; falling back to default");
#else
        TG_LOG_WARN("TG_GPU_BACKEND=vulkan requested but Vulkan backend not compiled in");
#endif
    }
    // 平台默认：wgpu 优先，失败回退 Metal / Vulkan
#if defined(TG_HAS_WGPU)
    if (try_init_wgpu()) {
        return;
    }
    TG_LOG_WARN("wgpu backend init failed; falling back to native backend");
#endif
#if defined(TG_HAS_METAL)
    try_init_metal();
#elif defined(TG_HAS_VULKAN)
    try_init_vulkan();
#else
    TG_LOG_INFO("No GPU backend available on this platform; gpu tasks will fail at runtime");
#endif
}

void ShutdownGpuBackend() {
    if (g_gpu_backend) {
        g_gpu_backend->shutdown();
        g_gpu_backend.reset();
    }
}

}  // namespace graph_studio
