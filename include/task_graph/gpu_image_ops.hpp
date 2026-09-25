#pragma once

#include <task_graph/data_types.hpp>
#include <task_graph/gpu_render_ops.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace task_graph {

// Compute kernel 的 buffer 绑定项：handle + offset。
struct GpuBinding {
    uintptr_t handle{0};
    size_t offset{0};
};

class IGpuImageBackend {
public:
    virtual ~IGpuImageBackend() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    virtual bool upload_to_gpu(Image& image) = 0;
    virtual bool download_to_cpu(Image& image) = 0;
    virtual bool copy_to_gpu(Image& image, uintptr_t gpu_ptr) = 0;
    virtual bool release_gpu_memory(Image& image) = 0;

    virtual uintptr_t allocate_gpu_memory(size_t size) = 0;
    virtual void free_gpu_memory(uintptr_t handle) = 0;

    virtual std::string get_backend_name() const = 0;
    virtual bool is_available() const = 0;

    // ===== Compute 能力（默认实现表示不支持计算）=====
    // 编译 kernel 源码，返回 pipeline handle（0 表示失败）。同一 name 重复调用应缓存。
    virtual uintptr_t compile_kernel(const std::string& name,
                                      const std::string& source) {
        (void)name; (void)source;
        return 0;
    }

    // 提交 compute dispatch。bindings 按 atIndex 顺序绑定 buffer；
    // uniform_data 绑定在 bindings.size() 的 index 上。
    virtual bool dispatch(uintptr_t kernel,
                          const std::vector<GpuBinding>& bindings,
                          const void* uniform_data, size_t uniform_size,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) {
        (void)kernel; (void)bindings; (void)uniform_data; (void)uniform_size;
        (void)grid_x; (void)grid_y; (void)grid_z;
        return false;
    }

    virtual void release_kernel(uintptr_t kernel) { (void)kernel; }

    virtual bool supports_compute() const { return false; }

    // 后端接受的 kernel 源码语言："msl"（Metal）或 "glsl"（Vulkan compute）。
    // run_gpu_op 据此从 GpuImageOp 选取 kernel_source / kernel_source_glsl。
    virtual std::string kernel_language() const { return "msl"; }

    // ===== Render 能力（默认实现 = 不支持；描述类型见 gpu_render_ops.hpp）=====
    // 离屏渲染到纹理。句柄语义与 buffer 一致：后端私有 opaque 句柄，0 表示失败。
    // 纹理生命周期由调用方管理（GpuTexture RAII 回调 free_texture）。

    virtual bool supports_render() const { return false; }

    // 创建纹理（render target + sampled usage），0 表示失败。
    virtual uintptr_t create_texture(const GpuTextureDesc& desc) {
        (void)desc;
        return 0;
    }

    virtual void free_texture(uintptr_t texture) { (void)texture; }

    // CPU 字节 <-> 纹理（按纹理尺寸紧密 RGBA 行布局搬运，size 为字节数）。
    virtual bool upload_texture(uintptr_t texture, const uint8_t* data, size_t size) {
        (void)texture; (void)data; (void)size;
        return false;
    }

    virtual bool download_texture(uintptr_t texture, uint8_t* data, size_t size) {
        (void)texture; (void)data; (void)size;
        return false;
    }

    // GPU 侧 buffer <-> 纹理（compute 链与 render 链互转，不经 CPU）。
    // buffer 为 allocate_gpu_memory 返回的句柄，size 为 buffer 字节数。
    virtual bool copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) {
        (void)buffer; (void)size; (void)texture;
        return false;
    }

    virtual bool copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) {
        (void)texture; (void)buffer; (void)size;
        return false;
    }

    // 采样器（0 = 失败）
    virtual uintptr_t create_sampler(const GpuSamplerDesc& desc) {
        (void)desc;
        return 0;
    }

    virtual void free_sampler(uintptr_t sampler) { (void)sampler; }

    // 编译渲染管线（按 desc.name 缓存，0 = 失败）。
    virtual uintptr_t compile_render_pipeline(const GpuRenderPipelineDesc& desc) {
        (void)desc;
        return 0;
    }

    virtual void release_render_pipeline(uintptr_t pipeline) { (void)pipeline; }

    // 立即模式 pass 录制：begin（打开目标纹理）-> render_draw（全屏三角形）->
    // end（结束本 pass 录制）。P1 起连续 pass 批处理在同一提交里，真正的
    // 提交与 CPU-GPU 同步发生在 wait_render_idle。
    virtual bool begin_render_pass(const GpuRenderPassDesc& desc) {
        (void)desc;
        return false;
    }

    virtual bool render_draw(const GpuDrawCall& draw) {
        (void)draw;
        return false;
    }

    virtual bool end_render_pass() { return false; }

    // 等待全部在飞渲染工作完成（提交批处理中的 pass 并阻塞至 GPU 执行完）。
    // 需要读到渲染结果的上传/下载/拷贝路径应先调用本方法。
    virtual bool wait_render_idle() { return true; }
};

using GpuBackendPtr = std::shared_ptr<IGpuImageBackend>;

bool to_gpu(Image& image);
bool to_cpu(Image& image);
bool ensure_cpu(Image& image);
bool ensure_gpu(Image& image);

void set_gpu_backend(GpuBackendPtr backend);
GpuBackendPtr get_gpu_backend();

}
