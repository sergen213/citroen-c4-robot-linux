// Loader-free check: c++ -std=c++20 -UNDEBUG -Isrc tests/test_vulkan_dispatch_timing.cpp -o /tmp/c4-dispatch-timing-test && /tmp/c4-dispatch-timing-test
#include "rendering/vulkan/VulkanDispatch.hpp"
#include <cassert>

namespace c4::rendering { VulkanDispatch* c4_active_vulkan_dispatch{}; }
static VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice,VkPipelineCache,std::uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice,VkPipelineCache,std::uint32_t,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*) { return VK_ERROR_INITIALIZATION_FAILED; }
int main() {
    c4::rendering::VulkanDispatch dispatch;
    c4::rendering::c4_active_vulkan_dispatch=&dispatch;
    dispatch.c4_vkCreateGraphicsPipelines=graphics;
    dispatch.c4_vkCreateComputePipelines=compute;
    const auto cache=reinterpret_cast<VkPipelineCache>(std::uintptr_t(1));
    dispatch.scene_pipeline_cache=cache;
    assert(vkCreateGraphicsPipelines(VK_NULL_HANDLE,cache,2,nullptr,nullptr,nullptr)==VK_SUCCESS);
    assert(vkCreateGraphicsPipelines(VK_NULL_HANDLE,VK_NULL_HANDLE,1,nullptr,nullptr,nullptr)==VK_SUCCESS);
    assert(vkCreateComputePipelines(VK_NULL_HANDLE,cache,1,nullptr,nullptr,nullptr)==VK_ERROR_INITIALIZATION_FAILED);
    assert(dispatch.pipeline_creation_calls==3);
    assert(dispatch.pipeline_cached_creations==2 && dispatch.pipeline_uncached_creations==1);
    assert(dispatch.pipeline_creation_failures==1);
    assert(dispatch.pipeline_cache_handle_mismatches==1);
    for(unsigned i=0;i<513;++i)assert(vkCreateGraphicsPipelines(VK_NULL_HANDLE,cache,1,nullptr,nullptr,nullptr)==VK_SUCCESS);
    assert(dispatch.pipeline_creation_calls==516 && dispatch.pipeline_cached_creations==515);
}
