#include <task_graph/gpu_backends/metal_backend.hpp>
#include <task_graph/data_types.hpp>
#include <cstring>
#include <memory>

#import <Metal/Metal.h>

namespace task_graph {

class MetalGpuBackendImpl {
public:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> commandQueue_ = nil;
};

MetalGpuBackend::MetalGpuBackend()
    : impl_(new MetalGpuBackendImpl()) {
}

MetalGpuBackend::~MetalGpuBackend() {
    shutdown();
    delete impl_;
}

bool MetalGpuBackend::is_available() const {
    return impl_ != nullptr && impl_->device_ != nil;
}

bool MetalGpuBackend::init() {
    @autoreleasepool {
        impl_->device_ = MTLCreateSystemDefaultDevice();
        if (!impl_->device_) {
            return false;
        }
        impl_->commandQueue_ = [impl_->device_ newCommandQueue];
        return impl_->commandQueue_ != nil;
    }
}

void MetalGpuBackend::shutdown() {
    @autoreleasepool {
        if (impl_) {
            impl_->commandQueue_ = nil;
            impl_->device_ = nil;
        }
    }
}

uintptr_t MetalGpuBackend::allocate_gpu_memory(size_t size) {
    @autoreleasepool {
        if (!is_available()) {
            return 0;
        }
        id<MTLBuffer> buffer = [impl_->device_ newBufferWithLength:size options:MTLResourceStorageModeShared];
        if (!buffer) {
            return 0;
        }
        return (__bridge uintptr_t)buffer;
    }
}

void MetalGpuBackend::free_gpu_memory(uintptr_t handle) {
    @autoreleasepool {
        if (handle == 0) {
            return;
        }
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)handle;
        (void)buffer;
        // ARC will release it
    }
}

bool MetalGpuBackend::upload_to_gpu(Image& image) {
    if (!image.is_on_cpu() || !is_available()) {
        return false;
    }

    @autoreleasepool {
        size_t totalBytes = image.total_size();
        id<MTLBuffer> buffer = [impl_->device_ newBufferWithLength:totalBytes options:MTLResourceStorageModeShared];
        if (!buffer) {
            return false;
        }

        std::memcpy(buffer.contents, image.ptr(), totalBytes);
        image.gpu_handle = (__bridge uintptr_t)buffer;
        return true;
    }
}

bool MetalGpuBackend::download_to_cpu(Image& image) {
    if (!image.is_on_gpu() || !is_available()) {
        return false;
    }

    @autoreleasepool {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)image.gpu_handle;
        if (!buffer) {
            return false;
        }

        size_t totalBytes = image.total_size();
        if (!image.data) {
            image.data = std::make_shared<std::vector<uint8_t>>(totalBytes);
        }
        if (image.data->size() != totalBytes) {
            image.data->resize(totalBytes);
        }

        std::memcpy(image.ptr(), buffer.contents, totalBytes);
        return true;
    }
}

bool MetalGpuBackend::copy_to_gpu(Image& image, uintptr_t gpu_ptr) {
    if (!image.is_on_cpu() || gpu_ptr == 0 || !is_available()) {
        return false;
    }

    @autoreleasepool {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)gpu_ptr;
        if (!buffer) {
            return false;
        }

        size_t totalBytes = image.total_size();
        if (buffer.length < (NSUInteger)totalBytes) {
            return false;
        }

        std::memcpy(buffer.contents, image.ptr(), totalBytes);
        image.gpu_handle = gpu_ptr;
        return true;
    }
}

bool MetalGpuBackend::release_gpu_memory(Image& image) {
    if (!image.is_on_gpu()) {
        return false;
    }

    // ARC will release when buffer goes out of scope
    image.gpu_handle = 0;
    return true;
}

} // namespace task_graph
