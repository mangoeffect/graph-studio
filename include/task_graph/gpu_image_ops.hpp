#pragma once

#include <task_graph/data_types.hpp>
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
};

using GpuBackendPtr = std::shared_ptr<IGpuImageBackend>;

bool to_gpu(Image& image);
bool to_cpu(Image& image);
bool ensure_cpu(Image& image);
bool ensure_gpu(Image& image);

void set_gpu_backend(GpuBackendPtr backend);
GpuBackendPtr get_gpu_backend();

}
