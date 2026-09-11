#pragma once

// GPU 渲染（离屏 render-to-texture）能力的描述类型与 Image 侧辅助函数。
//
// 与 compute 能力（IGpuImageBackend 的 compile_kernel/dispatch 段）同模式：
// 后端以"默认实现 = 不支持"的方式实现这组虚函数，supports_render() 探测。
// 渲染模型为立即模式：begin_render_pass -> render_draw* -> end_render_pass，
// 几何固定为全屏三角形（vertex shader 内置顶点，无顶点缓冲 API），一次 draw
// 覆盖整个 render target —— 覆盖图像后处理/多层合成式的渲染场景。
// 同步语义与 compute 一致：end_render_pass 提交并等待 GPU 完成。
//
// 渲染目标（render target）一律是后端私有纹理（create_texture 创建，
// RenderTarget | ShaderRead usage）；采样输入纹理同样用 create_texture +
// upload_texture / copy_buffer_to_texture 填充。多 pass = 多次 begin/end
// （任务内串行）或 DAG 图级串联（纹理经 Image::gpu_texture 在端口间链式传递）。

#include <task_graph/data_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace task_graph {

// 纹理像素格式（render target 与采样输入共用）。
// RGBA8 覆盖常规 8bit 图像链路；RGBA32F 用于 HDR 中间 pass。
// 注意任务层的契约：送入纹理的数据必须是 RGBA 字节序（BGRA/RGB 由任务层
// 先做 CPU 侧 swizzle/pad），shader 一律以 .rgba 语义读写。
enum class GpuTextureFormat {
    RGBA8_UNORM = 0,
    RGBA32_FLOAT = 1,
};

inline uint32_t gpu_texture_format_bytes(GpuTextureFormat f) {
    return f == GpuTextureFormat::RGBA32_FLOAT ? 16u : 4u;
}

// 纹理描述
struct GpuTextureDesc {
    uint32_t width{0};
    uint32_t height{0};
    GpuTextureFormat format{GpuTextureFormat::RGBA8_UNORM};
};

// 采样器描述（首期仅 linear/clamp 两个开关）
struct GpuSamplerDesc {
    bool linear{true};
    bool clamp_to_edge{true};
};

// 渲染管线描述：一份效果双语言源码（与 GpuImageOp 的 kernel_source/_glsl 约定一致）。
// Metal 用单份 MSL 源（vertex/fragment 函数名可指定，vertex 默认共享全屏三角形）；
// Vulkan 用 GLSL 双源（vertex/fragment 各一，shaderc 运行时编译）。
// target_format 是渲染目标的像素格式：管线与目标格式绑定（Metal pipeline
// descriptor / Vulkan renderpass 兼容性都按格式区分），缓存键需含格式。
struct GpuRenderPipelineDesc {
    std::string name;  // 缓存键（任务层负责保证唯一，建议含格式后缀）
    std::string msl_source;
    std::string msl_vertex_name{"fullscreen_vertex"};
    std::string msl_fragment_name;
    std::string glsl_vertex_source;
    std::string glsl_fragment_source;
    GpuTextureFormat target_format{GpuTextureFormat::RGBA8_UNORM};
    // 标准 alpha blend（src * srcAlpha + dst * (1 - srcAlpha)）
    bool enable_blend{false};
};

// 渲染 pass 描述：viewport 取 color_targets[0] 的尺寸。
struct GpuRenderPassDesc {
    std::vector<uintptr_t> color_targets;  // 纹理句柄（首期单目标）
    bool clear{true};
    std::array<float, 4> clear_color{0.f, 0.f, 0.f, 0.f};
};

// 一次全屏绘制：管线 + 采样纹理（binding 0..N-1，上限由后端定，当前 4）+
// fragment uniform（Metal setFragmentBytes / Vulkan fragment push_constant 128B）。
struct GpuDrawCall {
    uintptr_t pipeline{0};
    uintptr_t sampler{0};
    std::vector<uintptr_t> textures;
    const void* uniform_data{nullptr};
    size_t uniform_size{0};
};

class GpuTexture;

// Image 侧位置迁移辅助（定义在 src/gpu_render_ops.cpp）：
//
// ensure_texture(image)：确保 image 持有一张 RGBA 纹理（image.gpu_texture）。
//   已有纹理直接返回；只有 GPU buffer（compute 链输出）走 GPU 侧 buffer->texture
//   拷贝；只有 CPU 数据走 upload_texture。要求 image 已是 4 通道 RGBA 布局
//   （UINT8 -> RGBA8_UNORM / FLOAT32 -> RGBA32_FLOAT），否则失败——通道
//   swizzle/pad 是任务层的职责，这里只搬运字节。
bool ensure_texture(Image& image);

// ensure_gpu_buffer(image)：确保 image 持有可 compute dispatch 的 GPU buffer
// （image.gpu_handle）。已有 buffer 直接返回；只有纹理（render 输出）走 GPU 侧
// texture->buffer 拷贝。ensure_gpu 内部也会调用本逻辑（compute 链消费 render
// 输出时不经 CPU）。
bool ensure_gpu_buffer(Image& image);

// pack_gpu_buffer_to_rgba(image)：GPU-resident 非 RGBA 布局 buffer（GRAY/RGB/
// BGR/BGRA，UINT8）经 compute kernel 在 GPU 侧 swizzle/pad 成 RGBA buffer，
// 就地改写 image（channels=4、RGBA、gpu_handle 换新）——render 链消费 compute
// 链输出的非 RGBA buffer 时免去 CPU 往返。已是 RGBA/纹理驻留时为 no-op true；
// 后端无 compute 能力（或失败）返回 false，调用方回退 CPU 路径。
bool pack_gpu_buffer_to_rgba(Image& image);

}  // namespace task_graph
