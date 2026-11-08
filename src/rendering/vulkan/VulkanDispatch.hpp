#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <chrono>
#include <cstdint>

// One table per initialization phase; suffixes keep real loader names independent
// of the call-site aliases below.
#define C4_VK_REQUIRED_GLOBAL_FUNCTIONS(X) \
    X(CreateInstance) \
    X(EnumerateInstanceExtensionProperties) \
    X(EnumerateInstanceLayerProperties)

#define C4_VK_OPTIONAL_GLOBAL_FUNCTIONS(X) \
    X(EnumerateInstanceVersion)

#define C4_VK_REQUIRED_INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) \
    X(DestroySurfaceKHR) \
    X(GetDeviceProcAddr) \
    X(CreateDevice) \
    X(EnumeratePhysicalDevices) \
    X(EnumerateDeviceExtensionProperties) \
    X(GetPhysicalDeviceFeatures2) \
    X(GetPhysicalDeviceFormatProperties) \
    X(GetPhysicalDeviceImageFormatProperties) \
    X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceProperties2) \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(GetPhysicalDeviceSurfaceFormatsKHR) \
    X(GetPhysicalDeviceSurfacePresentModesKHR) \
    X(GetPhysicalDeviceSurfaceSupportKHR)

#define C4_VK_OPTIONAL_INSTANCE_FUNCTIONS(X) \
    X(CreateDebugUtilsMessengerEXT) \
    X(DestroyDebugUtilsMessengerEXT)

#define C4_VK_REQUIRED_DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) \
    X(DeviceWaitIdle) \
    X(AcquireNextImageKHR) \
    X(AllocateCommandBuffers) \
    X(AllocateDescriptorSets) \
    X(AllocateMemory) \
    X(BeginCommandBuffer) \
    X(BindBufferMemory) \
    X(BindImageMemory) \
    X(CmdBeginRenderPass) \
    X(CmdBindDescriptorSets) \
    X(CmdBindIndexBuffer) \
    X(CmdBindPipeline) \
    X(CmdBindVertexBuffers) \
    X(CmdBindVertexBuffers2) \
    X(CmdBlitImage) \
    X(CmdClearAttachments) \
    X(CmdClearColorImage) \
    X(CmdClearDepthStencilImage) \
    X(CmdCopyBuffer) \
    X(CmdCopyBufferToImage) \
    X(CmdCopyImage) \
    X(CmdCopyImageToBuffer) \
    X(CmdDispatch) \
    X(CmdDraw) \
    X(CmdDrawIndexed) \
    X(CmdEndRenderPass) \
    X(CmdPipelineBarrier) \
    X(CmdPushConstants) \
    X(CmdResetQueryPool) \
    X(CmdSetScissor) \
    X(CmdSetViewport) \
    X(CmdWriteTimestamp) \
    X(CreateBuffer) \
    X(CreateCommandPool) \
    X(CreateComputePipelines) \
    X(CreateDescriptorPool) \
    X(CreateDescriptorSetLayout) \
    X(CreateFence) \
    X(CreateFramebuffer) \
    X(CreateGraphicsPipelines) \
    X(CreateImage) \
    X(CreateImageView) \
    X(CreatePipelineCache) \
    X(CreatePipelineLayout) \
    X(CreateQueryPool) \
    X(CreateRenderPass) \
    X(CreateRenderPass2) \
    X(CreateSampler) \
    X(CreateSemaphore) \
    X(CreateShaderModule) \
    X(CreateSwapchainKHR) \
    X(DestroyBuffer) \
    X(DestroyCommandPool) \
    X(DestroyDescriptorPool) \
    X(DestroyDescriptorSetLayout) \
    X(DestroyFence) \
    X(DestroyFramebuffer) \
    X(DestroyImage) \
    X(DestroyImageView) \
    X(DestroyPipeline) \
    X(DestroyPipelineCache) \
    X(DestroyPipelineLayout) \
    X(DestroyQueryPool) \
    X(DestroyRenderPass) \
    X(DestroySampler) \
    X(DestroySemaphore) \
    X(DestroyShaderModule) \
    X(DestroySwapchainKHR) \
    X(EndCommandBuffer) \
    X(FreeCommandBuffers) \
    X(FreeMemory) \
    X(GetBufferMemoryRequirements) \
    X(GetDeviceQueue) \
    X(GetImageMemoryRequirements) \
    X(GetFenceStatus) \
    X(GetPipelineCacheData) \
    X(GetQueryPoolResults) \
    X(GetSwapchainImagesKHR) \
    X(MapMemory) \
    X(QueuePresentKHR) \
    X(QueueSubmit) \
    X(QueueWaitIdle) \
    X(ResetCommandBuffer) \
    X(ResetFences) \
    X(UnmapMemory) \
    X(UpdateDescriptorSets) \
    X(WaitForFences)

#define C4_VK_RAY_QUERY_FUNCTIONS(X) \
    X(GetBufferDeviceAddress) \
    X(CreateAccelerationStructureKHR) \
    X(DestroyAccelerationStructureKHR) \
    X(GetAccelerationStructureBuildSizesKHR) \
    X(GetAccelerationStructureDeviceAddressKHR) \
    X(CmdBuildAccelerationStructuresKHR)

namespace c4::rendering {
// Prefix storage names prevent ELF vkFoo interposition with loader functions.
struct VulkanDispatch {
    VulkanDispatch() = default;
    VulkanDispatch(const VulkanDispatch&) = delete;
    VulkanDispatch& operator=(const VulkanDispatch&) = delete;
    VulkanDispatch(VulkanDispatch&&) = delete;
    VulkanDispatch& operator=(VulkanDispatch&&) = delete;

    PFN_vkGetInstanceProcAddr c4_vkGetInstanceProcAddr{};
#define C4_VK_DECLARE(name) PFN_vk##name c4_vk##name{};
    C4_VK_REQUIRED_GLOBAL_FUNCTIONS(C4_VK_DECLARE)
    C4_VK_OPTIONAL_GLOBAL_FUNCTIONS(C4_VK_DECLARE)
    C4_VK_REQUIRED_INSTANCE_FUNCTIONS(C4_VK_DECLARE)
    C4_VK_OPTIONAL_INSTANCE_FUNCTIONS(C4_VK_DECLARE)
    C4_VK_REQUIRED_DEVICE_FUNCTIONS(C4_VK_DECLARE)
    C4_VK_RAY_QUERY_FUNCTIONS(C4_VK_DECLARE)
#undef C4_VK_DECLARE
    bool library_loaded{};
    std::array<std::uint64_t,512> pipeline_creation_ns{};
    std::uint64_t pipeline_creation_calls{}, pipeline_creation_total_ns{}, pipeline_cached_creations{}, pipeline_uncached_creations{}, pipeline_creation_failures{};
    VkPipelineCache scene_pipeline_cache{};
    std::uint64_t pipeline_cache_handle_mismatches{};
    void pipeline_created(VkPipelineCache cache,std::uint32_t count,VkResult result,std::chrono::steady_clock::time_point start) noexcept {
        const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
        pipeline_creation_ns[pipeline_creation_calls++%pipeline_creation_ns.size()]=ns;pipeline_creation_total_ns+=ns;
        if(result!=VK_SUCCESS)++pipeline_creation_failures;
        else if(cache)pipeline_cached_creations+=count;else pipeline_uncached_creations+=count;
        if(scene_pipeline_cache&&cache!=scene_pipeline_cache)pipeline_cache_handle_mismatches+=count;
    }
};

// ponytail: Vulkan renderer host operations and scoped dispatch selection stay
// on the main thread; GPU submissions may finish asynchronously.
class VulkanDispatchScope {
public:
    explicit VulkanDispatchScope(VulkanDispatch& dispatch) noexcept;
    ~VulkanDispatchScope();
    VulkanDispatchScope(const VulkanDispatchScope&) = delete;
    VulkanDispatchScope& operator=(const VulkanDispatchScope&) = delete;
    VulkanDispatchScope(VulkanDispatchScope&&) = delete;
    VulkanDispatchScope& operator=(VulkanDispatchScope&&) = delete;

private:
    VulkanDispatch* previous_;
};

extern VulkanDispatch* c4_active_vulkan_dispatch;
inline VulkanDispatch& active_vulkan_dispatch() noexcept {
    return *c4_active_vulkan_dispatch;
}
inline VkResult timed_compute_pipelines(VkDevice device,VkPipelineCache cache,std::uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* allocator,VkPipeline* pipelines) {
    auto& dispatch=active_vulkan_dispatch();const auto start=std::chrono::steady_clock::now();
    const auto result=dispatch.c4_vkCreateComputePipelines(device,cache,count,infos,allocator,pipelines);dispatch.pipeline_created(cache,count,result,start);return result;
}
inline VkResult timed_graphics_pipelines(VkDevice device,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkAllocationCallbacks* allocator,VkPipeline* pipelines) {
    auto& dispatch=active_vulkan_dispatch();const auto start=std::chrono::steady_clock::now();
    const auto result=dispatch.c4_vkCreateGraphicsPipelines(device,cache,count,infos,allocator,pipelines);dispatch.pipeline_created(cache,count,result,start);return result;
}
} // namespace c4::rendering

#define vkGetInstanceProcAddr (::c4::rendering::active_vulkan_dispatch().c4_vkGetInstanceProcAddr)
#define vkCreateInstance (::c4::rendering::active_vulkan_dispatch().c4_vkCreateInstance)
#define vkEnumerateInstanceExtensionProperties (::c4::rendering::active_vulkan_dispatch().c4_vkEnumerateInstanceExtensionProperties)
#define vkEnumerateInstanceLayerProperties (::c4::rendering::active_vulkan_dispatch().c4_vkEnumerateInstanceLayerProperties)
#define vkEnumerateInstanceVersion (::c4::rendering::active_vulkan_dispatch().c4_vkEnumerateInstanceVersion)
#define vkDestroyInstance (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyInstance)
#define vkDestroySurfaceKHR (::c4::rendering::active_vulkan_dispatch().c4_vkDestroySurfaceKHR)
#define vkGetDeviceProcAddr (::c4::rendering::active_vulkan_dispatch().c4_vkGetDeviceProcAddr)
#define vkCreateDevice (::c4::rendering::active_vulkan_dispatch().c4_vkCreateDevice)
#define vkEnumeratePhysicalDevices (::c4::rendering::active_vulkan_dispatch().c4_vkEnumeratePhysicalDevices)
#define vkEnumerateDeviceExtensionProperties (::c4::rendering::active_vulkan_dispatch().c4_vkEnumerateDeviceExtensionProperties)
#define vkGetPhysicalDeviceFeatures2 (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceFeatures2)
#define vkGetPhysicalDeviceFormatProperties (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceFormatProperties)
#define vkGetPhysicalDeviceImageFormatProperties (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceImageFormatProperties)
#define vkGetPhysicalDeviceMemoryProperties (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceMemoryProperties)
#define vkGetPhysicalDeviceProperties (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceProperties)
#define vkGetPhysicalDeviceProperties2 (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceProperties2)
#define vkGetPhysicalDeviceQueueFamilyProperties (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceQueueFamilyProperties)
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
#define vkGetPhysicalDeviceSurfaceFormatsKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceSurfaceFormatsKHR)
#define vkGetPhysicalDeviceSurfacePresentModesKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceSurfacePresentModesKHR)
#define vkGetPhysicalDeviceSurfaceSupportKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetPhysicalDeviceSurfaceSupportKHR)
#define vkCreateDebugUtilsMessengerEXT (::c4::rendering::active_vulkan_dispatch().c4_vkCreateDebugUtilsMessengerEXT)
#define vkDestroyDebugUtilsMessengerEXT (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyDebugUtilsMessengerEXT)
#define vkDestroyDevice (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyDevice)
#define vkDeviceWaitIdle (::c4::rendering::active_vulkan_dispatch().c4_vkDeviceWaitIdle)
#define vkAcquireNextImageKHR (::c4::rendering::active_vulkan_dispatch().c4_vkAcquireNextImageKHR)
#define vkAllocateCommandBuffers (::c4::rendering::active_vulkan_dispatch().c4_vkAllocateCommandBuffers)
#define vkAllocateDescriptorSets (::c4::rendering::active_vulkan_dispatch().c4_vkAllocateDescriptorSets)
#define vkAllocateMemory (::c4::rendering::active_vulkan_dispatch().c4_vkAllocateMemory)
#define vkBeginCommandBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkBeginCommandBuffer)
#define vkBindBufferMemory (::c4::rendering::active_vulkan_dispatch().c4_vkBindBufferMemory)
#define vkBindImageMemory (::c4::rendering::active_vulkan_dispatch().c4_vkBindImageMemory)
#define vkCmdBeginRenderPass (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBeginRenderPass)
#define vkCmdBindDescriptorSets (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBindDescriptorSets)
#define vkCmdBindIndexBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBindIndexBuffer)
#define vkCmdBindPipeline (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBindPipeline)
#define vkCmdBindVertexBuffers (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBindVertexBuffers)
#define vkCmdBindVertexBuffers2 (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBindVertexBuffers2)
#define vkCmdBlitImage (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBlitImage)
#define vkCmdClearAttachments (::c4::rendering::active_vulkan_dispatch().c4_vkCmdClearAttachments)
#define vkCmdClearColorImage (::c4::rendering::active_vulkan_dispatch().c4_vkCmdClearColorImage)
#define vkCmdClearDepthStencilImage (::c4::rendering::active_vulkan_dispatch().c4_vkCmdClearDepthStencilImage)
#define vkCmdCopyBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkCmdCopyBuffer)
#define vkCmdCopyBufferToImage (::c4::rendering::active_vulkan_dispatch().c4_vkCmdCopyBufferToImage)
#define vkCmdCopyImage (::c4::rendering::active_vulkan_dispatch().c4_vkCmdCopyImage)
#define vkCmdCopyImageToBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkCmdCopyImageToBuffer)
#define vkCmdDispatch (::c4::rendering::active_vulkan_dispatch().c4_vkCmdDispatch)
#define vkCmdDraw (::c4::rendering::active_vulkan_dispatch().c4_vkCmdDraw)
#define vkCmdDrawIndexed (::c4::rendering::active_vulkan_dispatch().c4_vkCmdDrawIndexed)
#define vkCmdEndRenderPass (::c4::rendering::active_vulkan_dispatch().c4_vkCmdEndRenderPass)
#define vkCmdPipelineBarrier (::c4::rendering::active_vulkan_dispatch().c4_vkCmdPipelineBarrier)
#define vkCmdPushConstants (::c4::rendering::active_vulkan_dispatch().c4_vkCmdPushConstants)
#define vkCmdResetQueryPool (::c4::rendering::active_vulkan_dispatch().c4_vkCmdResetQueryPool)
#define vkCmdSetScissor (::c4::rendering::active_vulkan_dispatch().c4_vkCmdSetScissor)
#define vkCmdSetViewport (::c4::rendering::active_vulkan_dispatch().c4_vkCmdSetViewport)
#define vkCmdWriteTimestamp (::c4::rendering::active_vulkan_dispatch().c4_vkCmdWriteTimestamp)
#define vkCreateBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkCreateBuffer)
#define vkCreateCommandPool (::c4::rendering::active_vulkan_dispatch().c4_vkCreateCommandPool)
#define vkCreateComputePipelines (::c4::rendering::timed_compute_pipelines)
#define vkCreateDescriptorPool (::c4::rendering::active_vulkan_dispatch().c4_vkCreateDescriptorPool)
#define vkCreateDescriptorSetLayout (::c4::rendering::active_vulkan_dispatch().c4_vkCreateDescriptorSetLayout)
#define vkCreateFence (::c4::rendering::active_vulkan_dispatch().c4_vkCreateFence)
#define vkCreateFramebuffer (::c4::rendering::active_vulkan_dispatch().c4_vkCreateFramebuffer)
#define vkCreateGraphicsPipelines (::c4::rendering::timed_graphics_pipelines)
#define vkCreateImage (::c4::rendering::active_vulkan_dispatch().c4_vkCreateImage)
#define vkCreateImageView (::c4::rendering::active_vulkan_dispatch().c4_vkCreateImageView)
#define vkCreatePipelineCache (::c4::rendering::active_vulkan_dispatch().c4_vkCreatePipelineCache)
#define vkCreatePipelineLayout (::c4::rendering::active_vulkan_dispatch().c4_vkCreatePipelineLayout)
#define vkCreateQueryPool (::c4::rendering::active_vulkan_dispatch().c4_vkCreateQueryPool)
#define vkCreateRenderPass (::c4::rendering::active_vulkan_dispatch().c4_vkCreateRenderPass)
#define vkCreateRenderPass2 (::c4::rendering::active_vulkan_dispatch().c4_vkCreateRenderPass2)
#define vkCreateSampler (::c4::rendering::active_vulkan_dispatch().c4_vkCreateSampler)
#define vkCreateSemaphore (::c4::rendering::active_vulkan_dispatch().c4_vkCreateSemaphore)
#define vkCreateShaderModule (::c4::rendering::active_vulkan_dispatch().c4_vkCreateShaderModule)
#define vkCreateSwapchainKHR (::c4::rendering::active_vulkan_dispatch().c4_vkCreateSwapchainKHR)
#define vkDestroyBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyBuffer)
#define vkDestroyCommandPool (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyCommandPool)
#define vkDestroyDescriptorPool (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyDescriptorPool)
#define vkDestroyDescriptorSetLayout (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyDescriptorSetLayout)
#define vkDestroyFence (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyFence)
#define vkDestroyFramebuffer (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyFramebuffer)
#define vkDestroyImage (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyImage)
#define vkDestroyImageView (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyImageView)
#define vkDestroyPipeline (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyPipeline)
#define vkDestroyPipelineCache (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyPipelineCache)
#define vkDestroyPipelineLayout (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyPipelineLayout)
#define vkDestroyQueryPool (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyQueryPool)
#define vkDestroyRenderPass (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyRenderPass)
#define vkDestroySampler (::c4::rendering::active_vulkan_dispatch().c4_vkDestroySampler)
#define vkDestroySemaphore (::c4::rendering::active_vulkan_dispatch().c4_vkDestroySemaphore)
#define vkDestroyShaderModule (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyShaderModule)
#define vkDestroySwapchainKHR (::c4::rendering::active_vulkan_dispatch().c4_vkDestroySwapchainKHR)
#define vkEndCommandBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkEndCommandBuffer)
#define vkFreeCommandBuffers (::c4::rendering::active_vulkan_dispatch().c4_vkFreeCommandBuffers)
#define vkFreeMemory (::c4::rendering::active_vulkan_dispatch().c4_vkFreeMemory)
#define vkGetBufferMemoryRequirements (::c4::rendering::active_vulkan_dispatch().c4_vkGetBufferMemoryRequirements)
#define vkGetDeviceQueue (::c4::rendering::active_vulkan_dispatch().c4_vkGetDeviceQueue)
#define vkGetImageMemoryRequirements (::c4::rendering::active_vulkan_dispatch().c4_vkGetImageMemoryRequirements)
#define vkGetQueryPoolResults (::c4::rendering::active_vulkan_dispatch().c4_vkGetQueryPoolResults)
#define vkGetSwapchainImagesKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetSwapchainImagesKHR)
#define vkMapMemory (::c4::rendering::active_vulkan_dispatch().c4_vkMapMemory)
#define vkQueuePresentKHR (::c4::rendering::active_vulkan_dispatch().c4_vkQueuePresentKHR)
#define vkQueueSubmit (::c4::rendering::active_vulkan_dispatch().c4_vkQueueSubmit)
#define vkQueueWaitIdle (::c4::rendering::active_vulkan_dispatch().c4_vkQueueWaitIdle)
#define vkResetCommandBuffer (::c4::rendering::active_vulkan_dispatch().c4_vkResetCommandBuffer)
#define vkResetFences (::c4::rendering::active_vulkan_dispatch().c4_vkResetFences)
#define vkUnmapMemory (::c4::rendering::active_vulkan_dispatch().c4_vkUnmapMemory)
#define vkUpdateDescriptorSets (::c4::rendering::active_vulkan_dispatch().c4_vkUpdateDescriptorSets)
#define vkWaitForFences (::c4::rendering::active_vulkan_dispatch().c4_vkWaitForFences)
#define vkGetBufferDeviceAddress (::c4::rendering::active_vulkan_dispatch().c4_vkGetBufferDeviceAddress)
#define vkCreateAccelerationStructureKHR (::c4::rendering::active_vulkan_dispatch().c4_vkCreateAccelerationStructureKHR)
#define vkDestroyAccelerationStructureKHR (::c4::rendering::active_vulkan_dispatch().c4_vkDestroyAccelerationStructureKHR)
#define vkGetAccelerationStructureBuildSizesKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetAccelerationStructureBuildSizesKHR)
#define vkGetAccelerationStructureDeviceAddressKHR (::c4::rendering::active_vulkan_dispatch().c4_vkGetAccelerationStructureDeviceAddressKHR)
#define vkCmdBuildAccelerationStructuresKHR (::c4::rendering::active_vulkan_dispatch().c4_vkCmdBuildAccelerationStructuresKHR)

namespace c4::rendering {
// Each table owns one explicit SDL loader reference in addition to SDL's
// Vulkan-window reference; failed global loading releases that table's reference.
void load_vulkan_global_functions(VulkanDispatch& dispatch);
// Call immediately after creation, before any query/surface/helper use. Partial
// instance/device failures preserve this table's destructor PFNs for owner cleanup.
void load_vulkan_instance_functions(VulkanDispatch& dispatch, VkInstance instance);
void load_vulkan_device_functions(VulkanDispatch& dispatch, VkDevice device, bool rt_enabled);
// Only after helpers, device, surface, debug messenger and instance are gone.
// Cleanup clears only this table's fields and releases only its loader reference.
void unload_vulkan_library(VulkanDispatch& dispatch) noexcept;
} // namespace c4::rendering
#define vkGetFenceStatus (::c4::rendering::active_vulkan_dispatch().c4_vkGetFenceStatus)
#define vkGetPipelineCacheData (::c4::rendering::active_vulkan_dispatch().c4_vkGetPipelineCacheData)
