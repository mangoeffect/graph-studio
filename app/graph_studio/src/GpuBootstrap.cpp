#include "GpuBootstrap.h"

#include <task_graph_api.hpp>
#include <task_graph/gpu_image_ops.hpp>

#if defined(__APPLE__)
    #include <task_graph/gpu_backends/metal_backend.hpp>
    #define TG_HAS_METAL 1
#elif (defined(_WIN32) || defined(__linux__)) && !defined(__EMSCRIPTEN__)
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
#if defined(__APPLE__)
    try_init_metal();
#elif (defined(_WIN32) || defined(__linux__)) && !defined(__EMSCRIPTEN__)
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
