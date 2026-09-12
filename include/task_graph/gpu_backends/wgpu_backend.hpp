#pragma once

// WgpuGpuBackend：基于 wgpu-native（C API）的统一 GPU 后端。
//
// 三端一张后端：macOS/iOS → Metal，Windows/Linux → Vulkan/DX12（由 wgpu 按
// 平台自选，可用环境变量 TG_WGPU_BACKEND=metal|vulkan|gl 强制），WASM →
// WebGPU（emscripten -sUSE_WEBGPU，同源 webgpu.h，经 wgpu_backend_impl.hpp
// 的兼容垫片吸收版本 drift）。
//
// shader 语言：一律 WGSL。
//   - compute：compile_kernel/dispatch 收 WGSL（entry 名 = kernel 名），
//     kernel_language() 返回 "wgsl"；
//   - render：GpuRenderPipelineDesc.wgsl_source（vs_main + fs_main），
//     MSL/GLSL 字段被忽略。
//
// 同步模型：所有操作提交后经 onSubmittedWorkDone / mapAsync 的 future
// wgpuInstanceWaitAny 阻塞等待（与 Metal waitUntilCompleted / Vulkan fence
// 同语义）；渲染批次 P1 批处理契约照搬（end 只结束 pass 录制，wait_render_idle
// 统一提交+等待，上传/下载/拷贝路径内部先排空）。
//
// 绑定布局契约见 wgpu_render.cpp 头注释（group0：tex0..3 / samp / uni）。
#include <task_graph/gpu_image_ops.hpp>

namespace task_graph {

struct WgpuGpuBackendImpl;

class WgpuGpuBackend : public IGpuImageBackend {
public:
    WgpuGpuBackend();
    ~WgpuGpuBackend() override;

    bool init() override;
    void shutdown() override;

    bool upload_to_gpu(Image& image) override;
    bool download_to_cpu(Image& image) override;
    bool copy_to_gpu(Image& image, uintptr_t gpu_ptr) override;
    bool release_gpu_memory(Image& image) override;

    uintptr_t allocate_gpu_memory(size_t size) override;
    void free_gpu_memory(uintptr_t handle) override;

    std::string get_backend_name() const override;
    bool is_available() const override;

    // ===== Compute（WGSL）=====
    uintptr_t compile_kernel(const std::string& name,
                             const std::string& source) override;
    bool dispatch(uintptr_t kernel,
                  const std::vector<GpuBinding>& bindings,
                  const void* uniform_data, size_t uniform_size,
                  uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) override;
    void release_kernel(uintptr_t kernel) override;
    bool supports_compute() const override;
    std::string kernel_language() const override;

    // ===== Render（WGSL，实现见 wgpu_render.cpp）=====
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

private:
    WgpuGpuBackendImpl* impl_ = nullptr;
};

}  // namespace task_graph
