#include <task_graph/gpu_render_ops.hpp>
#include <task_graph/gpu_texture.hpp>
#include <task_graph/gpu_buffer.hpp>
#include <task_graph/gpu_image_ops.hpp>

#include <vector>

namespace task_graph {

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
