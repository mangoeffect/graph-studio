#pragma once

#include <task_graph/gpu_image_ops.hpp>

namespace task_graph {

// Pimpl pattern to hide Metal implementation details
class MetalGpuBackendImpl;

class MetalGpuBackend : public IGpuImageBackend {
public:
    MetalGpuBackend();
    ~MetalGpuBackend() override;

    bool init() override;
    void shutdown() override;

    bool upload_to_gpu(Image& image) override;
    bool download_to_cpu(Image& image) override;
    bool copy_to_gpu(Image& image, uintptr_t gpu_ptr) override;
    bool release_gpu_memory(Image& image) override;

    uintptr_t allocate_gpu_memory(size_t size) override;
    void free_gpu_memory(uintptr_t handle) override;

    uintptr_t compile_kernel(const std::string& name,
                              const std::string& source) override;
    bool dispatch(uintptr_t kernel,
                  const std::vector<GpuBinding>& bindings,
                  const void* uniform_data, size_t uniform_size,
                  uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) override;
    void release_kernel(uintptr_t kernel) override;

    std::string get_backend_name() const override { return "metal"; }
    bool is_available() const override;
    bool supports_compute() const override { return true; }

private:
    MetalGpuBackendImpl* impl_ = nullptr;
};

} // namespace task_graph
