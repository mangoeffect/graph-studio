#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <vulkan/vulkan.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace task_graph {

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size{0};  // compute descriptor 绑定需要 buffer 大小
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

private:
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool create_buffer(size_t size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void destroy_kernel_unlocked(uintptr_t kernel);

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    uint32_t queueFamilyIndex_{0};

    // compute 基础设施：shaderc + 8bit storage 特性齐备才置位
    bool computeCapable_{false};
    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};

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
};

} // namespace task_graph
