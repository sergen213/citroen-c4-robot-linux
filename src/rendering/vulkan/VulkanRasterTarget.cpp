#include "VulkanRasterTarget.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace c4::rendering {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+": Vulkan result "+std::to_string(result));
}
VkResolveModeFlagBits resolve_mode(VkPhysicalDevice physical) noexcept {
    VkPhysicalDeviceDepthStencilResolveProperties resolve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext=&resolve;
    vkGetPhysicalDeviceProperties2(physical,&properties);
    if(resolve.supportedDepthResolveModes&VK_RESOLVE_MODE_MIN_BIT) return VK_RESOLVE_MODE_MIN_BIT;
    if(resolve.supportedDepthResolveModes&VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) return VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    return VK_RESOLVE_MODE_NONE;
}
VkSampleCountFlags image_samples(VkPhysicalDevice physical,VkFormat format,VkImageUsageFlags usage) noexcept {
    VkImageFormatProperties properties{};
    return vkGetPhysicalDeviceImageFormatProperties(physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&properties)==VK_SUCCESS
        ? properties.sampleCounts : 0;
}
bool format_supports(VkPhysicalDevice physical,VkFormat format,VkFormatFeatureFlags flags) noexcept {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical,format,&properties);
    return (properties.optimalTilingFeatures&flags)==flags;
}
}

VulkanRasterTarget::VulkanRasterTarget(Context context) noexcept : context_(context) {
    if(!context_.physical)return;
    const std::array<VkFormat,2> candidates{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM};
    for(bool sampled:std::array<bool,2>{true,false}) {
        const VkFormatFeatureFlags features=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|(sampled?VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT:0);
        const VkImageUsageFlags usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|(sampled?VK_IMAGE_USAGE_SAMPLED_BIT:0);
        for(VkFormat format:candidates) {
            if(format_supports(context_.physical,format,features)
                &&(image_samples(context_.physical,format,usage)&VK_SAMPLE_COUNT_1_BIT)) {
                depth_format_=format;sampled_depth_=sampled;return;
            }
        }
    }
}

unsigned VulkanRasterTarget::supported_sample_mask(VkFormat color_format) const noexcept {
    if(!context_.physical)return 0;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physical,&properties);
    if(properties.limits.maxColorAttachments<5)return 0;
    VkSampleCountFlags samples=properties.limits.framebufferColorSampleCounts&properties.limits.framebufferDepthSampleCounts;
    // Position/material receiver share RGBA32F; normal/sun direct share RGBA16F.
    const std::array<VkFormat,3> colors{color_format,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
    for(VkFormat format:colors) {
        if(!format_supports(context_.physical,format,VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
            ||!(image_samples(context_.physical,format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT)&VK_SAMPLE_COUNT_1_BIT))return 0;
        samples&=image_samples(context_.physical,format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
    if(depth_format_==VK_FORMAT_UNDEFINED)return 0;
    samples&=image_samples(context_.physical,depth_format_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    if(resolve_mode(context_.physical)==VK_RESOLVE_MODE_NONE)samples&=VK_SAMPLE_COUNT_1_BIT;
    return samples;
}
bool VulkanRasterTarget::hdr_supported() const noexcept {
    return context_.physical
        && format_supports(context_.physical,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        && (image_samples(context_.physical,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT)&VK_SAMPLE_COUNT_1_BIT);
}

std::uint32_t VulkanRasterTarget::memory_type(std::uint32_t bits) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(context_.physical,&properties);
    for(std::uint32_t i=0;i<properties.memoryTypeCount;++i)
        if((bits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
    throw std::runtime_error("Scene attachment device-local memory unavailable");
}
VulkanRasterTarget::Image VulkanRasterTarget::image(VkExtent2D extent,VkFormat format,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
    Image result{};
    result.format=format;result.width=extent.width;result.height=extent.height;
    try {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={extent.width,extent.height,1};
        info.mipLevels=1;info.arrayLayers=1;info.samples=samples;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(context_.device,&info,nullptr,&result.handle),"create scene attachment");
        VkMemoryRequirements required{};vkGetImageMemoryRequirements(context_.device,result.handle,&required);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize=required.size;allocation.memoryTypeIndex=memory_type(required.memoryTypeBits);
        check(vkAllocateMemory(context_.device,&allocation,nullptr,&result.memory),"allocate scene attachment");
        check(vkBindImageMemory(context_.device,result.handle,result.memory,0),"bind scene attachment");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image=result.handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;
        view.subresourceRange={aspect,0,1,0,1};
        check(vkCreateImageView(context_.device,&view,nullptr,&result.view),"create scene attachment view");
    } catch(...) {destroy(result);throw;}
    return result;
}
void VulkanRasterTarget::destroy(Image& image) noexcept {
    if(image.view)vkDestroyImageView(context_.device,image.view,nullptr);
    if(image.handle)vkDestroyImage(context_.device,image.handle,nullptr);
    if(image.memory)vkFreeMemory(context_.device,image.memory,nullptr);
    image={};
}
void VulkanRasterTarget::clear() noexcept {
    if(!context_.device)return;
    if(framebuffer_)vkDestroyFramebuffer(context_.device,framebuffer_,nullptr);
    if(pass_)vkDestroyRenderPass(context_.device,pass_,nullptr);
    framebuffer_={};pass_={};
    for(auto& image:multisampled_)destroy(image);
    for(auto& image:resolved_)destroy(image);
    samples_=VK_SAMPLE_COUNT_1_BIT;depth_resolve_=VK_RESOLVE_MODE_NONE;
}

void VulkanRasterTarget::resize(VkExtent2D extent,unsigned samples,VkFormat color_format) {
    if(!context_.physical||!context_.device||!extent.width||!extent.height)
        throw std::runtime_error("Invalid scene attachment context or extent");
    if(!samples||(samples&(samples-1))||(supported_sample_mask(color_format)&samples)==0)
        throw std::runtime_error("Requested scene attachment sample count unavailable");
    clear();
    samples_=static_cast<VkSampleCountFlagBits>(samples);
    depth_resolve_=samples>1?resolve_mode(context_.physical):VK_RESOLVE_MODE_NONE;
    const bool msaa=samples>1;
    const std::array<VkFormat,6> formats{color_format,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,depth_format_};
    try {
        for(unsigned i=0;i<6;++i) {
            const bool depth=i==5;
            const VkImageUsageFlags attachment=depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            const VkImageAspectFlags aspect=depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
            const VkImageUsageFlags sampled=(!depth||sampled_depth_)?VK_IMAGE_USAGE_SAMPLED_BIT:0;
            resolved_[i]=image(extent,formats[i],VK_SAMPLE_COUNT_1_BIT,attachment|sampled,aspect);
            if(msaa)multisampled_[i]=image(extent,formats[i],samples_,attachment,aspect);
        }
        std::array<VkAttachmentDescription2,12> attachments{};
        for(unsigned i=0;i<(msaa?12u:6u);++i) {
            auto& attachment=attachments[i];const bool depth=i%6==5;const bool resolve=msaa&&i>=6;
            attachment.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            attachment.format=formats[i%6];attachment.samples=resolve?VK_SAMPLE_COUNT_1_BIT:samples_;
            attachment.loadOp=resolve?VK_ATTACHMENT_LOAD_OP_DONT_CARE:VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp=msaa&&!resolve?VK_ATTACHMENT_STORE_OP_DONT_CARE:VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                (msaa&&!resolve?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        std::array<VkAttachmentReference2,5> colors{},resolves{};
        for(unsigned i=0;i<5;++i) {
            colors[i]={VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT};
            resolves[i]={VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,i+6,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT};
        }
        VkAttachmentReference2 depth{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,5,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT};
        VkAttachmentReference2 resolved_depth{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,11,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT};
        VkSubpassDescriptionDepthStencilResolve depth_resolve{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE};
        depth_resolve.depthResolveMode=depth_resolve_;depth_resolve.stencilResolveMode=VK_RESOLVE_MODE_NONE;
        depth_resolve.pDepthStencilResolveAttachment=&resolved_depth;
        VkSubpassDescription2 subpass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
        subpass.pNext=msaa?&depth_resolve:nullptr;subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount=5;subpass.pColorAttachments=colors.data();subpass.pResolveAttachments=msaa?resolves.data():nullptr;
        subpass.pDepthStencilAttachment=&depth;
        std::array<VkSubpassDependency2,2> dependencies{};
        for(auto& dependency:dependencies)dependency.sType=VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;
        VkRenderPassCreateInfo2 pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
        pass.attachmentCount=msaa?12:6;pass.pAttachments=attachments.data();pass.subpassCount=1;pass.pSubpasses=&subpass;
        pass.dependencyCount=2;pass.pDependencies=dependencies.data();
        check(vkCreateRenderPass2(context_.device,&pass,nullptr,&pass_),"create resolved scene render pass");
        std::array<VkImageView,12> views{};
        for(unsigned i=0;i<6;++i) {views[i]=msaa?multisampled_[i].view:resolved_[i].view;if(msaa)views[i+6]=resolved_[i].view;}
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass=pass_;framebuffer.attachmentCount=msaa?12:6;framebuffer.pAttachments=views.data();
        framebuffer.width=extent.width;framebuffer.height=extent.height;framebuffer.layers=1;
        check(vkCreateFramebuffer(context_.device,&framebuffer,nullptr,&framebuffer_),"create resolved scene framebuffer");
    } catch(...) {clear();throw;}
}
} // namespace c4::rendering
