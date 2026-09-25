#pragma once

// MetalGpuBackend 的 pimpl 实现类定义：metal_backend.mm 与 metal_render.mm
// 两个翻译单元共享（私有头，不进 public include/）。
#import <Metal/Metal.h>

namespace task_graph {

class MetalGpuBackendImpl {
public:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> commandQueue_ = nil;
    NSMutableDictionary<NSString*, id<MTLComputePipelineState>>* kernelCache_ = nil;
    // render（见 metal_render.mm）：管线缓存 + 立即模式 pass 的当前 encoder 状态
    NSMutableDictionary<NSString*, id<MTLRenderPipelineState>>* renderPipelineCache_ = nil;
    // 采样器缓存（键 = linear | clamp<<1 的 NSNumber）：缓存持有所有权，
    // free_sampler 为 no-op，shutdown 统一释放
    NSMutableDictionary<NSNumber*, id<MTLSamplerState>>* samplerCache_ = nil;
    id<MTLCommandBuffer> renderCmd_ = nil;
    id<MTLRenderCommandEncoder> renderEncoder_ = nil;
};

}  // namespace task_graph
