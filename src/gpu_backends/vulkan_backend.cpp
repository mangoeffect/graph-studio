#include <task_graph/gpu_backends/vulkan_backend.hpp>
#include <task_graph/data_types.hpp>
#include <cstring>
#include <cstdlib>

namespace task_graph {

uint32_t VulkanGpuBackend::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return ~0U;
}

bool VulkanGpuBackend::create_buffer(size_t size, VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties,
                                      VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

    if (allocInfo.memoryTypeIndex == ~0U) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return false;
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(device_, buffer, bufferMemory, 0);
    return true;
}

bool VulkanGpuBackend::init() {
    // Check for validation layers (not required for basic implementation)
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "task_graph GPU Image";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "task_graph";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        return false;
    }

    // Enumerate physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        shutdown();
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    physicalDevice_ = devices[0]; // Use first available device

    // Find a queue family that supports graphics/transfer
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) {
            queueFamilyIndex_ = i;
            found = true;
            break;
        }
    }
    if (!found) {
        shutdown();
        return false;
    }

    // Create logical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    if (vkCreateDevice(physicalDevice_, &deviceCreateInfo, nullptr, &device_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex_;
    poolInfo.flags = 0;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    return true;
}

void VulkanGpuBackend::shutdown() {
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }

    instance_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
}

uintptr_t VulkanGpuBackend::allocate_gpu_memory(size_t size) {
    VulkanBuffer* vulkanBuffer = (VulkanBuffer*)malloc(sizeof(VulkanBuffer));
    if (!vulkanBuffer) {
        return 0;
    }

    if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       vulkanBuffer->buffer, vulkanBuffer->memory)) {
        free(vulkanBuffer);
        return 0;
    }

    return (uintptr_t)vulkanBuffer;
}

void VulkanGpuBackend::free_gpu_memory(uintptr_t handle) {
    if (handle == 0 || device_ == VK_NULL_HANDLE) {
        return;
    }

    VulkanBuffer* vulkanBuffer = (VulkanBuffer*)handle;
    vkDestroyBuffer(device_, vulkanBuffer->buffer, nullptr);
    vkFreeMemory(device_, vulkanBuffer->memory, nullptr);
    free(vulkanBuffer);
}

bool VulkanGpuBackend::upload_to_gpu(Image& image) {
    if (!image.is_on_cpu() || device_ == VK_NULL_HANDLE) {
        return false;
    }

    size_t totalBytes = image.total_size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (!create_buffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory)) {
        return false;
    }

    // Copy data to staging buffer
    void* data;
    vkMapMemory(device_, stagingBufferMemory, 0, totalBytes, 0, &data);
    memcpy(data, image.ptr(), totalBytes);
    vkUnmapMemory(device_, stagingBufferMemory);

    // Allocate device buffer
    VulkanBuffer* deviceBuffer = (VulkanBuffer*)malloc(sizeof(VulkanBuffer));
    if (!deviceBuffer) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }

    if (!create_buffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       deviceBuffer->buffer, deviceBuffer->memory)) {
        free(deviceBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }

    // Transfer from staging to device
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        free_gpu_memory((uintptr_t)deviceBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = totalBytes;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, deviceBuffer->buffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingBufferMemory, nullptr);

    image.gpu_handle = (uintptr_t)deviceBuffer;
    return true;
}

bool VulkanGpuBackend::download_to_cpu(Image& image) {
    if (!image.is_on_gpu() || device_ == VK_NULL_HANDLE) {
        return false;
    }

    VulkanBuffer* deviceBuffer = (VulkanBuffer*)image.gpu_handle;
    size_t totalBytes = image.total_size();

    // Create staging buffer accessible by host
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (!create_buffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory)) {
        return false;
    }

    // Transfer from device to staging
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = totalBytes;
    vkCmdCopyBuffer(commandBuffer, deviceBuffer->buffer, stagingBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);

    // Map and copy to CPU
    if (!image.data) {
        image.data = std::make_shared<std::vector<uint8_t>>(totalBytes);
    }
    if (image.data->size() != totalBytes) {
        image.data->resize(totalBytes);
    }

    void* mappedData;
    vkMapMemory(device_, stagingBufferMemory, 0, totalBytes, 0, &mappedData);
    memcpy(image.ptr(), mappedData, totalBytes);
    vkUnmapMemory(device_, stagingBufferMemory);

    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingBufferMemory, nullptr);

    return true;
}

bool VulkanGpuBackend::copy_to_gpu(Image& image, uintptr_t gpu_ptr) {
    if (!image.is_on_cpu() || gpu_ptr == 0 || device_ == VK_NULL_HANDLE) {
        return false;
    }

    VulkanBuffer* deviceBuffer = (VulkanBuffer*)gpu_ptr;
    size_t totalBytes = image.total_size();

    // Query memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, deviceBuffer->buffer, &memRequirements);
    if (memRequirements.size < totalBytes) {
        return false;
    }

    // Create staging buffer and copy data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (!create_buffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory)) {
        return false;
    }

    void* data;
    vkMapMemory(device_, stagingBufferMemory, 0, totalBytes, 0, &data);
    memcpy(data, image.ptr(), totalBytes);
    vkUnmapMemory(device_, stagingBufferMemory);

    // Transfer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkBufferCopy copyRegion{};
    copyRegion.size = totalBytes;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, deviceBuffer->buffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingBufferMemory, nullptr);

    image.gpu_handle = gpu_ptr;
    return true;
}

bool VulkanGpuBackend::release_gpu_memory(Image& image) {
    if (!image.is_on_gpu()) {
        return false;
    }

    free_gpu_memory(image.gpu_handle);
    image.gpu_handle = 0;
    return true;
}

} // namespace task_graph
