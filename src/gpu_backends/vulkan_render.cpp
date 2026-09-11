// Vulkan 渲染能力（离屏 render-to-texture）：VulkanGpuBackend 的 render 段实现。
//
// 模型（与 gpu_render_ops.hpp 的接口契约一致）：
//   - 纹理：VkImage（OPTIMAL tiling，COLOR_ATTACHMENT|SAMPLED|TRANSFER usage）+
//     独立 VkDeviceMemory（首期一纹理一分配，简单优先）+ view；布局用显式
//     barrier 管理（VulkanTexture::layout 跟踪当前布局）。
//   - 管线：shaderc 运行时编译 GLSL vert/frag（与 compute 同一 TASK_GRAPH_VULKAN_COMPUTE
//     门槛）+ 经典 VkRenderPass/VkFramebuffer（兼容性优于 dynamic rendering 的
//     Vulkan 1.3 门槛）；VkRenderPass 按（格式, loadOp）缓存，管线按 name 缓存。
//   - pass：立即模式，end_render_pass 提交并 vkQueueWaitIdle（与 compute 同步语义一致）。
//
// 锁协议：renderMutex_ 串行化全部 render 入口；立即模式 pass 从 begin 持有到
// end（跨调用）。触及 commandPool_/queue_ 时嵌套加 gpuMutex_（锁序恒为
// renderMutex_ -> gpuMutex_）。契约：begin 成功后必须调用 end（哪怕 draw 失败）。
#include <task_graph/gpu_backends/vulkan_backend.hpp>
#include <task_graph/data_types.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#ifdef TASK_GRAPH_VULKAN_COMPUTE
#include <shaderc/shaderc.hpp>
#endif

namespace task_graph {

namespace {

// 单个 draw 最多绑定的采样纹理数（descriptor layout 固定 4 槽，未用槽位合法）
constexpr uint32_t kMaxRenderTextures = 4;
// render push constant 上限（GLSL 侧声明 vec4 u[8]；fragment 阶段）
constexpr uint32_t kRenderPushConstantBytes = 128;

VkFormat vk_format(GpuTextureFormat f) {
    return f == GpuTextureFormat::RGBA32_FLOAT ? VK_FORMAT_R32G32B32A32_SFLOAT
                                               : VK_FORMAT_R8G8B8A8_UNORM;
}

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
             VkImageLayout newLayout) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;

    // src 阶段用 ALL_COMMANDS 兜底（一纹理一 barrier 的离屏场景，简单优先于精细）
    b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    switch (newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

// ---- 一次性 command buffer 辅助（调用方持有 renderMutex_ + gpuMutex_）----

bool VulkanGpuBackend::record_one_time_cmd_lock_held(
    const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool = commandPool_;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &alloc, &cmd) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    bool ok = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    return ok;
}

// ---- 能力探测 / 纹理 ----

bool VulkanGpuBackend::supports_render() const {
    return is_available() && renderCapable_;
}

uintptr_t VulkanGpuBackend::create_texture(const GpuTextureDesc& desc) {
    std::lock_guard render_lock(renderMutex_);
    if (device_ == VK_NULL_HANDLE || desc.width == 0 || desc.height == 0) {
        return 0;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vk_format(desc.format);
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device_, image, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(memReq.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == ~0U) {
        vkDestroyImage(device_, image, nullptr);
        return 0;
    }
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        return 0;
    }
    vkBindImageMemory(device_, image, memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vk_format(desc.format);
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, memory, nullptr);
        return 0;
    }

    auto* tex = new (std::nothrow) VulkanTexture();
    if (!tex) {
        vkDestroyImageView(device_, view, nullptr);
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, memory, nullptr);
        return 0;
    }
    tex->image = image;
    tex->memory = memory;
    tex->view = view;
    tex->format = viewInfo.format;
    tex->width = desc.width;
    tex->height = desc.height;
    tex->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return (uintptr_t)tex;
}

void VulkanGpuBackend::free_texture(uintptr_t texture) {
    std::lock_guard render_lock(renderMutex_);
    if (texture == 0 || device_ == VK_NULL_HANDLE) {
        return;
    }
    VulkanTexture* tex = (VulkanTexture*)texture;
    vkDestroyImageView(device_, tex->view, nullptr);
    vkDestroyImage(device_, tex->image, nullptr);
    vkFreeMemory(device_, tex->memory, nullptr);
    delete tex;
}

bool VulkanGpuBackend::upload_texture(uintptr_t texture, const uint8_t* data, size_t size) {
    std::lock_guard render_lock(renderMutex_);
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
    if (device_ == VK_NULL_HANDLE || !data) {
        return false;
    }
    VulkanTexture* tex = (VulkanTexture*)texture;
    if (!tex) {
        return false;
    }
    const size_t bytesPerPixel = gpu_texture_format_bytes(
        tex->format == VK_FORMAT_R32G32B32A32_SFLOAT ? GpuTextureFormat::RGBA32_FLOAT
                                                     : GpuTextureFormat::RGBA8_UNORM);
    const size_t total = (size_t)tex->width * tex->height * bytesPerPixel;
    if (size < total) {
        return false;
    }

    // staging <- CPU 字节
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!create_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       staging, stagingMem)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, total, 0, &mapped);
    memcpy(mapped, data, total);
    vkUnmapMemory(device_, stagingMem);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {tex->width, tex->height, 1};

    bool ok = record_one_time_cmd_lock_held([&](VkCommandBuffer cmd) {
        barrier(cmd, tex->image, tex->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, staging, tex->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    if (ok) {
        tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);
    return ok;
}

bool VulkanGpuBackend::download_texture(uintptr_t texture, uint8_t* data, size_t size) {
    std::lock_guard render_lock(renderMutex_);
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
    if (device_ == VK_NULL_HANDLE || !data) {
        return false;
    }
    VulkanTexture* tex = (VulkanTexture*)texture;
    if (!tex) {
        return false;
    }
    const size_t bytesPerPixel = gpu_texture_format_bytes(
        tex->format == VK_FORMAT_R32G32B32A32_SFLOAT ? GpuTextureFormat::RGBA32_FLOAT
                                                     : GpuTextureFormat::RGBA8_UNORM);
    const size_t total = (size_t)tex->width * tex->height * bytesPerPixel;
    if (size < total) {
        return false;
    }

    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!create_buffer(total, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       staging, stagingMem)) {
        return false;
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {tex->width, tex->height, 1};

    bool ok = record_one_time_cmd_lock_held([&](VkCommandBuffer cmd) {
        barrier(cmd, tex->image, tex->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &region);
        barrier(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    if (ok) {
        tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        void* mapped = nullptr;
        vkMapMemory(device_, stagingMem, 0, total, 0, &mapped);
        memcpy(data, mapped, total);
        vkUnmapMemory(device_, stagingMem);
    }

    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);
    return ok;
}

bool VulkanGpuBackend::copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) {
    std::lock_guard render_lock(renderMutex_);
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
    if (device_ == VK_NULL_HANDLE) {
        return false;
    }
    VulkanBuffer* src = (VulkanBuffer*)buffer;
    VulkanTexture* tex = (VulkanTexture*)texture;
    if (!src || !tex) {
        return false;
    }
    const size_t bytesPerPixel = gpu_texture_format_bytes(
        tex->format == VK_FORMAT_R32G32B32A32_SFLOAT ? GpuTextureFormat::RGBA32_FLOAT
                                                     : GpuTextureFormat::RGBA8_UNORM);
    const size_t total = (size_t)tex->width * tex->height * bytesPerPixel;
    if (src->size < total || size < total) {
        return false;
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {tex->width, tex->height, 1};

    bool ok = record_one_time_cmd_lock_held([&](VkCommandBuffer cmd) {
        barrier(cmd, tex->image, tex->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, src->buffer, tex->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    if (ok) {
        tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return ok;
}

bool VulkanGpuBackend::copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) {
    std::lock_guard render_lock(renderMutex_);
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
    if (device_ == VK_NULL_HANDLE) {
        return false;
    }
    VulkanTexture* tex = (VulkanTexture*)texture;
    VulkanBuffer* dst = (VulkanBuffer*)buffer;
    if (!tex || !dst) {
        return false;
    }
    const size_t bytesPerPixel = gpu_texture_format_bytes(
        tex->format == VK_FORMAT_R32G32B32A32_SFLOAT ? GpuTextureFormat::RGBA32_FLOAT
                                                     : GpuTextureFormat::RGBA8_UNORM);
    const size_t total = (size_t)tex->width * tex->height * bytesPerPixel;
    if (dst->size < total || size < total) {
        return false;
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {tex->width, tex->height, 1};

    bool ok = record_one_time_cmd_lock_held([&](VkCommandBuffer cmd) {
        barrier(cmd, tex->image, tex->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               dst->buffer, 1, &region);
        barrier(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    if (ok) {
        tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return ok;
}

// ---- 采样器 ----

uintptr_t VulkanGpuBackend::create_sampler(const GpuSamplerDesc& desc) {
    std::lock_guard render_lock(renderMutex_);
    if (device_ == VK_NULL_HANDLE) {
        return 0;
    }
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = desc.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = info.magFilter;
    info.addressModeU = desc.clamp_to_edge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                           : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = info.addressModeU;
    info.addressModeW = info.addressModeU;
    info.anisotropyEnable = VK_FALSE;
    info.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device_, &info, nullptr, &sampler) != VK_SUCCESS) {
        return 0;
    }
    return (uintptr_t)sampler;
}

void VulkanGpuBackend::free_sampler(uintptr_t sampler) {
    std::lock_guard render_lock(renderMutex_);
    if (sampler == 0 || device_ == VK_NULL_HANDLE) {
        return;
    }
    vkDestroySampler(device_, (VkSampler)sampler, nullptr);
}

// ---- 管线 ----

VkRenderPass VulkanGpuBackend::render_pass_for(VkFormat format, bool clear) {
    const uint64_t key = (uint64_t)format * 2 + (clear ? 1 : 0);
    auto it = renderPassCache_.find(key);
    if (it != renderPassCache_.end()) {
        return it->second;
    }

    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // begin/end 显式 barrier 保证进入 pass 时布局已是 COLOR_ATTACHMENT_OPTIMAL
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &info, nullptr, &rp) == VK_SUCCESS) {
        renderPassCache_[key] = rp;
    }
    return rp;
}

void VulkanGpuBackend::destroy_render_pipeline_unlocked(uintptr_t pipeline) {
    auto it = renderPipelines_.find(pipeline);
    if (it == renderPipelines_.end()) return;
    if (device_ != VK_NULL_HANDLE) {
        if (it->second.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, it->second.pipeline, nullptr);
        if (it->second.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, it->second.pipelineLayout, nullptr);
        if (it->second.descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, it->second.descriptorSetLayout, nullptr);
        if (it->second.vertexModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, it->second.vertexModule, nullptr);
        if (it->second.fragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, it->second.fragmentModule, nullptr);
    }
    auto hit = renderPipelineHandles_.find(it->second.name);
    if (hit != renderPipelineHandles_.end() && hit->second == pipeline) {
        renderPipelineHandles_.erase(hit);
    }
    renderPipelines_.erase(it);
}

uintptr_t VulkanGpuBackend::compile_render_pipeline(const GpuRenderPipelineDesc& desc) {
#ifdef TASK_GRAPH_VULKAN_COMPUTE
    std::lock_guard render_lock(renderMutex_);
    if (device_ == VK_NULL_HANDLE || !renderCapable_) return 0;

    auto hit = renderPipelineHandles_.find(desc.name);
    if (hit != renderPipelineHandles_.end()) return hit->second;

    if (desc.glsl_vertex_source.empty() || desc.glsl_fragment_source.empty()) {
        fprintf(stderr, "  [vk-render] pipeline '%s': missing GLSL sources\n", desc.name.c_str());
        return 0;
    }

    // GLSL vert/frag -> SPIR-V（shaderc 运行时编译，与 compute 同基础设施）
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    auto compile_stage = [&](const std::string& src, shaderc_shader_kind kind) -> std::vector<uint32_t> {
        shaderc::SpvCompilationResult result =
            compiler.CompileGlslToSpv(src.c_str(), src.size(), kind, desc.name.c_str(), options);
        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            fprintf(stderr, "  [vk-render] shaderc FAILED for '%s': %s\n",
                    desc.name.c_str(), result.GetErrorMessage().c_str());
            return {};
        }
        return std::vector<uint32_t>(result.cbegin(), result.cend());
    };
    auto spirvVert = compile_stage(desc.glsl_vertex_source, shaderc_vertex_shader);
    auto spirvFrag = compile_stage(desc.glsl_fragment_source, shaderc_fragment_shader);
    if (spirvVert.empty() || spirvFrag.empty()) return 0;

    VkShaderModuleCreateInfo vmInfo{}, fmInfo{};
    vmInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vmInfo.codeSize = spirvVert.size() * sizeof(uint32_t);
    vmInfo.pCode = spirvVert.data();
    fmInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fmInfo.codeSize = spirvFrag.size() * sizeof(uint32_t);
    fmInfo.pCode = spirvFrag.data();
    VkShaderModule vertexModule = VK_NULL_HANDLE, fragmentModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &vmInfo, nullptr, &vertexModule) != VK_SUCCESS ||
        vkCreateShaderModule(device_, &fmInfo, nullptr, &fragmentModule) != VK_SUCCESS) {
        if (vertexModule) vkDestroyShaderModule(device_, vertexModule, nullptr);
        if (fragmentModule) vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return 0;
    }

    // descriptor set layout：kMaxRenderTextures 个 combined image sampler（fragment）
    VkDescriptorSetLayoutBinding bindings[kMaxRenderTextures];
    for (uint32_t i = 0; i < kMaxRenderTextures; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }
    VkDescriptorSetLayoutCreateInfo dsInfo{};
    dsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsInfo.bindingCount = kMaxRenderTextures;
    dsInfo.pBindings = bindings;
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device_, &dsInfo, nullptr, &dsLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return 0;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = kRenderPushConstantBytes;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &dsLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device_, dsLayout, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return 0;
    }

    // 管线兼容性只看附件格式：统一挂在 clear 变体的参考 renderpass 上
    VkRenderPass referencePass = render_pass_for(vk_format(desc.target_format), true);
    if (referencePass == VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device_, dsLayout, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return 0;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.enable_blend) {
        blendAtt.blendEnable = VK_TRUE;
        blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
        blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAtt;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamic;
    pipeInfo.layout = pipelineLayout;
    pipeInfo.renderPass = referencePass;
    pipeInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline) != VK_SUCCESS) {
        fprintf(stderr, "  [vk-render] vkCreateGraphicsPipelines FAILED for '%s'\n", desc.name.c_str());
        vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device_, dsLayout, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return 0;
    }

    const uintptr_t handle = nextRenderPipelineHandle_++;
    renderPipelines_[handle] = RenderPipelineEntry{desc.name, vertexModule, fragmentModule,
                                                   dsLayout, pipelineLayout, pipeline};
    renderPipelineHandles_[desc.name] = handle;
    return handle;
#else
    (void)desc;
    fprintf(stderr, "  [vk-render] compile_render_pipeline('%s') failed: built without "
                    "shaderc (TASK_GRAPH_VULKAN_COMPUTE off)\n", desc.name.c_str());
    return 0;
#endif
}

void VulkanGpuBackend::release_render_pipeline(uintptr_t pipeline) {
    std::lock_guard render_lock(renderMutex_);
    destroy_render_pipeline_unlocked(pipeline);
}

// ---- 立即模式 pass ----

bool VulkanGpuBackend::begin_render_pass(const GpuRenderPassDesc& desc) {
    renderMutex_.lock();  // 持有到 end_render_pass
    if (device_ == VK_NULL_HANDLE || !renderCapable_ || desc.color_targets.empty()) {
        renderMutex_.unlock();
        return false;
    }
    if (activePass_.cmd != VK_NULL_HANDLE) {
        renderMutex_.unlock();
        return false;  // 上一个 pass 未 end
    }
    VulkanTexture* target = (VulkanTexture*)desc.color_targets[0];
    if (!target) {
        renderMutex_.unlock();
        return false;
    }

    VkRenderPass rp = render_pass_for(target->format, desc.clear);
    if (rp == VK_NULL_HANDLE) {
        renderMutex_.unlock();
        return false;
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = rp;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &target->view;
    fbInfo.width = target->width;
    fbInfo.height = target->height;
    fbInfo.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &fb) != VK_SUCCESS) {
        renderMutex_.unlock();
        return false;
    }

    {
        std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandPool = commandPool_;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &alloc, &activePass_.cmd) != VK_SUCCESS) {
            vkDestroyFramebuffer(device_, fb, nullptr);
            activePass_.cmd = VK_NULL_HANDLE;
            renderMutex_.unlock();
            return false;
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(activePass_.cmd, &begin);
    }

    // 目标纹理 -> COLOR_ATTACHMENT_OPTIMAL（renderpass 的 initialLayout 约定）
    barrier(activePass_.cmd, target->image, target->layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    target->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkClearValue clearValue{};
    clearValue.color = {{desc.clear_color[0], desc.clear_color[1],
                         desc.clear_color[2], desc.clear_color[3]}};
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = rp;
    rpBegin.framebuffer = fb;
    rpBegin.renderArea.extent = {target->width, target->height};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(activePass_.cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    activePass_.renderPass = rp;
    activePass_.framebuffer = fb;
    activePass_.boundLayout = VK_NULL_HANDLE;
    activePass_.extent = {target->width, target->height};
    activePass_.target = target;
    activePass_.usedSets.clear();
    return true;
}

bool VulkanGpuBackend::render_draw(const GpuDrawCall& draw) {
    // renderMutex_ 已由 begin_render_pass 持有（本方法只允许在 begin..end 之间调用）
    if (activePass_.cmd == VK_NULL_HANDLE || draw.pipeline == 0) {
        return false;
    }
    auto it = renderPipelines_.find(draw.pipeline);
    if (it == renderPipelines_.end()) {
        return false;
    }
    const RenderPipelineEntry& entry = it->second;

    // descriptor set：采样纹理 + 采样器
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = renderDescriptorPool_;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &entry.descriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &dsAlloc, &set) != VK_SUCCESS) {
        return false;
    }

    // 数组必须零初始化：VkWriteDescriptorSet.dstArrayElement 等字段漏填时是
    // 栈垃圾，descriptor 会写到非法数组偏移（实测 MoltenVK 上表现为采样恒零，
    // 且验证层不报——MoltenVK 1.4.2 踩坑记录）
    VkDescriptorImageInfo imageInfos[kMaxRenderTextures]{};
    VkWriteDescriptorSet writes[kMaxRenderTextures]{};
    uint32_t writeCount = 0;
    for (uint32_t i = 0; i < kMaxRenderTextures && i < draw.textures.size(); i++) {
        VulkanTexture* tex = (VulkanTexture*)draw.textures[i];
        if (!tex) continue;
        imageInfos[writeCount] = VkDescriptorImageInfo{
            (VkSampler)draw.sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = i;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[writeCount].pImageInfo = &imageInfos[writeCount];
        ++writeCount;
    }
    vkUpdateDescriptorSets(device_, writeCount, writes, 0, nullptr);
    activePass_.usedSets.push_back(set);

    // push constant 固定 128 字节，不足补零
    uint8_t pushData[kRenderPushConstantBytes] = {};
    if (draw.uniform_data && draw.uniform_size > 0) {
        memcpy(pushData, draw.uniform_data,
               draw.uniform_size < kRenderPushConstantBytes ? draw.uniform_size
                                                            : kRenderPushConstantBytes);
    }

    VkViewport viewport{0.f, 0.f, (float)activePass_.extent.width,
                        (float)activePass_.extent.height, 0.f, 1.f};
    VkRect2D scissor{{0, 0}, activePass_.extent};

    vkCmdBindPipeline(activePass_.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, entry.pipeline);
    vkCmdBindDescriptorSets(activePass_.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            entry.pipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(activePass_.cmd, entry.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, kRenderPushConstantBytes, pushData);
    vkCmdSetViewport(activePass_.cmd, 0, 1, &viewport);
    vkCmdSetScissor(activePass_.cmd, 0, 1, &scissor);
    // 全屏三角形：顶点由 shader 内置（gl_VertexIndex），无顶点缓冲
    vkCmdDraw(activePass_.cmd, 3, 1, 0, 0);

    activePass_.boundLayout = entry.pipelineLayout;
    return true;
}

bool VulkanGpuBackend::end_render_pass() {
    if (activePass_.cmd == VK_NULL_HANDLE) {
        return false;
    }

    vkCmdEndRenderPass(activePass_.cmd);
    // 目标纹理：COLOR_ATTACHMENT -> SHADER_READ_ONLY（后续 pass 采样 / 拷贝到
    // buffer 都要求采样布局；barrier 必须记录在本 cmd buffer 提交前）
    if (activePass_.target) {
        barrier(activePass_.cmd, activePass_.target->image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        activePass_.target->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    bool ok = true;
    {
        std::lock_guard<std::mutex> gpu_lock(gpuMutex_);
        vkEndCommandBuffer(activePass_.cmd);
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &activePass_.cmd;
        ok = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
        if (ok) {
            vkQueueWaitIdle(queue_);
        }
    }

    vkDestroyFramebuffer(device_, activePass_.framebuffer, nullptr);
    if (!activePass_.usedSets.empty()) {
        vkFreeDescriptorSets(device_, renderDescriptorPool_,
                             (uint32_t)activePass_.usedSets.size(),
                             activePass_.usedSets.data());
    }
    vkFreeCommandBuffers(device_, commandPool_, 1, &activePass_.cmd);
    activePass_ = ActiveRenderPass{};
    renderMutex_.unlock();
    return ok;
}

}  // namespace task_graph
