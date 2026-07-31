#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <vulkan/vulkan.h>

namespace task_graph {

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
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

private:
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool create_buffer(size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    uint32_t queueFamilyIndex_{0};
};

} // namespace task_graph
