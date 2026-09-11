#include <task_graph/gpu_image_ops.hpp>
#include <task_graph/gpu_buffer.hpp>
#include <task_graph/gpu_texture.hpp>
#include <mutex>
#include <stdexcept>

namespace task_graph {

namespace {

std::shared_ptr<IGpuImageBackend> g_backend;
std::mutex g_backend_mutex;

class NullGpuBackend : public IGpuImageBackend {
public:
    bool init() override { return true; }
    void shutdown() override {}

    bool upload_to_gpu(Image& image) override {
        (void)image;
        return false;
    }

    bool download_to_cpu(Image& image) override {
        (void)image;
        return false;
    }

    bool copy_to_gpu(Image& image, uintptr_t gpu_ptr) override {
        (void)image;
        (void)gpu_ptr;
        return false;
    }

    bool release_gpu_memory(Image& image) override {
        (void)image;
        return false;
    }

    uintptr_t allocate_gpu_memory(size_t size) override {
        (void)size;
        return 0;
    }

    void free_gpu_memory(uintptr_t handle) override {
        (void)handle;
    }

    std::string get_backend_name() const override {
        return "null";
    }

    bool is_available() const override {
        return false;
    }
};

}

void set_gpu_backend(GpuBackendPtr backend) {
    std::lock_guard<std::mutex> lock(g_backend_mutex);
    g_backend = backend;
}

GpuBackendPtr get_gpu_backend() {
    std::lock_guard<std::mutex> lock(g_backend_mutex);
    if (!g_backend) {
        g_backend = std::make_shared<NullGpuBackend>();
    }
    return g_backend;
}

bool to_gpu(Image& image) {
    if (!image.is_on_cpu()) {
        return false;
    }

    auto backend = get_gpu_backend();
    if (!backend->is_available()) {
        return false;
    }

    if (backend->upload_to_gpu(image)) {
        image.gpu_buffer = std::make_shared<GpuBuffer>(
            image.gpu_handle, image.total_size(), backend);
        image.location = MemoryLocation::BOTH;
        return true;
    }

    return false;
}

bool to_cpu(Image& image) {
    if (!image.is_on_gpu()) {
        return false;
    }

    auto backend = get_gpu_backend();
    if (!backend->is_available()) {
        return false;
    }

    // 纹理驻留（render 链输出且未转过 buffer）：走纹理下载路径。
    if (image.gpu_texture && image.gpu_handle == 0) {
        const GpuTextureDesc& desc = image.gpu_texture->desc();
        size_t totalBytes = static_cast<size_t>(desc.width) * desc.height *
                            gpu_texture_format_bytes(desc.format);
        if (!image.data) {
            image.data = std::make_shared<std::vector<uint8_t>>(totalBytes);
        }
        if (image.data->size() != totalBytes) {
            image.data->resize(totalBytes);
        }
        if (!backend->download_texture(image.gpu_texture->handle(), image.ptr(), totalBytes)) {
            return false;
        }
        image.location = MemoryLocation::BOTH;
        return true;
    }

    if (backend->download_to_cpu(image)) {
        image.location = MemoryLocation::BOTH;
        return true;
    }

    return false;
}

bool ensure_cpu(Image& image) {
    if (image.is_on_cpu()) {
        return true;
    }
    return to_cpu(image);
}

bool ensure_gpu(Image& image) {
    if (image.is_on_gpu()) {
        // GPU 驻留但只有纹理（render 链输出）：转为 compute 链可用的 buffer。
        if (image.gpu_handle == 0 && image.gpu_texture) {
            return ensure_gpu_buffer(image);
        }
        return true;
    }
    return to_gpu(image);
}

}
