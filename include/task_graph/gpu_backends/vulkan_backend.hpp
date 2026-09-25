#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <vulkan/vulkan.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace task_graph {

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size{0};  // compute descriptor 绑定需要 buffer 大小
};

// 渲染纹理：VkImage + 绑定内存 + view + 布局跟踪（显式 barrier 管理，
// 见 src/gpu_backends/vulkan_render.cpp）。
struct VulkanTexture {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_UNDEFINED};
    uint32_t width{0};
    uint32_t height{0};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
};

class VulkanGpuBackend : public IGpuImageBackend {
public:
    bool init() override;
    void shutdown() override;

    bool upload_to_gpu(Image& image) override;
    bool download_to_cpu(Image& image) override;
    bool copy_to_gpu(Image& image, uintptr_t gpu_ptr) override;
    bool release_gpu_memory(Image& image) override;

    uintptr_t allocate_gpu_memory(size_t size) override;
    void free_gpu_memory(uintptr_t handle) override;

    std::string get_backend_name() const override { return "vulkan"; }
    bool is_available() const override { return device_ != VK_NULL_HANDLE; }

    // ===== compute（shaderc 可用时启用，见 TASK_GRAPH_VULKAN_COMPUTE）=====
    uintptr_t compile_kernel(const std::string& name,
                              const std::string& source) override;
    bool dispatch(uintptr_t kernel,
                  const std::vector<GpuBinding>& bindings,
                  const void* uniform_data, size_t uniform_size,
                  uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) override;
    void release_kernel(uintptr_t kernel) override;
    bool supports_compute() const override { return computeCapable_; }
    std::string kernel_language() const override { return "glsl"; }

    // ===== render（实现见 src/gpu_backends/vulkan_render.cpp）=====
    bool supports_render() const override;
    uintptr_t create_texture(const GpuTextureDesc& desc) override;
    void free_texture(uintptr_t texture) override;
    bool upload_texture(uintptr_t texture, const uint8_t* data, size_t size) override;
    bool download_texture(uintptr_t texture, uint8_t* data, size_t size) override;
    bool copy_buffer_to_texture(uintptr_t buffer, size_t size, uintptr_t texture) override;
    bool copy_texture_to_buffer(uintptr_t texture, uintptr_t buffer, size_t size) override;
    uintptr_t create_sampler(const GpuSamplerDesc& desc) override;
    void free_sampler(uintptr_t sampler) override;
    uintptr_t compile_render_pipeline(const GpuRenderPipelineDesc& desc) override;
    void release_render_pipeline(uintptr_t pipeline) override;
    bool begin_render_pass(const GpuRenderPassDesc& desc) override;
    bool render_draw(const GpuDrawCall& draw) override;
    bool end_render_pass() override;
    bool wait_render_idle() override;

private:
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool create_buffer(size_t size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void destroy_kernel_unlocked(uintptr_t kernel);

    // render 内部辅助（vulkan_render.cpp）
    bool record_one_time_cmd_lock_held(
        const std::function<void(VkCommandBuffer)>& record);
    VkRenderPass render_pass_for(VkFormat format, bool clear);
    void destroy_render_pipeline_unlocked(uintptr_t pipeline);

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    uint32_t queueFamilyIndex_{0};

    // compute 基础设施：shaderc + 8bit storage 特性齐备才置位
    bool computeCapable_{false};
    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};

    // ===== render 状态 =====
    // 渲染能力 = graphics 队列族 + shaderc（GLSL vert/frag 运行时编译）+ descriptor pool
    bool renderCapable_{false};
    bool graphicsCapable_{false};
    VkDescriptorPool renderDescriptorPool_{VK_NULL_HANDLE};

    struct RenderPipelineEntry {
        std::string name;
        VkShaderModule vertexModule{VK_NULL_HANDLE};
        VkShaderModule fragmentModule{VK_NULL_HANDLE};
        VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    };
    std::unordered_map<std::string, uintptr_t> renderPipelineHandles_;  // name -> handle
    std::unordered_map<uintptr_t, RenderPipelineEntry> renderPipelines_;
    uintptr_t nextRenderPipelineHandle_{1};

    // VkRenderPass 按（格式, loadOp）缓存：管线兼容性只看附件格式，clear/load
    // 两种变体各留一份；begin/end 复用。
    std::unordered_map<uint64_t, VkRenderPass> renderPassCache_;

    // 采样器按（filter, addressMode）组合缓存（键 = linear | clamp<<1）：
    // draw 热路径每 pass create/free 的开销全免；缓存持有所有权，
    // free_sampler 对缓存句柄是 no-op，shutdown 统一销毁。
    std::unordered_map<uint32_t, VkSampler> samplerCache_;

    // framebuffer 按（目标纹理, renderPass）缓存：begin 建 end 毁改为复用，
    // 纹理销毁时逐出（free_texture 内），shutdown 统一销毁。
    std::map<std::pair<VulkanTexture*, VkRenderPass>, VkFramebuffer> framebufferCache_;

    // 立即模式 pass 的当前状态。P1 批处理录制：begin..end 只往同一个打开的
    // cmd buffer 里录（同 buffer 内 barrier 天然有序），end 不提交；
    // wait_render_idle 统一 end+submit+fence 等待，并释放整批 usedSets/cmd。
    // usedSets：本 pass 内 draw 分配的 descriptor set，wait 时统一释放。
    // target：begin 时记录的目标纹理（end 收尾 barrier 转回 SHADER_READ_ONLY）。
    struct ActiveRenderPass {
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkRenderPass renderPass{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkPipelineLayout boundLayout{VK_NULL_HANDLE};  // draw 时绑定的管线布局（push constant 用）
        VkExtent2D extent{};
        VulkanTexture* target{nullptr};
        std::vector<VkDescriptorSet> usedSets;
    } activePass_;

    // P1：批处理渲染录制缓冲——连续 pass 的 begin..end 都录进它，
    // wait_render_idle 时 end+submit+fence 等待，然后释放并置空
    VkCommandBuffer renderBatch_{VK_NULL_HANDLE};
    // P1：批内已结束 pass 的 descriptor set（cmd buffer 提交并等完才能释放）
    std::vector<VkDescriptorSet> pendingSets_;
    // P1：渲染提交的唯一 fence（wait_render_idle 用；懒创建，shutdown 销毁）
    VkFence renderFence_{VK_NULL_HANDLE};

    struct KernelEntry {
        std::string name;  // release_kernel 反查 kernelHandles_ 清理用
        VkShaderModule module{VK_NULL_HANDLE};
        VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    };
    std::unordered_map<std::string, uintptr_t> kernelHandles_;  // name -> handle
    std::unordered_map<uintptr_t, KernelEntry> kernels_;        // handle -> entry
    uintptr_t nextKernelHandle_{1};
    // commandPool_ 与 queue_ 的所有使用（upload/download/copy/compile/dispatch）
    // 共用一把锁：VkCommandPool 非线程安全，vkQueueSubmit 也要求外部同步；
    // executor 会并行执行无依赖 task，buffer 搬运与 compute dispatch 可能交叠。
    std::mutex gpuMutex_;
    // 渲染专用锁：串行化全部 render 入口；立即模式 pass 从 begin_render_pass
    // 持有到 end_render_pass（跨调用，保证 activePass_ 与 descriptor pool 互斥）。
    // 锁序恒为 renderMutex_ -> gpuMutex_（嵌套时先 render 后 gpu），不得反向。
    // 契约：begin 成功后必须调用 end（哪怕 draw 失败），否则锁泄漏。
    // 必须递归：pass 持锁期间 render_draw 路径会再进 compile_render_pipeline /
    // create_sampler（同线程重入；Metal 侧 @synchronized 天然可重入，对齐语义）。
    std::recursive_mutex renderMutex_;
};

} // namespace task_graph
