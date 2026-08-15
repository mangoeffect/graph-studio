#include <task_graph/gpu_backends/vulkan_backend.hpp>
#include <task_graph/data_types.hpp>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>

#ifdef TASK_GRAPH_VULKAN_COMPUTE
#include <shaderc/shaderc.hpp>
#endif

#if defined(_WIN32) && defined(TASK_GRAPH_ENABLE_VULKAN)
#include <windows.h>
#include <delayimp.h>

// libtask_graph.dll 以 /DELAYLOAD:vulkan-1.dll 链接（见根 CMakeLists）：
// 进程启动不再要求 loader 存在。用户机器缺 vulkan-1.dll（无 GPU/老驱动）时，
// 首次 vk 调用触发 dliFailLoad——默认行为是弹窗并终止进程；这里把所有
// vk 导入指到失败桩，让 VulkanGpuBackend::init() 拿到错误码干净返回，
// 应用按既有约定降级（CPU 路径 / gpu 子模块 soft-skip）。
namespace {
extern "C" VkResult VKAPI_ATTR vk_delayload_missing_stub() {
    return VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" FARPROC WINAPI vk_delayload_hook(unsigned dliNotify, DelayLoadInfo* pdli) {
    (void)pdli;
    if (dliNotify == dliFailLoad) {
        return reinterpret_cast<FARPROC>(&vk_delayload_missing_stub);
    }
    return nullptr;  // 其余通知走 delayimp 默认处理
}
}  // namespace

extern "C" {
// delayimp 约定：自定义了 failure hook 就必须同时定义 notify hook。
const PfnDliHook __pfnDliNotifyHook2 = nullptr;
const PfnDliHook __pfnDliFailureHook2 = &vk_delayload_hook;
}
#endif

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
    // compute 需要 Vulkan 1.2 的 8-bit storage / int8 特性（GLSL SSBO 用 uint8_t），
    // 先探测实例 / 设备版本；不满足时仍以 Vulkan 1.0 初始化（仅 buffer 搬运）。
    uint32_t instanceApi = VK_API_VERSION_1_0;
    auto enumVersion = (PFN_vkEnumerateInstanceVersion)
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
    uint32_t maxInstanceApi = 0;
    if (enumVersion && enumVersion(&maxInstanceApi) == VK_SUCCESS &&
        maxInstanceApi >= VK_API_VERSION_1_2) {
        instanceApi = VK_API_VERSION_1_2;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "task_graph GPU Image";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "task_graph";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = instanceApi;

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

    // 优先选择 compute 队列（compute 队列必支持 transfer），找不到再退回
    // graphics/transfer（此时仅支持 buffer 上传/下载，不支持 compute dispatch）
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndex_ = i;
            found = true;
            break;
        }
    }
    if (!found) {
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) {
                queueFamilyIndex_ = i;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        shutdown();
        return false;
    }

    // GLSL kernel 的 SSBO 用 uint8_t：需要 Vulkan 1.2 的
    // storageBuffer8BitAccess + shaderInt8（查询结构体链，支持才启用）。
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.storageBuffer8BitAccess = VK_TRUE;
    features12.shaderInt8 = VK_TRUE;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    bool has8Bit = false;
    if (instanceApi >= VK_API_VERSION_1_2 && props.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
        has8Bit = features12.storageBuffer8BitAccess == VK_TRUE && features12.shaderInt8 == VK_TRUE;
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
    if (has8Bit) {
        deviceCreateInfo.pNext = &features12;
    }

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

#ifdef TASK_GRAPH_VULKAN_COMPUTE
    // compute 基础设施：8-bit 特性 + descriptor pool。
    // 任一条件缺失时 computeCapable_ 保持 false，仅保留 buffer 搬运能力。
    if (has8Bit) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 32;

        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descPoolInfo.maxSets = 32;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(device_, &descPoolInfo, nullptr, &descriptorPool_) == VK_SUCCESS) {
            computeCapable_ = true;
        }
    }
#else
    (void)has8Bit;
#endif

    return true;
}

void VulkanGpuBackend::shutdown() {
    // destroy_kernel_unlocked 会 erase 元素，不能边遍历边删；逐个弹出直到清空
    while (!kernels_.empty()) {
        destroy_kernel_unlocked(kernels_.begin()->first);
    }
    kernelHandles_.clear();
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    computeCapable_ = false;
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

    if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       vulkanBuffer->buffer, vulkanBuffer->memory)) {
        free(vulkanBuffer);
        return 0;
    }
    vulkanBuffer->size = size;

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
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);

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

    if (!create_buffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       deviceBuffer->buffer, deviceBuffer->memory)) {
        free(deviceBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return false;
    }
    deviceBuffer->size = totalBytes;

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
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);

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
    std::lock_guard<std::mutex> gpu_lock(gpuMutex_);

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

// ====================== compute（GLSL -> SPIR-V -> pipeline）======================

namespace {
// kernel 声明的固定 workgroup 尺寸（与 GLSL 源码中的 local_size_x/y 保持一致）
constexpr uint32_t kLocalSizeX = 8;
constexpr uint32_t kLocalSizeY = 8;
// 单个 kernel 最多绑定的 storage buffer 数（in / in2 / dst + 余量）
constexpr uint32_t kMaxBindings = 4;
// push constant 块大小（GLSL 侧统一声明 uint u[16]）
constexpr uint32_t kPushConstantBytes = 64;
}  // namespace

void VulkanGpuBackend::destroy_kernel_unlocked(uintptr_t kernel) {
    auto it = kernels_.find(kernel);
    if (it == kernels_.end()) return;
    if (device_ != VK_NULL_HANDLE) {
        if (it->second.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, it->second.pipeline, nullptr);
        if (it->second.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, it->second.pipelineLayout, nullptr);
        if (it->second.descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, it->second.descriptorSetLayout, nullptr);
        if (it->second.module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, it->second.module, nullptr);
    }
    // 同名重建会产生新 handle；只清理仍指向本 handle 的条目，避免误删后注册的
    auto hit = kernelHandles_.find(it->second.name);
    if (hit != kernelHandles_.end() && hit->second == kernel) {
        kernelHandles_.erase(hit);
    }
    kernels_.erase(it);
}

uintptr_t VulkanGpuBackend::compile_kernel(const std::string& name,
                                            const std::string& source) {
#ifdef TASK_GRAPH_VULKAN_COMPUTE
    if (device_ == VK_NULL_HANDLE || !computeCapable_) return 0;

    std::lock_guard<std::mutex> lock(gpuMutex_);

    auto hit = kernelHandles_.find(name);
    if (hit != kernelHandles_.end()) return hit->second;

    // GLSL compute -> SPIR-V（shaderc 运行时编译；算子由插件运行时注册，
    // kernel 源码是字符串，构建期预编译无法覆盖此场景）
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    shaderc::SpvCompilationResult module =
        compiler.CompileGlslToSpv(source.c_str(), source.size(),
                                  shaderc_compute_shader, name.c_str(), options);
    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        fprintf(stderr, "  [vk] shaderc FAILED for '%s': %s\n", name.c_str(),
                module.GetErrorMessage().c_str());
        return 0;
    }
    std::vector<uint32_t> spirv(module.cbegin(), module.cend());

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleInfo.pCode = spirv.data();
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return 0;
    }

    // descriptor set layout：kMaxBindings 个 storage buffer 槽位。
    // 未写入的槽位只要不被 shader 静态访问即为合法，单/双输入 kernel 共用同一布局。
    VkDescriptorSetLayoutBinding layoutBindings[kMaxBindings];
    for (uint32_t i = 0; i < kMaxBindings; i++) {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layoutBindings[i].pImmutableSamplers = nullptr;
    }
    VkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutInfo.bindingCount = kMaxBindings;
    dsLayoutInfo.pBindings = layoutBindings;
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device_, &dsLayoutInfo, nullptr, &dsLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        return 0;
    }

    // push constant：uniform 数据（宽高 + 算子参数，uint 打包）
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = kPushConstantBytes;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &dsLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device_, dsLayout, nullptr);
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        return 0;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device_, dsLayout, nullptr);
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        return 0;
    }

    const uintptr_t handle = nextKernelHandle_++;
    kernels_[handle] = KernelEntry{name, shaderModule, dsLayout, pipelineLayout, pipeline};
    kernelHandles_[name] = handle;
    return handle;
#else
    (void)name; (void)source;
    fprintf(stderr, "  [vk] compile_kernel('%s') failed: built without shaderc "
                    "(TASK_GRAPH_VULKAN_COMPUTE off)\n", name.c_str());
    return 0;
#endif
}

bool VulkanGpuBackend::dispatch(uintptr_t kernel,
                                 const std::vector<GpuBinding>& bindings,
                                 const void* uniform_data, size_t uniform_size,
                                 uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) {
    if (device_ == VK_NULL_HANDLE || !computeCapable_ || kernel == 0) return false;

    std::lock_guard<std::mutex> lock(gpuMutex_);

    auto kit = kernels_.find(kernel);
    if (kit == kernels_.end()) return false;

    // 从 descriptor pool 分配一个 set，写入实际用到的 storage buffer 绑定
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = descriptorPool_;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &kit->second.descriptorSetLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &dsAlloc, &descriptorSet) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(bindings.size());
    writes.reserve(bindings.size());
    for (size_t i = 0; i < bindings.size() && i < kMaxBindings; i++) {
        VulkanBuffer* buf = (VulkanBuffer*)bindings[i].handle;
        if (!buf) continue;
        bufferInfos.push_back(VkDescriptorBufferInfo{buf->buffer, bindings[i].offset, buf->size});
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = (uint32_t)i;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfos.back();
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(device_, (uint32_t)writes.size(), writes.data(), 0, nullptr);

    // push constant 固定 64 字节，不足补零
    uint8_t pushData[kPushConstantBytes] = {};
    if (uniform_data && uniform_size > 0) {
        memcpy(pushData, uniform_data, uniform_size < kPushConstantBytes ? uniform_size : kPushConstantBytes);
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandPool = commandPool_;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cmdAlloc, &commandBuffer) != VK_SUCCESS) {
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, kit->second.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kit->second.pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, kit->second.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, kPushConstantBytes, pushData);
    // GLSL 侧 local_size 8x8，dispatch 网格换算为 workgroup 数；越界由 kernel 边界检查兜底
    vkCmdDispatch(commandBuffer,
                  (grid_x + kLocalSizeX - 1) / kLocalSizeX,
                  (grid_y + kLocalSizeY - 1) / kLocalSizeY,
                  grid_z);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    if (vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet);
        return false;
    }
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet);
    return true;
}

void VulkanGpuBackend::release_kernel(uintptr_t kernel) {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    destroy_kernel_unlocked(kernel);
}

} // namespace task_graph
