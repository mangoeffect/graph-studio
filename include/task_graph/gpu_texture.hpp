#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <task_graph/gpu_render_ops.hpp>

#include <cstddef>
#include <memory>

namespace task_graph {

// GpuTexture：GPU 纹理的 RAII 所有者（镜像 GpuBuffer 的所有权模型）。
//
// 持有 raw handle + 描述 + backend 引用，析构时回调 backend->free_texture。
// 通过 shared_ptr<GpuTexture> 共享所有权（Image::gpu_texture），使 render 输出
// 在 DAG task 间链式传递时纹理生命周期安全（最后一个引用销毁时释放）。
class GpuTexture {
public:
    GpuTexture() = default;

    GpuTexture(uintptr_t handle, const GpuTextureDesc& desc, GpuBackendPtr backend)
        : handle_(handle), desc_(desc), backend_(std::move(backend)) {}

    ~GpuTexture() {
        if (handle_ != 0 && backend_) {
            backend_->free_texture(handle_);
        }
    }

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    GpuTexture(GpuTexture&& other) noexcept
        : handle_(other.handle_), desc_(other.desc_),
          backend_(std::move(other.backend_)) {
        other.handle_ = 0;
    }

    GpuTexture& operator=(GpuTexture&& other) noexcept {
        if (this != &other) {
            if (handle_ != 0 && backend_) {
                backend_->free_texture(handle_);
            }
            handle_ = other.handle_;
            desc_ = other.desc_;
            backend_ = std::move(other.backend_);
            other.handle_ = 0;
        }
        return *this;
    }

    uintptr_t handle() const { return handle_; }
    const GpuTextureDesc& desc() const { return desc_; }
    const GpuBackendPtr& backend() const { return backend_; }
    bool valid() const { return handle_ != 0 && backend_ != nullptr; }

private:
    uintptr_t handle_{0};
    GpuTextureDesc desc_;
    GpuBackendPtr backend_;
};

using GpuTexturePtr = std::shared_ptr<GpuTexture>;

}  // namespace task_graph
