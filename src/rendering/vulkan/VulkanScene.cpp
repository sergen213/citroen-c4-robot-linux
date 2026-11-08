#include "VulkanDispatch.hpp"
#include "VulkanScene.hpp"
#include "raytracing.hpp"
#include "VulkanRasterTarget.hpp"
#include "VulkanShaderData.hpp"
#include "SourceComposite.hpp"
#include "quality.hpp"
#include "VulkanPipelineCache.hpp"
#include "rendering/quality/ModernEnvironment.hpp"
#include "rendering/quality/QualityDiagnostics.hpp"
#include "rendering/SourceLighting.hpp"
#include "assets/GpuImage.hpp"
#include <compare>
#include <bit>
#include <map>
#include "game/ui/NativeUi.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace c4::rendering {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
}
// Used only at explicit loader/device/surface discovery and creation boundaries.
void check_availability(VkResult result, const char* operation) {
    switch (result) {
    case VK_ERROR_INCOMPATIBLE_DRIVER: case VK_ERROR_FEATURE_NOT_PRESENT:
    case VK_ERROR_EXTENSION_NOT_PRESENT: case VK_ERROR_FORMAT_NOT_SUPPORTED:
    case VK_ERROR_INITIALIZATION_FAILED: case VK_ERROR_DEVICE_LOST:
    case VK_ERROR_SURFACE_LOST_KHR: case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        throw VulkanUnavailableError(std::string(operation) + ": Vulkan result " + std::to_string(result));
    default:
        check(result, operation);
    }
}
std::filesystem::path shader_directory() {
    if (const char* base = SDL_GetBasePath()) {
        const auto installed = std::filesystem::path(base) / "../share/citroen-c4-robot/shaders/vulkan";
        if (std::filesystem::is_regular_file(installed / "raster.vert.spv")) return installed;
    }
    return C4_VULKAN_SHADER_BUILD_DIR;
}
std::string quoted(std::string_view s) {
    std::string out="\"";
    for(unsigned char c:s) { if(c=='\"'||c=='\\') { out+='\\'; out+=char(c); } else if(c<32) out+=' '; else out+=char(c); }
    return out+'\"';
}
}
struct VulkanScene::Impl {
    struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; void* mapped{}; };
    struct Image { VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{},flat_view{}; VkFormat format{};std::uint32_t width{},height{},levels{1},source_levels{1},faces{1};bool mip_blits_supported{}; };
    struct Mesh { Buffer vertices,indices,pose_staging,uv1,uv2,tangent_staging; std::uint32_t count{},vertex_count{}; const assets::Vertex* vertex_data{}; const std::uint32_t* index_data{}; bool owns_vertices{true},owns_uv1{true},owns_uv2{true}; std::uint64_t pose_revision{},tangent_revision{}; };
    struct MaterialBinding { VkDescriptorSet set{},shadow_set{}; VulkanMaterial value{}; std::array<VkDescriptorImageInfo,12> images{}; std::array<VkDescriptorBufferInfo,6> buffers{}; std::array<RasterState,5> raster{}; std::string source_material; bool initialized{},shadow_caster{}; };
    struct PipelineKey {
        bool shadow{},blend{},depth_test{},depth_write{}; BlendFactor source{},destination{};
        unsigned cull_mode{1};
        bool sky{};
        auto operator<=>(const PipelineKey&) const = default;
    };
    SDL_Window* window{};
    VulkanDispatch dispatch;
    std::uint32_t max_texture_dimension{};
    const RenderScene* scene{};
    VulkanBackendDiagnostics diag;
    std::string texture_fallback_reason,filtering_precision_reason;
    std::atomic<std::uint64_t> validation_errors{};
    VkInstance instance{}; VkDebugUtilsMessengerEXT messenger{}; VkSurfaceKHR surface{};
    VkPhysicalDevice physical{}; VkDevice device{}; VkQueue graphics{},present{};
    std::uint32_t graphics_family{},present_family{};
    VkCommandPool command_pool{}; VkCommandBuffer command{};
    VkFence fence{}; VkSemaphore acquired{};
    VkQueryPool timestamp_pool{};VkPipelineCache pipeline_cache{};std::uint32_t timestamp_bits{};float timestamp_period{};bool timing_pending{};
    struct PendingUpload { VkCommandBuffer command{}; VkFence fence{}; VkQueryPool timestamps{}; Buffer staging; std::array<Image,2> retired{}; };
    std::vector<PendingUpload> pending_uploads;
    PendingUpload recording_upload;
    VkCommandBuffer saved_frame_command{};
    VkDeviceSize pending_upload_bytes{};
    std::array<std::uint64_t,2048> gpu_samples{},upload_samples{};
    std::size_t gpu_sample_count{},upload_sample_count{};
    ::c4::rendering::pipeline_cache::Prepared cache_storage;
    bool frame_upload_pending{},frame_pending{};
    std::uint64_t history_epoch{};
    std::array<std::vector<std::uint32_t>,6> shader_words;
    static constexpr std::array<const char*,6> shader_names{"raster.vert.spv","raster.frag.spv","compose.frag.spv","rt.comp.spv","temporal.comp.spv","fullscreen.vert.spv"};
    VkSwapchainKHR swapchain{}; VkFormat swap_format{}; VkExtent2D extent{};
    std::vector<VkImage> swap_images; std::vector<VkImageView> swap_views;
    std::vector<VkFramebuffer> swap_framebuffers; std::vector<VkSemaphore> rendered;
    VkRenderPass compose_pass{};
    std::vector<Image> textures,filtering_images; Image neutral,neutral_cube,neutral_shadow;
    struct Environment { Image irradiance,prefiltered; VkSampler sampler{}; };
    std::map<std::uint32_t,Environment> environments;
    std::vector<RayTracingTexture> ray_textures;
    std::vector<Mesh> meshes; VkSampler sampler{};
    std::map<std::array<unsigned,7>,VkSampler> samplers;
    VkDescriptorSetLayout raster_set_layout{};
    VkDescriptorPool descriptor_pool{}; std::vector<MaterialBinding> material_bindings;
    VkPipelineLayout raster_layout{};
    std::map<PipelineKey,VkPipeline> pipelines;
    Buffer globals_buffer,sky_globals_buffer,material_buffer,quality_buffer,lighting_buffer,neutral_shadow_buffer,capture_buffer,zero_attribute;
    VkDeviceSize material_stride{},lighting_stride{}; VulkanGlobals globals{};
    SourceLightingState lighting{};
    std::unique_ptr<VulkanRasterTarget> target;
    std::unique_ptr<QualityPasses> quality_passes;
    std::unique_ptr<SourceComposite> composite;
    std::unique_ptr<RayTracing> rays;
    quality::GraphicSettings settings;
    quality::GraphicsCapabilities capabilities;
    quality::ResolvedGraphics resolved,reported_resolved,frame_quality;
    const FrameSnapshot* active_frame{};
    std::span<const Instance> submitted_instances;
    std::vector<std::uint32_t> draw_order; // Reused per frame: source entity list order.
    std::uint64_t submitted_omitted_sprites{};
    bool vsync{},recreate{true},can_capture{},rt_features{};
    bool capability_probe{},capabilities_probed{},preparing_settings{};
    bool sampler_anisotropy{};float device_max_anisotropy{1};
    bool mip_blits_available{},sampled_image_update_after_bind{};
    bool prepared_ssao{};
    bool bc_images_enabled{};
    bool source_mipmaps_supported{};
    bool maintenance4{};
    SourceLightingDeviceState lighting_device;
    std::array<VkFormatFeatureFlags,6> texture_features{};
    static constexpr std::array<VkFormat,6> texture_formats{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_A1R5G5B5_UNORM_PACK16,VK_FORMAT_BC1_RGBA_UNORM_BLOCK,VK_FORMAT_BC2_UNORM_BLOCK,VK_FORMAT_BC3_UNORM_BLOCK};
    unsigned hdr_sample_mask{1}; VkSampler neutral_shadow_sampler{};
    VkImageLayout neutral_shadow_layout{VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    std::filesystem::path shaders;
    ~Impl() { cleanup(); }
    static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user) {
        auto& self=*static_cast<Impl*>(user);
        if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++self.validation_errors;
        SDL_Log("Vulkan validation: %s", data->pMessage); return VK_FALSE;
    }
    void destroy(Buffer& b) noexcept { if(!device)return; if(b.mapped)vkUnmapMemory(device,b.memory); if(b.handle)vkDestroyBuffer(device,b.handle,nullptr); if(b.memory)vkFreeMemory(device,b.memory,nullptr); b={}; }
    void destroy(Image& image) noexcept { if(!device)return; if(image.flat_view&&image.flat_view!=image.view)vkDestroyImageView(device,image.flat_view,nullptr);if(image.view)vkDestroyImageView(device,image.view,nullptr); if(image.handle)vkDestroyImage(device,image.handle,nullptr); if(image.memory)vkFreeMemory(device,image.memory,nullptr); image={}; }
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(physical,&properties);
        for(std::uint32_t i=0;i<properties.memoryTypeCount;++i) if((bits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw VulkanUnavailableError("Required Vulkan memory type unavailable");
    }
    Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags) {
        Buffer b; b.size=std::max<VkDeviceSize>(size,4);
        try {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=b.size; info.usage=usage; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(device,&info,nullptr,&b.handle),"create buffer");
            VkMemoryRequirements required; vkGetBufferMemoryRequirements(device,b.handle,&required);
            VkMemoryAllocateFlagsInfo address{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; address.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=required.size; allocation.memoryTypeIndex=memory_type(required.memoryTypeBits,flags);
            if(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) allocation.pNext=&address;
            check(vkAllocateMemory(device,&allocation,nullptr,&b.memory),"allocate buffer"); check(vkBindBufferMemory(device,b.handle,b.memory,0),"bind buffer");
            if(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) check(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped),"map buffer");
        } catch(...) { destroy(b); throw; } return b;
    }
    Image image(std::uint32_t width,std::uint32_t height,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,std::uint32_t levels=1,std::uint32_t faces=1) {
        Image result; result.format=format;result.width=width;result.height=height;result.levels=result.source_levels=levels;result.faces=faces;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.extent={width,height,1}; info.mipLevels=levels; info.arrayLayers=faces; info.format=format; info.tiling=VK_IMAGE_TILING_OPTIMAL; info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; info.usage=usage; info.samples=VK_SAMPLE_COUNT_1_BIT; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE; if(faces==6)info.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            check(vkCreateImage(device,&info,nullptr,&result.handle),"create image");
            VkMemoryRequirements required; vkGetImageMemoryRequirements(device,result.handle,&required);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=required.size; allocation.memoryTypeIndex=memory_type(required.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device,&allocation,nullptr,&result.memory),"allocate image"); check(vkBindImageMemory(device,result.handle,result.memory,0),"bind image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=result.handle; view.viewType=faces==6?VK_IMAGE_VIEW_TYPE_CUBE:faces>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D; view.format=format; view.subresourceRange={aspect,0,levels,0,faces};
            check(vkCreateImageView(device,&view,nullptr,&result.view),"create image view");
            result.flat_view=result.view;
            if(faces==6){view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange.layerCount=1;check(vkCreateImageView(device,&view,nullptr,&result.flat_view),"create cube first-face view");}
        } catch(...) { destroy(result); throw; } return result;
    }
    void release_upload(PendingUpload& upload) noexcept {
        destroy(upload.staging);for(auto& image:upload.retired)destroy(image);
        if(upload.timestamps)vkDestroyQueryPool(device,upload.timestamps,nullptr);
        if(upload.fence)vkDestroyFence(device,upload.fence,nullptr);
        if(upload.command)vkFreeCommandBuffers(device,command_pool,1,&upload.command);
        upload={};
    }
    void retire_uploads(bool drain=false) {
        while(!pending_uploads.empty()){
            auto& upload=pending_uploads.front();
            if(drain)check(vkWaitForFences(device,1,&upload.fence,VK_TRUE,UINT64_MAX),"drain retained upload");
            else {const auto status=vkGetFenceStatus(device,upload.fence);if(status==VK_NOT_READY)break;check(status,"query retained upload fence");}
            if(upload.timestamps){
                std::array<std::uint64_t,2> values{};
                check(vkGetQueryPoolResults(device,upload.timestamps,0,2,sizeof(values),values.data(),sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT),"read upload GPU timestamps");
                const auto mask=timestamp_bits==64?UINT64_MAX:((std::uint64_t(1)<<timestamp_bits)-1);
                const auto ns=std::uint64_t(double((values[1]-values[0])&mask)*timestamp_period);
                upload_samples[upload_sample_count++%upload_samples.size()]=ns;diag.upload_gpu_total_ns+=ns;++diag.upload_gpu_samples;
            }
            pending_upload_bytes-=upload.staging.size;release_upload(upload);pending_uploads.erase(pending_uploads.begin());++diag.upload_completed;
        }
    }
    void drain_uploads() {if(!pending_uploads.empty()){++diag.upload_readiness_waits;retire_uploads(true);}}
    void abort_upload() noexcept {
        if(!saved_frame_command)return;
        command=saved_frame_command;saved_frame_command={};release_upload(recording_upload);
    }
    void begin_upload(VkDeviceSize bytes=0) {
        retire_uploads();
        constexpr VkDeviceSize byte_bound=64u*1024u*1024u;
        while(!pending_uploads.empty()&&(pending_uploads.size()>=16||bytes>byte_bound-std::min(byte_bound,pending_upload_bytes))){
            ++diag.upload_backpressure_waits;check(vkWaitForFences(device,1,&pending_uploads.front().fence,VK_TRUE,UINT64_MAX),"wait oldest upload under pressure");retire_uploads();
        }
        pending_uploads.reserve(16);
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=command_pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        try{
            check(vkAllocateCommandBuffers(device,&allocation,&recording_upload.command),"allocate retained upload command");
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fence_info,nullptr,&recording_upload.fence),"create retained upload fence");
            saved_frame_command=command;command=recording_upload.command;
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&begin),"begin upload");
            if(timestamp_bits){VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};queries.queryType=VK_QUERY_TYPE_TIMESTAMP;queries.queryCount=2;check(vkCreateQueryPool(device,&queries,nullptr,&recording_upload.timestamps),"create upload timestamp pool");vkCmdResetQueryPool(command,recording_upload.timestamps,0,2);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,recording_upload.timestamps,0);}
        }catch(...){if(saved_frame_command)abort_upload();else release_upload(recording_upload);throw;}
    }
    void finish_upload(Buffer* staging=nullptr) {
        try{
            if(recording_upload.timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,recording_upload.timestamps,1);
            check(vkEndCommandBuffer(command),"end upload");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
            check(vkQueueSubmit(graphics,1,&submit,recording_upload.fence),"submit retained upload");
        }catch(...){abort_upload();throw;}
        if(staging){recording_upload.staging=*staging;*staging={};}
        pending_upload_bytes+=recording_upload.staging.size;diag.upload_bytes+=recording_upload.staging.size;++diag.upload_submissions;
        pending_uploads.push_back(recording_upload);recording_upload={};command=saved_frame_command;saved_frame_command={};
        diag.upload_peak_pending=std::max<std::uint64_t>(diag.upload_peak_pending,pending_uploads.size());diag.upload_peak_bytes=std::max<std::uint64_t>(diag.upload_peak_bytes,pending_upload_bytes);
    }
    void retire(Image& image) {
        if(!image.handle)return;
        if(!pending_uploads.empty())for(auto& retired:pending_uploads.back().retired)if(!retired.handle){retired=image;image={};return;}
        drain_uploads();destroy(image);
    }
    void transition(VkImage handle,VkImageLayout before,VkImageLayout after,VkPipelineStageFlags source,VkPipelineStageFlags target,VkAccessFlags source_access,VkAccessFlags target_access) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout=before; barrier.newLayout=after; barrier.srcAccessMask=source_access; barrier.dstAccessMask=target_access; barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.image=handle; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,source,target,0,0,nullptr,0,nullptr,1,&barrier);
    }
    Buffer upload(const void* data,VkDeviceSize size,VkBufferUsageFlags usage) {
        Buffer staging=buffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),result;
        try {
            if(size)std::memcpy(staging.mapped,data,std::size_t(size)); result=buffer(size,usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            begin_upload(staging.size); VkBufferCopy copy{0,0,std::max<VkDeviceSize>(size,4)}; vkCmdCopyBuffer(command,staging.handle,result.handle,1,&copy);
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_SHADER_READ_BIT; if(rt_features)barrier.dstAccessMask|=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.buffer=result.handle; barrier.size=VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&barrier,0,nullptr); finish_upload(&staging);
        } catch(...) { abort_upload();destroy(staging); destroy(result); throw; } return result;
    }
    Image upload_texture(const assets::Image& source) {
        const auto base_bytes=std::uint64_t(source.width)*source.height*4*source.face_count;
        if(!source.width||!source.height||(source.face_count!=1&&source.face_count!=6)||source.rgba.size()!=base_bytes||(source.face_count==6&&source.width!=source.height))throw std::runtime_error("Invalid shared scene RGBA image");
        Image result=image(source.width,source.height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,std::uint32_t(source.mip_levels.size()+1),source.face_count);
        constexpr auto mip_features=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        result.mip_blits_supported=(texture_features[0]&mip_features)==mip_features;
        Buffer staging;
        try {
            staging=buffer(source.rgba.size()+source.mip_rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            std::memcpy(staging.mapped,source.rgba.data(),source.rgba.size());
            if(!source.mip_rgba.empty())std::memcpy(static_cast<std::byte*>(staging.mapped)+source.rgba.size(),source.mip_rgba.data(),source.mip_rgba.size());
            std::vector<VkBufferImageCopy> copies(result.levels);
            copies[0].imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,source.face_count};copies[0].imageExtent={source.width,source.height,1};
            for(std::size_t level=0;level<source.mip_levels.size();++level){
                const auto& mip=source.mip_levels[level];const auto bytes=std::uint64_t(mip.width)*mip.height*4*source.face_count;
                if(mip.width!=std::max(1u,source.width>>unsigned(level+1))||mip.height!=std::max(1u,source.height>>unsigned(level+1))||mip.offset>source.mip_rgba.size()||bytes>source.mip_rgba.size()-mip.offset)throw std::runtime_error("Invalid stored source image mip");
                auto& copy=copies[level+1];copy.bufferOffset=source.rgba.size()+mip.offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,std::uint32_t(level+1),0,source.face_count};copy.imageExtent={mip.width,mip.height,1};
            }
            begin_upload(staging.size);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=result.handle;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,result.levels,0,result.faces};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            vkCmdCopyBufferToImage(command,staging.handle,result.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,std::uint32_t(copies.size()),copies.data());
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);finish_upload(&staging);
        }catch(...){abort_upload();destroy(staging);destroy(result);throw;}return result;
    }
    void initialize(SDL_Window* win,bool synchronize,std::uint32_t effects,bool probe);
    void prepare_source(const RenderScene&);
    void create_rays(std::span<const RayTracingMesh> meshes={});
    void create_device(); void create_descriptors(); void create_swapchain(); void destroy_swapchain() noexcept;
    void create_passes(); void prepare_materials(const FrameSnapshot&); void ensure_materials(std::size_t);
    Image upload_gpu_texture(const assets::GpuImage&);
    Image generate_mips(const Image&,bool count_generation);
    void prepare_filtering();
    void prepare_environments() {
        if(!scene||(!resolved.pbr&&(diag.rt_requested&5u)==0))return;
        for(std::uint32_t i=0;i<textures.size();++i){
            if(!textures[i].handle||textures[i].faces!=6||environments.contains(i))continue;
            const auto prepared=quality::prepare_modern_environment(scene->gpu_textures[i]);
            Environment environment;
            try{
                environment.irradiance=upload_texture(prepared.irradiance);environment.prefiltered=upload_texture(prepared.prefiltered);
                SamplerState sampling;sampling.u=sampling.v=sampling.w=AddressMode::Clamp;sampling.linear=sampling.filter_explicit=true;
                environment.sampler=material_sampler(sampling,environment.prefiltered.levels,false);
                environments.emplace(i,environment);
            }catch(...){retire(environment.irradiance);retire(environment.prefiltered);throw;}
        }
    }
    void ensure_composite();
    static RasterState effective_raster(const Instance& entity,const MaterialPass& pass) noexcept {
        auto raster=pass.raster;
        if(entity.original_alpha)raster=apply_original_model_alpha(raster,*entity.original_alpha);
        if(entity.source_sprite_no_depth_write&&(raster.effect_override_mask&EffectRasterOverride::DepthWrite)==0)raster.depth_write=false;
        return raster;
    }
    VkSampler material_sampler(const SamplerState&,std::uint32_t,bool allow_quality=true);
    void update_globals(const FrameSnapshot&); void refresh_capabilities(); void record_geometry(VkCommandBuffer,bool,unsigned=0);
    void record_sky(VkCommandBuffer);
    quality::ResolvedGraphics resolve_quality() const {
        auto requested=settings;
        if(diag.rt_requested&&rays&&rays->available_effects()&&!requested.denoising&&requested.preset!=quality::Preset::Ultra)requested.denoising=quality::Denoising::Standard;
        return quality::resolve_graphics(requested,capabilities);
    }
    bool supplemental_shadows_allowed() const noexcept { return (!scene||!authored_source_shadows(*scene))&&(diag.rt_enabled&2u)==0; }
    VkDescriptorImageInfo shadow_image_info() const noexcept { const bool actual=supplemental_shadows_allowed()&&quality_passes&&quality_passes->shadow_view();return {actual?quality_passes->shadow_sampler():neutral_shadow_sampler,actual?quality_passes->shadow_view():neutral_shadow.view,actual?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:neutral_shadow_layout}; }
    VkDescriptorBufferInfo shadow_buffer_info() const noexcept { return {supplemental_shadows_allowed()&&quality_passes&&quality_passes->shadow_uniform_buffer()?quality_passes->shadow_uniform_buffer():neutral_shadow_buffer.handle,0,sizeof(VulkanShadowUniforms)}; }
    static void shadow_draw(void*,VkCommandBuffer,unsigned,const std::array<float,16>&,VkRenderPass,VkExtent2D);
    static SourceComposite::Output postprocess(void*,const FrameSnapshot&,VkCommandBuffer,VkImageView,VkImageView,VkImageView);
    VkShaderModule shader(const char* name);
    VkPipeline pipeline(const PipelineKey&);
    void cleanup() noexcept;
    bool draw(const FrameSnapshot& frame,const std::filesystem::path& capture);
    void save_capture(const std::filesystem::path& path);
    void completed();
    void save_pipeline_cache() noexcept {
        if(!pipeline_cache||cache_storage.path.empty())return;
        const auto start=std::chrono::steady_clock::now();
        try{
            diag.pipeline_cache_saved_bytes=0;diag.pipeline_cache_error.clear();
            std::size_t bytes=0;check(vkGetPipelineCacheData(device,pipeline_cache,&bytes,nullptr),"query pipeline cache bytes");
            if(bytes > ::c4::rendering::pipeline_cache::max_bytes-::c4::rendering::pipeline_cache::detail::envelope_size){
                diag.pipeline_cache_save_status="too_large";diag.pipeline_cache_error="Queried Vulkan pipeline cache exceeds the on-disk size limit";
            }else{
                std::vector<std::uint8_t> data(bytes);
                if(bytes){check(vkGetPipelineCacheData(device,pipeline_cache,&bytes,data.data()),"extract pipeline cache");if(bytes>data.size())throw std::runtime_error("Vulkan pipeline cache reported more bytes than the supplied buffer");data.resize(bytes);}
                const auto result=::c4::rendering::pipeline_cache::save(cache_storage,data);diag.pipeline_cache_save_status=::c4::rendering::pipeline_cache::status_name(result.status);
                if(result.error)diag.pipeline_cache_error=result.error.message();
                else if(!result.reason.empty())diag.pipeline_cache_error=std::string(result.reason)+" (reported bytes="+std::to_string(bytes)+")";
                if(result.status==::c4::rendering::pipeline_cache::Status::saved)diag.pipeline_cache_saved_bytes=bytes;
            }
        }catch(const std::exception& error){diag.pipeline_cache_save_status="io_error";diag.pipeline_cache_error=error.what();}
        diag.pipeline_cache_save_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
        if(diag.pipeline_cache_save_status!="saved")SDL_Log("Vulkan cache save %s: %s",diag.pipeline_cache_save_status.c_str(),diag.pipeline_cache_error.c_str());
    }
    void summarize_timings() {
        auto summarize=[](const auto& values,std::size_t total,std::uint64_t& median,std::uint64_t& p95){
            auto sorted=values;const auto count=std::min(total,values.size());if(!count)return;
            std::sort(sorted.begin(),sorted.begin()+count);median=sorted[(count-1)/2];p95=sorted[(count*95+99)/100-1];
        };
        summarize(gpu_samples,gpu_sample_count,diag.gpu_median_ns,diag.gpu_p95_ns);
        summarize(upload_samples,upload_sample_count,diag.upload_gpu_median_ns,diag.upload_gpu_p95_ns);
        summarize(dispatch.pipeline_creation_ns,dispatch.pipeline_creation_calls,diag.pipeline_creation_median_ns,diag.pipeline_creation_p95_ns);
        diag.gpu_distribution_samples=std::min(gpu_sample_count,gpu_samples.size());
    }
};

void VulkanScene::Impl::initialize(SDL_Window* win,bool synchronize,std::uint32_t effects,bool probe) {
    VulkanDispatchScope scope(dispatch);
    window=win;vsync=synchronize;diag.rt_requested=effects&7u;capability_probe=probe;shaders=shader_directory();
    load_vulkan_global_functions(dispatch);
    std::uint32_t extension_count=0; const char* const* required=SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if(!required)throw VulkanUnavailableError(SDL_GetError());
    std::vector<const char*> extensions(required,required+extension_count),layers;
    if(const char* enabled=SDL_getenv("C4_VULKAN_VALIDATION"); enabled&&std::string_view(enabled)!="0") {
        std::uint32_t count=0; check(vkEnumerateInstanceLayerProperties(&count,nullptr),"enumerate validation layers"); std::vector<VkLayerProperties> available(count); check(vkEnumerateInstanceLayerProperties(&count,available.data()),"read validation layers");
        if(std::none_of(available.begin(),available.end(),[](const auto& layer){return std::strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation")==0;}))throw std::runtime_error("C4_VULKAN_VALIDATION requested but VK_LAYER_KHRONOS_validation unavailable");
        layers.push_back("VK_LAYER_KHRONOS_validation"); extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); diag.validation_enabled=true;
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.pApplicationName="Citroen C4 Robot Native"; application.apiVersion=VK_API_VERSION_1_3;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT}; debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; debug.pfnUserCallback=validation; debug.pUserData=this;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&application; info.enabledExtensionCount=std::uint32_t(extensions.size()); info.ppEnabledExtensionNames=extensions.data(); info.enabledLayerCount=std::uint32_t(layers.size()); info.ppEnabledLayerNames=layers.data(); if(diag.validation_enabled)info.pNext=&debug;
    const auto instance_result=vkCreateInstance(&info,nullptr,&instance);
    if(diag.validation_enabled)check(instance_result,"create Vulkan instance");
    else check_availability(instance_result,"create Vulkan instance");
    load_vulkan_instance_functions(dispatch,instance);
    if(diag.validation_enabled) { if(!vkCreateDebugUtilsMessengerEXT||!vkDestroyDebugUtilsMessengerEXT)throw std::runtime_error("Debug utils entrypoints missing");check(vkCreateDebugUtilsMessengerEXT(instance,&debug,nullptr,&messenger),"create validation messenger"); }
    if(!SDL_Vulkan_CreateSurface(window,instance,nullptr,&surface))throw VulkanUnavailableError(SDL_GetError());
    create_device();
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex=graphics_family; check(vkCreateCommandPool(device,&pool,nullptr,&command_pool),"create command pool");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; commands.commandPool=command_pool; commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; commands.commandBufferCount=1; check(vkAllocateCommandBuffers(device,&commands,&command),"allocate frame command");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence_info.flags=VK_FENCE_CREATE_SIGNALED_BIT; check(vkCreateFence(device,&fence_info,nullptr,&fence),"create frame fence");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; check(vkCreateSemaphore(device,&semaphore,nullptr,&acquired),"create acquisition semaphore");
    VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampling.magFilter=sampling.minFilter=VK_FILTER_LINEAR; sampling.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST; sampling.addressModeU=sampling.addressModeV=sampling.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT; sampling.maxLod=0; check(vkCreateSampler(device,&sampling,nullptr,&sampler),"create texture sampler");
    const std::array<float,4> zero{0,0,0,1};zero_attribute=upload(zero.data(),sizeof(zero),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    neutral=upload_texture(assets::Image{1,1,{255,255,255,255}});
    neutral_cube=upload_texture(assets::Image{1,1,std::vector<std::uint8_t>(24,255),6});
    globals_buffer=buffer(sizeof(VulkanGlobals),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    quality_buffer=buffer(sizeof(quality::QualityUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    neutral_shadow_buffer=buffer(sizeof(VulkanShadowUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);std::memset(neutral_shadow_buffer.mapped,0,sizeof(VulkanShadowUniforms));
    VkFormat neutral_depth_format=VK_FORMAT_UNDEFINED;
    for(const auto format:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM}){VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physical,format,&properties);if((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))==(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)){neutral_depth_format=format;break;}}
    const bool depth_neutral=neutral_depth_format!=VK_FORMAT_UNDEFINED;
    neutral_shadow_layout=depth_neutral?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    neutral_shadow=image(1,1,depth_neutral?neutral_depth_format:VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|(depth_neutral?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:0),depth_neutral?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,1,4);
    SamplerState shadow_sampling;shadow_sampling.u=shadow_sampling.v=shadow_sampling.w=AddressMode::Clamp;shadow_sampling.filter_explicit=true;neutral_shadow_sampler=material_sampler(shadow_sampling,1,false);
    begin_upload();
    VkImageMemoryBarrier neutral_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};neutral_barrier.srcQueueFamilyIndex=neutral_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;neutral_barrier.image=neutral_shadow.handle;neutral_barrier.subresourceRange={depth_neutral?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,4};neutral_barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;neutral_barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;neutral_barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&neutral_barrier);
    if(depth_neutral){const VkClearDepthStencilValue far_depth{1,0};vkCmdClearDepthStencilImage(command,neutral_shadow.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&far_depth,1,&neutral_barrier.subresourceRange);}
    else{const VkClearColorValue white{{1,1,1,1}};vkCmdClearColorImage(command,neutral_shadow.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&white,1,&neutral_barrier.subresourceRange);}
    neutral_barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;neutral_barrier.newLayout=neutral_shadow_layout;neutral_barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;neutral_barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&neutral_barrier);finish_upload();
    target=std::make_unique<VulkanRasterTarget>(VulkanRasterTarget::Context{physical,device});
    quality_passes=std::make_unique<QualityPasses>(QualityPasses::Context{physical,device,shaders,pipeline_cache});
    create_descriptors();
    create_swapchain();
}

void VulkanScene::Impl::prepare_source(const RenderScene& source) {
    VulkanDispatchScope scope(dispatch);
    if(scene)throw std::runtime_error("Vulkan scene cache is already prepared");
    check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"wait source preparation fence");completed();
    scene=&source;
    if(source.sky)sky_globals_buffer=buffer(sizeof(VulkanGlobals),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    meshes.resize(source.meshes.size());
    const VkBufferUsageFlags rt_usage=rt_features?(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR):0;
    std::vector<RayTracingMesh> ray_meshes; ray_meshes.reserve(meshes.size());
    for(std::size_t i=0;i<meshes.size();++i) {
        const auto& view=source.meshes[i]; auto& mesh=meshes[i];
        const bool sky_mesh=source.sky&&std::any_of(source.sky->faces.begin(),source.sky->faces.end(),[i](const auto& face){return face.mesh_index==i;});
        const auto mesh_rt_usage=sky_mesh?VkBufferUsageFlags{}:rt_usage;
        if(view.indices.size()>std::numeric_limits<std::uint32_t>::max()||view.vertices.size()>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Scene mesh exceeds Vulkan index limits");
        if(view.indices.size()%3!=0)throw std::runtime_error("Source mesh topology is not a triangle list");
        for(const auto index:view.indices)if(index>=view.vertices.size())throw std::runtime_error("Source mesh index exceeds vertex allocation");
        mesh.vertex_count=std::uint32_t(view.vertices.size());mesh.vertex_data=view.vertices.data();mesh.index_data=view.indices.data();
        if(view.tangent_binding){
            if(*view.tangent_binding>=source.tangent_bindings.size())throw std::runtime_error("Source tangent binding index out of range");
            const auto& producer=source.tangent_bindings[*view.tangent_binding];
            if(producer.vertices.data()!=view.vertices.data()||producer.vertices.size()!=view.vertices.size())throw std::runtime_error("Source tangent binding changed full-group vertex allocation");
        }
        for(std::size_t previous=0;previous<i;++previous) if(source.meshes[previous].vertices.data()==view.vertices.data()&&source.meshes[previous].vertices.size()==view.vertices.size()){mesh.vertices=meshes[previous].vertices;mesh.owns_vertices=false;break;}
        if(mesh.owns_vertices)mesh.vertices=upload(view.vertices.data(),view.vertices.size_bytes(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|mesh_rt_usage);
        mesh.indices=upload(view.indices.data(),view.indices.size_bytes(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT|mesh_rt_usage); mesh.count=std::uint32_t(view.indices.size());
        mesh.pose_revision=view.pose_revision;
        const bool mutable_vertices = mesh.owns_vertices&&(
            std::any_of(source.posed_vertices.begin(),source.posed_vertices.end(),[&](const auto& posed){return posed.data()==view.vertices.data()&&posed.size()==view.vertices.size();}) ||
            std::any_of(source.sprite_bindings.begin(),source.sprite_bindings.end(),[&](const auto& binding){
                const auto& sprite=source.meshes.at(binding.mesh_index);
                return sprite.vertices.data()==view.vertices.data()&&sprite.vertices.size()==view.vertices.size();
            }));
        if(mutable_vertices)mesh.pose_staging=buffer(view.vertices.size_bytes(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if((!view.secondary_uv.empty()&&view.secondary_uv.size()!=view.vertices.size())||(!view.tertiary_uv.empty()&&view.tertiary_uv.size()!=view.vertices.size()))throw std::runtime_error("Source auxiliary UV vertex count mismatch");
        if(!view.secondary_uv.empty()) {
            for(std::size_t previous=0;previous<i;++previous)if(source.meshes[previous].secondary_uv.data()==view.secondary_uv.data()&&source.meshes[previous].secondary_uv.size()==view.secondary_uv.size()){mesh.uv1=meshes[previous].uv1;mesh.owns_uv1=false;break;}
            if(mesh.owns_uv1)mesh.uv1=upload(view.secondary_uv.data(),view.secondary_uv.size_bytes(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|mesh_rt_usage);
        }
        if(!view.tertiary_uv.empty()||view.tangent_binding) {
            for(std::size_t previous=0;previous<i;++previous){
                const auto& prior=source.meshes[previous];
                const bool shared_channel=view.tangent_binding
                    ? prior.tangent_binding==view.tangent_binding&&prior.vertices.data()==view.vertices.data()&&prior.vertices.size()==view.vertices.size()
                    : !prior.tangent_binding&&prior.tertiary_uv.data()==view.tertiary_uv.data()&&prior.tertiary_uv.size()==view.tertiary_uv.size();
                if(shared_channel){mesh.uv2=meshes[previous].uv2;mesh.owns_uv2=false;break;}
            }
            if(mesh.owns_uv2){
                const auto tangent_bytes=VkDeviceSize(view.vertices.size())*16;
                mesh.uv2=view.tertiary_uv.empty()
                    ? buffer(tangent_bytes,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|mesh_rt_usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    : upload(view.tertiary_uv.data(),view.tertiary_uv.size_bytes(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|mesh_rt_usage);
            }
        }
        mesh.tangent_revision=view.pose_revision;
        if(mesh.owns_uv2&&mesh.uv2.handle)mesh.tangent_staging=buffer(VkDeviceSize(view.vertices.size())*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Preserve scene-indexed RT slots, but never give the sky a BLAS geometry.
        if(sky_mesh){ray_meshes.emplace_back();continue;}
        VkDeviceAddress vertex_address=0,index_address=0;
        if(rt_features){VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=mesh.vertices.handle;vertex_address=vkGetBufferDeviceAddress(device,&address);address.buffer=mesh.indices.handle;index_address=vkGetBufferDeviceAddress(device,&address);}
        RayTracingMesh ray_mesh{mesh.vertices.handle,mesh.indices.handle,vertex_address,index_address,std::uint32_t(view.vertices.size()),mesh.count,sizeof(assets::Vertex),offsetof(assets::Vertex,x),offsetof(assets::Vertex,u),offsetof(assets::Vertex,nx)};
        ray_mesh.tangent_buffer=mesh.uv2.handle;ray_mesh.tangent_stride=16;ray_mesh.uv1_buffer=mesh.uv1.handle;ray_mesh.uv1_stride=8;
        if(rt_features){VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};if(mesh.uv2.handle){address.buffer=mesh.uv2.handle;ray_mesh.tangent_address=vkGetBufferDeviceAddress(device,&address);}if(mesh.uv1.handle){address.buffer=mesh.uv1.handle;ray_mesh.uv1_address=vkGetBufferDeviceAddress(device,&address);}}
        ray_meshes.push_back(ray_mesh);
    }
    textures.resize(source.textures.size());
    if(source.gpu_textures.size()!=textures.size())throw std::runtime_error("Source native texture cache slots do not match scene texture indices");
    filtering_images.resize(textures.size());
    std::vector<bool> referenced(textures.size());
    const auto reference=[&](std::optional<std::uint32_t> index){if(index){if(*index>=referenced.size())throw std::runtime_error("Source material image index out of range");referenced[*index]=true;}};
    for(const auto& entity:source.instances){reference(entity.texture_index);for(const auto index:entity.material_state.entity_skins)reference(index);for(const auto index:entity.material_state.material_skins)reference(index);}
    if(source.sky)for(const auto& face:source.sky->faces){if(face.mesh_index>=meshes.size())throw std::runtime_error("Source sky mesh index out of range");reference(face.texture_index);}
    for(const auto index:source.referenced_texture_indices)reference(index);
    for(std::size_t i=0;i<textures.size();++i)if(referenced[i])textures[i]=upload_gpu_texture(source.gpu_textures[i]);
    ray_textures.reserve(textures.size());
    for(const auto& texture:textures)ray_textures.push_back({texture.flat_view?texture.flat_view:neutral.view,sampler});
    ensure_materials(source.instances.size()+(source.sky?source.sky->faces.size():0));
    if(rays)rays.reset();
    create_rays(ray_meshes);
    recreate=true;preparing_settings=true;
    try{create_swapchain();preparing_settings=false;if(recreate)throw std::runtime_error("Cannot prepare Vulkan source while the drawable is minimized");}
    catch(...){preparing_settings=false;throw;}
}

void VulkanScene::Impl::create_rays(std::span<const RayTracingMesh> meshes) {
    if(rays||(!diag.rt_requested&&!capability_probe))return;
    RayTracingContext context{physical,device,graphics,graphics_family,command_pool,rt_features,rt_features,rt_features,rt_features,rt_features};
    context.sampled_image_update_after_bind=sampled_image_update_after_bind;
    context.initialize_for_capabilities=capability_probe;
    context.pipeline_cache=pipeline_cache;
    try{
        if(scene)rays=std::make_unique<RayTracing>(context,*scene,meshes,ray_textures,shaders,diag.rt_requested);
        else{static const RenderScene empty_capability_metadata;rays=std::make_unique<RayTracing>(context,empty_capability_metadata,meshes,ray_textures,shaders,diag.rt_requested);}
        diag.rt_enabled=rays->enabled_effects();diag.rt_supported_effects=rays->available_effects();diag.rt_fallback_reason=rays->fallback_reason();diag.rt_triangles=rays->triangles();diag.rt_instances=rays->instances();
    }catch(const std::exception& error){
        if(preparing_settings)throw;
        check(vkDeviceWaitIdle(device),"wait failed ray-query setup");rays.reset();diag.rt_enabled=diag.rt_supported_effects=0;diag.rt_fallback_reason=std::string("Hardware ray-query setup failed; Original raster retained: ")+error.what();
    }
}


void VulkanScene::Impl::create_device() {
    std::uint32_t count=0; check_availability(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate devices");
    std::vector<VkPhysicalDevice> devices(count); check_availability(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"read devices");
    int best=-1;
    VkBool32 shader_demote_to_helper_invocation=VK_FALSE;
    for(auto candidate:devices) {
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(candidate,&properties);
        if(properties.apiVersion<VK_API_VERSION_1_3)continue;
        std::uint32_t ext_count=0; check_availability(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&ext_count,nullptr),"enumerate device extensions");
        std::vector<VkExtensionProperties> exts(ext_count); check_availability(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&ext_count,exts.data()),"read device extensions");
        const auto has=[&](const char* name){return std::any_of(exts.begin(),exts.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});};
        if(!has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))continue;
        std::uint32_t families=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
        std::uint32_t gf=UINT32_MAX,pf=UINT32_MAX;
        for(std::uint32_t i=0;i<families;++i) {
            VkBool32 supported=VK_FALSE; check_availability(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&supported),"query presentation support");
            if(queues[i].queueCount&&(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&(queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT)) { if(gf==UINT32_MAX)gf=i; if(supported){gf=pf=i;break;} }
            if(queues[i].queueCount&&supported)pf=i;
        }
        if(gf==UINT32_MAX||pf==UINT32_MAX)continue;
        VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR}; acceleration.pNext=&ray;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};dynamic.pNext=&acceleration;
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; v12.pNext=&dynamic;
        VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};v13.pNext=&v12;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&v13;vkGetPhysicalDeviceFeatures2(candidate,&features);
        if(!features.features.independentBlend||!dynamic.extendedDynamicState||!v13.shaderDemoteToHelperInvocation)continue;
        const bool rt=has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)&&has(VK_KHR_RAY_QUERY_EXTENSION_NAME)&&has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)&&v13.maintenance4&&v12.bufferDeviceAddress&&v12.runtimeDescriptorArray&&v12.shaderSampledImageArrayNonUniformIndexing&&acceleration.accelerationStructure&&ray.rayQuery;
        const int score=(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?100:0)+(rt?1000:0);
        if(score>best){best=score;physical=candidate;graphics_family=gf;present_family=pf;rt_features=rt;maintenance4=v13.maintenance4;sampler_anisotropy=features.features.samplerAnisotropy;device_max_anisotropy=properties.limits.maxSamplerAnisotropy;sampled_image_update_after_bind=v12.descriptorBindingSampledImageUpdateAfterBind;bc_images_enabled=features.features.textureCompressionBC;}
        if(physical==candidate)shader_demote_to_helper_invocation=v13.shaderDemoteToHelperInvocation;
    }
    if(!physical)throw VulkanUnavailableError("No Vulkan 1.3 graphics/compute/presentation device with independentBlend, extendedDynamicState and shaderDemoteToHelperInvocation");
    for(std::size_t i=0;i<texture_formats.size();++i){VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physical,texture_formats[i],&properties);texture_features[i]=properties.optimalTilingFeatures;}
    diag.rt_supported=rt_features;
    rt_features=rt_features&&(diag.rt_requested!=0||capability_probe);
    sampled_image_update_after_bind=sampled_image_update_after_bind&&rt_features;
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; properties.pNext=&driver; vkGetPhysicalDeviceProperties2(physical,&properties);
    diag.device_name=properties.properties.deviceName; diag.driver_name=driver.driverName; diag.driver_info=driver.driverInfo;
    diag.vendor_id=properties.properties.vendorID;diag.device_id=properties.properties.deviceID;diag.api_version=properties.properties.apiVersion;diag.driver_version=properties.properties.driverVersion;
    timestamp_period=properties.properties.limits.timestampPeriod;
    std::uint32_t family_count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,nullptr);std::vector<VkQueueFamilyProperties> family_properties(family_count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,family_properties.data());timestamp_bits=family_properties[graphics_family].timestampValidBits;
    const float priority=1; std::array<VkDeviceQueueCreateInfo,2> queues{};
    queues[0].sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queues[0].queueFamilyIndex=graphics_family;queues[0].queueCount=1;queues[0].pQueuePriorities=&priority;
    queues[1]=queues[0];queues[1].queueFamilyIndex=present_family;
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};ray.rayQuery=rt_features;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};acceleration.accelerationStructure=rt_features;acceleration.pNext=&ray;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};dynamic.extendedDynamicState=VK_TRUE;if(rt_features)dynamic.pNext=&acceleration;
    VkPhysicalDeviceVulkan12Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};features.bufferDeviceAddress=rt_features;features.runtimeDescriptorArray=rt_features;features.shaderSampledImageArrayNonUniformIndexing=rt_features;features.descriptorBindingSampledImageUpdateAfterBind=sampled_image_update_after_bind;features.pNext=&dynamic;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};v13.maintenance4=maintenance4;v13.shaderDemoteToHelperInvocation=shader_demote_to_helper_invocation;v13.pNext=&features;
    if(rt_features){extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);}
    VkPhysicalDeviceFeatures core_features{};core_features.independentBlend=VK_TRUE;core_features.samplerAnisotropy=sampler_anisotropy;core_features.textureCompressionBC=bc_images_enabled;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.pNext=&v13;info.pEnabledFeatures=&core_features;info.queueCreateInfoCount=graphics_family==present_family?1:2;info.pQueueCreateInfos=queues.data();info.enabledExtensionCount=std::uint32_t(extensions.size());info.ppEnabledExtensionNames=extensions.data();
    check_availability(vkCreateDevice(physical,&info,nullptr,&device),"create Vulkan device");load_vulkan_device_functions(dispatch,device,rt_features);vkGetDeviceQueue(device,graphics_family,0,&graphics);vkGetDeviceQueue(device,present_family,0,&present);
    VkImageFormatProperties source_image_properties{};
    const auto source_usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|((texture_features[0]&VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    const auto source_image_result=vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,source_usage,0,&source_image_properties);
    if(source_image_result!=VK_ERROR_FORMAT_NOT_SUPPORTED)check_availability(source_image_result,"query selected device source mip support");
    source_mipmaps_supported=source_image_result==VK_SUCCESS&&source_image_properties.maxMipLevels>=2&&(texture_features[0]&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)!=0;
    std::array<std::span<const std::uint32_t>,6> shader_spans{};
    for(std::size_t i=0;i<shader_names.size();++i){
        std::ifstream file(shaders/shader_names[i],std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Cannot load Vulkan shader "+(shaders/shader_names[i]).string());
        const auto bytes=file.tellg();if(bytes<=0||bytes%4!=0||bytes>32*1024*1024)throw std::runtime_error("Invalid SPIR-V shader");
        auto& code=shader_words[i];code.resize(std::size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);if(!file)throw std::runtime_error("Cannot read SPIR-V shader");shader_spans[i]=code;
    }
    const auto cache_start=std::chrono::steady_clock::now();
    cache_storage=::c4::rendering::pipeline_cache::prepare(properties.properties,shader_spans);
    const auto stored=::c4::rendering::pipeline_cache::load(cache_storage);
    diag.pipeline_cache_path=cache_storage.path.string();diag.pipeline_cache_load_status=::c4::rendering::pipeline_cache::status_name(stored.status);
    if(stored.error)diag.pipeline_cache_error=stored.error.message();else if(!stored.reason.empty())diag.pipeline_cache_error=stored.reason;
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};cache.initialDataSize=stored.bytes.size();cache.pInitialData=stored.bytes.data();
    auto cache_result=vkCreatePipelineCache(device,&cache,nullptr,&pipeline_cache);
    if(cache_result!=VK_SUCCESS&&!stored.bytes.empty()){diag.pipeline_cache_load_status="driver_rejected";cache.initialDataSize=0;cache.pInitialData=nullptr;cache_result=vkCreatePipelineCache(device,&cache,nullptr,&pipeline_cache);}
    check(cache_result,"create pipeline cache");
    dispatch.scene_pipeline_cache=pipeline_cache;
    if(diag.pipeline_cache_load_status=="loaded")diag.pipeline_cache_loaded_bytes=stored.bytes.size();
    diag.pipeline_cache_load_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-cache_start).count());
    if(timestamp_bits){VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};queries.queryType=VK_QUERY_TYPE_TIMESTAMP;queries.queryCount=3;check(vkCreateQueryPool(device,&queries,nullptr,&timestamp_pool),"create GPU timestamp pool");diag.gpu_timing_available=true;}
}

void VulkanScene::Impl::create_descriptors() {
    std::array<VkDescriptorSetLayoutBinding,18> bindings{};
    for(unsigned i=0;i<bindings.size();++i)bindings[i]={i,(i==1||i==10||i==11||i==13||i==14||i==15)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=std::uint32_t(bindings.size());layout.pBindings=bindings.data();check(vkCreateDescriptorSetLayout(device,&layout,nullptr,&raster_set_layout),"create source raster descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(VulkanDraw)};
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};info.setLayoutCount=1;info.pSetLayouts=&raster_set_layout;info.pushConstantRangeCount=1;info.pPushConstantRanges=&push;check(vkCreatePipelineLayout(device,&info,nullptr,&raster_layout),"create source raster pipeline layout");
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    const auto alignment=properties.limits.minUniformBufferOffsetAlignment;material_stride=(sizeof(VulkanMaterial)+alignment-1)/alignment*alignment;
    lighting_stride=(sizeof(SourceLightingUniforms)+alignment-1)/alignment*alignment;
    max_texture_dimension=properties.limits.maxImageDimension2D;
    lighting_device.sourceMaxTextureWidth=max_texture_dimension;
    ensure_materials(0);
}

void VulkanScene::Impl::ensure_materials(std::size_t count) {
    count=std::max<std::size_t>(count,1);
    if(count<=material_bindings.size())return;
    if(count>UINT32_MAX/24)throw std::runtime_error("Source material descriptor count exceeds Vulkan limits");
    composite.reset();
    if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);descriptor_pool={};destroy(material_buffer);destroy(lighting_buffer);
    material_buffer=buffer(material_stride*(count+1),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    lighting_buffer=buffer(lighting_stride*(count+1),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memset(lighting_buffer.mapped,0,std::size_t(lighting_buffer.size));
    VulkanMaterial neutral_material{};neutral_material.diffuse={1,1,1,1};neutral_material.config[2]=1;
    std::memcpy(material_buffer.mapped,&neutral_material,sizeof(neutral_material));
    material_bindings.assign(count,{});
    const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,std::uint32_t(count*24)},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,std::uint32_t(count*12)}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=std::uint32_t(count*2);pool.poolSizeCount=2;pool.pPoolSizes=sizes.data();check(vkCreateDescriptorPool(device,&pool,nullptr,&descriptor_pool),"create source material descriptor pool");
    std::vector<VkDescriptorSetLayout> layouts(count*2,raster_set_layout);std::vector<VkDescriptorSet> sets(count*2);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=descriptor_pool;allocate.descriptorSetCount=std::uint32_t(count*2);allocate.pSetLayouts=layouts.data();check(vkAllocateDescriptorSets(device,&allocate,sets.data()),"allocate source material descriptors");
    for(std::size_t i=0;i<count;++i){material_bindings[i].set=sets[i*2];material_bindings[i].shadow_set=sets[i*2+1];}
}

void VulkanScene::Impl::create_passes() {
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.subpassCount=1;pass.pSubpasses=&subpass;
    VkAttachmentDescription output{};output.format=swap_format;output.samples=VK_SAMPLE_COUNT_1_BIT;output.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;output.storeOp=VK_ATTACHMENT_STORE_OP_STORE;output.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;output.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;output.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;output.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference output_reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};subpass.colorAttachmentCount=1;subpass.pColorAttachments=&output_reference;subpass.pDepthStencilAttachment=nullptr;
    VkSubpassDependency output_dependency{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_DEPENDENCY_BY_REGION_BIT};
    pass.attachmentCount=1;pass.pAttachments=&output;pass.dependencyCount=1;pass.pDependencies=&output_dependency;check(vkCreateRenderPass(device,&pass,nullptr,&compose_pass),"create presentation render pass");
}

VkShaderModule VulkanScene::Impl::shader(const char* name) {
    const auto found=std::find_if(shader_names.begin(),shader_names.end(),[&](const auto* candidate){return std::strcmp(candidate,name)==0;});
    if(found==shader_names.end())throw std::runtime_error("Unknown native Vulkan shader");
    const auto& code=shader_words[std::size_t(found-shader_names.begin())];
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=code.size()*sizeof(std::uint32_t);info.pCode=code.data();VkShaderModule module{};check(vkCreateShaderModule(device,&info,nullptr,&module),"create shader module");return module;
}

VkPipeline VulkanScene::Impl::pipeline(const PipelineKey& key) {
    if(const auto found=pipelines.find(key);found!=pipelines.end())return found->second;
    VkShaderModule vertex{},fragment{};VkPipeline result{};
    try {
        vertex=shader("raster.vert.spv");fragment=shader("raster.frag.spv");
        const std::array<VkBool32,2> specialization{key.shadow,key.sky};
        const std::array<VkSpecializationMapEntry,2> entries{{{4,0,sizeof(VkBool32)},{6,sizeof(VkBool32),sizeof(VkBool32)}}};
        VkSpecializationInfo constants{std::uint32_t(entries.size()),entries.data(),sizeof(specialization),specialization.data()};
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[0].module=vertex;stages[1].module=fragment;stages[0].pName=stages[1].pName="main";stages[0].pSpecializationInfo=stages[1].pSpecializationInfo=&constants;
        const std::array<VkVertexInputBindingDescription,4> bindings{{{0,sizeof(assets::Vertex),VK_VERTEX_INPUT_RATE_VERTEX},{1,8,VK_VERTEX_INPUT_RATE_VERTEX},{2,16,VK_VERTEX_INPUT_RATE_VERTEX},{3,0,VK_VERTEX_INPUT_RATE_VERTEX}}};
        const std::array<VkVertexInputAttributeDescription,6> attributes{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(assets::Vertex,x)},{1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(assets::Vertex,u)},{2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(assets::Vertex,nx)},{3,1,VK_FORMAT_R32G32_SFLOAT,0},{4,2,VK_FORMAT_R32G32B32A32_SFLOAT,0},{5,3,VK_FORMAT_R32G32B32A32_SFLOAT,0}}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};input.vertexBindingDescriptionCount=4;input.pVertexBindingDescriptions=bindings.data();input.vertexAttributeDescriptionCount=6;input.pVertexAttributeDescriptions=attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=key.cull_mode==3?VK_CULL_MODE_BACK_BIT:key.cull_mode==2?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo sample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};sample.rasterizationSamples=key.shadow?VK_SAMPLE_COUNT_1_BIT:target->sample_count();
        VkPipelineDepthStencilStateCreateInfo depth_test{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth_test.depthTestEnable=key.depth_test;depth_test.depthWriteEnable=key.depth_write;depth_test.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        std::array<VkPipelineColorBlendAttachmentState,5> blending{};
        for(auto& state:blending)state.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        const auto factor=[](BlendFactor value){switch(value){case BlendFactor::Zero:return VK_BLEND_FACTOR_ZERO;case BlendFactor::One:return VK_BLEND_FACTOR_ONE;case BlendFactor::SourceAlpha:return VK_BLEND_FACTOR_SRC_ALPHA;case BlendFactor::InverseSourceAlpha:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;}return VK_BLEND_FACTOR_ONE;};
        if(key.blend){auto& state=blending[0];state.blendEnable=VK_TRUE;state.srcColorBlendFactor=state.srcAlphaBlendFactor=factor(key.source);state.dstColorBlendFactor=state.dstAlphaBlendFactor=factor(key.destination);state.colorBlendOp=state.alphaBlendOp=VK_BLEND_OP_ADD;}
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=key.shadow?0:5;blend.pAttachments=blending.data();
        const std::array<VkDynamicState,3> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=3;dynamic.pDynamicStates=dynamic_states.data();
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=2;info.pStages=stages.data();info.pVertexInputState=&input;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&sample;info.pDepthStencilState=&depth_test;info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=raster_layout;info.renderPass=key.shadow?quality_passes->shadow_render_pass():target->render_pass();
        check(vkCreateGraphicsPipelines(device,pipeline_cache,1,&info,nullptr,&result),"create graphics pipeline");
        if(!key.shadow){lighting_device.supportedADD=true;lighting_device.supportedMOD2X=true;lighting_device.capabilitiesKnown=true;}
    } catch(...) {if(vertex)vkDestroyShaderModule(device,vertex,nullptr);if(fragment)vkDestroyShaderModule(device,fragment,nullptr);throw;}
    vkDestroyShaderModule(device,vertex,nullptr);vkDestroyShaderModule(device,fragment,nullptr);
    try{pipelines.emplace(key,result);}catch(...){vkDestroyPipeline(device,result,nullptr);throw;}return result;
}

void VulkanScene::Impl::destroy_swapchain() noexcept {
    if(!device)return;
    if(composite)composite->release_extent();
    for(auto framebuffer:swap_framebuffers)if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);swap_framebuffers.clear();
    for(const auto& [key,value]:pipelines)vkDestroyPipeline(device,value,nullptr);pipelines.clear();
    if(compose_pass)vkDestroyRenderPass(device,compose_pass,nullptr);compose_pass={};
    if(target)target->clear();destroy(capture_buffer);
    for(auto view:swap_views)if(view)vkDestroyImageView(device,view,nullptr);swap_views.clear();
    for(auto semaphore:rendered)if(semaphore)vkDestroySemaphore(device,semaphore,nullptr);rendered.clear();
    if(swapchain)vkDestroySwapchainKHR(device,swapchain,nullptr);swapchain={};swap_images.clear();
}

void VulkanScene::Impl::create_swapchain() {
    int width=0,height=0;if(!SDL_GetWindowSizeInPixels(window,&width,&height))throw std::runtime_error(SDL_GetError());
    if(width<=0||height<=0)return;
    check(vkDeviceWaitIdle(device),"wait swapchain recreation");completed();destroy_swapchain();
    VkSurfaceCapabilitiesKHR capabilities;check_availability(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&capabilities),"query surface capabilities");
    if(!(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))throw VulkanUnavailableError("Surface cannot render color attachments");
    extent=capabilities.currentExtent;
    if(extent.width==UINT32_MAX){extent.width=std::clamp(std::uint32_t(width),capabilities.minImageExtent.width,capabilities.maxImageExtent.width);extent.height=std::clamp(std::uint32_t(height),capabilities.minImageExtent.height,capabilities.maxImageExtent.height);}
    if(!extent.width||!extent.height)return;
    std::uint32_t count=0;check_availability(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"query surface formats");std::vector<VkSurfaceFormatKHR> formats(count);check_availability(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"read surface formats");
    if(formats.empty())throw VulkanUnavailableError("Surface has no image formats");
    auto chosen=formats.front();if(formats.size()==1&&chosen.format==VK_FORMAT_UNDEFINED)chosen={VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for(const auto& format:formats)if((format.format==VK_FORMAT_B8G8R8A8_UNORM||format.format==VK_FORMAT_R8G8B8A8_UNORM)&&format.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){chosen=format;break;}
    if(chosen.format!=VK_FORMAT_B8G8R8A8_UNORM&&chosen.format!=VK_FORMAT_R8G8B8A8_UNORM){std::ostringstream message;message<<"Surface has no UNORM8 Original presentation format; available Vulkan formats:";for(const auto& format:formats)message<<' '<<std::uint32_t(format.format);throw VulkanUnavailableError(message.str());}
    swap_format=chosen.format;
    check_availability(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,nullptr),"query present modes");std::vector<VkPresentModeKHR> modes(count);check_availability(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,modes.data()),"read present modes");
    VkPresentModeKHR mode=VK_PRESENT_MODE_FIFO_KHR;
    if(!vsync){if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end())mode=VK_PRESENT_MODE_MAILBOX_KHR;else if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=modes.end())mode=VK_PRESENT_MODE_IMMEDIATE_KHR;}
    diag.present_mode=mode==VK_PRESENT_MODE_MAILBOX_KHR?"mailbox":mode==VK_PRESENT_MODE_IMMEDIATE_KHR?"immediate":"fifo";
    std::uint32_t images=capabilities.minImageCount+1;if(capabilities.maxImageCount)images=std::min(images,capabilities.maxImageCount);
    const std::array<std::uint32_t,2> families{graphics_family,present_family};
    VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swap.surface=surface;swap.minImageCount=images;swap.imageFormat=swap_format;swap.imageColorSpace=chosen.colorSpace;swap.imageExtent=extent;swap.imageArrayLayers=1;swap.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    can_capture=(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;if(can_capture)swap.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swap.imageSharingMode=graphics_family==present_family?VK_SHARING_MODE_EXCLUSIVE:VK_SHARING_MODE_CONCURRENT;if(graphics_family!=present_family){swap.queueFamilyIndexCount=2;swap.pQueueFamilyIndices=families.data();}
    swap.preTransform=capabilities.currentTransform;swap.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for(auto alpha:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})if(capabilities.supportedCompositeAlpha&alpha){swap.compositeAlpha=alpha;break;}
    swap.presentMode=mode;swap.clipped=VK_TRUE;check_availability(vkCreateSwapchainKHR(device,&swap,nullptr,&swapchain),"create swapchain");
    check_availability(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"query swapchain images");swap_images.resize(count);check_availability(vkGetSwapchainImagesKHR(device,swapchain,&count,swap_images.data()),"read swapchain images");
    swap_views.resize(count);rendered.resize(count);swap_framebuffers.resize(count);
    for(std::size_t i=0;i<count;++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=swap_images[i];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=swap_format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};check(vkCreateImageView(device,&view,nullptr,&swap_views[i]),"create swapchain view");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(vkCreateSemaphore(device,&semaphore,nullptr,&rendered[i]),"create per-image presentation semaphore");
    }
    if(!capabilities_probed)refresh_capabilities();
    resolved=resolve_quality();
    prepare_environments();
    prepare_filtering();
    auto effective=resolved;
    if(!supplemental_shadows_allowed()){effective.shadows=quality::ShadowQuality::Off;effective.shadow_cascades=effective.shadow_size=0;}
    if(quality_passes)try{quality_passes->prepare(extent,effective);}catch(const std::exception& error){if(preparing_settings)throw;quality_passes.reset();this->capabilities.fxaa=this->capabilities.ssao=this->capabilities.bloom=this->capabilities.internal_hdr=false;this->capabilities.max_shadow_cascades=this->capabilities.max_shadow_size=0;diag.quality_fallback_reason=error.what();const auto previous=resolved;resolved=resolve_quality();resolved.texture_filtering=previous.texture_filtering;resolved.anisotropy=previous.anisotropy;resolved.fallbacks|=previous.fallbacks;}
    if(resolved.internal_hdr&&(hdr_sample_mask&resolved.samples)==0){
        unsigned samples=resolved.samples;while(samples>1&&(hdr_sample_mask&samples)==0)samples/=2;resolved.samples=samples;resolved.fallbacks|=quality::SamplesUnavailable;
    }
    for(;;){
        try{target->resize(extent,resolved.samples,resolved.internal_hdr?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM);pipeline({false,false,true,true,BlendFactor::One,BlendFactor::Zero});break;}
        catch(const std::exception& error){
            diag.quality_fallback_reason=error.what();for(const auto& [key,value]:pipelines)vkDestroyPipeline(device,value,nullptr);pipelines.clear();
            if(preparing_settings)throw;
            if(resolved.samples>1){do{resolved.samples/=2;}while(resolved.samples>1&&(this->capabilities.sample_count_mask&resolved.samples)==0);resolved.fallbacks|=quality::SamplesUnavailable;continue;}
            if(resolved.internal_hdr){resolved.internal_hdr=false;resolved.fallbacks|=quality::HdrUnavailable;if(resolved.bloom){resolved.bloom=false;resolved.bloom_strength=0;resolved.fallbacks|=quality::BloomUnavailable;}continue;}
            throw;
        }
    }
    if(quality_passes){
        effective=resolved;if(!supplemental_shadows_allowed()){effective.shadows=quality::ShadowQuality::Off;effective.shadow_cascades=effective.shadow_size=0;}
        try{quality_passes->prepare(extent,effective);}
        catch(const std::exception& error){
            if(preparing_settings)throw;
            quality_passes.reset();this->capabilities.fxaa=this->capabilities.ssao=this->capabilities.bloom=this->capabilities.internal_hdr=false;this->capabilities.max_shadow_cascades=this->capabilities.max_shadow_size=0;diag.quality_fallback_reason=error.what();
            resolved.post_aa=quality::PostAA::Off;resolved.shadows=quality::ShadowQuality::Off;resolved.shadow_cascades=resolved.shadow_size=0;resolved.ssao=false;resolved.ssao_samples=0;resolved.bloom=false;resolved.bloom_strength=0;
            if(resolved.internal_hdr){resolved.internal_hdr=false;resolved.fallbacks|=quality::HdrUnavailable;for(const auto& [key,value]:pipelines)vkDestroyPipeline(device,value,nullptr);pipelines.clear();target->resize(extent,resolved.samples,VK_FORMAT_R8G8B8A8_UNORM);pipeline({false,false,true,true,BlendFactor::One,BlendFactor::Zero});}
            resolved.fallbacks|=quality::resolve_graphics(settings,this->capabilities).fallbacks;
        }
    }
    create_passes();
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=compose_pass;framebuffer.attachmentCount=1;framebuffer.width=extent.width;framebuffer.height=extent.height;framebuffer.layers=1;
    for(std::size_t i=0;i<swap_views.size();++i){framebuffer.pAttachments=&swap_views[i];check(vkCreateFramebuffer(device,&framebuffer,nullptr,&swap_framebuffers[i]),"create swapchain framebuffer");}
    if(rays)try{rays->update_textures(ray_textures);rays->resize(extent,{target->color().view,target->position().view,target->normal().view,target->material_receiver().view,target->sun_direct().view});}catch(const std::exception& error){if(preparing_settings)throw;rays.reset();diag.rt_enabled=diag.rt_supported_effects=0;diag.rt_fallback_reason=std::string("Hardware ray-query resize failed; Original raster retained: ")+error.what();}
    this->capabilities.temporal_denoise=rays&&rays->temporal_denoise_available();this->capabilities.high_quality_denoise=rays&&rays->high_quality_denoise_available();
    const auto denoising=resolve_quality();resolved.denoising=denoising.denoising;resolved.denoise_passes=denoising.denoise_passes;
    resolved.fallbacks=(resolved.fallbacks&~std::uint32_t(quality::DenoisingUnavailable))|(denoising.fallbacks&quality::DenoisingUnavailable);
    diag.color_attachment_format=std::uint32_t(target->color().format);diag.depth_resolve_mode=std::uint32_t(target->depth_resolve_mode());
    ensure_composite();
    composite->set_auxiliary_descriptors({quality_buffer.handle,0,sizeof(quality::QualityUniforms)},shadow_image_info(),shadow_buffer_info(),{globals_buffer.handle,0,128},{lighting_buffer.handle,0,sizeof(SourceLightingUniforms)});
    composite->resize(extent,target->depth().view,target->depth_format(),swap_format,compose_pass,target->color().format);
    if(scene)for(const auto& entity:scene->instances){
        for(const bool effects_enabled:{false,true}){
            const auto draw=original_material_draw_state(entity.material_state,effects_enabled);
            for(const auto& pass:draw.passes){const auto raster=effective_raster(entity,pass);pipeline({false,raster.blend,raster.depth_test,raster.depth_write,raster.source_blend,raster.destination_blend,entity.source_cull_mode});if(supplemental_shadows_allowed()&&effective.shadow_cascades&&this->capabilities.max_shadow_cascades)pipeline({true,false,true,true,raster.source_blend,raster.destination_blend,entity.source_cull_mode});}
        }
    }
    if(scene&&scene->sky)for(const auto& face:scene->sky->faces){
        const auto draw=original_material_draw_state(face.material_state,false);
        if(draw.technique!=MaterialTechnique::FixedFunction||draw.passes.size()!=1)throw std::runtime_error("Source sky requires one fixed bitmap pass");
        const auto raster=apply_original_model_alpha(draw.passes[0].raster,face.original_alpha);
        pipeline({false,raster.blend,true,false,raster.source_blend,raster.destination_blend,1,true});
        const auto levels=[&](const Image& image){return resolved.texture_filtering==quality::TextureFiltering::Original?image.source_levels:image.levels;};
        const auto index=face.texture_index;
        const auto& texture=resolved.texture_filtering!=quality::TextureFiltering::Original&&filtering_images[index].handle?filtering_images[index]:textures[index];
        material_sampler(draw.passes[0].sampler,levels(texture));
        material_sampler(draw.passes[0].sampler,levels(neutral));
        for(const auto& sampling:face.material_state.material_samplers)material_sampler(sampling,levels(neutral));
        material_sampler(face.material_state.material_samplers[0],levels(neutral_cube));
    }
    drain_uploads();
    reported_resolved=resolved;reported_resolved.effective_preset=quality::effective_profile(resolved,quality::RendererBackend::Vulkan,diag.rt_enabled);
    frame_quality=resolved;prepared_ssao=resolved.ssao;
    recreate=false;
}

void VulkanScene::Impl::ensure_composite() {
    if(composite)return;
    SourceComposite::Context context{physical,device,pipeline_cache,raster_set_layout,raster_layout,globals_buffer.handle,sizeof(VulkanGlobals),material_buffer.handle,0,sizeof(VulkanMaterial),neutral.view,sampler,neutral_cube.view,sampler,shaders};
    context.bc_images_enabled=bc_images_enabled;
    context.sampler_anisotropy_enabled=sampler_anisotropy;context.max_sampler_anisotropy=device_max_anisotropy;
    context.quality_uniform={quality_buffer.handle,0,sizeof(quality::QualityUniforms)};
    context.shadow_depth=shadow_image_info();context.shadow_uniform=shadow_buffer_info();
    context.quality_matrices={globals_buffer.handle,0,128};context.lighting_uniform={lighting_buffer.handle,0,sizeof(SourceLightingUniforms)};
    composite=std::make_unique<SourceComposite>(context);
    composite->resize(extent,target->depth().view,target->depth_format(),swap_format,compose_pass,target->color().format);
    composite->set_postprocess(this,&Impl::postprocess);
}

bool VulkanScene::Impl::draw(const FrameSnapshot& source_frame,const std::filesystem::path& capture) {
    if(!scene)throw std::runtime_error("Vulkan source scene must be prepared before drawing");
    auto frame=source_frame;
    if(frame.overlay&&!frame.overlay->draw_world){frame.instances={};frame.effects=nullptr;}
    if(&frame.scene!=scene)throw std::runtime_error("Vulkan frame changed cached scene identity");
    check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"wait frame fence");completed();
    if(rays&&history_epoch!=frame.history_epoch)rays->invalidate_history();
    history_epoch=frame.history_epoch;
    if(recreate)create_swapchain();if(recreate||!swapchain)return false;
    frame_quality=resolved;
    if(frame_quality.ssao&&!frame.camera.projection_known){frame_quality.ssao=false;frame_quality.ssao_samples=0;frame_quality.fallbacks|=quality::SsaoUnavailable;diag.quality_fallback_reason="SSAO requires a source-known native camera projection";}
    if(quality_passes&&prepared_ssao!=frame_quality.ssao){auto effective=frame_quality;if(!supplemental_shadows_allowed()){effective.shadows=quality::ShadowQuality::Off;effective.shadow_cascades=effective.shadow_size=0;}quality_passes->prepare(extent,effective);prepared_ssao=frame_quality.ssao;}
    update_globals(frame);ensure_materials(std::max(frame.instances.size(),scene->instances.size())+(scene->sky?scene->sky->faces.size():0));prepare_materials(frame);
    if(rays){
        if(environments.empty())rays->update_environment({});
        else {const auto& environment=environments.begin()->second;rays->update_environment({environment.irradiance.view,environment.prefiltered.view,environment.sampler,float(environment.prefiltered.levels-1)});}
    }
    ensure_composite();
    composite->prepare(frame);
    if(!capture.empty()&&!can_capture)throw std::runtime_error("Surface does not support framebuffer capture");
    if(!capture.empty()&&!capture_buffer.handle)capture_buffer=buffer(VkDeviceSize(extent.width)*extent.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::uint32_t image_index=0;const auto acquired_result=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,acquired,VK_NULL_HANDLE,&image_index);
    if(acquired_result==VK_ERROR_OUT_OF_DATE_KHR){recreate=true;return false;}
    if(acquired_result!=VK_SUBOPTIMAL_KHR)check(acquired_result,"acquire swapchain image");
    submitted_instances=frame.instances;
    const auto submitted=submitted_scene_counts(frame);
    submitted_omitted_sprites=submitted.omitted_sprite_entities;
    diag.sprite_private_flags_unknown=submitted.sprite_private_flags_unknown;diag.sprite_cached_euler_unknown=submitted.sprite_cached_euler_unknown;diag.source_sprite_fallback_reason={};
    for(const auto& instance:submitted_instances)if(instance.source_sprite&&!instance.source_sprite_state_known&&!instance.native_prepared_inactive&&(instance.flags&(1u<<8))==0){const auto reason=source_sprite_unavailable_reason(instance);if(!reason.empty()){diag.source_sprite_fallback_reason=reason;break;}}
    if(acquired_result==VK_SUBOPTIMAL_KHR)recreate=true;
    check(vkResetCommandBuffer(command,0),"reset frame command");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&begin),"begin frame");
    if(timestamp_pool){vkCmdResetQueryPool(command,timestamp_pool,0,3);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamp_pool,0);}
    std::uint64_t uploaded_copies=0,uploaded_bytes=0;
    for(std::size_t i=0;i<meshes.size();++i) {
        auto& mesh=meshes[i];const auto& source=frame.scene.meshes[i];
        if(mesh.pose_revision==source.pose_revision)continue;
        if(source.vertices.data()!=mesh.vertex_data||source.vertices.size()!=mesh.vertex_count||source.indices.data()!=mesh.index_data||source.indices.size()!=mesh.count)throw std::runtime_error("Shared animation changed immutable mesh topology");
        for(const auto index:source.indices)if(index>=source.vertices.size())throw std::runtime_error("Shared animation index exceeds vertex allocation");
        if((!source.secondary_uv.empty()&&source.secondary_uv.size()!=source.vertices.size())||(!source.tertiary_uv.empty()&&source.tertiary_uv.size()!=source.vertices.size()))throw std::runtime_error("Shared animation auxiliary UV vertex count mismatch");
        if(!mesh.owns_vertices)continue;
        if(!mesh.pose_staging.mapped){
            if(source.tangent_binding&&*source.tangent_binding<frame.scene.tangent_bindings.size()&&source.tangent_provenance==assets::TangentProvenance::SourceAlgorithmNativePort){
                const auto& producer=frame.scene.tangent_bindings[*source.tangent_binding];
                if(producer.generated&&producer.vertices.data()==source.vertices.data()&&producer.vertices.size()==source.vertices.size())continue;
            }
            throw std::runtime_error("Shared animation changed immutable vertex allocation");
        }
        if(source.vertices.size_bytes()>mesh.pose_staging.size||source.vertices.size_bytes()>mesh.vertices.size)throw std::runtime_error("Shared animation changed immutable vertex allocation");
        std::memcpy(mesh.pose_staging.mapped,source.vertices.data(),source.vertices.size_bytes());
        const VkBufferCopy copy{0,0,source.vertices.size_bytes()};vkCmdCopyBuffer(command,mesh.pose_staging.handle,mesh.vertices.handle,1,&copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_READ_BIT;if(rt_features)barrier.dstAccessMask|=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=mesh.vertices.handle;barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&barrier,0,nullptr);++uploaded_copies;uploaded_bytes+=source.vertices.size_bytes();
    }
    for(std::size_t i=0;i<meshes.size();++i){
        auto& mesh=meshes[i];const auto& source=frame.scene.meshes[i];
        if(!mesh.owns_uv2||!mesh.uv2.handle||mesh.tangent_revision==source.pose_revision)continue;
        if(source.tertiary_uv.empty())continue;
        if(source.tertiary_uv.size()!=source.vertices.size()||source.vertices.size()!=mesh.vertex_count||!mesh.tangent_staging.mapped||source.tertiary_uv.size_bytes()>mesh.tangent_staging.size||source.tertiary_uv.size_bytes()>mesh.uv2.size)throw std::runtime_error("Animation changed immutable tangent allocation");
        std::memcpy(mesh.tangent_staging.mapped,source.tertiary_uv.data(),source.tertiary_uv.size_bytes());
        const VkBufferCopy copy{0,0,source.tertiary_uv.size_bytes()};vkCmdCopyBuffer(command,mesh.tangent_staging.handle,mesh.uv2.handle,1,&copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=mesh.uv2.handle;barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&barrier,0,nullptr);++uploaded_copies;uploaded_bytes+=source.tertiary_uv.size_bytes();
    }
    composite->record_uploads(command);
    if(timestamp_pool)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamp_pool,2);
    active_frame=&frame;
    diag.draw_calls=diag.world_draw_calls=diag.mesh_draw_calls=diag.image_draw_calls=diag.material_draw_calls=diag.incomplete_material_draw_calls=diag.pbr_draw_calls=diag.shadow_draw_calls=0;
    if(quality_passes&&supplemental_shadows_allowed())quality_passes->record_shadows(command,frame,lighting,this,&Impl::shadow_draw);
    std::array<VkClearValue,6> clear{};clear[0].color=frame.overlay&&!frame.overlay->draw_world?VkClearColorValue{{0,0,0,1}}:VkClearColorValue{{0.025f,0.03f,0.045f,1}};clear[5].depthStencil={1,0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=target->render_pass();pass.framebuffer=target->framebuffer();pass.renderArea={{0,0},extent};pass.clearValueCount=6;pass.pClearValues=clear.data();vkCmdBeginRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};const VkRect2D scissor{{0,0},extent};vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
    record_geometry(command,false);vkCmdEndRenderPass(command);
    const bool high_quality=resolved.effective_preset==quality::Preset::Ultra&&rays&&diag.rt_requested==7u;
    const auto output=rays?rays->record(frame,command,frame.camera.view_position,resolved.denoising,high_quality,resolved.internal_hdr):RayTracingOutput{};
    diag.rt_enabled=output.effects;if(rays){diag.rt_supported_effects=rays->available_effects();diag.rt_fallback_reason=rays->fallback_reason();}
    diag.rt_high_quality=output.effects&&rays->high_quality();diag.rt_rays_per_valid_pixel=output.effects?rays->ray_budget_per_valid_pixel():0;diag.rt_denoise_radius=output.effects?rays->spatial_radius():0;
    diag.rt_denoising=output.effects?rays->denoising_mode():quality::Denoising::Off;diag.rt_denoise_passes=output.effects?rays->denoise_passes():0;
    reported_resolved=frame_quality;reported_resolved.denoising=diag.rt_denoising;reported_resolved.denoise_passes=diag.rt_denoise_passes;reported_resolved.effective_preset=quality::effective_profile(reported_resolved,quality::RendererBackend::Vulkan,diag.rt_enabled);
    composite->record(frame,command,output.view?output.view:target->color().view,output.view?output.layout:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,swap_framebuffers[image_index],target->position().view,target->normal().view);
    const auto stats=composite->diagnostics();diag.ui_draw_calls=stats.ui_draw_calls;diag.particle_draw_calls=stats.effect_draw_calls;diag.post_passes=stats.post_passes;
    // Every component counts actual vkCmdDraw/vkCmdDrawIndexed calls in this frame.
    // Material includes sky; UI meshes are already in UI; RT compute and resolves are not draws.
    diag.draw_calls+=diag.shadow_draw_calls+stats.ui_draw_calls+stats.effect_draw_calls+stats.post_passes;
    diag.source_bloom_passes=stats.source_bloom_passes;diag.gamma_passes=stats.gamma_passes;diag.ui_mesh_draw_calls=stats.ui_mesh_draw_calls;
    diag.ui_bc_images=stats.ui_bc_images;diag.ui_packed16_images=stats.ui_packed16_images;diag.ui_rgba8_images=stats.ui_rgba8_images;diag.ui_precision_fallback_images=stats.ui_precision_fallback_images;
    diag.source_particle_anisotropy_raw=stats.source_particle_anisotropy_raw;diag.source_particle_anisotropy_dword=stats.source_particle_anisotropy_dword;diag.source_particle_sampler_anisotropy=stats.source_particle_sampler_anisotropy;diag.source_particle_sampler_anisotropic=stats.source_particle_sampler_anisotropic;diag.source_effect_sampler_unresolved=stats.source_effect_sampler_unresolved;
    if(stats.source_effect_sampler_unresolved){if(!diag.source_material_fallback_reason.empty())diag.source_material_fallback_reason+="; ";diag.source_material_fallback_reason+=stats.source_effect_sampler_reason;}
    diag.msaa_resolves=target->sample_count()==VK_SAMPLE_COUNT_1_BIT?0:5;
    active_frame=nullptr;
    if(!capture.empty()) {
        transition(swap_images[image_index],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={extent.width,extent.height,1};vkCmdCopyImageToBuffer(command,swap_images[image_index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,capture_buffer.handle,1,&copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=capture_buffer.handle;host.size=VK_WHOLE_SIZE;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        transition(swap_images[image_index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);
    }
    if(timestamp_pool)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamp_pool,1);
    check(vkEndCommandBuffer(command),"end frame");
    const VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&wait;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&rendered[image_index];
    check(vkResetFences(device,1,&fence),"reset frame fence");check(vkQueueSubmit(graphics,1,&submit,fence),"submit frame");timing_pending=timestamp_pool!=VK_NULL_HANDLE;frame_pending=true;composite->submitted();
    for(std::size_t i=0;i<meshes.size();++i){meshes[i].pose_revision=frame.scene.meshes[i].pose_revision;meshes[i].tangent_revision=frame.scene.meshes[i].pose_revision;}
    uploaded_copies+=stats.upload_buffer_copies+stats.upload_image_copies;uploaded_bytes+=stats.upload_bytes;
    diag.composition_upload_waits+=stats.upload_waits;
    diag.frame_upload_copies+=uploaded_copies;diag.frame_upload_bytes+=uploaded_bytes;frame_upload_pending=uploaded_copies!=0;
    VkPresentInfoKHR presentation{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};presentation.waitSemaphoreCount=1;presentation.pWaitSemaphores=&rendered[image_index];presentation.swapchainCount=1;presentation.pSwapchains=&swapchain;presentation.pImageIndices=&image_index;
    const auto result=vkQueuePresentKHR(present,&presentation);
    if(result==VK_ERROR_OUT_OF_DATE_KHR){recreate=true;return false;}
    if(result==VK_SUBOPTIMAL_KHR)recreate=true;else check(result,"present frame");
    ++diag.frames;diag.validation_errors=validation_errors.load();
    if(!capture.empty()){check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"wait capture");completed();save_capture(capture);}
    return true;
}

void VulkanScene::Impl::save_capture(const std::filesystem::path& path) {
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file(std::fopen(path.string().c_str(),"wbx"),&std::fclose);if(!file)throw std::runtime_error("Cannot exclusively create Vulkan capture "+path.string());
    if(std::fprintf(file.get(),"P6\n%u %u\n255\n",extent.width,extent.height)<0)throw std::runtime_error("Cannot write Vulkan capture header "+path.string());
    auto* pixels=static_cast<unsigned char*>(capture_buffer.mapped);const bool bgra=swap_format==VK_FORMAT_B8G8R8A8_UNORM;
    for(std::size_t i=0;i<std::size_t(extent.width)*extent.height;++i){const auto r=pixels[4*i+(bgra?2:0)],g=pixels[4*i+1],b=pixels[4*i+(bgra?0:2)];pixels[3*i]=r;pixels[3*i+1]=g;pixels[3*i+2]=b;}
    const auto bytes=std::size_t(extent.width)*extent.height*3;const bool written=std::fwrite(pixels,1,bytes,file.get())==bytes;
    const bool closed=std::fclose(file.release())==0;if(!written||!closed)throw std::runtime_error("Cannot write Vulkan capture "+path.string());
}

void VulkanScene::Impl::cleanup() noexcept {
    VulkanDispatchScope scope(dispatch);
    if(device&&vkDeviceWaitIdle)vkDeviceWaitIdle(device);
    abort_upload();for(auto& upload:pending_uploads)release_upload(upload);pending_uploads.clear();
    rays.reset();composite.reset();destroy_swapchain();
    if(device) {
        quality_passes.reset();target.reset();
        for(auto& [index,environment]:environments){destroy(environment.irradiance);destroy(environment.prefiltered);}environments.clear();
        for(auto& mesh:meshes){if(mesh.owns_vertices)destroy(mesh.vertices);if(mesh.owns_uv1)destroy(mesh.uv1);if(mesh.owns_uv2)destroy(mesh.uv2);destroy(mesh.indices);destroy(mesh.pose_staging);destroy(mesh.tangent_staging);}
        for(auto& texture:filtering_images)destroy(texture);
        for(auto& texture:textures)destroy(texture);destroy(neutral);destroy(neutral_cube);destroy(neutral_shadow);destroy(globals_buffer);destroy(sky_globals_buffer);destroy(material_buffer);destroy(quality_buffer);destroy(lighting_buffer);destroy(neutral_shadow_buffer);destroy(zero_attribute);
        if(raster_layout)vkDestroyPipelineLayout(device,raster_layout,nullptr);
        if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);if(raster_set_layout)vkDestroyDescriptorSetLayout(device,raster_set_layout,nullptr);
        for(const auto& [key,value]:samplers)vkDestroySampler(device,value,nullptr);samplers.clear();
        save_pipeline_cache();
        if(timestamp_pool)vkDestroyQueryPool(device,timestamp_pool,nullptr);if(pipeline_cache)vkDestroyPipelineCache(device,pipeline_cache,nullptr);
        if(sampler)vkDestroySampler(device,sampler,nullptr);if(fence)vkDestroyFence(device,fence,nullptr);if(acquired)vkDestroySemaphore(device,acquired,nullptr);if(command_pool)vkDestroyCommandPool(device,command_pool,nullptr);if(vkDestroyDevice)vkDestroyDevice(device,nullptr);device={};
    }
    if(surface&&vkDestroySurfaceKHR)vkDestroySurfaceKHR(instance,surface,nullptr);
    if(messenger&&vkDestroyDebugUtilsMessengerEXT)vkDestroyDebugUtilsMessengerEXT(instance,messenger,nullptr);
    if(instance&&vkDestroyInstance)vkDestroyInstance(instance,nullptr);
    unload_vulkan_library(dispatch);
}
void VulkanScene::Impl::completed() {
    retire_uploads();frame_pending=false;
    if(rays){rays->frame_completed();diag.rt_queries=rays->completed_queries();diag.rt_instances=rays->instances();diag.rt_triangles=rays->triangles();diag.rt_blas_updates=rays->blas_updates();diag.rt_tlas_updates=rays->tlas_updates();}
    if(timing_pending) {
        std::array<std::uint64_t,3> values{};check(vkGetQueryPoolResults(device,timestamp_pool,0,3,sizeof(values),values.data(),sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT),"read completed GPU timestamps");
        const auto mask=timestamp_bits==64?UINT64_MAX:((std::uint64_t(1)<<timestamp_bits)-1);diag.gpu_frame_ns=std::uint64_t(double((values[1]-values[0])&mask)*timestamp_period);diag.gpu_total_ns+=diag.gpu_frame_ns;++diag.gpu_timed_frames;timing_pending=false;
        gpu_samples[gpu_sample_count++%gpu_samples.size()]=diag.gpu_frame_ns;
        if(frame_upload_pending){const auto ns=std::uint64_t(double((values[2]-values[0])&mask)*timestamp_period);upload_samples[upload_sample_count++%upload_samples.size()]=ns;diag.upload_gpu_total_ns+=ns;++diag.upload_gpu_samples;frame_upload_pending=false;}
    }
}

VkSampler VulkanScene::Impl::material_sampler(const SamplerState& state,std::uint32_t levels,bool allow_quality) {
    const auto filtering=allow_quality?resolved.texture_filtering:quality::TextureFiltering::Original;
    const float anisotropy=filtering==quality::TextureFiltering::Anisotropic?resolved.anisotropy:1.0f;
    const std::array<unsigned,7> key{unsigned(state.u),unsigned(state.v),unsigned(state.w),unsigned(state.linear)|(unsigned(state.filter_explicit)<<1),levels,unsigned(filtering),std::bit_cast<unsigned>(anisotropy)};
    if(const auto found=samplers.find(key);found!=samplers.end())return found->second;
    const auto address=[](AddressMode mode){switch(mode){case AddressMode::Clamp:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;case AddressMode::Mirror:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;default:return VK_SAMPLER_ADDRESS_MODE_REPEAT;}};
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.minFilter=info.magFilter=state.filter_explicit&&!state.linear?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
    info.mipmapMode=state.linear?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;info.addressModeU=address(state.u);info.addressModeV=address(state.v);info.addressModeW=address(state.w);info.maxLod=float(levels-1);
    if(filtering!=quality::TextureFiltering::Original){info.minFilter=info.magFilter=VK_FILTER_LINEAR;info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;}
    info.anisotropyEnable=filtering==quality::TextureFiltering::Anisotropic&&sampler_anisotropy;info.maxAnisotropy=info.anisotropyEnable?anisotropy:1;
    VkSampler result{};check(vkCreateSampler(device,&info,nullptr,&result),"create source material sampler");
    try{samplers.emplace(key,result);}catch(...){vkDestroySampler(device,result,nullptr);throw;}return result;
}

void VulkanScene::Impl::update_globals(const FrameSnapshot& frame) {
    globals={};globals.view_projection=frame.camera.view_projection;
    if(frame.visuals){
        const auto& source=*frame.visuals;globals.view_position={source.view_position[0],source.view_position[1],source.view_position[2],1};
        globals.sun_direction={source.sun_direction[0],source.sun_direction[1],source.sun_direction[2],0};
        globals.view_direction={source.view_direction[0],source.view_direction[1],source.view_direction[2],0};
        globals.sun_color=source.sun_color;globals.ambient=source.ambient;globals.fog_color=source.fog_color;
        globals.fog_params={source.fog_start,source.fog_end,float(source.fog_enabled),float(source.view_inputs_known)};globals.world_to_view=source.world_to_view;
    }
    if(frame.bloom){const auto& bloom=*frame.bloom;globals.bloom_params={bloom.threshold,bloom.blur_width,bloom.highlight,bloom.scene};globals.bloom_extra={bloom.glow,float(bloom.enabled),float(bloom.width),float(bloom.height)};}
    globals.render_options={frame.gamma?1.0f:0.0f,float(resolved.pbr),rays&&(rays->enabled_effects()&2u)?1.0f:0.0f,0};
    std::memcpy(globals_buffer.mapped,&globals,sizeof(globals));
    if(scene->sky&&frame.sky){
        auto sky_globals=globals;sky_globals.view_projection=frame.sky->camera.view_projection;
        std::memcpy(sky_globals_buffer.mapped,&sky_globals,sizeof(sky_globals));
    }
    auto effective=frame_quality;
    if(authored_source_shadows(frame.scene)||((diag.rt_requested&2u)!=0&&rays&&(rays->enabled_effects()&2u)!=0)){effective.shadows=quality::ShadowQuality::Off;effective.shadow_cascades=effective.shadow_size=0;}
    const auto uniforms=quality::quality_uniforms(effective,extent.width,extent.height);std::memcpy(quality_buffer.mapped,&uniforms,sizeof(uniforms));
}

void VulkanScene::Impl::prepare_materials(const FrameSnapshot& frame) {
    bool material_identity_changed=false;
    diag.source_material_fallback_reason=texture_fallback_reason;
    diag.incomplete_shadow_casters=0;
    lighting={};
    if(frame.source_lighting)lighting=*frame.source_lighting;
    else if(scene->world&&frame.visuals)lighting=source_lighting_state(*scene->world,*frame.visuals);
    const auto sky_faces=scene->sky&&frame.sky?scene->sky->faces.size():0;
    for(std::size_t slot=0;slot<frame.instances.size()+sky_faces;++slot){
        const bool sky=slot>=frame.instances.size();
        const auto face_index=sky?slot-frame.instances.size():0;
        const auto i=sky?scene->instances.size()+face_index:slot;
        const auto* entity=sky?nullptr:&frame.instances[slot];
        const auto* face=sky?&scene->sky->faces[face_index]:nullptr;
        const auto& source=sky?face->material_state:entity->material_state;
        const auto mesh_index=sky?face->mesh_index:entity->mesh_index;
        const auto* alpha=sky?&face->original_alpha:entity->original_alpha?&*entity->original_alpha:nullptr;
        const auto flags=sky?scene->sky->authored_flags:entity->flags;
        const auto* cache=sky?&scene->sky->source_lighting_cache:entity->source_lighting_cache;
        const bool effects_enabled=!sky&&frame.source_effects_enabled;
        if(mesh_index>=meshes.size())throw std::runtime_error("Source mesh index out of range");
        const auto& mesh=meshes[mesh_index];auto& binding=material_bindings[i];
        const auto& mesh_view=frame.scene.meshes[mesh_index];
        if(entity&&(entity->source_cull_mode<1||entity->source_cull_mode>3))throw std::runtime_error("Invalid Original raster cull mode");
        const auto draw=original_material_draw_state(source,effects_enabled);
        if(draw.passes.empty()||draw.passes.size()>binding.raster.size())throw std::runtime_error("Invalid Original material draw pass count");
        for(unsigned p=0;p<draw.passes.size();++p){
            auto raster=sky?apply_original_model_alpha(draw.passes[p].raster,*alpha):effective_raster(*entity,draw.passes[p]);
            if(sky){raster.depth_test=true;raster.depth_write=false;}
            binding.raster[p]=raster;
        }
        auto material=vulkan_material_data(source,sky?std::string_view{}:std::string_view(entity->material),settings.material_overrides);material.config[0]=std::uint32_t(draw.technique);
        material.environment[2]=sky?0.0f:float(i+1);
        bool pbr_known=!sky&&(material.modern[3]==2||(effects_enabled?material.extra[2]!=0:((source.color_known_mask&0x1c0u)==0x1c0u&&source.power_known)));
        std::array<VkDescriptorImageInfo,12> images{};
        bool complete=material_has_required_textures(source,effects_enabled)&&(!effects_enabled||source.complete);
        const auto bind_image=[&](unsigned slot,std::optional<std::uint32_t> index,const SamplerState& sampling,bool cube=false){
            const Image* image_ptr=cube?&neutral_cube:&neutral;
            if(index){
                if(*index>=textures.size())throw std::runtime_error("Source material texture index out of range");
                const auto& texture=textures[*index];if(!texture.handle)throw std::runtime_error("Source material changed immutable texture references");
                image_ptr=resolved.texture_filtering!=quality::TextureFiltering::Original&&filtering_images[*index].handle?&filtering_images[*index]:&texture;material.config[1]|=1u<<slot;
                const auto& original=scene->gpu_textures[*index];
                if(original.runtime&&(!original.runtime->original_padding_known||!original.runtime->postfilter_output_known))diag.source_material_fallback_reason="Original texture padding or encoded postfilter output is unproven; native provided pixels are not claimed bit-exact";
                if((cube&&texture.faces!=6)||(!cube&&texture.faces!=1&&!(slot==4&&material_uses_cube(draw.technique))))complete=false;
                if(!sampling.filter_explicit&&diag.source_material_fallback_reason.empty())diag.source_material_fallback_reason="Original inherited sampler filtering is not recovered; native sampling is reported, not source parity";
            }
            const auto view=cube?(image_ptr->faces==6?image_ptr->view:neutral_cube.view):image_ptr->flat_view;
            const auto levels=resolved.texture_filtering==quality::TextureFiltering::Original?image_ptr->source_levels:image_ptr->levels;
            images[slot]={material_sampler(sampling,levels),view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        };
        const bool terrain=draw.technique>=MaterialTechnique::Terrain4A&&draw.technique<=MaterialTechnique::Terrain5C;
        for(unsigned slot=0;slot<4;++slot){
            const auto entity_index=slot==0?(sky?std::optional<std::uint32_t>{face->texture_index}:source.entity_skins[0]?source.entity_skins[0]:entity->texture_index):source.entity_skins[slot];
            bind_image(slot,effects_enabled||slot==0?entity_index:std::nullopt,(!effects_enabled||(terrain&&slot==0))?draw.passes[0].sampler:source.entity_samplers[slot]);
            bind_image(slot+4,effects_enabled?source.material_skins[slot]:std::nullopt,terrain&&slot+1<draw.passes.size()?draw.passes[slot+1].sampler:source.material_samplers[slot]);
        }
        bind_image(8,material_uses_cube(draw.technique)?source.material_skins[0]:std::nullopt,source.material_samplers[0],true);
        images[9]=shadow_image_info();
        images[10]=images[11]={sampler,neutral_cube.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if(!sky&&resolved.pbr&&material_uses_cube(draw.technique)&&source.material_skins[0]){
            const auto found=environments.find(*source.material_skins[0]);
            if(found!=environments.end()){
                const auto& environment=found->second;
                images[10]={environment.sampler,environment.irradiance.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                images[11]={environment.sampler,environment.prefiltered.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                material.environment[0]=float(environment.prefiltered.levels-1);material.environment[1]=1;
            }
        }
        material.config[3]=(mesh.uv1.handle?1u:0u)|(mesh.uv2.handle?2u:0u);
        if(material_requires_tangent(draw.technique)){
            const std::uint32_t required=draw.technique>=MaterialTechnique::CubeNormalOptim?0x17fu:0x7fu;
            const bool known_tangent=mesh_view.tangent_provenance==assets::TangentProvenance::ImportedSource||mesh_view.tangent_provenance==assets::TangentProvenance::SourceAlgorithmNativePort;
            complete=complete&&mesh.uv2.handle&&known_tangent&&mesh_view.tertiary_uv.size()==mesh_view.vertices.size()&&frame.visuals&&frame.visuals->view_inputs_known&&(source.skills_known_mask&required)==required&&(source.color_known_mask&7u)==7u;
        }
        if(material_requires_uv1(draw.technique))complete=complete&&mesh.uv1.handle;
        const auto source_light=source_lighting_uniforms(lighting,source,flags,alpha,cache,&draw,&lighting_device);
        const bool fixed_function=draw.technique==MaterialTechnique::FixedFunction||(draw.technique>=MaterialTechnique::EnvCubeMix&&draw.technique<=MaterialTechnique::Water);
        if(!sky&&fixed_function){
            // Match GL: the selected D3D draw payload may differ from the
            // named factory material. Keep Original fields and reflection input.
            NativeMaterialState selected;
            selected.technique=material_requires_tangent(source.technique)?MaterialTechnique::FixedFunction:source.technique;
            selected.specular=source_light.specular;
            selected.power=source_light.material_params[0];
            selected.power_known=source_light.status[0]!=0;
            selected.color_known_mask=selected.power_known?0x1c0u:0u;
            selected.texture_factor=source.texture_factor;
            selected.texture_factor_known=source.texture_factor_known;
            selected.skills[8]=source.skills[8];
            selected.skills_known_mask=source.skills_known_mask&(1u<<8);
            material.modern=quality::modern_material_data(selected,entity->material,settings.material_overrides);
            bool known=selected.power_known&&std::isfinite(selected.power)&&selected.power>=0;
            for(unsigned channel=0;channel<3;++channel)known=known&&std::isfinite(selected.specular[channel]);
            pbr_known=known||material.modern[3]==2;
        }
        const auto& base_raster=binding.raster[0];
        const auto caster=entity&&supplemental_shadows_allowed()?raster_shadow_caster(*entity,draw.technique,base_raster,alpha):std::optional<bool>{false};
        binding.shadow_caster=caster.value_or(false);
        if(!caster&&frame_quality.shadow_cascades&&supplemental_shadows_allowed()){++diag.incomplete_shadow_casters;diag.quality_fallback_reason="Raster shadow casters with unproven source draw transparency are excluded";frame_quality.fallbacks|=quality::ShadowsUnavailable;}
        if(!material_requires_tangent(draw.technique))complete=complete&&source_light.status[0]!=0;
        if(entity&&(entity->kind==InstanceKind::Model||entity->source_sprite)&&draw.technique==MaterialTechnique::FixedFunction&&!alpha)complete=false;
        if(draw.technique==MaterialTechnique::Water)complete=complete&&source.bump_matrix_known;
        if(draw.technique==MaterialTechnique::EnvCubeMix||draw.technique==MaterialTechnique::EnvCubeAlpha)complete=complete&&source.texture_factor_known&&frame.visuals&&frame.visuals->view_inputs_known;
        const bool alpha_sensitive=base_raster.blend||base_raster.color_key||base_raster.alpha_comparison!=AlphaComparison::Always;
        if(material_requires_tangent(draw.technique))complete=complete&&source.ambient_alpha_known;
        else if(alpha_sensitive)complete=complete&&(source.fixed_alpha_known||(!effects_enabled&&alpha));
        const auto sun_slot=source_light.options[3];
        const bool source_sun_known=source_light.status[0]!=0&&sun_slot>0&&sun_slot<=source_light.status[1]&&sun_slot<=8&&source_light.lights[sun_slot-1].cone_enabled[2]!=0;
        const bool physical_sun=source_sun_known&&frame.visuals&&frame.visuals->view_inputs_known&&(!fixed_function||source_light.controls[0]!=0);
        material.extra[2]=!sky&&pbr_known&&physical_sun?1.0f:0.0f;
        if(frame_quality.shadow_cascades&&supplemental_shadows_allowed()&&material_requires_tangent(draw.technique)&&material.extra[2]==0){frame_quality.fallbacks|=quality::ShadowsUnavailable;diag.quality_fallback_reason="Original Normal/cube artistic SUN has no proven physical shadow-receiver binding";}
        material.config[2]=complete?1u:0u;
        if(!complete)diag.source_material_fallback_reason="Original material requires unproven source lighting, constants, textures or vertex channels";
        auto* mapped=static_cast<std::byte*>(material_buffer.mapped)+material_stride*(i+1);
        if(!binding.initialized||std::memcmp(&binding.value,&material,sizeof(material))!=0)std::memcpy(mapped,&material,sizeof(material));
        auto* light_mapped=static_cast<std::byte*>(lighting_buffer.mapped)+lighting_stride*(i+1);
        if(std::memcmp(light_mapped,&source_light,sizeof(source_light))!=0)std::memcpy(light_mapped,&source_light,sizeof(source_light));
        const auto frame_buffer=sky?sky_globals_buffer.handle:globals_buffer.handle;
        const std::array<VkDescriptorBufferInfo,6> buffers{{{frame_buffer,0,sizeof(VulkanGlobals)},{material_buffer.handle,material_stride*(i+1),sizeof(VulkanMaterial)},{quality_buffer.handle,0,sizeof(quality::QualityUniforms)},shadow_buffer_info(),{globals_buffer.handle,0,128},{lighting_buffer.handle,lighting_stride*(i+1),sizeof(SourceLightingUniforms)}}};
        const std::string_view material_name=sky?std::string_view{}:std::string_view(entity->material);
        if(!sky&&binding.initialized&&(binding.source_material!=material_name||std::memcmp(images.data(),binding.images.data(),9*sizeof(VkDescriptorImageInfo))!=0))material_identity_changed=true;
        if(binding.source_material!=material_name)binding.source_material=material_name;
        if(!binding.initialized||std::memcmp(images.data(),binding.images.data(),sizeof(images))!=0||std::memcmp(buffers.data(),binding.buffers.data(),sizeof(buffers))!=0){
            constexpr std::array<unsigned,12> image_slots{0,2,3,4,5,6,7,8,9,12,16,17};constexpr std::array<unsigned,6> buffer_slots{1,10,11,13,14,15};
            const VkDescriptorImageInfo caster_depth{neutral_shadow_sampler,neutral_shadow.view,neutral_shadow_layout};
            std::array<VkWriteDescriptorSet,36> writes{};
            for(unsigned variant=0;variant<2;++variant){
                const auto set=variant?binding.shadow_set:binding.set;
                for(unsigned slot=0;slot<12;++slot){auto& write=writes[variant*18+slot];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.dstBinding=image_slots[slot];write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.descriptorCount=1;write.pImageInfo=variant&&slot==9?&caster_depth:&images[slot];}
                for(unsigned slot=0;slot<6;++slot){auto& write=writes[variant*18+slot+12];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.dstBinding=buffer_slots[slot];write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.descriptorCount=1;write.pBufferInfo=&buffers[slot];}
            }
            vkUpdateDescriptorSets(device,std::uint32_t(writes.size()),writes.data(),0,nullptr);
            binding.images=images;binding.buffers=buffers;
        }
        binding.value=material;binding.initialized=true;
    }
    if(material_identity_changed&&rays)rays->invalidate_history();
    if(resolved.texture_filtering!=quality::TextureFiltering::Original&&!filtering_precision_reason.empty()){
        if(!diag.source_material_fallback_reason.empty())diag.source_material_fallback_reason+="; ";
        diag.source_material_fallback_reason+=filtering_precision_reason;
    }
}

void VulkanScene::Impl::record_sky(VkCommandBuffer cmd) {
    if(!scene->sky||!active_frame->sky)return;
    for(std::size_t i=0;i<scene->sky->faces.size();++i){
        const auto& face=scene->sky->faces[i];
        const auto& mesh=meshes[face.mesh_index];
        const auto& binding=material_bindings[scene->instances.size()+i];
        const auto& raster=binding.raster[0];
        const std::array<VkBuffer,4> buffers{mesh.vertices.handle,mesh.uv1.handle?mesh.uv1.handle:zero_attribute.handle,mesh.uv2.handle?mesh.uv2.handle:zero_attribute.handle,zero_attribute.handle};
        const std::array<VkDeviceSize,4> offsets{},strides{sizeof(assets::Vertex),mesh.uv1.handle?8u:0u,mesh.uv2.handle?16u:0u,0};
        vkCmdBindVertexBuffers2(cmd,0,4,buffers.data(),offsets.data(),nullptr,strides.data());vkCmdBindIndexBuffer(cmd,mesh.indices.handle,0,VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,raster_layout,0,1,&binding.set,0,nullptr);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline({false,raster.blend,true,false,raster.source_blend,raster.destination_blend,1,true}));
        VulkanDraw push{};push.model=active_frame->sky->model_to_world[i];
        push.config[1]=raster.color_key?2u:raster.alpha_comparison==AlphaComparison::GreaterEqual?1u:0u;
        push.params={1,1,raster.blend?face.material_state.entity_alpha:1,raster.alpha_reference};
        vkCmdPushConstants(cmd,raster_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);vkCmdDrawIndexed(cmd,mesh.count,1,0,0,0);
        ++diag.draw_calls;++diag.material_draw_calls;
        if(!binding.value.config[2])++diag.incomplete_material_draw_calls;
    }
}

void VulkanScene::Impl::record_geometry(VkCommandBuffer cmd,bool shadow,unsigned cascade) {
    if(shadow&&!supplemental_shadows_allowed())return;
    // 0x100552f0: 10054c20(0) opaque list, 10054220/10011df0 sky, 10054c20(4) sorted transparent list.
    const auto opaque=rendering::source_entity_draw_order(*active_frame,draw_order);
    for(std::size_t k=0;k<=draw_order.size();++k){
        if(!shadow&&k==opaque)record_sky(cmd);
        if(k==draw_order.size())break;
        const auto i=draw_order[k];
        const auto& entity=active_frame->instances[i];if(!instance_visible(entity))continue;
        const auto& mesh=meshes[entity.mesh_index];if(!mesh.count)continue;
        const auto& source=entity.material_state;const auto& material=material_bindings[i];
        if(shadow&&!material.shadow_caster)continue;
        const std::array<VkBuffer,4> buffers{mesh.vertices.handle,mesh.uv1.handle?mesh.uv1.handle:zero_attribute.handle,mesh.uv2.handle?mesh.uv2.handle:zero_attribute.handle,zero_attribute.handle};
        const std::array<VkDeviceSize,4> offsets{},strides{sizeof(assets::Vertex),mesh.uv1.handle?8u:0u,mesh.uv2.handle?16u:0u,0};
        vkCmdBindVertexBuffers2(cmd,0,4,buffers.data(),offsets.data(),nullptr,strides.data());vkCmdBindIndexBuffer(cmd,mesh.indices.handle,0,VK_INDEX_TYPE_UINT32);
        const auto descriptor=shadow?material.shadow_set:material.set;
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,raster_layout,0,1,&descriptor,0,nullptr);
        const auto draw=original_material_draw_state(source,active_frame->source_effects_enabled);
        for(unsigned p=0;p<draw.passes.size();++p){
            const auto& pass=draw.passes[p];const auto& raster=material.raster[p];
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline({shadow,shadow?false:raster.blend,shadow?true:raster.depth_test,shadow?true:raster.depth_write,raster.source_blend,raster.destination_blend,entity.source_cull_mode}));
            VulkanDraw push{};push.model=entity.model_to_world;push.diffuse=entity.diffuse;push.config[0]=p;push.config[3]=cascade;
            push.config[1]=raster.color_key?2u:raster.alpha_comparison==AlphaComparison::GreaterEqual?1u:0u;
            push.params={pass.uv_scale[0],pass.uv_scale[1],raster.blend?source.entity_alpha:1,raster.alpha_reference};
            vkCmdPushConstants(cmd,raster_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);vkCmdDrawIndexed(cmd,mesh.count,1,0,0,0);
            if(shadow){++diag.shadow_draw_calls;continue;}
            ++diag.draw_calls;++diag.material_draw_calls;if(!material.value.config[2])++diag.incomplete_material_draw_calls;
            if(resolved.pbr&&material.value.config[2]&&material.value.extra[2]>0)++diag.pbr_draw_calls;
            if(entity.kind==InstanceKind::World)++diag.world_draw_calls;else if(entity.kind==InstanceKind::Image)++diag.image_draw_calls;else ++diag.mesh_draw_calls;
        }
    }
}

void VulkanScene::Impl::shadow_draw(void* user,VkCommandBuffer cmd,unsigned cascade,const std::array<float,16>&,VkRenderPass,VkExtent2D size) {
    auto& self=*static_cast<Impl*>(user);const VkViewport viewport{0,0,float(size.width),float(size.height),0,1};const VkRect2D scissor{{0,0},size};
    vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);self.record_geometry(cmd,true,cascade);
}

SourceComposite::Output VulkanScene::Impl::postprocess(void* user,const FrameSnapshot& frame,VkCommandBuffer cmd,VkImageView color,VkImageView,VkImageView normal) {
    auto& self=*static_cast<Impl*>(user);
    if(!self.quality_passes)return {color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0};
    if(self.target->sampled_depth_supported()&&self.frame_quality.ssao){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=self.target->depth().handle;barrier.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    const auto output=self.quality_passes->record_post(cmd,color,self.target->depth().view,normal,frame.camera,self.globals,self.resolved.internal_hdr);
    return {output.view,output.layout,output.post_passes};
}

VulkanScene::Impl::Image VulkanScene::Impl::upload_gpu_texture(const assets::GpuImage& source) {
    const auto index=unsigned(source.format);
    if(index>=texture_formats.size()||!source.width||!source.height||(source.face_count!=1&&source.face_count!=6)||(source.face_count==6&&source.width!=source.height)||source.levels.empty()||source.levels.size()>std::bit_width(std::max(source.width,source.height)))throw std::runtime_error("Invalid native source image metadata");
    const bool bc=index>=3;const auto format=texture_formats[index];
    constexpr auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    bool supported=(!bc||bc_images_enabled)&&(texture_features[index]&required)==required;
    VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(texture_features[index]&VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(supported){VkImageFormatProperties properties{};const auto result=vkGetPhysicalDeviceImageFormatProperties(physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,source.face_count==6?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,&properties);if(result==VK_ERROR_FORMAT_NOT_SUPPORTED)supported=false;else{check(result,"query native source texture format");supported=source.width<=properties.maxExtent.width&&source.height<=properties.maxExtent.height&&source.levels.size()<=properties.maxMipLevels&&source.face_count<=properties.maxArrayLayers;}}
    if(!supported){auto decoded=assets::decode_gpu_image(source);auto result=upload_texture(decoded);++diag.texture_precision_fallbacks;texture_fallback_reason="Native source texture format unavailable; explicit RGBA8 precision fallback";return result;}
    const unsigned unit=index==0?4:index<3?2:index==3?8:16;
    std::array<VkBufferImageCopy,32*6> regions{};unsigned region_count=0;
    const auto base_offset=source.levels.front().offset;
    std::size_t previous_end=0;
    for(unsigned lod=0;lod<source.levels.size();++lod){
        const auto& level=source.levels[lod];const auto columns=bc?(std::uint64_t(level.width)+3)/4:level.width;const auto rows=bc?(std::uint64_t(level.height)+3)/4:level.height;
        if(level.width!=std::max(1u,source.width>>lod)||level.height!=std::max(1u,source.height>>lod)||level.offset<previous_end||level.offset>source.texels.size()||level.byte_size>source.texels.size()-level.offset||!level.face_stride||level.face_stride>level.byte_size/source.face_count||level.byte_size!=level.face_stride*source.face_count||level.row_pitch<columns*unit||level.row_pitch%unit||rows>level.face_stride/level.row_pitch||level.row_pitch/unit*(bc?4u:1u)>UINT32_MAX||(level.offset-base_offset)%unit||level.face_stride%unit)throw std::runtime_error("Invalid native source mip byte ranges");
        for(unsigned face=0;face<source.face_count;++face){auto& copy=regions[region_count++];copy.bufferOffset=level.offset+face*level.face_stride-base_offset;copy.bufferRowLength=level.row_pitch==columns*unit?0:std::uint32_t(level.row_pitch/unit*(bc?4u:1u));copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,lod,face,1};copy.imageExtent={level.width,level.height,1};}
        previous_end=level.offset+level.byte_size;
    }
    Image result=image(source.width,source.height,format,usage,VK_IMAGE_ASPECT_COLOR_BIT,std::uint32_t(source.levels.size()),source.face_count);Buffer staging;
    result.mip_blits_supported=(texture_features[index]&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))==(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    try{
        staging=buffer(source.texels.size()-base_offset,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);std::memcpy(staging.mapped,source.texels.data()+base_offset,source.texels.size()-base_offset);begin_upload(staging.size);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=result.handle;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,result.levels,0,result.faces};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        vkCmdCopyBufferToImage(command,staging.handle,result.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,region_count,regions.data());
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);finish_upload(&staging);
    }catch(...){abort_upload();destroy(staging);destroy(result);throw;}
    ++diag.native_texture_images;if(bc)++diag.bc_texture_images;else if(index==1||index==2)++diag.packed_texture_images;
    if(source.implementation_precision_fallback){++diag.texture_precision_fallbacks;texture_fallback_reason="Native texture derivation has an explicit precision or resampling fidelity boundary";}
    return result;
}

VulkanScene::Impl::Image VulkanScene::Impl::generate_mips(const Image& source,bool count_generation) {
    if(!source.handle||source.source_levels!=1||source.levels!=1||std::max(source.width,source.height)<=1||!source.mip_blits_supported)throw std::runtime_error("Source image cannot build a complete GPU filtering mip chain");
    const auto levels=std::bit_width(std::max(source.width,source.height));
    Image replacement=image(source.width,source.height,source.format,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,levels,source.faces);
    replacement.source_levels=source.source_levels;
    replacement.mip_blits_supported=source.mip_blits_supported;
    try {
        begin_upload();
        const auto barrier=[&](VkImage handle,unsigned base,unsigned count,VkImageLayout before,VkImageLayout after,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags src_access,VkAccessFlags dst_access){
            VkImageMemoryBarrier value{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};value.image=handle;value.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,base,count,0,source.faces};value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;value.oldLayout=before;value.newLayout=after;value.srcAccessMask=src_access;value.dstAccessMask=dst_access;
            vkCmdPipelineBarrier(command,src,dst,0,0,nullptr,0,nullptr,1,&value);
        };
        barrier(source.handle,0,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        barrier(replacement.handle,0,levels,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,source.faces};copy.extent={source.width,source.height,1};
        vkCmdCopyImage(command,source.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,replacement.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier(source.handle,0,1,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
        for(unsigned level=1;level<levels;++level){
            barrier(replacement.handle,level-1,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level-1,0,source.faces};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,source.faces};
            blit.srcOffsets[1]={int(std::max(1u,source.width>>(level-1))),int(std::max(1u,source.height>>(level-1))),1};blit.dstOffsets[1]={int(std::max(1u,source.width>>level)),int(std::max(1u,source.height>>level)),1};
            vkCmdBlitImage(command,replacement.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,replacement.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
        }
        barrier(replacement.handle,0,levels-1,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
        barrier(replacement.handle,levels-1,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        finish_upload();
    }catch(...){abort_upload();destroy(replacement);throw;}
    if(count_generation){++diag.generated_mip_images;diag.generated_mip_levels+=levels-1;}
    return replacement;
}

void VulkanScene::Impl::prepare_filtering() {
    if(resolved.texture_filtering==quality::TextureFiltering::Original)return;
    for(std::size_t i=0;i<textures.size();++i){
        const auto& texture=textures[i];
        if(!texture.handle||texture.source_levels!=1||std::max(texture.width,texture.height)<=1||filtering_images[i].handle)continue;
        Image converted;
        try{
            if(texture.mip_blits_supported)filtering_images[i]=generate_mips(texture,true);
            else{
                converted=upload_texture(assets::decode_gpu_image(scene->gpu_textures[i]));
                filtering_images[i]=generate_mips(converted,true);
                retire(converted);
                ++diag.texture_precision_fallbacks;
                filtering_precision_reason="Modern filtering uses derived RGBA8 mip images for native formats without GPU blit support; converted sampling is not claimed bit-exact Original";
            }
        }catch(const std::exception& error){
            retire(converted);
            if(preparing_settings)throw;
            resolved.texture_filtering=quality::TextureFiltering::Original;resolved.anisotropy=1;resolved.fallbacks|=quality::TextureFilteringUnavailable;diag.quality_fallback_reason=error.what();
            return;
        }
    }
}

void VulkanScene::Impl::refresh_capabilities() {
    quality::ResolvedGraphics shadow_probe;
    shadow_probe.shadows=quality::ShadowQuality::High;shadow_probe.shadow_cascades=4;shadow_probe.shadow_size=2048;
    try{quality_passes->prepare({1,1},shadow_probe);capabilities=quality_passes->capabilities();}
    catch(const std::exception& error){quality_passes.reset();capabilities={};diag.quality_fallback_reason=error.what();}
    const bool post_hdr=capabilities.internal_hdr;capabilities.sample_count_mask=0;capabilities.pbr=false;capabilities.internal_hdr=false;capabilities.hdr_display_output=false;
    const auto clear_pipelines=[&]{for(const auto& [key,value]:pipelines)vkDestroyPipeline(device,value,nullptr);pipelines.clear();};
    const auto mask=target->supported_sample_mask();
    for(unsigned samples:{1u,2u,4u,8u})if(mask&samples){
        try{target->resize({1,1},samples);pipeline({false,false,true,true,BlendFactor::One,BlendFactor::Zero});capabilities.sample_count_mask|=samples;capabilities.pbr=true;}
        catch(const std::exception& error){diag.quality_fallback_reason=error.what();}
        clear_pipelines();
    }
    if(!(capabilities.sample_count_mask&1u))throw std::runtime_error("Vulkan Original raster target/pipeline unavailable");
    capabilities.ssao=capabilities.ssao&&target->sampled_depth_supported();
    if(post_hdr&&target->hdr_supported()){
        hdr_sample_mask=0;const auto hdr_mask=target->supported_sample_mask(VK_FORMAT_R16G16B16A16_SFLOAT);
        for(unsigned samples:{1u,2u,4u,8u})if(hdr_mask&samples){
            try{target->resize({1,1},samples,VK_FORMAT_R16G16B16A16_SFLOAT);pipeline({false,false,true,true,BlendFactor::One,BlendFactor::Zero});hdr_sample_mask|=samples;}
            catch(const std::exception& error){diag.quality_fallback_reason=error.what();}
            clear_pipelines();
        }
        capabilities.internal_hdr=(hdr_sample_mask&1u)!=0;
    }
    if(capabilities.max_shadow_cascades){
        try{pipeline({true,false,true,true,BlendFactor::One,BlendFactor::Zero});}
        catch(const std::exception& error){capabilities.max_shadow_cascades=capabilities.max_shadow_size=0;diag.quality_fallback_reason=error.what();}
        clear_pipelines();
    }
    Image filtering_probe;
    VkFormatProperties texture_properties{};vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&texture_properties);
    const auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if((texture_properties.optimalTilingFeatures&required)==required)try{
        filtering_probe=image(2,2,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        filtering_probe.mip_blits_supported=true;
        begin_upload();transition(filtering_probe.handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkClearColorValue white{{1,1,1,1}};const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(command,filtering_probe.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&white,1,&range);
        transition(filtering_probe.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);finish_upload();
        auto generated_probe=generate_mips(filtering_probe,false);
        retire(filtering_probe);filtering_probe=generated_probe;mip_blits_available=true;
    }catch(const std::exception& error){abort_upload();diag.quality_fallback_reason=error.what();}
    retire(filtering_probe);drain_uploads();
    SamplerState filtering_state;filtering_state.u=filtering_state.v=filtering_state.w=AddressMode::Wrap;filtering_state.linear=filtering_state.filter_explicit=true;
    const auto saved=resolved;
    if(mip_blits_available)try{resolved.texture_filtering=quality::TextureFiltering::Trilinear;material_sampler(filtering_state,2);capabilities.trilinear=true;}catch(const std::exception& error){diag.quality_fallback_reason=error.what();}
    if(mip_blits_available&&sampler_anisotropy)try{resolved.texture_filtering=quality::TextureFiltering::Anisotropic;resolved.anisotropy=device_max_anisotropy;material_sampler(filtering_state,2);capabilities.max_anisotropy=device_max_anisotropy;}catch(const std::exception& error){diag.quality_fallback_reason=error.what();}
    resolved=saved;
    target->clear();capabilities_probed=true;
}


VulkanScene::VulkanScene(SDL_Window* window,bool vsync,std::uint32_t effects,bool probe):impl_(std::make_unique<Impl>()){impl_->initialize(window,vsync,effects,probe);}
VulkanScene::~VulkanScene()=default;
bool VulkanScene::draw(const FrameSnapshot& frame,const std::filesystem::path& capture){VulkanDispatchScope scope(impl_->dispatch);return impl_->draw(frame,capture);}
void VulkanScene::resize(){VulkanDispatchScope scope(impl_->dispatch);int width=0,height=0;if(SDL_GetWindowSizeInPixels(impl_->window,&width,&height)&&(std::uint32_t(std::max(width,0))!=impl_->extent.width||std::uint32_t(std::max(height,0))!=impl_->extent.height))impl_->recreate=true;}
bool VulkanScene::set_vsync(bool vsync){VulkanDispatchScope scope(impl_->dispatch);if(impl_->vsync!=vsync){impl_->vsync=vsync;impl_->recreate=true;}return true;}
std::uint32_t VulkanScene::max_texture_dimension() const noexcept{return impl_->max_texture_dimension;}
bool VulkanScene::source_mipmaps_supported() const noexcept{return impl_->source_mipmaps_supported;}
void VulkanScene::prepare_scene(const RenderScene& scene){impl_->prepare_source(scene);}
void VulkanScene::configure_graphics(const quality::GraphicSettings& settings){
    VulkanDispatchScope scope(impl_->dispatch);
    int width=0,height=0;if(!SDL_GetWindowSizeInPixels(impl_->window,&width,&height))throw std::runtime_error(SDL_GetError());
    if(width<=0||height<=0)throw std::runtime_error("Cannot prepare Vulkan graphics settings while the drawable is minimized");
    check(vkWaitForFences(impl_->device,1,&impl_->fence,VK_TRUE,UINT64_MAX),"wait graphics settings fence");impl_->completed();
    const auto previous=impl_->settings;const auto previous_resolved=impl_->resolved;const auto previous_reported=impl_->reported_resolved;const auto previous_capabilities=impl_->capabilities;
    impl_->settings=settings;impl_->recreate=true;impl_->preparing_settings=true;
    try{if(!impl_->scene)impl_->create_rays();impl_->create_swapchain();if(impl_->recreate||!impl_->swapchain||!impl_->extent.width||!impl_->extent.height)throw std::runtime_error("Vulkan graphics settings have no prepared drawable");impl_->preparing_settings=false;}
    catch(...){impl_->preparing_settings=false;impl_->settings=previous;impl_->resolved=previous_resolved;impl_->reported_resolved=previous_reported;impl_->capabilities=previous_capabilities;impl_->recreate=true;throw;}
}
const quality::GraphicsCapabilities& VulkanScene::graphics_capabilities() const noexcept{return impl_->capabilities;}
const quality::ResolvedGraphics& VulkanScene::resolved_quality() const noexcept{return impl_->reported_resolved;}
std::uint32_t VulkanScene::ray_tracing_capabilities() const noexcept{return impl_->diag.rt_supported_effects;}
const VulkanBackendDiagnostics& VulkanScene::diagnostics() const noexcept{impl_->diag.validation_errors=impl_->validation_errors.load();return impl_->diag;}
std::uint64_t VulkanScene::draw_calls() const noexcept{return impl_->diag.draw_calls;}
std::string VulkanScene::diagnostics_json() const {
    VulkanDispatchScope scope(impl_->dispatch);
    if(impl_->frame_pending){check(vkWaitForFences(impl_->device,1,&impl_->fence,VK_TRUE,UINT64_MAX),"drain diagnostic frame");impl_->completed();}
    impl_->drain_uploads();impl_->summarize_timings();impl_->save_pipeline_cache();
    const auto& d=diagnostics();std::ostringstream out;
    const auto shadow_stats=impl_->quality_passes?impl_->quality_passes->diagnostics():QualityDiagnostics{};
    const bool authored_shadows=impl_->scene&&authored_source_shadows(*impl_->scene);
    const bool supplemental_shadows=!authored_shadows&&impl_->supplemental_shadows_allowed()&&shadow_stats.shadow_cascades!=0;
    out<<"{\"backend\":\"vulkan\",\"device_name\":"<<quoted(d.device_name)
       <<",\"driver_name\":"<<quoted(d.driver_name)<<",\"driver_info\":"<<quoted(d.driver_info)
       <<",\"vendor_id\":"<<d.vendor_id<<",\"device_id\":"<<d.device_id<<",\"api_version\":"<<d.api_version
       <<",\"driver_version\":"<<d.driver_version<<",\"present_mode\":"<<quoted(d.present_mode)
       <<",\"rt_requested\":"<<d.rt_requested<<",\"rt_enabled\":"<<d.rt_enabled
       <<",\"rt_supported\":"<<(d.rt_supported?"true":"false")<<",\"rt_supported_effects\":"<<d.rt_supported_effects
       <<",\"rt_fallback_reason\":"<<quoted(d.rt_fallback_reason)<<",\"quality_fallback_reason\":"<<quoted(d.quality_fallback_reason)
       <<",\"rt_high_quality\":"<<(d.rt_high_quality?"true":"false")<<",\"rt_rays_per_valid_pixel\":"<<d.rt_rays_per_valid_pixel<<",\"rt_denoise_radius\":"<<d.rt_denoise_radius
       <<",\"rt_denoising\":"<<quoted(std::string(quality::denoising_name(d.rt_denoising)))<<",\"rt_denoise_passes\":"<<d.rt_denoise_passes
       <<",\"native_texture_images\":"<<d.native_texture_images<<",\"bc_texture_images\":"<<d.bc_texture_images<<",\"packed_texture_images\":"<<d.packed_texture_images<<",\"texture_precision_fallbacks\":"<<d.texture_precision_fallbacks
       <<",\"source_material_fallback_reason\":"<<quoted(d.source_material_fallback_reason)
       <<",\"sprite_private_flags_unknown\":"<<d.sprite_private_flags_unknown<<",\"sprite_cached_euler_unknown\":"<<d.sprite_cached_euler_unknown<<",\"source_sprite_fallback_reason\":"<<quoted(d.source_sprite_fallback_reason)
       <<",\"draw_calls\":"<<d.draw_calls<<",\"world_draw_calls\":"<<d.world_draw_calls
       <<",\"mesh_draw_calls\":"<<d.mesh_draw_calls<<",\"image_draw_calls\":"<<d.image_draw_calls
       <<",\"incomplete_shadow_casters\":"<<d.incomplete_shadow_casters
       <<",\"ui_draw_calls\":"<<d.ui_draw_calls<<",\"ui_mesh_draw_calls\":"<<d.ui_mesh_draw_calls
       <<",\"material_draw_calls\":"<<d.material_draw_calls<<",\"incomplete_material_draw_calls\":"<<d.incomplete_material_draw_calls
       <<",\"pbr_draw_calls\":"<<d.pbr_draw_calls<<",\"particle_draw_calls\":"<<d.particle_draw_calls
       <<",\"ui_bc_images\":"<<d.ui_bc_images<<",\"ui_packed16_images\":"<<d.ui_packed16_images<<",\"ui_rgba8_images\":"<<d.ui_rgba8_images<<",\"ui_precision_fallback_images\":"<<d.ui_precision_fallback_images
       <<",\"source_particle_anisotropy_raw\":"<<d.source_particle_anisotropy_raw<<",\"source_particle_anisotropy_dword\":"<<d.source_particle_anisotropy_dword<<",\"source_particle_sampler_anisotropy\":"<<d.source_particle_sampler_anisotropy<<",\"source_particle_sampler_anisotropic\":"<<(d.source_particle_sampler_anisotropic?"true":"false")<<",\"source_effect_sampler_unresolved\":"<<(d.source_effect_sampler_unresolved?"true":"false")
       <<",\"shadow_draw_calls\":"<<d.shadow_draw_calls<<",\"post_passes\":"<<d.post_passes
       <<",\"active_shadows\":{\"mode\":"<<quoted(authored_shadows?"authored":supplemental_shadows?"supplemental":"off")
       <<",\"cascades\":"<<(supplemental_shadows?shadow_stats.shadow_cascades:0u)<<",\"size\":"<<(supplemental_shadows?shadow_stats.shadow_size:0u)<<'}'
       <<",\"source_bloom_passes\":"<<d.source_bloom_passes<<",\"gamma_passes\":"<<d.gamma_passes
       <<",\"msaa_resolves\":"<<d.msaa_resolves<<",\"color_attachment_format\":"<<d.color_attachment_format
       <<",\"generated_mip_images\":"<<d.generated_mip_images<<",\"generated_mip_levels\":"<<d.generated_mip_levels
       <<",\"depth_resolve_mode\":"<<d.depth_resolve_mode<<",\"frames\":"<<d.frames
       <<",\"validation_enabled\":"<<(d.validation_enabled?"true":"false")<<",\"validation_errors\":"<<d.validation_errors
       <<",\"gpu_timing_available\":"<<(d.gpu_timing_available?"true":"false")<<",\"gpu_frame_ns\":"<<d.gpu_frame_ns
       <<",\"gpu_total_ns\":"<<d.gpu_total_ns<<",\"gpu_timed_frames\":"<<d.gpu_timed_frames
       <<",\"gpu_median_ns\":"<<d.gpu_median_ns<<",\"gpu_p95_ns\":"<<d.gpu_p95_ns<<",\"gpu_distribution_samples\":"<<d.gpu_distribution_samples
       <<",\"gpu_distribution_scope\":\"last_2048_completed_frames\",\"gpu_timing_scope\":\"frame_commands_including_uploads_and_capture_excluding_present\""
       <<",\"upload_submissions\":"<<d.upload_submissions<<",\"upload_completed\":"<<d.upload_completed<<",\"upload_bytes\":"<<d.upload_bytes
       <<",\"upload_backpressure_waits\":"<<d.upload_backpressure_waits<<",\"upload_readiness_waits\":"<<d.upload_readiness_waits
       <<",\"upload_peak_pending\":"<<d.upload_peak_pending<<",\"upload_peak_bytes\":"<<d.upload_peak_bytes
       <<",\"upload_byte_scope\":\"retained_static_staging_allocation_including_copy_padding\",\"frame_upload_byte_scope\":\"submitted_transfer_payload\""
       <<",\"upload_gpu_total_ns\":"<<d.upload_gpu_total_ns<<",\"upload_gpu_samples\":"<<d.upload_gpu_samples
       <<",\"upload_gpu_median_ns\":"<<d.upload_gpu_median_ns<<",\"upload_gpu_p95_ns\":"<<d.upload_gpu_p95_ns
       <<",\"upload_gpu_distribution_scope\":\"last_2048_completed_batches_and_nonempty_frame_upload_sections\""
       <<",\"frame_upload_copies\":"<<d.frame_upload_copies<<",\"frame_upload_bytes\":"<<d.frame_upload_bytes
       <<",\"pipeline_cache_path\":"<<quoted(d.pipeline_cache_path)<<",\"pipeline_cache_load_status\":"<<quoted(d.pipeline_cache_load_status)
       <<",\"pipeline_cache_save_status\":"<<quoted(d.pipeline_cache_save_status)<<",\"pipeline_cache_error\":"<<quoted(d.pipeline_cache_error)
       <<",\"pipeline_cache_loaded_bytes\":"<<d.pipeline_cache_loaded_bytes<<",\"pipeline_cache_saved_bytes\":"<<d.pipeline_cache_saved_bytes
       <<",\"pipeline_cache_load_ns\":"<<d.pipeline_cache_load_ns<<",\"pipeline_cache_save_ns\":"<<d.pipeline_cache_save_ns
       <<",\"pipeline_cache_key\":"<<impl_->cache_storage.key
       <<",\"pipeline_creation_calls\":"<<impl_->dispatch.pipeline_creation_calls<<",\"pipeline_creation_total_ns\":"<<impl_->dispatch.pipeline_creation_total_ns
       <<",\"pipeline_creation_median_ns\":"<<d.pipeline_creation_median_ns<<",\"pipeline_creation_p95_ns\":"<<d.pipeline_creation_p95_ns
       <<",\"pipeline_cached_creations\":"<<impl_->dispatch.pipeline_cached_creations<<",\"pipeline_uncached_creations\":"<<impl_->dispatch.pipeline_uncached_creations
       <<",\"pipeline_creation_failures\":"<<impl_->dispatch.pipeline_creation_failures<<",\"pipeline_creation_distribution_scope\":\"last_512_host_creation_calls_not_driver_cache_hits\""
       <<",\"pipeline_cache_handle_mismatches\":"<<impl_->dispatch.pipeline_cache_handle_mismatches
       <<",\"upload_gpu_distribution_samples\":"<<std::min(impl_->upload_sample_count,impl_->upload_samples.size())
       <<",\"native_environment_cubes\":"<<impl_->environments.size()
       <<",\"composition_upload_waits\":"<<d.composition_upload_waits<<",\"upload_pending_bound\":16,\"upload_byte_window\":67108864,\"upload_byte_bound_policy\":\"window_or_single_larger_source_payload\""
       <<",\"rt_queries\":"<<d.rt_queries<<",\"rt_triangles\":"<<d.rt_triangles<<",\"rt_instances\":"<<d.rt_instances
       <<",\"rt_blas_updates\":"<<d.rt_blas_updates<<",\"rt_tlas_updates\":"<<d.rt_tlas_updates
       <<",\"graphics_capabilities\":"<<quality::capabilities_json(impl_->capabilities)
       <<",\"resolved_graphics\":"<<quality::resolved_graphics_json(impl_->reported_resolved)
       <<",\"raytracing\":"<<(impl_->rays?impl_->rays->diagnostics_json():"null");
    out<<",\"source_sprite_omissions\":{\"total_omitted\":"<<impl_->submitted_omitted_sprites<<",\"entries\":[";
    std::size_t entry_count=0;
    for(const auto& instance:impl_->submitted_instances){
        if(!instance.source_sprite||instance.source_sprite_state_known||instance.native_prepared_inactive||(instance.flags&(1u<<8))!=0)continue;
        const auto reason=source_sprite_unavailable_reason(instance);if(reason.empty())continue;
        if(entry_count==64)break;
        if(entry_count++)out<<',';
        out<<"{\"source_entity\":";if(instance.source_entity)out<<*instance.source_entity;else out<<"null";
        out<<",\"source\":"<<quoted(instance.source)<<",\"reason\":"<<quoted(reason)<<'}';
    }
    out<<"],\"list_truncated\":"<<(impl_->submitted_omitted_sprites>entry_count?"true":"false")<<"}}";
    return out.str();
}
} // namespace c4::rendering
