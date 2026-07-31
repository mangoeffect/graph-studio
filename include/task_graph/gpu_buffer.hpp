#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <cstddef>
#include <memory>

namespace task_graph {

// GpuBuffer：GPU 内存的 RAII 所有者。
//
// 持有 raw handle + backend 引用，析构时回调 backend->free_gpu_memory。
// 通过 shared_ptr<GpuBuffer> 共享所有权，使 Image 在 DAG task 间链式传递时
// GPU buffer 生命周期安全（最后一个引用销毁时释放）。
class GpuBuffer {
public:
    GpuBuffer() = default;

    GpuBuffer(uintptr_t handle, size_t size, GpuBackendPtr backend)
        : handle_(handle), size_(size), backend_(std::move(backend)) {}

    ~GpuBuffer() {
        if (handle_ != 0 && backend_) {
            backend_->free_gpu_memory(handle_);
        }
    }

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    GpuBuffer(GpuBuffer&& other) noexcept
        : handle_(other.handle_), size_(other.size_),
          backend_(std::move(other.backend_)) {
        other.handle_ = 0;
        other.size_ = 0;
    }

    GpuBuffer& operator=(GpuBuffer&& other) noexcept {
        if (this != &other) {
            if (handle_ != 0 && backend_) {
                backend_->free_gpu_memory(handle_);
            }
            handle_ = other.handle_;
            size_ = other.size_;
            backend_ = std::move(other.backend_);
            other.handle_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    uintptr_t handle() const { return handle_; }
    size_t size() const { return size_; }
    const GpuBackendPtr& backend() const { return backend_; }
    bool valid() const { return handle_ != 0 && backend_ != nullptr; }

private:
    uintptr_t handle_{0};
    size_t size_{0};
    GpuBackendPtr backend_;
};

using GpuBufferPtr = std::shared_ptr<GpuBuffer>;

}  // namespace task_graph
