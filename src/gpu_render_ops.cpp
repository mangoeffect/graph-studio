#include <task_graph/gpu_render_ops.hpp>
#include <task_graph/gpu_texture.hpp>
#include <task_graph/gpu_buffer.hpp>
#include <task_graph/gpu_image_ops.hpp>
#include <plugin_api.hpp>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace task_graph {

namespace {

// ---- P4：GPU 侧 非 RGBA -> RGBA pack kernel（MSL + GLSL 双源）----
// mode：1=GRAY, 2=RGB, 3=BGR, 4=RGBA 直拷, 5=BGRA。grid = (width, height)。

const char* kPackRgbaMsl = R"METAL(#include <metal_stdlib>
using namespace metal;

kernel void tg_pack_rgba(device const uchar* src [[buffer(0)]],
                         device uchar* dst       [[buffer(1)]],
                         constant const uint* u  [[buffer(2)]],
                         uint2 gid [[thread_position_in_grid]]) {
    const uint width = u[0], height = u[1], mode = u[2];
    if (gid.x >= width || gid.y >= height) return;
    const size_t p = (size_t)gid.y * width + gid.x;
    uchar r, g, b, a;
    switch (mode) {
        case 1:  // GRAY
            r = g = b = src[p]; a = 255; break;
        case 2:  // RGB
            r = src[p * 3]; g = src[p * 3 + 1]; b = src[p * 3 + 2]; a = 255; break;
        case 3:  // BGR
            b = src[p * 3]; g = src[p * 3 + 1]; r = src[p * 3 + 2]; a = 255; break;
        case 5:  // BGRA
            b = src[p * 4]; g = src[p * 4 + 1]; r = src[p * 4 + 2]; a = src[p * 4 + 3]; break;
        default:  // RGBA 直拷
            r = src[p * 4]; g = src[p * 4 + 1]; b = src[p * 4 + 2]; a = src[p * 4 + 3]; break;
    }
    dst[p * 4 + 0] = r; dst[p * 4 + 1] = g; dst[p * 4 + 2] = b; dst[p * 4 + 3] = a;
}
)METAL";

const char* kPackRgbaGlsl = R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 0) readonly buffer SrcBuf { uint8_t src[]; };
layout(std430, set = 0, binding = 1) buffer DstBuf { uint8_t dst[]; };
layout(push_constant) uniform Uni { uint u[16]; } pc;
void main() {
    const uint width = pc.u[0], height = pc.u[1], mode = pc.u[2];
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= width || gid.y >= height) return;
    uint p = gid.y * width + gid.x;
    uint8_t r, g, b, a;
    if (mode == 1u) { r = g = b = src[p]; a = uint8_t(255); }
    else if (mode == 2u) { r = src[p*3u]; g = src[p*3u+1u]; b = src[p*3u+2u]; a = uint8_t(255); }
    else if (mode == 3u) { b = src[p*3u]; g = src[p*3u+1u]; r = src[p*3u+2u]; a = uint8_t(255); }
    else if (mode == 5u) { b = src[p*4u]; g = src[p*4u+1u]; r = src[p*4u+2u]; a = src[p*4u+3u]; }
    else { r = src[p*4u]; g = src[p*4u+1u]; b = src[p*4u+2u]; a = src[p*4u+3u]; }
    dst[p*4u+0u] = r; dst[p*4u+1u] = g; dst[p*4u+2u] = b; dst[p*4u+3u] = a;
}
)GLSL";

// WGSL（wgpu 后端）：entry 名 = kernel 名（"tg_pack_rgba"），绑定 0=src
// storage read / 1=dst storage read_write / 2=uniform（vec4<u32>）。WebGPU
// 的 storage 不支持 u8 数组视角，用 u32 位操作按字节拆取（小端，全平台一致）。
// @workgroup_size(1,1,1)，grid 直派 workgroup（见 wgpu_backend.cpp 约定）。
const char* kPackRgbaWgsl = R"WGSL(
@group(0) @binding(0) var<storage, read> src: array<u32>;
@group(0) @binding(1) var<storage, read_write> dst: array<u32>;
struct Uni {
    u: vec4<u32>,
};
@group(0) @binding(2) var<uniform> uni: Uni;

fn get_byte(i: u32) -> u32 {
    let word = src[i / 4u];
    return (word >> ((i % 4u) * 8u)) & 0xFFu;
}

@compute @workgroup_size(1, 1, 1)
fn tg_pack_rgba(@builtin(global_invocation_id) g: vec3<u32>) {
    let width = uni.u.x;
    let height = uni.u.y;
    let mode = uni.u.z;
    if (g.x >= width || g.y >= height) {
        return;
    }
    let p = g.y * width + g.x;
    var r: u32 = 0u;
    var g: u32 = 0u;
    var b: u32 = 0u;
    var a: u32 = 255u;
    if (mode == 1u) {  // GRAY
        let v = get_byte(p);
        r = v; g = v; b = v;
    } else if (mode == 2u) {  // RGB
        r = get_byte(p * 3u); g = get_byte(p * 3u + 1u); b = get_byte(p * 3u + 2u);
    } else if (mode == 3u) {  // BGR
        b = get_byte(p * 3u); g = get_byte(p * 3u + 1u); r = get_byte(p * 3u + 2u);
    } else if (mode == 5u) {  // BGRA
        b = get_byte(p * 4u); g = get_byte(p * 4u + 1u);
        r = get_byte(p * 4u + 2u); a = get_byte(p * 4u + 3u);
    } else {  // RGBA 直拷
        r = get_byte(p * 4u); g = get_byte(p * 4u + 1u);
        b = get_byte(p * 4u + 2u); a = get_byte(p * 4u + 3u);
    }
    // 小端：dst 的 u32 word 按字节序 r,g,b,a（每线程独占一个 word，无竞争）
    dst[p] = r | (g << 8u) | (b << 16u) | (a << 24u);
}
)WGSL";

}  // namespace

bool pack_gpu_buffer_to_rgba(Image& image) {
    if (image.gpu_texture) {
        return true;  // 纹理驻留：采样即可，无需 pack
    }
    if (image.gpu_handle == 0 || !image.is_on_gpu() ||
        image.width <= 0 || image.height <= 0 ||
        image.data_type != DataType::UINT8) {
        return false;
    }
    const int c = image.channels;
    int mode = -1;
    if (c == 1) {
        mode = 1;
    } else if (c == 3) {
        mode = image.pixel_format == PixelFormat::BGR ? 3 : 2;
    } else if (c == 4) {
        if (image.pixel_format == PixelFormat::RGBA) return true;  // 已是 RGBA
        mode = image.pixel_format == PixelFormat::BGRA ? 5 : 4;
    } else {
        return false;
    }

    auto backend = get_gpu_backend();
    if (!backend || !backend->is_available() || !backend->supports_compute()) {
        return false;
    }

    // kernel 编译缓存（每后端一份）
    static std::mutex cache_mutex;
    static std::unordered_map<IGpuImageBackend*, uintptr_t> kernel_cache;
    uintptr_t kernel = 0;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = kernel_cache.find(backend.get());
        if (it != kernel_cache.end()) {
            kernel = it->second;
        }
    }
    if (kernel == 0) {
        const std::string lang = backend->kernel_language();
        const char* src = lang == "glsl"   ? kPackRgbaGlsl
                          : lang == "wgsl" ? kPackRgbaWgsl
                                           : kPackRgbaMsl;
        kernel = backend->compile_kernel("tg_pack_rgba", src);
        if (kernel == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(cache_mutex);
        kernel_cache[backend.get()] = kernel;
    }

    const size_t pixels = static_cast<size_t>(image.width) * image.height;
    const size_t dst_bytes = pixels * 4;
    uintptr_t dst = backend->allocate_gpu_memory(dst_bytes);
    if (dst == 0) {
        return false;
    }

    // WGSL 侧 uniform 是 vec4<u32>（16B，须整体写入）；MSL/GLSL 是 uint[3]
    const uint32_t uniform[4] = {static_cast<uint32_t>(image.width),
                                 static_cast<uint32_t>(image.height),
                                 static_cast<uint32_t>(mode), 0};
    const size_t uniform_size = backend->kernel_language() == "wgsl" ? 16 : 12;
    const std::vector<GpuBinding> bindings = {
        {image.gpu_handle}, {dst}};
    if (!backend->dispatch(kernel, bindings, uniform, uniform_size,
                           static_cast<uint32_t>(image.width),
                           static_cast<uint32_t>(image.height), 1)) {
        backend->free_gpu_memory(dst);
        return false;
    }

    // 就地改写：新 RGBA buffer（旧 buffer 所有权仍归上游 Image，这里不释放）
    image.gpu_handle = dst;
    image.gpu_buffer = std::make_shared<GpuBuffer>(dst, dst_bytes, backend);
    image.channels = 4;
    image.pixel_format = PixelFormat::RGBA;
    image.location = MemoryLocation::GPU;
    return true;
}

namespace {

// 从 Image 推导纹理描述；布局不满足 RGBA 纹理契约（4 通道 + UINT8/FLOAT32）
// 返回 false（通道 swizzle/pad 是任务层职责，这里只搬运字节）。
bool texture_desc_of(const Image& image, GpuTextureDesc& desc) {
    if (image.width <= 0 || image.height <= 0 || image.channels != 4) {
        return false;
    }
    if (image.data_type == DataType::UINT8) {
        desc.format = GpuTextureFormat::RGBA8_UNORM;
    } else if (image.data_type == DataType::FLOAT32) {
        desc.format = GpuTextureFormat::RGBA32_FLOAT;
    } else {
        return false;
    }
    desc.width = static_cast<uint32_t>(image.width);
    desc.height = static_cast<uint32_t>(image.height);
    return true;
}

}  // namespace

bool ensure_texture(Image& image) {
    if (image.gpu_texture) {
        return true;
    }
    auto backend = get_gpu_backend();
    if (!backend || !backend->is_available() || !backend->supports_render()) {
        return false;
    }

    GpuTextureDesc desc;
    if (!texture_desc_of(image, desc)) {
        return false;
    }
    const size_t tex_bytes = static_cast<size_t>(desc.width) * desc.height *
                             gpu_texture_format_bytes(desc.format);

    uintptr_t tex = backend->create_texture(desc);
    if (tex == 0) {
        return false;
    }

    bool ok = false;
    if (image.gpu_handle != 0 && image.is_on_gpu()) {
        // compute 链输出：GPU 侧 buffer -> 纹理（不经 CPU）
        ok = backend->copy_buffer_to_texture(image.gpu_handle, tex_bytes, tex);
    } else if (image.is_on_cpu() && image.ptr()) {
        ok = backend->upload_texture(tex, image.ptr(), tex_bytes);
    }

    if (!ok) {
        backend->free_texture(tex);
        return false;
    }

    image.gpu_texture = std::make_shared<GpuTexture>(tex, desc, backend);
    if (!image.is_on_gpu()) {
        image.location = MemoryLocation::BOTH;  // CPU 数据仍在（或刚上传）
    }
    return true;
}

bool ensure_gpu_buffer(Image& image) {
    if (image.gpu_handle != 0) {
        return true;
    }
    if (!image.gpu_texture) {
        return false;
    }
    auto backend = get_gpu_backend();
    if (!backend || !backend->is_available()) {
        return false;
    }

    const GpuTextureDesc& desc = image.gpu_texture->desc();
    const size_t tex_bytes = static_cast<size_t>(desc.width) * desc.height *
                             gpu_texture_format_bytes(desc.format);

    uintptr_t buf = backend->allocate_gpu_memory(tex_bytes);
    if (buf == 0) {
        return false;
    }
    if (!backend->copy_texture_to_buffer(image.gpu_texture->handle(), buf, tex_bytes)) {
        backend->free_gpu_memory(buf);
        return false;
    }

    image.gpu_handle = buf;
    image.gpu_buffer = std::make_shared<GpuBuffer>(buf, tex_bytes, backend);
    image.location = MemoryLocation::GPU;  // buffer 与纹理同驻（纹理仍在 gpu_texture）
    return true;
}

}  // namespace task_graph
