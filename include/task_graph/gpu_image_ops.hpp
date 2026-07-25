#pragma once

#include <task_graph/data_types.hpp>
#include <memory>
#include <string>

namespace task_graph {

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
};

using GpuBackendPtr = std::shared_ptr<IGpuImageBackend>;

bool to_gpu(Image& image);
bool to_cpu(Image& image);
bool ensure_cpu(Image& image);
bool ensure_gpu(Image& image);

void set_gpu_backend(GpuBackendPtr backend);
GpuBackendPtr get_gpu_backend();

}
