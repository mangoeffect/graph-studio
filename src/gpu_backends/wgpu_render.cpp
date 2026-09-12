// WgpuGpuBackend — wgpu-native 统一 GPU 后端：render 段（WGSL）。
//
// 模型与 Metal/Vulkan render 段同构（P1 批处理：end 只结束 pass 录制，
// wait_render_idle 统一 finish+submit+等待；上传/下载/拷贝路径内部先排空）。
//
// WGSL 渲染契约（与 render_task 的 kSharedWgslVertex / 预置效果一致）：
//   - 入口名固定 vs_main（vertex）/ fs_main（fragment），单 module 同文件；
//   - 绑定固定 group0（显式 layout，本文件创建）：
//       @binding(0..3) var texN : texture_2d<f32>;   // 采样输入，按序
//       @binding(4)     var samp : sampler;           // linear/clamp 由 desc 定
//       @binding(5)     var<uniform> uni : Uniforms;  // float4 v[8]，128B
//     显式 layout 固定 6 个 binding，shader 未静态使用的 binding 也必须在
//     bind group 中绑定 → 未用的纹理槽绑 4x4 dummy 纹理。
//   - uv 约定：uv(0,0)=图像左上（WebGPU NDC y 向上 + 帧缓冲 top-left，与
//     Vulkan 公式一致，不翻转；MSL 才需要 1-x）。
//   - uniform 128B：WebGPU 无 push_constant → 每 draw 一个临时小 UBO
//     （UNIFORM|COPY_DST + writeBuffer），bind group 引用，批提交完成后释放。
//   - 数据搬运：writeTexture 上传（无 256B 对齐要求）；下载/互拷走 staging
//     （copyTextureToBuffer 的 bytesPerRow 必须 256B 对齐）。
#include <task_graph/gpu_backends/wgpu_backend.hpp>
#include <task_graph/data_types.hpp>
#include "wgpu_backend_impl.hpp"

#include <atomic>
#include <cstring>
#include <cstdio>

namespace task_graph {

using wgpu_compat::str_view;

namespace {

WGPUTextureFormat wgpu_pixel_format(GpuTextureFormat f) {
    return f == GpuTextureFormat::RGBA32_FLOAT ? WGPUTextureFormat_RGBA32Float
                                               : WGPUTextureFormat_RGBA8Unorm;
}

uint32_t tex_bpp(GpuTextureFormat f) {
    return gpu_texture_format_bytes(f);
}

uint32_t align256(uint32_t v) {
    return (v + 255u) & ~255u;
}

// 渲染 uniform 块大小（与 render_task 的 kUniformBytes 一致；核心库不能引
// 插件头，这里独立定义，float4[8] = 128B）
constexpr uint32_t kRenderUniformBytes = 128;

}  // namespace

bool WgpuGpuBackend::supports_render() const {
    return is_available();
}

// ---- 纹理 ----

uintptr_t WgpuGpuBackend::create_texture(const GpuTextureDesc& desc) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || desc.width == 0 || desc.height == 0) {
        return 0;
    }
    WGPUTextureDescriptor d{};
    d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
              WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {desc.width, desc.height, 1};
    d.format = wgpu_pixel_format(desc.format);
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(impl_->device, &d);
    if (!tex) {
        return 0;
    }
    const uintptr_t handle = reinterpret_cast<uintptr_t>(tex);
    // webgpu.h 无法从句柄反查尺寸，登记 desc 供搬运路径使用
    impl_->texture_descs[handle] = desc;
    return handle;
}

void WgpuGpuBackend::free_texture(uintptr_t texture) {
    if (texture == 0) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    impl_->texture_descs.erase(texture);
    WGPUTexture tex = reinterpret_cast<WGPUTexture>(texture);
    if (impl_->batch_textures.count(texture)) {
        // 被未提交的批次引用：转 zombie，wait_render_idle 后销毁
        impl_->zombie_textures.push_back(tex);
        return;
    }
    wgpuTextureDestroy(tex);
    wgpuTextureRelease(tex);
}

bool WgpuGpuBackend::upload_texture(uintptr_t texture, const uint8_t* data, size_t size) {
    wait_render_idle();  // P1：排空在飞渲染批次再搬运
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || !data) {
        return false;
    }
    auto it = impl_->texture_descs.find(texture);
    if (it == impl_->texture_descs.end()) {
        return false;
    }
    const GpuTextureDesc& td = it->second;
    // writeTexture 无 256B 对齐要求，紧排直写
    const uint32_t bpr = td.width * tex_bpp(td.format);
    const size_t total = (size_t)bpr * td.height;
    if (size < total) {
        return false;
    }
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = reinterpret_cast<WGPUTexture>(texture);
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = bpr;
    layout.rowsPerImage = td.height;
    WGPUExtent3D extent = {td.width, td.height, 1};
    wgpuQueueWriteTexture(impl_->queue, &dst, data, total, &layout, &extent);
    return true;
}

bool WgpuGpuBackend::download_texture(uintptr_t texture, uint8_t* data, size_t size) {
    wait_render_idle();
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || !data) {
        return false;
    }
    auto it = impl_->texture_descs.find(texture);
    if (it == impl_->texture_descs.end()) {
        return false;
    }
    const GpuTextureDesc& td = it->second;
    const uint32_t bpr = td.width * tex_bpp(td.format);
    const uint32_t aligned = align256(bpr);
    const size_t total = (size_t)bpr * td.height;
    if (size < total) {
        return false;
    }

    // MAP_READ staging（256B 对齐行距）+ copyTextureToBuffer + 逐行拷出
    WGPUBufferDescriptor sd{};
    sd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    sd.size = (size_t)aligned * td.height;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(impl_->device, &sd);
    if (!staging) {
        return false;
    }
    WGPUTexelCopyTextureInfo src{};
    src.texture = reinterpret_cast<WGPUTexture>(texture);
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout = {0, aligned, td.height};
    WGPUExtent3D extent = {td.width, td.height, 1};

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    struct MapState {
        std::atomic<bool> done{false};
        bool ok = false;
    } map_state;
    {
        WGPUBufferMapCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = &map_state;
        cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
            auto* s = static_cast<MapState*>(u1);
            s->ok = status == WGPUMapAsyncStatus_Success;
            s->done.store(true, std::memory_order_release);
        };
        wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, sd.size, cb);
        impl_->spin_until(map_state);
    }
    bool ok = map_state.ok;
    if (ok) {
        const uint8_t* mapped =
            static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(staging, 0, sd.size));
        if (mapped) {
            if (aligned == bpr) {
                std::memcpy(data, mapped, total);
            } else {
                for (uint32_t y = 0; y < td.height; ++y) {
                    std::memcpy(data + (size_t)y * bpr, mapped + (size_t)y * aligned,
                                bpr);
                }
            }
        } else {
            ok = false;
        }
        wgpuBufferUnmap(staging);
    }
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    return ok;
}

bool WgpuGpuBackend::copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) {
    wait_render_idle();
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available()) {
        return false;
    }
    auto it = impl_->texture_descs.find(texture);
    if (it == impl_->texture_descs.end()) {
        return false;
    }
    const GpuTextureDesc& td = it->second;
    const uint32_t bpr = td.width * tex_bpp(td.format);
    const size_t total = (size_t)bpr * td.height;
    if (size < total) {
        return false;
    }
    WGPUBuffer src_buf = reinterpret_cast<WGPUBuffer>(buffer);
    if (!src_buf) {
        return false;
    }

    // 设备 buffer 是紧密行布局；copyBufferToTexture 的源必须 256B 对齐 →
    // 经 padded staging 中转（GPU-GPU，不经 CPU）。
    const uint32_t aligned = align256(bpr);
    WGPUBufferDescriptor sd{};
    sd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    sd.size = (size_t)aligned * td.height;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(impl_->device, &sd);
    if (!staging) {
        return false;
    }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    // 逐行设备->staging（写紧排数据到对齐行距：用 writeBuffer 逐行会经 CPU；
    // 直接 copyBufferToBuffer 逐行拷贝，bpr 行宽无对齐限制的是 offset——
    // copyBufferToBuffer 的 offset 需 4B 对齐（满足），size 任意）
    for (uint32_t y = 0; y < td.height; ++y) {
        wgpuCommandEncoderCopyBufferToBuffer(enc, src_buf, (uint64_t)y * bpr,
                                             staging, (uint64_t)y * aligned, bpr);
    }
    WGPUTexelCopyBufferInfo sbuf{};
    sbuf.buffer = staging;
    sbuf.layout = {0, aligned, td.height};
    WGPUTexelCopyTextureInfo dtex{};
    dtex.texture = reinterpret_cast<WGPUTexture>(texture);
    dtex.origin = {0, 0, 0};
    dtex.aspect = WGPUTextureAspect_All;
    WGPUExtent3D extent = {td.width, td.height, 1};
    wgpuCommandEncoderCopyBufferToTexture(enc, &sbuf, &dtex, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    impl_->wait_queue_idle();
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    return true;
}

bool WgpuGpuBackend::copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) {
    wait_render_idle();
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available()) {
        return false;
    }
    auto it = impl_->texture_descs.find(texture);
    if (it == impl_->texture_descs.end()) {
        return false;
    }
    const GpuTextureDesc& td = it->second;
    const uint32_t bpr = td.width * tex_bpp(td.format);
    const size_t total = (size_t)bpr * td.height;
    if (size < total) {
        return false;
    }
    WGPUBuffer dst_buf = reinterpret_cast<WGPUBuffer>(buffer);
    if (!dst_buf) {
        return false;
    }
    const uint32_t aligned = align256(bpr);
    WGPUBufferDescriptor sd{};
    sd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    sd.size = (size_t)aligned * td.height;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(impl_->device, &sd);
    if (!staging) {
        return false;
    }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    WGPUTexelCopyTextureInfo stex{};
    stex.texture = reinterpret_cast<WGPUTexture>(texture);
    stex.origin = {0, 0, 0};
    stex.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dbuf{};
    dbuf.buffer = staging;
    dbuf.layout = {0, aligned, td.height};
    WGPUExtent3D extent = {td.width, td.height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &stex, &dbuf, &extent);
    for (uint32_t y = 0; y < td.height; ++y) {
        wgpuCommandEncoderCopyBufferToBuffer(enc, staging, (uint64_t)y * aligned,
                                             dst_buf, (uint64_t)y * bpr, bpr);
    }
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    impl_->wait_queue_idle();
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    return true;
}

// ---- 采样器 ----

uintptr_t WgpuGpuBackend::create_sampler(const GpuSamplerDesc& desc) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available()) {
        return 0;
    }
    // 按（filter, addressMode）组合缓存；句柄归缓存所有，free_sampler 为 no-op
    const int key = (desc.linear ? 1 : 0) | (desc.clamp_to_edge ? 2 : 0);
    auto it = impl_->sampler_cache.find(std::to_string(key));
    if (it != impl_->sampler_cache.end()) {
        return reinterpret_cast<uintptr_t>(it->second);
    }
    WGPUSamplerDescriptor d{};
    d.minFilter = desc.linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    d.magFilter = d.minFilter;
    d.addressModeU = desc.clamp_to_edge ? WGPUAddressMode_ClampToEdge
                                        : WGPUAddressMode_Repeat;
    d.addressModeV = d.addressModeU;
    d.addressModeW = d.addressModeU;
    // 零初始化非法项：maxAnisotropy 须 1..16，lodMaxClamp 须 >= lodMinClamp
    d.lodMinClamp = 0.0f;
    d.lodMaxClamp = 32.0f;
    d.maxAnisotropy = 1;
    WGPUSampler s = wgpuDeviceCreateSampler(impl_->device, &d);
    if (!s) {
        return 0;
    }
    impl_->sampler_cache[std::to_string(key)] = s;
    return reinterpret_cast<uintptr_t>(s);
}

void WgpuGpuBackend::free_sampler(uintptr_t sampler) {
    (void)sampler;  // 缓存持有所有权，shutdown 统一释放
}

// ---- 管线 ----

uintptr_t WgpuGpuBackend::compile_render_pipeline(const GpuRenderPipelineDesc& desc) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || desc.wgsl_source.empty()) {
        // 明确报错：wgpu 后端只接受 WGSL（MSL/GLSL 字段被忽略）
        if (is_available() && desc.wgsl_source.empty()) {
            std::fprintf(stderr,
                         "  [wgpu-render] pipeline '%s': no wgsl_source "
                         "(wgpu backend requires WGSL)\n", desc.name.c_str());
        }
        return 0;
    }
    auto it = impl_->render_pipeline_cache.find(desc.name);
    if (it != impl_->render_pipeline_cache.end()) {
        return reinterpret_cast<uintptr_t>(it->second);
    }

    // 固定绑定布局 + dummy 纹理（懒创建）
    if (!impl_->render_bgl) {
        WGPUBindGroupLayoutEntry entries[6] = {};
        for (uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = WGPUShaderStage_Fragment;
            entries[i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Fragment;
        entries[4].sampler.type = WGPUSamplerBindingType_Filtering;
        entries[5].binding = 5;
        entries[5].visibility = WGPUShaderStage_Fragment;
        entries[5].buffer.type = WGPUBufferBindingType_Uniform;
        entries[5].buffer.minBindingSize = kRenderUniformBytes;

        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 6;
        bd.entries = entries;
        impl_->render_bgl = wgpuDeviceCreateBindGroupLayout(impl_->device, &bd);
        if (!impl_->render_bgl) {
            return 0;
        }
        WGPUBindGroupLayout layouts[1] = {impl_->render_bgl};
        WGPUPipelineLayoutDescriptor ld{};
        ld.bindGroupLayoutCount = 1;
        ld.bindGroupLayouts = layouts;
        impl_->render_layout = wgpuDeviceCreatePipelineLayout(impl_->device, &ld);
        if (!impl_->render_layout) {
            return 0;
        }
        // 4x4 dummy（未用纹理槽位兜底）
        WGPUTextureDescriptor td{};
        td.usage = WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.size = {4, 4, 1};
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        impl_->dummy_texture = wgpuDeviceCreateTexture(impl_->device, &td);
        if (impl_->dummy_texture) {
            impl_->dummy_view = wgpuTextureCreateView(impl_->dummy_texture, nullptr);
        }
        if (!impl_->dummy_view) {
            return 0;
        }
    }

    WGPUShaderModule module = impl_->create_shader_module(desc.wgsl_source,
                                                           desc.name.c_str());
    if (!module) {
        return 0;
    }

    WGPUVertexState vs{};
    vs.module = module;
    vs.entryPoint = str_view("vs_main");

    WGPUBlendComponent blend_color{};
    blend_color.operation = WGPUBlendOperation_Add;
    blend_color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend_color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUBlendComponent blend_alpha{};
    blend_alpha.operation = WGPUBlendOperation_Add;
    blend_alpha.srcFactor = WGPUBlendFactor_One;
    blend_alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUBlendState blend{};
    blend.color = blend_color;
    blend.alpha = blend_alpha;

    WGPUColorTargetState target{};
    target.format = wgpu_pixel_format(desc.target_format);
    target.blend = desc.enable_blend ? &blend : nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module = module;
    fs.entryPoint = str_view("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;

    WGPURenderPipelineDescriptor d{};
    d.layout = impl_->render_layout;
    d.vertex = vs;
    d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    d.multisample.count = 1;
    d.multisample.mask = 0xFFFFFFFFu;  // 零初始化的 mask=0 非法，必须显式全开
    d.fragment = &fs;

    WGPURenderPipeline pipe = nullptr;
    const bool ok = impl_->validated(desc.name.c_str(), [&] {
        pipe = wgpuDeviceCreateRenderPipeline(impl_->device, &d);
    });
    wgpuShaderModuleRelease(module);
    if (!ok || !pipe) {
        if (pipe) wgpuRenderPipelineRelease(pipe);
        return 0;
    }
    impl_->render_pipeline_cache[desc.name] = pipe;
    return reinterpret_cast<uintptr_t>(pipe);
}

void WgpuGpuBackend::release_render_pipeline(uintptr_t pipeline) {
    (void)pipeline;  // 管线由 cache 持有，随 shutdown 释放
}

// ---- 立即模式 pass（P1 批处理）----

bool WgpuGpuBackend::begin_render_pass(const GpuRenderPassDesc& desc) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || desc.color_targets.empty()) {
        return false;
    }
    if (impl_->pass_open) {
        return false;  // 上一个 pass 未 end（违反 begin..end 契约）
    }
    auto it = impl_->texture_descs.find(desc.color_targets[0]);
    if (it == impl_->texture_descs.end()) {
        return false;  // 目标必须由 create_texture 创建（登记过 desc）
    }

    // 批首创建 command encoder；连续 pass 共享（同 encoder 内 pass 有序）
    if (!impl_->render_cmd) {
        impl_->render_cmd = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
        if (!impl_->render_cmd) {
            return false;
        }
    }

    WGPUTextureView view = wgpuTextureCreateView(
        reinterpret_cast<WGPUTexture>(desc.color_targets[0]), nullptr);
    if (!view) {
        return false;
    }
    impl_->pending_views.push_back(view);
    impl_->batch_textures.insert(desc.color_targets[0]);

    WGPURenderPassColorAttachment att{};
    att.view = view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // 零初始化=0 会被当成 3D 切片
    att.loadOp = desc.clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue = {desc.clear_color[0], desc.clear_color[1],
                      desc.clear_color[2], desc.clear_color[3]};
    WGPURenderPassDescriptor pd{};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &att;
    impl_->render_encoder = wgpuCommandEncoderBeginRenderPass(impl_->render_cmd, &pd);
    if (!impl_->render_encoder) {
        return false;
    }
    impl_->pass_open = true;
    return true;
}

bool WgpuGpuBackend::render_draw(const GpuDrawCall& draw) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || !impl_->pass_open) {
        return false;
    }
    WGPURenderPipeline pipe = reinterpret_cast<WGPURenderPipeline>(draw.pipeline);
    WGPUSampler sampler = draw.sampler
                              ? reinterpret_cast<WGPUSampler>(draw.sampler)
                              : nullptr;
    if (!pipe || !sampler) {
        return false;
    }
    WGPURenderPassEncoder enc = impl_->render_encoder;

    // per-draw uniform 小 UBO（固定 128B：布局恒有 binding5，无数据时绑零块）
    WGPUBufferDescriptor ud{};
    ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ud.size = kRenderUniformBytes;
    WGPUBuffer uniform_buf = wgpuDeviceCreateBuffer(impl_->device, &ud);
    if (!uniform_buf) {
        return false;
    }
    if (draw.uniform_data && draw.uniform_size > 0) {
        wgpuQueueWriteBuffer(impl_->queue, uniform_buf, 0, draw.uniform_data,
                             draw.uniform_size);
    }
    impl_->pending_buffers.push_back(uniform_buf);

    WGPUTextureView views[4] = {};
    for (size_t i = 0; i < 4; ++i) {
        if (i < draw.textures.size() && draw.textures[i] != 0) {
            views[i] = wgpuTextureCreateView(reinterpret_cast<WGPUTexture>(draw.textures[i]),
                                             nullptr);
        } else {
            views[i] = impl_->dummy_view;  // 布局固定 6 binding：未用槽绑 dummy
        }
        if (!views[i]) {
            return false;
        }
        if (views[i] != impl_->dummy_view) {
            // dummy_view 由 impl 长期持有，绝不进 pending（否则 wait 时被
            // 释放，后续批次引用已释放 view → Metal 侧堆损坏）
            impl_->pending_views.push_back(views[i]);
            if (i < draw.textures.size() && draw.textures[i] != 0) {
                impl_->batch_textures.insert(draw.textures[i]);
            }
        }
    }

    WGPUBindGroupEntry entries[6] = {};
    for (uint32_t i = 0; i < 4; ++i) {
        entries[i].binding = i;
        entries[i].textureView = views[i];
    }
    entries[4].binding = 4;
    entries[4].sampler = sampler;
    entries[5].binding = 5;
    entries[5].buffer = uniform_buf;
    entries[5].offset = 0;
    entries[5].size = kRenderUniformBytes;

    WGPUBindGroupDescriptor bd{};
    bd.layout = impl_->render_bgl;
    bd.entryCount = 6;
    bd.entries = entries;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(impl_->device, &bd);
    if (!bg) {
        return false;
    }
    impl_->pending_bind_groups.push_back(bg);

    wgpuRenderPassEncoderSetPipeline(enc, pipe);
    wgpuRenderPassEncoderSetBindGroup(enc, 0, bg, 0, nullptr);
    // 全屏三角形：顶点由 shader 内置（vertex_index 索引 3 顶点），无顶点缓冲
    wgpuRenderPassEncoderDraw(enc, 3, 1, 0, 0);
    return true;
}

bool WgpuGpuBackend::end_render_pass() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || !impl_->pass_open) {
        return false;
    }
    // 只结束本 pass 的录制；command encoder 保持，提交延迟到 wait_render_idle
    wgpuRenderPassEncoderEnd(impl_->render_encoder);
    wgpuRenderPassEncoderRelease(impl_->render_encoder);
    impl_->render_encoder = nullptr;
    impl_->pass_open = false;
    return true;
}

bool WgpuGpuBackend::wait_render_idle() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available()) {
        return false;
    }
    if (impl_->pass_open) {
        return false;  // 违反 begin..end 契约
    }
    if (!impl_->render_cmd) {
        return true;  // 无在飞渲染工作
    }
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(impl_->render_cmd, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(impl_->render_cmd);
    impl_->render_cmd = nullptr;

    bool ok = impl_->wait_queue_idle();

    // 提交完成，释放批处理期临时资源（bind group / uniform / view）
    for (WGPUBindGroup bg : impl_->pending_bind_groups) wgpuBindGroupRelease(bg);
    for (WGPUBuffer b : impl_->pending_buffers) wgpuBufferRelease(b);
    for (WGPUTextureView v : impl_->pending_views) wgpuTextureViewRelease(v);
    impl_->pending_bind_groups.clear();
    impl_->pending_buffers.clear();
    impl_->pending_views.clear();
    // 批次已提交完成：销毁被延迟的 zombie 纹理，复位批次纹理集合
    for (WGPUTexture t : impl_->zombie_textures) {
        wgpuTextureDestroy(t);
        wgpuTextureRelease(t);
    }
    impl_->zombie_textures.clear();
    impl_->batch_textures.clear();
    return ok;
}

}  // namespace task_graph
