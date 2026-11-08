#include "quality.hpp"
#include "rendering/quality/ShadowCascades.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace c4::rendering {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan quality: ")+operation+" ("+std::to_string(result)+")");
}
using Matrix = std::array<float,16>;
bool inverse(const Matrix& input, Matrix& output) noexcept {
    double rows[4][8]{};
    for (unsigned r=0;r<4;++r) for (unsigned c=0;c<4;++c) { rows[r][c]=input[c*4+r]; rows[r][c+4]=r==c?1.:0.; }
    for (unsigned c=0;c<4;++c) {
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r) if(std::abs(rows[r][c])>std::abs(rows[pivot][c])) pivot=r;
        if(!std::isfinite(rows[pivot][c]) || std::abs(rows[pivot][c])<1.e-12) return false;
        for(unsigned k=0;k<8;++k) std::swap(rows[c][k],rows[pivot][k]);
        const double divisor=rows[c][c];
        for(double& value:rows[c]) value/=divisor;
        for(unsigned r=0;r<4;++r) if(r!=c) { const double scale=rows[r][c]; for(unsigned k=0;k<8;++k) rows[r][k]-=scale*rows[c][k]; }
    }
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) { const float value=static_cast<float>(rows[r][c+4]); if(!std::isfinite(value))return false; output[c*4+r]=value; }
    return true;
}
struct alignas(16) PostMatrices { Matrix projection{}, inverse_projection{}; };
static_assert(sizeof(PostMatrices)==128);
}

struct QualityPasses::Impl {
    struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{}; };
    struct Target { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; VkFramebuffer framebuffer{}; VkExtent2D extent{}; };
    enum Slot : unsigned { Normal, Ao, AoHorizontal, AoFiltered, Bright, BloomHorizontal, BloomFiltered, Mapped, Antialiased, SlotCount };
    Context context;
    VkDevice device{};
    VkExtent2D extent{};
    quality::ResolvedGraphics settings{};
    quality::GraphicsCapabilities caps{};
    QualityDiagnostics stats{};
    VkSampler linear{}, nearest{};
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool descriptor_pool{};
    VkPipelineLayout pipeline_layout{};
    std::array<VkRenderPass,SlotCount> passes{};
    std::array<VkPipeline,SlotCount> pipelines{};
    std::array<VkDescriptorSet,SlotCount> sets{};
    std::array<Target,SlotCount> targets{};
    Buffer globals_buffer{}, quality_buffer{}, matrices_buffer{}, shadows_buffer{};
    Target shadow;
    VkRenderPass shadow_pass{};
    VkFormat shadow_format{VK_FORMAT_UNDEFINED};
    std::array<VkImageView,4> shadow_layers{};
    std::array<VkFramebuffer,4> shadow_frames{};
    VulkanShadowUniforms shadow_data{};
    unsigned shadow_resolution{};
    bool initialized{}, shadow_readable{};
    static constexpr std::array<unsigned,SlotCount> modes{9,10,11,12,13,14,15,16,17};
    static constexpr std::array<VkFormat,SlotCount> formats{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16_SFLOAT,VK_FORMAT_R16_SFLOAT,VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM};

    explicit Impl(const Context& value):context(value),device(value.device) {
        if(!device || !context.physical) throw std::invalid_argument("Vulkan quality requires a live physical/device pair");
    }
    ~Impl(){ destroy(); }
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(context.physical,&properties);
        for(std::uint32_t i=0;i<properties.memoryTypeCount;++i) if((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Vulkan quality memory type unavailable");
    }
    void destroy(Buffer& buffer) noexcept {
        if(buffer.mapped)vkUnmapMemory(device,buffer.memory);
        if(buffer.buffer)vkDestroyBuffer(device,buffer.buffer,nullptr);
        if(buffer.memory)vkFreeMemory(device,buffer.memory,nullptr);
        buffer={};
    }
    void destroy(Target& target) noexcept {
        if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        if(target.view)vkDestroyImageView(device,target.view,nullptr);
        if(target.image)vkDestroyImage(device,target.image,nullptr);
        if(target.memory)vkFreeMemory(device,target.memory,nullptr);
        target={};
    }
    Buffer make_buffer(VkDeviceSize size) {
        Buffer output;
        try {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=size; info.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            check(vkCreateBuffer(device,&info,nullptr,&output.buffer),"create uniform buffer");
            VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device,output.buffer,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=requirements.size;
            allocation.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkAllocateMemory(device,&allocation,nullptr,&output.memory),"allocate uniform memory");
            check(vkBindBufferMemory(device,output.buffer,output.memory,0),"bind uniform memory");
            check(vkMapMemory(device,output.memory,0,VK_WHOLE_SIZE,0,&output.mapped),"map uniform memory");
        } catch(...) { destroy(output); throw; }
        return output;
    }
    bool supports(VkFormat format,VkFormatFeatureFlags required) const noexcept {
        VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(context.physical,format,&properties);
        return (properties.optimalTilingFeatures&required)==required;
    }
    Target make_target(VkExtent2D size,VkFormat format,VkRenderPass pass,unsigned layers=1) {
        Target output; output.extent=size;
        const bool depth=format==shadow_format && layers==4;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.format=format;
            info.extent={size.width,size.height,1}; info.mipLevels=1; info.arrayLayers=layers; info.samples=VK_SAMPLE_COUNT_1_BIT;
            info.tiling=VK_IMAGE_TILING_OPTIMAL; info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateImage(device,&info,nullptr,&output.image),"create quality target");
            VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device,output.image,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=requirements.size;
            allocation.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device,&allocation,nullptr,&output.memory),"allocate target memory");
            check(vkBindImageMemory(device,output.image,output.memory,0),"bind target memory");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=output.image; view.viewType=layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
            view.format=format; view.subresourceRange={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers};
            check(vkCreateImageView(device,&view,nullptr,&output.view),"create target view");
            if(pass && layers==1) {
                VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; frame.renderPass=pass; frame.attachmentCount=1; frame.pAttachments=&output.view;
                frame.width=size.width; frame.height=size.height; frame.layers=1;
                check(vkCreateFramebuffer(device,&frame,nullptr,&output.framebuffer),"create target framebuffer");
            }
        } catch(...) { destroy(output); throw; }
        return output;
    }
    VkRenderPass make_pass(VkFormat format,bool depth) {
        VkAttachmentDescription attachment{}; attachment.format=format; attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference reference{0,depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        if(depth)subpass.pDepthStencilAttachment=&reference; else { subpass.colorAttachmentCount=1; subpass.pColorAttachments=&reference; }
        std::array<VkSubpassDependency,2> dependencies{};
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask=depth?(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT):VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask=depth?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0; dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask=dependencies[0].dstStageMask;
        dependencies[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask=dependencies[0].dstAccessMask; dependencies[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; info.attachmentCount=1; info.pAttachments=&attachment;
        info.subpassCount=1; info.pSubpasses=&subpass; info.dependencyCount=2; info.pDependencies=dependencies.data();
        VkRenderPass output{}; check(vkCreateRenderPass(device,&info,nullptr,&output),"create quality render pass"); return output;
    }
    VkShaderModule shader(const char* name) {
        std::ifstream input(context.shader_directory/name,std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error(std::string("Missing signed Vulkan quality shader: ")+name);
        const auto length=input.tellg();
        if(length<20 || length%4!=0)throw std::runtime_error("Invalid Vulkan shader byte length");
        std::vector<std::uint32_t> words(static_cast<std::size_t>(length)/4); input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(words.data()),length) || words.front()!=0x07230203u)throw std::runtime_error("Invalid Vulkan SPIR-V");
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize=words.size()*4; info.pCode=words.data();
        VkShaderModule output{}; check(vkCreateShaderModule(device,&info,nullptr,&output),"create quality shader"); return output;
    }
    VkPipeline make_pipeline(VkShaderModule vertex,VkShaderModule fragment,unsigned slot) {
        const std::uint32_t mode=modes[slot];
        const VkSpecializationMapEntry entry{2,0,sizeof(mode)};
        const VkSpecializationInfo specialization{1,&entry,sizeof(mode),&mode};
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};
        stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[0].module=vertex; stages[1].module=fragment; stages[0].pName=stages[1].pName="main";
        stages[0].pSpecializationInfo=stages[1].pSpecializationInfo=&specialization;
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blending.attachmentCount=1; blending.pAttachments=&attachment;
        constexpr std::array<VkDynamicState,2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamic_states.data();
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.stageCount=2; info.pStages=stages.data();
        info.pVertexInputState=&vertex_input; info.pInputAssemblyState=&assembly; info.pViewportState=&viewport;
        info.pRasterizationState=&raster; info.pMultisampleState=&samples; info.pColorBlendState=&blending;
        info.pDynamicState=&dynamic; info.pDepthStencilState=&depth; info.layout=pipeline_layout; info.renderPass=passes[slot];
        VkPipeline output{}; check(vkCreateGraphicsPipelines(device,context.pipeline_cache,1,&info,nullptr,&output),"create quality fullscreen pipeline"); return output;
    }
    void initialize() {
        constexpr VkFormatFeatureFlags color_features=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        for(VkFormat format:formats) if(!supports(format,color_features))throw std::runtime_error("Vulkan optional quality target format unavailable");
        if(!supports(VK_FORMAT_R16G16B16A16_SFLOAT,color_features|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))throw std::runtime_error("Vulkan quality HDR linear filtering unavailable");
        for(VkFormat candidate:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM})if(supports(candidate,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)){shadow_format=candidate;break;}
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST; sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device,&sampler,nullptr,&linear),"create quality linear sampler");
        sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
        check(vkCreateSampler(device,&sampler,nullptr,&nearest),"create quality nearest sampler");
        globals_buffer=make_buffer(sizeof(VulkanGlobals)); quality_buffer=make_buffer(sizeof(quality::QualityUniforms));
        matrices_buffer=make_buffer(sizeof(PostMatrices)); shadows_buffer=make_buffer(sizeof(VulkanShadowUniforms));
        std::memset(shadows_buffer.mapped,0,sizeof(VulkanShadowUniforms));
        std::array<VkDescriptorSetLayoutBinding,10> bindings{};
        constexpr std::array<unsigned,10> numbers{0,1,2,3,4,5,6,7,11,14};
        for(unsigned i=0;i<bindings.size();++i) {
            bindings[i].binding=numbers[i]; bindings[i].descriptorCount=1;
            bindings[i].descriptorType=(numbers[i]==1 || numbers[i]>=10)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=static_cast<unsigned>(bindings.size()); layout.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device,&layout,nullptr,&descriptor_layout),"create quality descriptor layout");
        VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipeline.setLayoutCount=1; pipeline.pSetLayouts=&descriptor_layout;
        check(vkCreatePipelineLayout(device,&pipeline,nullptr,&pipeline_layout),"create quality pipeline layout");
        const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,7*SlotCount},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3*SlotCount}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=SlotCount; pool.poolSizeCount=2; pool.pPoolSizes=sizes.data();
        check(vkCreateDescriptorPool(device,&pool,nullptr,&descriptor_pool),"create quality descriptor pool");
        std::array<VkDescriptorSetLayout,SlotCount> layouts{}; layouts.fill(descriptor_layout);
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocation.descriptorPool=descriptor_pool; allocation.descriptorSetCount=SlotCount; allocation.pSetLayouts=layouts.data();
        check(vkAllocateDescriptorSets(device,&allocation,sets.data()),"allocate persistent quality sets");
        VkShaderModule vertex{},fragment{};
        try {
            vertex=shader("fullscreen.vert.spv"); fragment=shader("compose.frag.spv");
            for(unsigned i=0;i<SlotCount;++i) { passes[i]=make_pass(formats[i],false); pipelines[i]=make_pipeline(vertex,fragment,i); }
        } catch(...) { if(vertex)vkDestroyShaderModule(device,vertex,nullptr);if(fragment)vkDestroyShaderModule(device,fragment,nullptr);throw; }
        vkDestroyShaderModule(device,vertex,nullptr); vkDestroyShaderModule(device,fragment,nullptr);
        if(shadow_format!=VK_FORMAT_UNDEFINED)shadow_pass=make_pass(shadow_format,true);
        initialized=true;
    }
    void clear_targets() noexcept { for(auto& target:targets)destroy(target); extent={}; }
    void clear_shadows() noexcept {
        for(auto& frame:shadow_frames){if(frame)vkDestroyFramebuffer(device,frame,nullptr);frame={};}
        for(auto& view:shadow_layers){if(view)vkDestroyImageView(device,view,nullptr);view={};}
        destroy(shadow); shadow_resolution=0; shadow_readable=false;
        shadow_data={};stats.shadow_cascades=stats.shadow_size=0;
        if(shadows_buffer.mapped)std::memset(shadows_buffer.mapped,0,sizeof(VulkanShadowUniforms));
    }
    void destroy() noexcept {
        clear_targets(); clear_shadows();
        for(auto& pipeline:pipelines){if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);pipeline={};}
        for(auto& pass:passes){if(pass)vkDestroyRenderPass(device,pass,nullptr);pass={};}
        if(shadow_pass)vkDestroyRenderPass(device,shadow_pass,nullptr);
        if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
        if(pipeline_layout)vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(descriptor_layout)vkDestroyDescriptorSetLayout(device,descriptor_layout,nullptr);
        shadow_pass={};descriptor_pool={};pipeline_layout={};descriptor_layout={};
        destroy(globals_buffer);destroy(quality_buffer);destroy(matrices_buffer);destroy(shadows_buffer);
        if(linear)vkDestroySampler(device,linear,nullptr);if(nearest)vkDestroySampler(device,nearest,nullptr);
        linear={};nearest={};initialized=false;caps={};shadow_format=VK_FORMAT_UNDEFINED;
    }
    void resize(VkExtent2D size,const quality::ResolvedGraphics& graphics) {
        if(!size.width || !size.height)throw std::invalid_argument("Vulkan quality extent is empty");
        if(!initialized){try{initialize();}catch(...){destroy();throw;}}
        settings=graphics;
        if(size.width!=extent.width || size.height!=extent.height) {
            clear_targets();
            try {
                for(unsigned i=0;i<SlotCount;++i) {
                    VkExtent2D target_size=size;
                    if(i>=Bright && i<=BloomFiltered)target_size={std::max(1u,size.width/2),std::max(1u,size.height/2)};
                    targets[i]=make_target(target_size,formats[i],passes[i]);
                }
                extent=size;
            }catch(...){clear_targets();throw;}
        }
        // Capabilities retain the explicit resource probe; Off owns no CSM atlas.
        if(!settings.shadow_cascades)clear_shadows();
        if(shadow_pass&&settings.shadow_cascades) {
            VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(context.physical,&properties);
            const unsigned maximum=std::min(2048u,properties.limits.maxImageDimension2D);
            const unsigned resolution=std::min(settings.shadow_size,maximum);
            if(shadow.image && shadow_resolution!=resolution)clear_shadows();
            if(!shadow.image) {
            if(resolution>=256 && properties.limits.maxImageArrayLayers>=4) {
                try {
                    shadow=make_target({resolution,resolution},shadow_format,VK_NULL_HANDLE,4);
                    for(unsigned i=0;i<4;++i) {
                        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=shadow.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=shadow_format;view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,i,1};
                        check(vkCreateImageView(device,&view,nullptr,&shadow_layers[i]),"create cascade view");
                        VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};frame.renderPass=shadow_pass;frame.attachmentCount=1;frame.pAttachments=&shadow_layers[i];frame.width=resolution;frame.height=resolution;frame.layers=1;
                        check(vkCreateFramebuffer(device,&frame,nullptr,&shadow_frames[i]),"create cascade framebuffer");
                    }
                    shadow_resolution=resolution;
                }catch(...){clear_shadows();throw;}
            }
            }
        }
        caps.fxaa=caps.ssao=caps.bloom=caps.internal_hdr=true;
        if(shadow.image){caps.max_shadow_cascades=4;caps.max_shadow_size=std::max(caps.max_shadow_size,shadow_resolution);}
    }
    void update_set(unsigned slot,VkImageView scene,VkImageView depth,VkImageView normal,VkImageView ao,VkImageView bloom,VkImageLayout depth_layout) {
        const std::array<VkDescriptorImageInfo,7> images{{
            {linear,scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {linear,scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {linear,scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest,depth,depth_layout},
            {nearest,normal,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest,ao,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {linear,bloom,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        const std::array<unsigned,7> image_bindings{0,2,3,4,5,6,7};
        const std::array<VkDescriptorBufferInfo,3> buffers{{
            {globals_buffer.buffer,0,sizeof(VulkanGlobals)},
            {quality_buffer.buffer,0,sizeof(quality::QualityUniforms)},
            {matrices_buffer.buffer,0,sizeof(PostMatrices)}}};
        const std::array<unsigned,3> buffer_bindings{1,11,14};
        std::array<VkWriteDescriptorSet,10> writes{};
        for(unsigned i=0;i<7;++i) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=sets[slot];writes[i].dstBinding=image_bindings[i];writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i]; }
        for(unsigned i=0;i<3;++i) {auto& write=writes[i+7];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[slot];write.dstBinding=buffer_bindings[i];write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffers[i];}
        vkUpdateDescriptorSets(device,static_cast<unsigned>(writes.size()),writes.data(),0,nullptr);
    }
    void draw(VkCommandBuffer command,unsigned slot) {
        const Target& target=targets[slot];
        VkClearValue clear{};clear.color.float32[0]=clear.color.float32[1]=clear.color.float32[2]=1.f;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=passes[slot];begin.framebuffer=target.framebuffer;begin.renderArea.extent=target.extent;begin.clearValueCount=1;begin.pClearValues=&clear;
        vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0,0,static_cast<float>(target.extent.width),static_cast<float>(target.extent.height),0,1};
        VkRect2D scissor{{0,0},target.extent};vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[slot]);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,1,&sets[slot],0,nullptr);
        vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);++stats.post_passes;
    }
    QualityOutput post(VkCommandBuffer command,VkImageView input,VkImageView resolved_depth,VkImageView world_normal,const CameraState& camera,const VulkanGlobals& globals,bool input_hdr) {
        stats.post_passes=0;stats.fxaa=stats.ssao=stats.bloom=stats.internal_hdr=false;
        const bool active=settings.post_aa==quality::PostAA::Fxaa || settings.ssao || settings.bloom || settings.internal_hdr || settings.exposure_ev!=0.f;
        if(!active)return {VK_NULL_HANDLE,input,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0};
        if(!initialized || !extent.width)throw std::logic_error("Vulkan quality prepare must precede record_post");
        if(!input || (settings.ssao && (!resolved_depth || !world_normal)))throw std::invalid_argument("Vulkan quality missing resolved depth/normal input");
        auto effective=settings;effective.internal_hdr=effective.internal_hdr && input_hdr;
        VulkanGlobals post_globals=globals;
        post_globals.world_to_view=camera.view;
        std::memcpy(globals_buffer.mapped,&post_globals,sizeof(post_globals));
        const auto uniform=quality::quality_uniforms(effective,extent.width,extent.height);
        std::memcpy(quality_buffer.mapped,&uniform,sizeof(uniform));
        PostMatrices matrices{};
        if(settings.ssao) {
            if(!camera.projection_known)throw std::runtime_error("SSAO requires the actual propagated camera projection");
            matrices.projection=camera.projection;
            // raster.vert applies these same GL->Vulkan clip changes.
            for(unsigned c=0;c<4;++c){matrices.projection[c*4+1]=-matrices.projection[c*4+1];matrices.projection[c*4+2]=(matrices.projection[c*4+2]+matrices.projection[c*4+3])*.5f;}
            // inverse(Vulkan clip conversion) is Y=-Y, Z=2Z-W.
            matrices.inverse_projection=camera.inverse_projection;
            for(unsigned r=0;r<4;++r){
                matrices.inverse_projection[4+r]=-camera.inverse_projection[4+r];
                matrices.inverse_projection[8+r]=2.f*camera.inverse_projection[8+r];
                matrices.inverse_projection[12+r]=camera.inverse_projection[12+r]-camera.inverse_projection[8+r];
            }
        }
        std::memcpy(matrices_buffer.mapped,&matrices,sizeof(matrices));
        // Every descriptor names a real sampled input. Unused specialization
        // bindings use input rather than unread/uninitialized output attachments.
        auto render=[&](unsigned slot,VkImageView scene,VkImageView depth,VkImageView normal,VkImageView ao,VkImageView bloom,VkImageLayout depth_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){update_set(slot,scene,depth,normal,ao,bloom,depth_layout);draw(command,slot);};
        VkImageView ao=input,bloom=input;
        if(settings.ssao) {
            render(Normal,input,resolved_depth,world_normal,input,input,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            render(Ao,input,resolved_depth,targets[Normal].view,input,input,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            render(AoHorizontal,input,resolved_depth,targets[Normal].view,targets[Ao].view,input,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            render(AoFiltered,input,resolved_depth,targets[Normal].view,targets[AoHorizontal].view,input,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            ao=targets[AoFiltered].view;stats.ssao=true;
        }
        if(settings.bloom) {
            render(Bright,input,input,input,input,input);
            render(BloomHorizontal,targets[Bright].view,input,input,input,input);
            render(BloomFiltered,targets[BloomHorizontal].view,input,input,input,input);
            bloom=targets[BloomFiltered].view;stats.bloom=true;
        }
        render(Mapped,input,input,input,ao,bloom);stats.internal_hdr=effective.internal_hdr;
        unsigned final_slot=Mapped;
        if(settings.post_aa==quality::PostAA::Fxaa){render(Antialiased,targets[Mapped].view,input,input,input,input);final_slot=Antialiased;stats.fxaa=true;}
        return {targets[final_slot].image,targets[final_slot].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,stats.post_passes};
    }
    void record_shadow(VkCommandBuffer command,const FrameSnapshot& frame,const SourceLightingState& lighting,void* user,ShadowDraw callback) {
        shadow_data={};stats.shadow_cascades=stats.shadow_size=0;
        if(authored_source_shadows(frame.scene)){
            std::memcpy(shadows_buffer.mapped,&shadow_data,sizeof(shadow_data));
            return;
        }
        const SourceLight* directional=nullptr;
        // SUN identity is explicit producer metadata, never a directional-light
        // enumeration heuristic. HUD/unproved packets keep this selector zero.
        const unsigned sun=lighting.sun_slot_plus_one;
        if(settings.shadow_cascades && shadow.image && callback && lighting.lights_known &&
           sun!=0 && sun<=std::min(lighting.light_count,8u)){
            const auto& light=lighting.lights[sun-1];
            if(light.direction_type[3]==3.f && light.cone_enabled[2]>0.f)directional=&light;
        }
        if(settings.shadow_cascades && shadow.image && callback && directional && frame.camera.projection_known) {
            Matrix inverse_vp{};
            if(inverse(frame.camera.view_projection,inverse_vp)) {
                float padding=0;
                if(frame.scene.has_bounds){for(unsigned i=0;i<3;++i){const float span=frame.scene.bounds_max[i]-frame.scene.bounds_min[i];padding+=span*span;}padding=std::sqrt(padding);}
                const unsigned count=std::min(4u,settings.shadow_cascades);
                // D3DLIGHT9 stores rays in shader X,Z,Y axes. Neither source
                // vecSunDir polarity nor a fabricated sun enters this volume.
                const auto& rays=directional->direction_type;
                const std::array<float,3> sun_rays{rays[0],rays[2],rays[1]};
                const auto cascades=quality::build_shadow_cascades(inverse_vp,sun_rays,
                    frame.camera.clip_near,frame.camera.clip_far,-1,1,count,
                    shadow_resolution,quality::native_shadow_split_lambda,padding);
                if(cascades) {
                    shadow_data.matrices=cascades->matrices;shadow_data.splits=cascades->split_depths;
                    for(unsigned i=0;i<3;++i){
                        shadow_data.view_forward[i]=-frame.camera.view[i*4+2];
                        shadow_data.view_position[i]=frame.camera.view_position[i];
                        shadow_data.sun_direction[i]=sun_rays[i];
                    }
                    shadow_data.params={static_cast<float>(count),1.f/static_cast<float>(shadow_resolution),0,0};
                    for(unsigned i=0;i<count;++i){
                        shadow_data.constant_bias[i]=cascades->depth_bias[i][0];
                        shadow_data.slope_bias[i]=cascades->depth_bias[i][1];
                    }
                    stats.shadow_cascades=count;stats.shadow_size=shadow_resolution;
                }
            }
        }
        std::memcpy(shadows_buffer.mapped,&shadow_data,sizeof(shadow_data));
        if(!shadow.image)return;
        if(!shadow_readable) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.image=shadow.image;barrier.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,4};
            barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            shadow_readable=true;
        }
        // Off/Original records no shadow draws. Inactive array layers are never
        // sampled; b13 count=0 also disables missing-source-sun shadows.
        for(unsigned layer=0;layer<stats.shadow_cascades;++layer) {
            VkClearValue clear{};clear.depthStencil={1,0};
            VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=shadow_pass;begin.framebuffer=shadow_frames[layer];begin.renderArea.extent=shadow.extent;begin.clearValueCount=1;begin.pClearValues=&clear;
            vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
            VkViewport viewport{0,0,static_cast<float>(shadow_resolution),static_cast<float>(shadow_resolution),0,1};VkRect2D scissor{{0,0},shadow.extent};
            vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
            callback(user,command,layer,shadow_data.matrices[layer],shadow_pass,shadow.extent);
            vkCmdEndRenderPass(command);
        }
    }
};
QualityPasses::QualityPasses(const Context& context):impl_(std::make_unique<Impl>(context)){}
QualityPasses::~QualityPasses()=default;
void QualityPasses::prepare(VkExtent2D extent,const quality::ResolvedGraphics& settings){impl_->resize(extent,settings);}
void QualityPasses::record_shadows(VkCommandBuffer command,const FrameSnapshot& frame,const SourceLightingState& lighting,void* user,ShadowDraw draw){impl_->record_shadow(command,frame,lighting,user,draw);}
QualityOutput QualityPasses::record_post(VkCommandBuffer command,VkImageView color,VkImageView depth,VkImageView normal,const CameraState& camera,const VulkanGlobals& globals,bool hdr){return impl_->post(command,color,depth,normal,camera,globals,hdr);}
VkImageView QualityPasses::shadow_view()const noexcept{return impl_->shadow.view;}
VkSampler QualityPasses::shadow_sampler()const noexcept{return impl_->nearest;}
VkBuffer QualityPasses::shadow_uniform_buffer()const noexcept{return impl_->shadows_buffer.buffer;}
VkRenderPass QualityPasses::shadow_render_pass()const noexcept{return impl_->shadow_pass;}
const VulkanShadowUniforms& QualityPasses::shadow_uniforms()const noexcept{return impl_->shadow_data;}
quality::GraphicsCapabilities QualityPasses::capabilities()const noexcept{return impl_->caps;}
QualityDiagnostics QualityPasses::diagnostics()const noexcept{return impl_->stats;}
}
