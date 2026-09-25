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

    // ===== render（离屏渲染到纹理；实现见 src/gpu_backends/metal_render.mm）=====
    bool supports_render() const override;
    uintptr_t create_texture(const GpuTextureDesc& desc) override;
    void free_texture(uintptr_t texture) override;
    bool upload_texture(uintptr_t texture, const uint8_t* data, size_t size) override;
    bool download_texture(uintptr_t texture, uint8_t* data, size_t size) override;
    bool copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) override;
    bool copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) override;
    uintptr_t create_sampler(const GpuSamplerDesc& desc) override;
    void free_sampler(uintptr_t sampler) override;
    uintptr_t compile_render_pipeline(const GpuRenderPipelineDesc& desc) override;
    void release_render_pipeline(uintptr_t pipeline) override;
    bool begin_render_pass(const GpuRenderPassDesc& desc) override;
    bool render_draw(const GpuDrawCall& draw) override;
    bool end_render_pass() override;
    bool wait_render_idle() override;

    std::string get_backend_name() const override { return "metal"; }
    bool is_available() const override;
    bool supports_compute() const override { return true; }

private:
    MetalGpuBackendImpl* impl_ = nullptr;
};

} // namespace task_graph
