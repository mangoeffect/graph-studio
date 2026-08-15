#include "GpuBootstrap.h"

#include <task_graph_api.hpp>
#include <task_graph/gpu_image_ops.hpp>

// 后端可用性由 CMake 判定（TG_APP_HAS_*），而非平台硬编码：
// 例如 Linux 没装 libvulkan-dev 时这里编译为无后端空实现。
#if defined(__APPLE__)
    #include <task_graph/gpu_backends/metal_backend.hpp>
    #define TG_HAS_METAL 1
#elif defined(TG_APP_HAS_VULKAN)
    #include <task_graph/gpu_backends/vulkan_backend.hpp>
    #define TG_HAS_VULKAN 1
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
