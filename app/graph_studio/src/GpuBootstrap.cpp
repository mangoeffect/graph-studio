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
// wgpu 统一后端（WGSL）：三端可用；TG_GPU_BACKEND=wgpu 显式选择
#if defined(TASK_GRAPH_ENABLE_WGPU)
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
    // TG_GPU_BACKEND=wgpu 显式选择 wgpu 统一后端（Metal/Vulkan 后端保留，
    // compute op 在 wgpu 上暂无 WGSL kernel）；未指定/失败时走平台默认
    if (const char* pref = std::getenv("TG_GPU_BACKEND");
        pref && std::string(pref) == "wgpu") {
#if defined(TG_HAS_WGPU)
        if (try_init_wgpu()) {
            return;
        }
        TG_LOG_WARN("TG_GPU_BACKEND=wgpu requested but init failed; falling back to default");
#else
        TG_LOG_WARN("TG_GPU_BACKEND=wgpu requested but wgpu backend not compiled in "
                    "(build with -DTASK_GRAPH_ENABLE_WGPU=ON and run scripts/fetch_wgpu.py)");
#endif
    }
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
