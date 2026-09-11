// Metal 渲染能力（离屏 render-to-texture）：MetalGpuBackend 的 render 段实现。
//
// 模型（与 gpu_render_ops.hpp 的接口契约一致）：
//   - 纹理：MTLStorageModePrivate + RenderTarget|ShaderRead usage，句柄 =
//     CFRBridgingRetain 的 id<MTLTexture>（与 buffer 句柄同一管理方式）。
//   - 数据搬运：CPU 上传/下载与 GPU 侧 buffer<->纹理互转一律走 blit encoder
//     （CPU 路径经 Shared staging buffer）。不要用 replaceRegion/getBytes 直写
//     Private 纹理：AGX 驱动的纹理压缩路径（processCompressedRegion2D）实测
//     SIGSEGV（macOS 26 / M 系芯片）。
//   - 管线：newLibraryWithSource 编译 MSL（vertex+fragment），按 name 缓存
//     MTLRenderPipelineState；颜色附件格式取 desc.target_format。
//   - pass：立即模式 MTLRenderCommandEncoder；end_render_pass 提交并
//     waitUntilCompleted（与 compute dispatch 的同步语义一致）。
// 线程安全：与 compute 共用 commandQueue_，所有方法套 @synchronized(cache)。
#include <task_graph/gpu_backends/metal_backend.hpp>
#include <task_graph/data_types.hpp>
#include "metal_backend_impl.hpp"
#include <cstring>
#include <cstdio>

#import <Metal/Metal.h>

namespace task_graph {

namespace {

MTLPixelFormat metal_pixel_format(GpuTextureFormat f) {
    return f == GpuTextureFormat::RGBA32_FLOAT ? MTLPixelFormatRGBA32Float
                                               : MTLPixelFormatRGBA8Unorm;
}

NSUInteger texture_bpp(id<MTLTexture> tex) {
    return gpu_texture_format_bytes(tex.pixelFormat == MTLPixelFormatRGBA32Float
                                        ? GpuTextureFormat::RGBA32_FLOAT
                                        : GpuTextureFormat::RGBA8_UNORM);
}

// blit 辅助：buffer <-> private 纹理（同步提交等待完成，与 compute 语义一致）。
// CPU 上传/下载也走这条路径（经 Shared staging buffer）：AGX 上 Private 纹理的
// replaceRegion/getBytes 直写路径会踩进驱动的纹理压缩代码崩溃（实测
// processCompressedRegion2D SIGSEGV），blit 是全平台稳妥路径。
bool blit_buffer_to_texture(id<MTLCommandQueue> queue, id<MTLBuffer> buf,
                            id<MTLTexture> tex) {
    const NSUInteger bytesPerRow = tex.width * texture_bpp(tex);
    const NSUInteger total = bytesPerRow * tex.height;
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    if (!cmd) return false;
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    if (!blit) return false;
    [blit copyFromBuffer:buf
            sourceOffset:0
       sourceBytesPerRow:bytesPerRow
     sourceBytesPerImage:total
              sourceSize:MTLSizeMake(tex.width, tex.height, 1)
             toTexture:tex
      destinationSlice:0
      destinationLevel:0
     destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    return true;
}

bool blit_texture_to_buffer(id<MTLCommandQueue> queue, id<MTLTexture> tex,
                            id<MTLBuffer> buf) {
    const NSUInteger bytesPerRow = tex.width * texture_bpp(tex);
    const NSUInteger total = bytesPerRow * tex.height;
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    if (!cmd) return false;
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    if (!blit) return false;
    [blit copyFromTexture:tex
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(tex.width, tex.height, 1)
                 toBuffer:buf
         destinationOffset:0
    destinationBytesPerRow:bytesPerRow
  destinationBytesPerImage:total];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    return true;
}

}  // namespace

bool MetalGpuBackend::supports_render() const {
    return is_available();
}

uintptr_t MetalGpuBackend::create_texture(const GpuTextureDesc& desc) {
    @autoreleasepool {
        if (!is_available() || desc.width == 0 || desc.height == 0) {
            return 0;
        }
        MTLTextureDescriptor* d = [[MTLTextureDescriptor alloc] init];
        d.textureType = MTLTextureType2D;
        d.pixelFormat = metal_pixel_format(desc.format);
        d.width = desc.width;
        d.height = desc.height;
        d.mipmapLevelCount = 1;
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex = [impl_->device_ newTextureWithDescriptor:d];
        if (!tex) {
            return 0;
        }
        return (uintptr_t)CFBridgingRetain(tex);
    }
}

void MetalGpuBackend::free_texture(uintptr_t texture) {
    @autoreleasepool {
        if (texture == 0) {
            return;
        }
        CFBridgingRelease((CFTypeRef)texture);
    }
}

bool MetalGpuBackend::upload_texture(uintptr_t texture, const uint8_t* data, size_t size) {
    @autoreleasepool {
        if (!is_available()) {
            return false;
        }
        id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)texture;
        if (!tex || !data) {
            return false;
        }
        const NSUInteger bytesPerRow = tex.width * texture_bpp(tex);
        const NSUInteger total = bytesPerRow * tex.height;
        if (size < total) {
            return false;
        }
        // CPU 字节 -> Shared staging buffer -> blit 到 private 纹理
        //（replaceRegion 直写在 AGX 上会崩溃，见文件头注释）
        id<MTLBuffer> staging = [impl_->device_ newBufferWithLength:total
                                                            options:MTLResourceStorageModeShared];
        if (!staging) {
            return false;
        }
        std::memcpy(staging.contents, data, total);
        return blit_buffer_to_texture(impl_->commandQueue_, staging, tex);
    }
}

bool MetalGpuBackend::download_texture(uintptr_t texture, uint8_t* data, size_t size) {
    @autoreleasepool {
        if (!is_available()) {
            return false;
        }
        id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)texture;
        if (!tex || !data) {
            return false;
        }
        const NSUInteger bytesPerRow = tex.width * texture_bpp(tex);
        const NSUInteger total = bytesPerRow * tex.height;
        if (size < total) {
            return false;
        }
        id<MTLBuffer> staging = [impl_->device_ newBufferWithLength:total
                                                            options:MTLResourceStorageModeShared];
        if (!staging) {
            return false;
        }
        if (!blit_texture_to_buffer(impl_->commandQueue_, tex, staging)) {
            return false;
        }
        std::memcpy(data, staging.contents, total);
        return true;
    }
}

bool MetalGpuBackend::copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) {
    @autoreleasepool {
        if (!is_available()) {
            return false;
        }
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)(void*)buffer;
        id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)texture;
        if (!buf || !tex) {
            return false;
        }
        const NSUInteger total = tex.width * texture_bpp(tex) * tex.height;
        if (buf.length < total || size < total) {
            return false;
        }
        return blit_buffer_to_texture(impl_->commandQueue_, buf, tex);
    }
}

bool MetalGpuBackend::copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) {
    @autoreleasepool {
        if (!is_available()) {
            return false;
        }
        id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)texture;
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)(void*)buffer;
        if (!tex || !buf) {
            return false;
        }
        const NSUInteger total = tex.width * texture_bpp(tex) * tex.height;
        if (buf.length < total || size < total) {
            return false;
        }
        return blit_texture_to_buffer(impl_->commandQueue_, tex, buf);
    }
}

uintptr_t MetalGpuBackend::create_sampler(const GpuSamplerDesc& desc) {
    @autoreleasepool {
        if (!is_available()) {
            return 0;
        }
        MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
        d.minFilter = desc.linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = d.minFilter;
        d.sAddressMode = desc.clamp_to_edge ? MTLSamplerAddressModeClampToEdge
                                            : MTLSamplerAddressModeRepeat;
        d.tAddressMode = d.sAddressMode;
        id<MTLSamplerState> s = [impl_->device_ newSamplerStateWithDescriptor:d];
        if (!s) {
            return 0;
        }
        return (uintptr_t)CFBridgingRetain(s);
    }
}

void MetalGpuBackend::free_sampler(uintptr_t sampler) {
    @autoreleasepool {
        if (sampler == 0) {
            return;
        }
        CFBridgingRelease((CFTypeRef)sampler);
    }
}

uintptr_t MetalGpuBackend::compile_render_pipeline(const GpuRenderPipelineDesc& desc) {
    if (!is_available() || desc.msl_source.empty() || desc.msl_fragment_name.empty()) {
        return 0;
    }

    @autoreleasepool {
        NSString* key = [NSString stringWithUTF8String:desc.name.c_str()];

        @synchronized(impl_->renderPipelineCache_) {
            id<MTLRenderPipelineState> cached = impl_->renderPipelineCache_[key];
            if (cached) {
                return (uintptr_t)(__bridge void*)cached;
            }
        }

        NSError* error = nil;
        NSString* src = [NSString stringWithUTF8String:desc.msl_source.c_str()];
        id<MTLLibrary> library = [impl_->device_ newLibraryWithSource:src
                                                              options:nil
                                                                error:&error];
        if (!library) {
            fprintf(stderr, "  [mtl-render] newLibrary FAILED for '%s': %s\n", desc.name.c_str(),
                    error ? [[error localizedDescription] UTF8String] : "unknown");
            return 0;
        }

        NSString* vsName = [NSString stringWithUTF8String:desc.msl_vertex_name.c_str()];
        NSString* fsName = [NSString stringWithUTF8String:desc.msl_fragment_name.c_str()];
        id<MTLFunction> vs = [library newFunctionWithName:vsName];
        id<MTLFunction> fs = [library newFunctionWithName:fsName];
        if (!vs || !fs) {
            fprintf(stderr, "  [mtl-render] function not found ('%s' / '%s') for '%s'\n",
                    desc.msl_vertex_name.c_str(), desc.msl_fragment_name.c_str(),
                    desc.name.c_str());
            return 0;
        }

        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vs;
        pd.fragmentFunction = fs;
        pd.colorAttachments[0].pixelFormat = metal_pixel_format(desc.target_format);
        if (desc.enable_blend) {
            MTLRenderPipelineColorAttachmentDescriptor* att = pd.colorAttachments[0];
            att.blendingEnabled = YES;
            att.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            att.sourceAlphaBlendFactor = MTLBlendFactorOne;
            att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }

        id<MTLRenderPipelineState> ps =
            [impl_->device_ newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!ps) {
            fprintf(stderr, "  [mtl-render] newRenderPipelineState FAILED for '%s': %s\n",
                    desc.name.c_str(),
                    error ? [[error localizedDescription] UTF8String] : "unknown");
            return 0;
        }

        @synchronized(impl_->renderPipelineCache_) {
            impl_->renderPipelineCache_[key] = ps;
        }
        return (uintptr_t)(__bridge void*)ps;
    }
}

void MetalGpuBackend::release_render_pipeline(uintptr_t pipeline) {
    (void)pipeline;  // 管线由 cache 字典持有；缓存生命周期与后端一致
}

bool MetalGpuBackend::begin_render_pass(const GpuRenderPassDesc& desc) {
    @autoreleasepool {
        if (!is_available() || desc.color_targets.empty()) {
            return false;
        }
        @synchronized(impl_->renderPipelineCache_) {
            if (impl_->renderEncoder_ != nil) {
                return false;  // 上一个 pass 未 end
            }
            id<MTLTexture> target = (__bridge id<MTLTexture>)(void*)desc.color_targets[0];
            if (!target) {
                return false;
            }

            MTLRenderPassDescriptor* d = [MTLRenderPassDescriptor renderPassDescriptor];
            d.colorAttachments[0].texture = target;
            d.colorAttachments[0].loadAction =
                desc.clear ? MTLLoadActionClear : MTLLoadActionLoad;
            d.colorAttachments[0].storeAction = MTLStoreActionStore;
            d.colorAttachments[0].clearColor =
                MTLClearColorMake(desc.clear_color[0], desc.clear_color[1],
                                  desc.clear_color[2], desc.clear_color[3]);

            id<MTLCommandBuffer> cmd = [impl_->commandQueue_ commandBuffer];
            if (!cmd) return false;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:d];
            if (!enc) return false;

            MTLViewport vp = {0.0, 0.0, (double)target.width, (double)target.height,
                              0.0, 1.0};
            [enc setViewport:vp];

            impl_->renderCmd_ = cmd;
            impl_->renderEncoder_ = enc;
        }
        return true;
    }
}

bool MetalGpuBackend::render_draw(const GpuDrawCall& draw) {
    @autoreleasepool {
        id<MTLRenderPipelineState> ps = (__bridge id<MTLRenderPipelineState>)(void*)draw.pipeline;
        id<MTLSamplerState> smp = draw.sampler ? (__bridge id<MTLSamplerState>)(void*)draw.sampler
                                               : nil;
        if (!is_available() || impl_->renderEncoder_ == nil || !ps) {
            return false;
        }
        id<MTLRenderCommandEncoder> enc = impl_->renderEncoder_;

        [enc setRenderPipelineState:ps];

        for (size_t i = 0; i < draw.textures.size() && i < 4; i++) {
            if (draw.textures[i] == 0) continue;
            id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)draw.textures[i];
            [enc setFragmentTexture:tex atIndex:i];
        }
        if (smp) {
            [enc setFragmentSamplerState:smp atIndex:0];
        }
        if (draw.uniform_data && draw.uniform_size > 0) {
            [enc setFragmentBytes:draw.uniform_data
                           length:draw.uniform_size
                          atIndex:0];
        }

        // 全屏三角形：顶点由 shader 内置（vertex_id 索引 3 顶点），无顶点缓冲
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        return true;
    }
}

bool MetalGpuBackend::end_render_pass() {
    @autoreleasepool {
        if (!is_available()) {
            return false;
        }
        @synchronized(impl_->renderPipelineCache_) {
            if (impl_->renderEncoder_ == nil || impl_->renderCmd_ == nil) {
                return false;
            }
            [impl_->renderEncoder_ endEncoding];
            [impl_->renderCmd_ commit];
            [impl_->renderCmd_ waitUntilCompleted];
            impl_->renderEncoder_ = nil;
            impl_->renderCmd_ = nil;
        }
        return true;
    }
}

}  // namespace task_graph
