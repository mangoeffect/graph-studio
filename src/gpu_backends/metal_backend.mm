#include <task_graph/gpu_backends/metal_backend.hpp>
#include <task_graph/data_types.hpp>
#include <cstring>
#include <cstdio>
#include <memory>

#import <Metal/Metal.h>

namespace task_graph {

class MetalGpuBackendImpl {
public:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> commandQueue_ = nil;
    NSMutableDictionary<NSString*, id<MTLComputePipelineState>>* kernelCache_ = nil;
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
        impl_->kernelCache_ = [NSMutableDictionary dictionary];
        return impl_->commandQueue_ != nil;
    }
}

void MetalGpuBackend::shutdown() {
    @autoreleasepool {
        if (impl_) {
            impl_->commandQueue_ = nil;
            impl_->device_ = nil;
            impl_->kernelCache_ = nil;
        }
    }
}

uintptr_t MetalGpuBackend::allocate_gpu_memory(size_t size) {
    @autoreleasepool {
        if (!is_available()) {
            return 0;
        }
        id<MTLBuffer> buffer = [impl_->device_ newBufferWithLength:size
                                                           options:MTLResourceStorageModeShared];
        if (!buffer) {
            return 0;
        }
        return (uintptr_t)CFBridgingRetain(buffer);
    }
}

void MetalGpuBackend::free_gpu_memory(uintptr_t handle) {
    @autoreleasepool {
        if (handle == 0) {
            return;
        }
        CFBridgingRelease((CFTypeRef)handle);
    }
}

bool MetalGpuBackend::upload_to_gpu(Image& image) {
    if (!image.is_on_cpu() || !is_available()) {
        return false;
    }

    @autoreleasepool {
        size_t totalBytes = image.total_size();
        id<MTLBuffer> buffer = [impl_->device_ newBufferWithLength:totalBytes
                                                           options:MTLResourceStorageModeShared];
        if (!buffer) {
            return false;
        }

        std::memcpy(buffer.contents, image.ptr(), totalBytes);
        image.gpu_handle = (uintptr_t)CFBridgingRetain(buffer);
        return true;
    }
}

bool MetalGpuBackend::download_to_cpu(Image& image) {
    if (!image.is_on_gpu() || !is_available()) {
        return false;
    }

    @autoreleasepool {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)(void*)image.gpu_handle;
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
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)(void*)gpu_ptr;
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

    if (image.gpu_buffer) {
        image.gpu_buffer.reset();
    } else if (image.gpu_handle != 0) {
        @autoreleasepool {
            CFBridgingRelease((CFTypeRef)image.gpu_handle);
        }
    }
    image.gpu_handle = 0;
    return true;
}

uintptr_t MetalGpuBackend::compile_kernel(const std::string& name,
                                           const std::string& source) {
    if (!is_available()) return 0;

    @autoreleasepool {
        NSString* key = [NSString stringWithUTF8String:name.c_str()];

        @synchronized(impl_->kernelCache_) {
            id<MTLComputePipelineState> cached = impl_->kernelCache_[key];
            if (cached) {
                return (uintptr_t)(__bridge void*)cached;
            }
        }

        NSError* error = nil;
        NSString* src = [NSString stringWithUTF8String:source.c_str()];
        id<MTLLibrary> library = [impl_->device_ newLibraryWithSource:src
                                                              options:nil
                                                                error:&error];
        if (!library) {
            fprintf(stderr, "  [mtl] newLibrary FAILED for '%s': %s\n", name.c_str(),
                    error ? [[error localizedDescription] UTF8String] : "unknown");
            return 0;
        }

        id<MTLFunction> function = [library newFunctionWithName:key];
        if (!function) {
            fprintf(stderr, "  [mtl] newFunction '%s' not found\n", name.c_str());
            return 0;
        }

        id<MTLComputePipelineState> pipeline =
            [impl_->device_ newComputePipelineStateWithFunction:function
                                                         error:&error];
        if (!pipeline) {
            fprintf(stderr, "  [mtl] newPipeline FAILED for '%s': %s\n", name.c_str(),
                    error ? [[error localizedDescription] UTF8String] : "unknown");
            return 0;
        }

        @synchronized(impl_->kernelCache_) {
            impl_->kernelCache_[key] = pipeline;
        }

        return (uintptr_t)(__bridge void*)pipeline;
    }
}

bool MetalGpuBackend::dispatch(uintptr_t kernel,
                                const std::vector<GpuBinding>& bindings,
                                const void* uniform_data, size_t uniform_size,
                                uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) {
    if (!is_available() || kernel == 0) return false;

    @autoreleasepool {
        id<MTLComputePipelineState> pipeline =
            (__bridge id<MTLComputePipelineState>)(void*)kernel;

        id<MTLCommandBuffer> cmd = [impl_->commandQueue_ commandBuffer];
        if (!cmd) return false;

        id<MTLComputeCommandEncoder> encoder = [cmd computeCommandEncoder];
        if (!encoder) return false;

        [encoder setComputePipelineState:pipeline];

        for (size_t i = 0; i < bindings.size(); i++) {
            if (bindings[i].handle != 0) {
                id<MTLBuffer> buf = (__bridge id<MTLBuffer>)(void*)bindings[i].handle;
                [encoder setBuffer:buf offset:bindings[i].offset atIndex:i];
            }
        }

        if (uniform_data && uniform_size > 0) {
            [encoder setBytes:uniform_data
                       length:uniform_size
                      atIndex:bindings.size()];
        }

        NSUInteger maxPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        NSUInteger tgw = 16, tgh = 16;
        if (maxPerGroup < 256) { tgw = 8; tgh = 8; }
        if (maxPerGroup < 64)  { tgw = 4; tgh = 4; }
        if (tgw > grid_x) tgw = grid_x > 0 ? grid_x : 1;
        if (tgh > grid_y) tgh = grid_y > 0 ? grid_y : 1;

        MTLSize threadGroupSize = MTLSizeMake(tgw, tgh, 1);
        MTLSize gridSize = MTLSizeMake(grid_x, grid_y, grid_z);

        [encoder dispatchThreadgroups:gridSize
                threadsPerThreadgroup:threadGroupSize];
        [encoder endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];

        return true;
    }
}

void MetalGpuBackend::release_kernel(uintptr_t kernel) {
    (void)kernel;
}

} // namespace task_graph
