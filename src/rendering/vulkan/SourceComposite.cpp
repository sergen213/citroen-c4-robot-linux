#include "SourceComposite.hpp"
#include "VulkanDispatch.hpp"
#include "VulkanShaderData.hpp"
#include "rendering/SourceLighting.hpp"
#include "assets/GpuImage.hpp"
#include "game/effects/EffectFrame.hpp"
#include "game/ui/NativeUi.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace c4::rendering {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
constexpr VkMemoryPropertyFlags host_memory =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
std::array<float, 16> identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}
}

struct SourceComposite::Impl {
    struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; void* mapped{}; };
    struct Image {
        VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{};
        std::uint32_t width{}, height{}, levels{1};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    };
    struct Target { Image image; VkFramebuffer framebuffer{}; };
    struct Texture {
        Image image; Buffer staging;
        std::array<VkDescriptorSet, 2> sets{};
        std::uint64_t revision{};
        std::uint64_t prepared_revision{};
        bool valid{}, pending{};
        const assets::GpuImage* native_source{};
        std::array<float,2> uv_scale{1,1};
        bool precision_fallback{};
    };
    struct Cache {
        const assets::Image* source{};
        std::vector<Texture> textures;
        VkDescriptorPool pool{};
    };
    struct UiMesh {
        const assets::Vertex* source_vertices{};
        const std::uint32_t* source_indices{};
        std::size_t vertex_count{}, index_count{};
        std::uint64_t revision{};
        std::uint64_t prepared_revision{};
        bool valid{}, pending{};
        Buffer vertices, indices, staging;
        Buffer globals, material, lighting;
        VkDescriptorPool pool{};
        std::array<VkDescriptorSet,2> sets{};
        std::uint32_t image_index{UINT32_MAX};
    };
    Context c;
    VkSampler source_anisotropic_sampler{}, effect_sampler_selected{};
    struct Upload {
        Image* image{};
        VkBuffer source{}, target{};
        VkBufferImageCopy image_copy{};
        VkDeviceSize bytes{};
        VkAccessFlags access{};
    };
    std::vector<Upload> uploads;
    bool prepared{}, uploads_recorded{};
    std::uint64_t upload_buffer_copies{}, upload_image_copies{}, upload_bytes{};
    VkSampler linear_sampler{}, nearest_sampler{};
    VkDescriptorPool pass_pool{};
    // Every recorded pass has its own set: updates cannot change earlier draws.
    std::array<VkDescriptorSet, 6> pass_sets{};
    VkRenderPass scene_pass{}, color_pass{}, ui_pass{}, swap_pass{};
    VkExtent2D extent{};
    VkFormat scene_format{VK_FORMAT_R8G8B8A8_UNORM};
    Target scene, final_scene;
    std::array<Target, 3> bloom;
    std::array<VkPipeline, 6> compose_pipelines{};
    VkPipeline scene_copy{}, swap_copy{}, ui_pipeline{};
    std::array<VkPipeline, 8> effect_pipelines{};
    std::array<VkPipeline,6> hud_pipelines{}; // Three source cull modes, opaque/faded.
    Image ui_depth;
    VkFramebuffer ui_framebuffer{};
    VkShaderModule raster_vertex{}, fullscreen_vertex{}, raster_fragment{}, compose_fragment{};
    Buffer zero, ui_vertices, ui_indices, effect_vertices, effect_indices;
    Buffer effect_vertex_staging, effect_index_staging, gamma_staging;
    Image gamma;
    std::array<std::array<std::uint16_t, 256>, 3> gamma_values{};
    bool gamma_valid{}, effects_valid{};
    std::uint64_t effects_revision{};
    std::uint64_t prepared_effects_revision{};
    bool effects_prepared{}, gamma_pending{};
    std::array<std::array<std::uint16_t, 256>, 3> prepared_gamma_values{};
    Cache ui_cache, effect_cache;
    std::array<UiMesh, 2> ui_meshes; // The original fill and needle view entities.
    Diagnostics diag;
    std::array<std::uint64_t,4> ui_image_counts{};
    void* post_user{};
    Postprocess postprocess{};

    explicit Impl(const Context& context) : c(context) {
        try {
            if (!c.physical || !c.device ||
                !c.descriptor_set_layout || !c.pipeline_layout || !c.globals_buffer ||
                !c.default_material_buffer || !c.neutral_2d_view || !c.neutral_2d_sampler ||
                !c.neutral_cube_view || !c.neutral_cube_sampler ||
                c.globals_range != sizeof(VulkanGlobals) ||
                c.default_material_range != sizeof(VulkanMaterial) ||
                !c.quality_uniform.buffer || !c.shadow_depth.imageView || !c.shadow_depth.sampler ||
                !c.shadow_uniform.buffer || !c.quality_matrices.buffer || !c.lighting_uniform.buffer)
                throw std::runtime_error("Invalid source Vulkan composition context");
            if (c.sampler_anisotropy_enabled &&
                (!std::isfinite(c.max_sampler_anisotropy) || c.max_sampler_anisotropy < 1))
                throw std::runtime_error("Invalid enabled Vulkan sampler anisotropy capability");
            linear_sampler = sampler(VK_FILTER_LINEAR);
            nearest_sampler = sampler(VK_FILTER_NEAREST);
            effect_sampler_selected = linear_sampler;
            if (c.sampler_anisotropy_enabled) source_anisotropic_sampler = sampler(VK_FILTER_LINEAR,true);
            pass_pool = descriptor_pool(std::uint32_t(pass_sets.size()));
            for (auto& set : pass_sets) set = allocate_set(pass_pool);
            zero = buffer(16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host_memory);
            std::memset(zero.mapped, 0, 16);
            static_assert(sizeof(game::EffectVertex) == 36);
            static_assert(offsetof(game::EffectVertex, uv) == 12);
            static_assert(offsetof(game::EffectVertex, color) == 20);
            const std::array<game::EffectVertex, 4> vertices{{
                {{0,0,0},{0,0},{1,1,1,1}}, {{1,0,0},{1,0},{1,1,1,1}},
                {{1,1,0},{1,1},{1,1,1,1}}, {{0,1,0},{0,1},{1,1,1,1}}}};
            const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
            ui_vertices = buffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host_memory);
            ui_indices = buffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host_memory);
            std::memcpy(ui_vertices.mapped, vertices.data(), sizeof(vertices));
            std::memcpy(ui_indices.mapped, indices.data(), sizeof(indices));
            const auto vb = VkDeviceSize(game::effect_vertex_capacity) * sizeof(game::EffectVertex);
            const auto ib = VkDeviceSize(game::effect_index_capacity) * sizeof(std::uint32_t);
            effect_vertices = buffer(vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            effect_indices = buffer(ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            effect_vertex_staging = buffer(vb, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_memory);
            effect_index_staging = buffer(ib, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_memory);
            gamma = image(256, 1, VK_FORMAT_R16G16B16A16_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            gamma_staging = buffer(256 * 4 * sizeof(std::uint16_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_memory);
            raster_vertex = shader("raster.vert.spv");
            fullscreen_vertex = shader("fullscreen.vert.spv");
            raster_fragment = shader("raster.frag.spv");
            compose_fragment = shader("compose.frag.spv");
        } catch (...) { cleanup(); throw; }
    }
    ~Impl() { cleanup(); }

    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(c.physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("Required source composition memory type unavailable");
    }
    void destroy(Buffer& b) noexcept {
        if (b.mapped) vkUnmapMemory(c.device, b.memory);
        if (b.handle) vkDestroyBuffer(c.device, b.handle, nullptr);
        if (b.memory) vkFreeMemory(c.device, b.memory, nullptr);
        b = {};
    }
    void destroy(Image& i) noexcept {
        if (i.view) vkDestroyImageView(c.device, i.view, nullptr);
        if (i.handle) vkDestroyImage(c.device, i.handle, nullptr);
        if (i.memory) vkFreeMemory(c.device, i.memory, nullptr);
        i = {};
    }
    void destroy(Target& t) noexcept {
        if (t.framebuffer) vkDestroyFramebuffer(c.device, t.framebuffer, nullptr);
        t.framebuffer = {}; destroy(t.image);
    }
    void destroy(Cache& cache) noexcept {
        if (cache.pool) vkDestroyDescriptorPool(c.device, cache.pool, nullptr);
        for (auto& texture : cache.textures) { destroy(texture.image); destroy(texture.staging); }
        cache = {};
    }
    void destroy(UiMesh& mesh) noexcept {
        if (mesh.pool) vkDestroyDescriptorPool(c.device,mesh.pool,nullptr);
        destroy(mesh.vertices); destroy(mesh.indices); destroy(mesh.staging);
        destroy(mesh.globals); destroy(mesh.material); destroy(mesh.lighting);
        mesh = {};
    }
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags) {
        Buffer b; b.size = std::max<VkDeviceSize>(size, 4);
        try {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = b.size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(c.device, &info, nullptr, &b.handle), "create source buffer");
            VkMemoryRequirements required; vkGetBufferMemoryRequirements(c.device, b.handle, &required);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = required.size; allocation.memoryTypeIndex = memory_type(required.memoryTypeBits, flags);
            check(vkAllocateMemory(c.device, &allocation, nullptr, &b.memory), "allocate source buffer");
            check(vkBindBufferMemory(c.device, b.handle, b.memory, 0), "bind source buffer");
            if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                check(vkMapMemory(c.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "map source buffer");
        } catch (...) { destroy(b); throw; }
        return b;
    }
    Image image(std::uint32_t width, std::uint32_t height, VkFormat format,
                VkImageUsageFlags usage, std::uint32_t levels = 1,
                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        Image result; result.width = width; result.height = height; result.levels = levels; result.format = format;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D; info.extent = {width,height,1};
            info.mipLevels = levels; info.arrayLayers = 1; info.format = format;
            info.tiling = VK_IMAGE_TILING_OPTIMAL; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            info.usage = usage; info.samples = VK_SAMPLE_COUNT_1_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateImage(c.device, &info, nullptr, &result.handle), "create source image");
            VkMemoryRequirements required; vkGetImageMemoryRequirements(c.device, result.handle, &required);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = required.size;
            allocation.memoryTypeIndex = memory_type(required.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(c.device, &allocation, nullptr, &result.memory), "allocate source image");
            check(vkBindImageMemory(c.device, result.handle, result.memory, 0), "bind source image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = result.handle; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {aspect,0,levels,0,1};
            check(vkCreateImageView(c.device, &view, nullptr, &result.view), "create source image view");
        } catch (...) { destroy(result); throw; }
        return result;
    }
    VkSampler sampler(VkFilter filter, bool anisotropic = false) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = info.minFilter = filter;
        info.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = anisotropic ? 0.0f : VK_LOD_CLAMP_NONE;
        info.anisotropyEnable = anisotropic;
        info.maxAnisotropy = anisotropic ? c.max_sampler_anisotropy : 1.0f;
        VkSampler result{}; check(vkCreateSampler(c.device, &info, nullptr, &result), "create source sampler");
        return result;
    }
    VkDescriptorPool descriptor_pool(std::uint32_t count) {
        if (count > UINT32_MAX / 12) throw std::runtime_error("Source descriptor count overflow");
        const std::array<VkDescriptorPoolSize, 2> sizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 12},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count * 6}}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = count; info.poolSizeCount = std::uint32_t(sizes.size()); info.pPoolSizes = sizes.data();
        VkDescriptorPool result{}; check(vkCreateDescriptorPool(c.device, &info, nullptr, &result), "create source descriptor pool");
        return result;
    }
    VkDescriptorSet allocate_set(VkDescriptorPool pool) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool; info.descriptorSetCount = 1; info.pSetLayouts = &c.descriptor_set_layout;
        VkDescriptorSet result{}; check(vkAllocateDescriptorSets(c.device, &info, &result), "allocate source descriptors");
        return result;
    }
    void write_set(VkDescriptorSet set, VkImageView color, VkSampler color_sampler,
                   VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VkImageView blur = VK_NULL_HANDLE, const UiMesh* mesh = nullptr) {
        std::array<VkDescriptorImageInfo, 18> images{};
        std::array<VkWriteDescriptorSet, 18> writes{};
        const std::array<VkDescriptorBufferInfo, 6> buffers{{
            mesh ? VkDescriptorBufferInfo{mesh->globals.handle,0,sizeof(VulkanGlobals)}
                 : VkDescriptorBufferInfo{c.globals_buffer,0,c.globals_range},
            mesh ? VkDescriptorBufferInfo{mesh->material.handle,0,sizeof(VulkanMaterial)}
                 : VkDescriptorBufferInfo{c.default_material_buffer,c.default_material_offset,c.default_material_range},
            c.quality_uniform,c.shadow_uniform,c.quality_matrices,
            mesh ? VkDescriptorBufferInfo{mesh->lighting.handle,0,sizeof(SourceLightingUniforms)} : c.lighting_uniform}};
        for (std::uint32_t binding = 0; binding < writes.size(); ++binding) {
            auto& write = writes[binding]; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1;
            if (binding == 1 || binding == 10 || binding == 11 || binding == 13 || binding == 14 || binding == 15) {
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                const auto index = binding == 1 ? 0 : binding == 10 ? 1 : binding == 11 ? 2 : binding - 10;
                write.pBufferInfo = &buffers[index];
            } else {
                images[binding] = {c.neutral_2d_sampler,c.neutral_2d_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                if (binding == 0) images[binding] = {color_sampler,color,layout};
                if (binding == 2 && blur) images[binding] = {linear_sampler,blur,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                if (binding == 3 && (gamma_valid || gamma_pending)) images[binding] = {nearest_sampler,gamma.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                if (binding == 9 || binding == 16 || binding == 17) images[binding] = {c.neutral_cube_sampler,c.neutral_cube_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                if (binding == 12) images[binding] = c.shadow_depth;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &images[binding];
            }
        }
        vkUpdateDescriptorSets(c.device, std::uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
    void auxiliary_descriptors(VkDescriptorBufferInfo quality, VkDescriptorImageInfo depth,
                               VkDescriptorBufferInfo shadow, VkDescriptorBufferInfo matrices,
                               VkDescriptorBufferInfo lighting) {
        if (!quality.buffer || !depth.imageView || !depth.sampler || !shadow.buffer ||
            !matrices.buffer || !lighting.buffer)
            throw std::runtime_error("Invalid source composition auxiliary descriptors");
        c.quality_uniform = quality; c.shadow_depth = depth; c.shadow_uniform = shadow;
        c.quality_matrices = matrices; c.lighting_uniform = lighting;
        for (auto& texture : ui_cache.textures) {
            write_set(texture.sets[0],texture.image.view,linear_sampler);
            write_set(texture.sets[1],texture.image.view,nearest_sampler);
        }
        for (auto& texture : effect_cache.textures)
            write_set(texture.sets[0],texture.image.view,effect_sampler_selected);
        for (auto& mesh : ui_meshes) if (mesh.pool) write_ui_mesh_sets(mesh);
    }
    void transition(VkCommandBuffer command, const Image& image, VkImageLayout before, VkImageLayout after) {
        const bool initial = before == VK_IMAGE_LAYOUT_UNDEFINED;
        const bool transfer = after == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = before; barrier.newLayout = after;
        barrier.srcAccessMask = initial ? 0 : transfer ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = transfer ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.handle; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,image.levels,0,1};
        vkCmdPipelineBarrier(command,
            initial ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : transfer ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
            transfer ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);
    }
    void copy_image(Image& image, const Buffer& staging, std::uint32_t level,
                    VkDeviceSize offset, std::uint32_t width, std::uint32_t height, VkDeviceSize bytes) {
        Upload upload; upload.image = &image; upload.source = staging.handle; upload.bytes = bytes;
        upload.image_copy.bufferOffset = offset;
        upload.image_copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,level,0,1};
        upload.image_copy.imageExtent = {width,height,1};
        uploads.push_back(upload);
    }
    void upload_buffer(const Buffer& target, const Buffer& staging, const void* data, VkDeviceSize bytes, VkAccessFlags access) {
        if (!bytes) return;
        if (data != staging.mapped) std::memcpy(staging.mapped, data, std::size_t(bytes));
        Upload upload; upload.source = staging.handle; upload.target = target.handle;
        upload.bytes = bytes; upload.access = access;
        uploads.push_back(upload);
    }
    void record_uploads(VkCommandBuffer command) {
        if (!command || !prepared || uploads_recorded) throw std::runtime_error("Invalid source upload recording");
        upload_buffer_copies = upload_image_copies = upload_bytes = 0;
        Image* current = nullptr;
        for (const auto& upload : uploads) {
            if (current != upload.image) {
                if (current) transition(command,*current,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                current = upload.image;
                if (current) transition(command,*current,current->layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            }
            if (current) {
                vkCmdCopyBufferToImage(command,upload.source,current->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload.image_copy);
                ++upload_image_copies;
            } else {
                const VkBufferCopy copy{0,0,upload.bytes};
                vkCmdCopyBuffer(command,upload.source,upload.target,1,&copy);
                VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = upload.access;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = upload.target; barrier.size = upload.bytes;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                     0,0,nullptr,1,&barrier,0,nullptr);
                ++upload_buffer_copies;
            }
            upload_bytes += upload.bytes;
        }
        if (current) transition(command,*current,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        uploads_recorded = true;
    }
    void discard_uploads() noexcept {
        uploads.clear(); prepared = uploads_recorded = false;
        for (auto& texture : ui_cache.textures) texture.pending = false;
        for (auto& texture : effect_cache.textures) texture.pending = false;
        for (auto& mesh : ui_meshes) mesh.pending = false;
        effects_prepared = gamma_pending = false;
    }
    void submitted() noexcept {
        if (!uploads_recorded) return;
        for (const auto& upload : uploads)
            if (upload.image) upload.image->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        for (auto* cache : {&ui_cache,&effect_cache})
            for (auto& texture : cache->textures) if (texture.pending) {
                texture.revision = texture.prepared_revision; texture.valid = true;
            }
        for (auto& mesh : ui_meshes) if (mesh.pending) {
            mesh.revision = mesh.prepared_revision; mesh.valid = true;
        }
        if (effects_prepared) { effects_revision = prepared_effects_revision; effects_valid = true; }
        if (gamma_pending) { gamma_values = prepared_gamma_values; gamma_valid = true; }
        discard_uploads();
    }
    void validate_image(const assets::Image& source, bool stored_mips) const {
        if (!source.width || !source.height || source.face_count != 1 ||
            source.rgba.size() != std::uint64_t(source.width) * source.height * 4)
            throw std::runtime_error("Invalid original UI/effect RGBA image");
        if (!stored_mips) return;
        auto width = source.width, height = source.height;
        for (const auto& mip : source.mip_levels) {
            if (width == 1 && height == 1) throw std::runtime_error("Original image has excess mip levels");
            width = std::max(1u,width/2); height = std::max(1u,height/2);
            const auto bytes = std::uint64_t(width) * height * 4;
            if (mip.width != width || mip.height != height || mip.offset > source.mip_rgba.size() ||
                bytes > source.mip_rgba.size() - mip.offset)
                throw std::runtime_error("Invalid original image stored mip");
        }
    }
    void upload_texture(Texture& target, const assets::Image& source, bool stored_mips) {
        validate_image(source,stored_mips);
        const auto levels = stored_mips ? source.mip_levels.size()+1 : 1;
        const auto bytes = source.rgba.size()+(stored_mips ? source.mip_rgba.size() : 0);
        if (target.image.width != source.width || target.image.height != source.height ||
            target.image.levels != levels || target.staging.size != bytes)
            throw std::runtime_error("Original video changed cached image dimensions/storage");
        std::memcpy(target.staging.mapped, source.rgba.data(), source.rgba.size());
        if (stored_mips && !source.mip_rgba.empty())
            std::memcpy(static_cast<std::uint8_t*>(target.staging.mapped) + source.rgba.size(),
                        source.mip_rgba.data(), source.mip_rgba.size());
        copy_image(target.image, target.staging, 0, 0, source.width, source.height, source.rgba.size());
        for (std::uint32_t i = 0; stored_mips && i < source.mip_levels.size(); ++i) {
            const auto& mip = source.mip_levels[i];
            copy_image(target.image, target.staging, i+1, source.rgba.size()+mip.offset, mip.width, mip.height,
                       VkDeviceSize(mip.width)*mip.height*4);
        }
    }
    struct GpuFormat { VkFormat format; std::uint32_t block_width, block_height, block_bytes; bool compressed; };
    static GpuFormat gpu_format(assets::GpuImageFormat format) {
        using F = assets::GpuImageFormat;
        switch (format) {
        case F::Rgba8Unorm: return {VK_FORMAT_R8G8B8A8_UNORM,1,1,4,false};
        case F::Rgb565Unorm: return {VK_FORMAT_R5G6B5_UNORM_PACK16,1,1,2,false};
        case F::Argb1555Unorm: return {VK_FORMAT_A1R5G5B5_UNORM_PACK16,1,1,2,false};
        case F::Bc1Unorm: return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK,4,4,8,true};
        case F::Bc2Unorm: return {VK_FORMAT_BC2_UNORM_BLOCK,4,4,16,true};
        case F::Bc3Unorm: return {VK_FORMAT_BC3_UNORM_BLOCK,4,4,16,true};
        }
        throw std::runtime_error("Unknown source UI GPU image format");
    }
    bool gpu_format_supported(const GpuFormat& format) const {
        if (format.compressed && !c.bc_images_enabled) return false;
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(c.physical,format.format,&properties);
        constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return (properties.optimalTilingFeatures & required) == required;
    }
    static VkDeviceSize aligned_gpu_offset(VkDeviceSize value, std::uint32_t block_bytes) {
        const auto alignment = VkDeviceSize(std::max(4u,block_bytes));
        if (value > UINT64_MAX-(alignment-1)) throw std::runtime_error("Source GPU image staging overflow");
        return (value+alignment-1) & ~(alignment-1);
    }
    static VkDeviceSize gpu_staging_size(const assets::GpuImage& source, const GpuFormat& format) {
        if (!source.width || !source.height || source.face_count != 1 || source.levels.empty() ||
            source.levels.size() > 32) throw std::runtime_error("Invalid original UI GPU image");
        auto width = source.width, height = source.height;
        VkDeviceSize total = 0;
        for (std::size_t i=0; i<source.levels.size(); ++i) {
            const auto& level = source.levels[i];
            const auto row = ((std::uint64_t(width)+format.block_width-1)/format.block_width)*format.block_bytes;
            const auto rows = (std::uint64_t(height)+format.block_height-1)/format.block_height;
            if (row && rows > UINT64_MAX/row) throw std::runtime_error("Source GPU image byte-count overflow");
            const auto bytes = row*rows;
            if (level.width != width || level.height != height || level.row_pitch != row ||
                level.face_stride != bytes || level.byte_size != bytes ||
                level.offset > source.texels.size() || bytes > source.texels.size()-level.offset)
                throw std::runtime_error("Invalid original UI GPU image level/pitch");
            total = aligned_gpu_offset(total,format.block_bytes);
            if (bytes > UINT64_MAX-total) throw std::runtime_error("Source GPU image staging overflow");
            total += bytes;
            if (i+1 < source.levels.size() && width == 1 && height == 1)
                throw std::runtime_error("Source GPU image has excess levels");
            width = std::max(1u,width/2); height = std::max(1u,height/2);
        }
        return total;
    }
    void upload_gpu_texture(Texture& target, const assets::GpuImage& source) {
        const auto format = gpu_format(source.format);
        const auto bytes = gpu_staging_size(source,format);
        if (target.image.width != source.width || target.image.height != source.height ||
            target.image.format != format.format || target.image.levels != source.levels.size() ||
            target.staging.size != bytes)
            throw std::runtime_error("Original UI GPU image changed cached dimensions/format/levels");
        VkDeviceSize offset = 0;
        for (std::uint32_t i=0; i<source.levels.size(); ++i) {
            const auto& level = source.levels[i];
            offset = aligned_gpu_offset(offset,format.block_bytes);
            std::memcpy(static_cast<std::uint8_t*>(target.staging.mapped)+std::size_t(offset),
                        source.texels.data()+level.offset,level.byte_size);
            copy_image(target.image,target.staging,i,offset,level.width,level.height,level.byte_size);
            offset += level.byte_size;
        }
    }
    void initialize_texture(Texture& target, const assets::Image& source, bool stored_mips,
                            const assets::GpuImage* native) {
        const auto create_rgba = [&](const assets::Image& pixels) {
            validate_image(pixels,stored_mips);
            target.image = image(pixels.width,pixels.height,VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 stored_mips ? std::uint32_t(pixels.mip_levels.size()+1) : 1);
            target.staging = buffer(pixels.rgba.size()+(stored_mips ? pixels.mip_rgba.size() : 0),
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,host_memory);
            upload_texture(target,pixels,stored_mips);
        };
        target.native_source = native;
        if (!native) { create_rgba(source); return; }
        const auto logical_width = native->runtime ? native->runtime->source_width : native->width;
        const auto logical_height = native->runtime ? native->runtime->source_height : native->height;
        if (logical_width != source.width || logical_height != source.height || native->face_count != source.face_count)
            throw std::runtime_error("Original UI GPU record is not aligned with its logical bitmap slot");
        if (native->runtime) {
            const auto& runtime = *native->runtime;
            if (!runtime.nominal_width || !runtime.nominal_height)
                throw std::runtime_error("Original UI nominal bitmap dimensions are unresolved");
            target.uv_scale = {float(logical_width)/float(runtime.nominal_width),
                               float(logical_height)/float(runtime.nominal_height)};
        }
        const auto format = gpu_format(native->format);
        const auto bytes = gpu_staging_size(*native,format);
        target.precision_fallback = native->implementation_precision_fallback;
        if (gpu_format_supported(format)) {
            target.image = image(native->width,native->height,format.format,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 std::uint32_t(native->levels.size()));
            target.staging = buffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,host_memory);
            upload_gpu_texture(target,*native);
        } else {
            target.precision_fallback = true; // Explicit device-capability fallback, once at cache creation.
            const auto decoded = assets::decode_gpu_image(*native);
            create_rgba(decoded);
        }
    }
    void cache_images(Cache& cache, std::span<const assets::Image> images,
                      std::span<const std::uint64_t> revisions, bool dual_filter, bool stored_mips = true,
                      std::span<const assets::GpuImage* const> native_images = {}) {
        if (!revisions.empty() && revisions.size() != images.size())
            throw std::runtime_error("Original image/revision cache mismatch");
        if (!native_images.empty() && native_images.size() != images.size())
            throw std::runtime_error("Original UI GPU records are not aligned with bitmap slots");
        if (cache.source) {
            if (cache.source != images.data() || cache.textures.size() != images.size())
                throw std::runtime_error("Original immutable image cache identity changed");
            for (std::size_t i=0; i<images.size(); ++i) {
                auto& texture = cache.textures[i];
                const auto* native = native_images.empty() ? nullptr : native_images[i];
                if (texture.native_source != native)
                    throw std::runtime_error("Original immutable UI GPU cache identity changed");
                if (!texture.valid || (!revisions.empty() && texture.revision != revisions[i])) {
                    if (native) {
                        if (texture.valid && texture.precision_fallback)
                            throw std::runtime_error("Immutable source GPU precision fallback became a dynamic image");
                        if (texture.image.format == gpu_format(native->format).format)
                            upload_gpu_texture(texture,*native);
                        else {
                            const auto decoded = assets::decode_gpu_image(*native);
                            upload_texture(texture,decoded,stored_mips);
                        }
                    } else upload_texture(texture,images[i],stored_mips);
                    texture.prepared_revision = revisions.empty() ? 0 : revisions[i];
                    texture.pending = true;
                }
            }
            return;
        }
        if (images.empty()) return;
        const auto multiplier = dual_filter ? 2u : 1u;
        if (images.size() > UINT32_MAX / (12 * multiplier))
            throw std::runtime_error("Too many original UI/effect textures");
        const auto previous_counts = ui_image_counts;
        try {
            cache.textures.resize(images.size());
            cache.pool = descriptor_pool(std::uint32_t(images.size()) * multiplier);
            for (std::size_t i = 0; i < images.size(); ++i) {
                const auto& source = images[i]; auto& target = cache.textures[i];
                initialize_texture(target,source,stored_mips,native_images.empty() ? nullptr : native_images[i]);
                if (dual_filter) {
                    const auto format = target.image.format;
                    if (format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format == VK_FORMAT_BC2_UNORM_BLOCK ||
                        format == VK_FORMAT_BC3_UNORM_BLOCK) ++ui_image_counts[0];
                    else if (format == VK_FORMAT_R5G6B5_UNORM_PACK16 || format == VK_FORMAT_A1R5G5B5_UNORM_PACK16)
                        ++ui_image_counts[1];
                    else ++ui_image_counts[2];
                    if (target.precision_fallback) ++ui_image_counts[3];
                }
                target.prepared_revision = revisions.empty() ? 0 : revisions[i]; target.pending = true;
                target.sets[0] = allocate_set(cache.pool); write_set(target.sets[0],target.image.view,linear_sampler);
                if (dual_filter) {
                    target.sets[1] = allocate_set(cache.pool); write_set(target.sets[1],target.image.view,nearest_sampler);
                }
            }
            cache.source = images.data();
        } catch (...) { destroy(cache); ui_image_counts = previous_counts; throw; }
    }
    VkShaderModule shader(const char* name) {
        const auto path = c.shader_directory / name;
        std::ifstream file(path,std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot load source Vulkan shader " + path.string());
        const auto bytes = file.tellg();
        if (bytes <= 0 || bytes % 4 != 0) throw std::runtime_error("Invalid source SPIR-V shader");
        std::vector<std::uint32_t> code(std::size_t(bytes)/4);
        file.seekg(0); file.read(reinterpret_cast<char*>(code.data()),bytes);
        if (!file) throw std::runtime_error("Cannot read source SPIR-V shader");
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = std::size_t(bytes); info.pCode = code.data();
        VkShaderModule result{}; check(vkCreateShaderModule(c.device,&info,nullptr,&result),"create source shader module");
        return result;
    }
    VkPipeline pipeline(VkRenderPass pass, bool compose, bool overlay, bool effect,
                        std::uint32_t mode, bool transparent = false, bool additive = false,
                        bool depth_write = false, bool hud_mesh = false, std::uint8_t cull = 1) {
        const VkShaderModule vertex = compose ? fullscreen_vertex : raster_vertex;
        const VkShaderModule fragment = compose ? compose_fragment : raster_fragment;
        VkPipeline result{};
        try {
            const std::array<std::uint32_t,4> values{
                std::uint32_t(overlay && !hud_mesh),std::uint32_t(effect),0,std::uint32_t(hud_mesh)};
            const std::array<VkSpecializationMapEntry,4> entries{{{1,0,4},{3,4,4},{4,8,4},{5,12,4}}};
            const VkSpecializationInfo mesh_constants{4,entries.data(),sizeof(values),values.data()};
            const VkSpecializationMapEntry compose_entry{2,0,sizeof(mode)};
            const VkSpecializationInfo compose_constants{1,&compose_entry,sizeof(mode),&mode};
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[0].module = vertex; stages[1].module = fragment;
            stages[0].pName = stages[1].pName = "main";
            stages[0].pSpecializationInfo = stages[1].pSpecializationInfo = compose ? &compose_constants : &mesh_constants;
            const std::array<VkVertexInputBindingDescription,2> bindings{{
                {0,std::uint32_t(hud_mesh ? sizeof(assets::Vertex) : sizeof(game::EffectVertex)),VK_VERTEX_INPUT_RATE_VERTEX},
                {1,0,VK_VERTEX_INPUT_RATE_VERTEX}}};
            const std::array<VkVertexInputAttributeDescription,6> attributes{{
                {0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32_SFLOAT,12},
                {2,hud_mesh ? 0u : 1u,VK_FORMAT_R32G32B32_SFLOAT,hud_mesh ? 20u : 0u},
                {3,1,VK_FORMAT_R32G32_SFLOAT,0}, {4,1,VK_FORMAT_R32G32B32A32_SFLOAT,0},
                {5,hud_mesh ? 1u : 0u,VK_FORMAT_R32G32B32A32_SFLOAT,hud_mesh ? 0u : 20u}}};
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            if (!compose) { input.vertexBindingDescriptionCount = std::uint32_t(bindings.size()); input.pVertexBindingDescriptions = bindings.data();
                input.vertexAttributeDescriptionCount = std::uint32_t(attributes.size()); input.pVertexAttributeDescriptions = attributes.data(); }
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
            if (hud_mesh) {
                // Original visual MDL indices retain source order. After the
                // shader's GL Y flip, D3DCULL_CCW survives Vulkan clockwise faces.
                raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
                raster.cullMode = cull == 1 ? VK_CULL_MODE_NONE : cull == 2 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
            }
            VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable = effect || hud_mesh;
            depth.depthWriteEnable = (effect || hud_mesh) && depth_write; depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blending{};
            blending.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blending.blendEnable = transparent;
            blending.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blending.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blending.colorBlendOp = blending.alphaBlendOp = VK_BLEND_OP_ADD;
            blending.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blending.dstAlphaBlendFactor = blending.dstColorBlendFactor;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = 1; blend.pAttachments = &blending;
            const std::array<VkDynamicState,2> dynamics{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics.data();
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2; info.pStages = stages.data(); info.pVertexInputState = &input; info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &samples;
            info.pDepthStencilState = &depth; info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
            info.layout = c.pipeline_layout; info.renderPass = pass;
            check(vkCreateGraphicsPipelines(c.device,c.pipeline_cache,1,&info,nullptr,&result),"create source composition pipeline");
        } catch (...) { if (result) vkDestroyPipeline(c.device,result,nullptr); throw; }
        return result;
    }
    VkRenderPass render_pass(VkFormat depth_format = VK_FORMAT_UNDEFINED,
                             VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM, bool private_ui = false) {
        std::array<VkAttachmentDescription,2> attachments{};
        auto& color = attachments[0]; color.format = color_format; color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = private_ui ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = private_ui ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkAttachmentReference color_ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth_ref{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        const bool has_depth = depth_format != VK_FORMAT_UNDEFINED;
        auto& depth = attachments[1]; depth.format = depth_format; depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = private_ui ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = private_ui ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = private_ui ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color_ref;
        if (has_depth) subpass.pDepthStencilAttachment = &depth_ref;
        std::array<VkSubpassDependency,2> dependencies{{
            {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                0}, // Bloom reads other pixels and differently sized producer attachments.
            {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,0}}};
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = has_depth ? 2 : 1; info.pAttachments = attachments.data();
        info.subpassCount = 1; info.pSubpasses = &subpass; info.dependencyCount = 2; info.pDependencies = dependencies.data();
        VkRenderPass result{}; check(vkCreateRenderPass(c.device,&info,nullptr,&result),"create source composition render pass"); return result;
    }
    VkFormat ui_depth_format() const {
        for (const auto format : {VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT}) {
            VkFormatProperties properties; vkGetPhysicalDeviceFormatProperties(c.physical,format,&properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
        }
        throw std::runtime_error("Source HUD requires a supported private depth/stencil attachment");
    }
    void create_target(Target& target, VkExtent2D size, VkRenderPass pass, VkImageView depth = VK_NULL_HANDLE,
                       VkFormat format = VK_FORMAT_R8G8B8A8_UNORM) {
        target.image = image(size.width,size.height,format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        const std::array<VkImageView,2> views{target.image.view,depth};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; info.renderPass = pass;
        info.attachmentCount = depth ? 2 : 1; info.pAttachments = views.data();
        info.width = size.width; info.height = size.height; info.layers = 1;
        check(vkCreateFramebuffer(c.device,&info,nullptr,&target.framebuffer),"create source composition framebuffer");
    }
    void destroy_extent() noexcept {
        for (auto& p : compose_pipelines) { if (p) vkDestroyPipeline(c.device,p,nullptr); p = {}; }
        for (auto& p : effect_pipelines) { if (p) vkDestroyPipeline(c.device,p,nullptr); p = {}; }
        for (auto& p : hud_pipelines) { if (p) vkDestroyPipeline(c.device,p,nullptr); p = {}; }
        if (scene_copy) vkDestroyPipeline(c.device,scene_copy,nullptr); scene_copy = {};
        if (swap_copy) vkDestroyPipeline(c.device,swap_copy,nullptr); swap_copy = {};
        if (ui_pipeline) vkDestroyPipeline(c.device,ui_pipeline,nullptr); ui_pipeline = {};
        if (ui_framebuffer) vkDestroyFramebuffer(c.device,ui_framebuffer,nullptr); ui_framebuffer = {};
        destroy(ui_depth);
        destroy(scene); destroy(final_scene); for (auto& target : bloom) destroy(target);
        if (scene_pass) vkDestroyRenderPass(c.device,scene_pass,nullptr); scene_pass = {};
        if (color_pass) vkDestroyRenderPass(c.device,color_pass,nullptr); color_pass = {};
        if (ui_pass) vkDestroyRenderPass(c.device,ui_pass,nullptr); ui_pass = {};
        swap_pass = {}; extent = {};
    }
    void resize(VkExtent2D size, VkImageView depth, VkFormat depth_format, VkFormat swap_format, VkRenderPass swap,
                VkFormat format) {
        if (!size.width || !size.height || !depth || depth_format == VK_FORMAT_UNDEFINED || !swap ||
            swap_format == VK_FORMAT_UNDEFINED ||
            (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R16G16B16A16_SFLOAT))
            throw std::runtime_error("Invalid source composition extent/attachments");
        destroy_extent();
        try {
            extent = size; swap_pass = swap; scene_format = format;
            scene_pass = render_pass(depth_format,scene_format); color_pass = render_pass();
            create_target(scene,extent,scene_pass,depth,scene_format); create_target(final_scene,extent,color_pass);
            const auto ui_format = ui_depth_format();
            ui_pass = render_pass(ui_format,VK_FORMAT_R8G8B8A8_UNORM,true);
            ui_depth = image(extent.width,extent.height,ui_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                             1,VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
            const std::array<VkImageView,2> ui_views{final_scene.image.view,ui_depth.view};
            VkFramebufferCreateInfo ui_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            ui_info.renderPass = ui_pass; ui_info.attachmentCount = 2; ui_info.pAttachments = ui_views.data();
            ui_info.width = extent.width; ui_info.height = extent.height; ui_info.layers = 1;
            check(vkCreateFramebuffer(c.device,&ui_info,nullptr,&ui_framebuffer),"create source HUD framebuffer");
            for (auto& target : bloom) create_target(target,{256,256},color_pass);
            scene_copy = pipeline(scene_pass,true,false,false,0);
            swap_copy = pipeline(swap_pass,true,false,false,0);
            for (std::uint32_t mode = 0; mode < compose_pipelines.size(); ++mode)
                compose_pipelines[mode] = pipeline(mode == 5 ? swap_pass : color_pass,true,false,false,mode);
            ui_pipeline = pipeline(ui_pass,false,true,false,0,true);
            for (std::uint32_t blend = 0; blend < 2; ++blend)
                for (std::uint8_t cull = 1; cull <= 3; ++cull)
                    hud_pipelines[blend*3+cull-1] = pipeline(ui_pass,false,true,false,0,blend != 0,
                                                            false,blend == 0,true,cull);
            for (std::uint32_t i = 0; i < effect_pipelines.size(); ++i)
                effect_pipelines[i] = pipeline(scene_pass,false,false,true,0,(i & 1) != 0,(i & 2) != 0,(i & 4) != 0);
        } catch (...) { destroy_extent(); throw; }
    }
    void write_ui_mesh_sets(UiMesh& target) {
        if (target.image_index >= ui_cache.textures.size())
            throw std::runtime_error("Original HUD material image outside prepared source cache");
        const auto view = ui_cache.textures[target.image_index].image.view;
        write_set(target.sets[0],view,linear_sampler,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_NULL_HANDLE,&target);
        write_set(target.sets[1],view,nearest_sampler,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_NULL_HANDLE,&target);
    }
    void prepare_ui_meshes(const ui::UiFrame& frame) {
        const auto meshes = frame.prepared_meshes.empty() ? frame.meshes : frame.prepared_meshes;
        if (meshes.size() > ui_meshes.size())
            throw std::runtime_error("Original HUD exceeds two source view entities");
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            const auto& source = meshes[i]; auto& target = ui_meshes[i];
            if (source.vertices.empty() || source.indices.empty() || source.indices.size() > UINT32_MAX ||
                !source.material_state || !source.lighting || source.image_index >= ui_cache.textures.size())
                throw std::runtime_error("Original HUD geometry/material/lighting is not prepared");
            const auto& state = *source.material_state;
            if (state.technique != MaterialTechnique::FixedFunction || !state.complete ||
                source.lighting->status[0] == 0 || source.lighting->options[1] == 0)
                throw std::runtime_error("Original HUD source material/RGB lighting/draw alpha is unresolved");
            if (source.source_cull_mode < 1 || source.source_cull_mode > 3 ||
                !state.raster.depth_test || state.raster.depth_write == state.raster.blend ||
                (state.raster.blend && (state.raster.source_blend != BlendFactor::SourceAlpha ||
                                       state.raster.destination_blend != BlendFactor::InverseSourceAlpha)))
                throw std::runtime_error("Original HUD raster state is outside the recovered source VIEW variants");
            if (!target.source_vertices) {
                for (auto index : source.indices) if (index >= source.vertices.size())
                    throw std::runtime_error("Original HUD model vertex index out of range");
                try {
                    target.vertices = buffer(source.vertices.size_bytes(),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    target.staging = buffer(source.vertices.size_bytes(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,host_memory);
                    target.indices = buffer(source.indices.size_bytes(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,host_memory);
                    std::memcpy(target.indices.mapped,source.indices.data(),source.indices.size_bytes());
                    target.globals = buffer(sizeof(VulkanGlobals),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,host_memory);
                    target.material = buffer(sizeof(VulkanMaterial),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,host_memory);
                    target.lighting = buffer(sizeof(SourceLightingUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,host_memory);
                    std::memset(target.globals.mapped,0,sizeof(VulkanGlobals));
                    std::memset(target.material.mapped,0,sizeof(VulkanMaterial));
                    std::memset(target.lighting.mapped,0,sizeof(SourceLightingUniforms));
                    target.pool = descriptor_pool(2);
                    for (auto& set : target.sets) set = allocate_set(target.pool);
                    target.vertex_count = source.vertices.size(); target.index_count = source.indices.size();
                    target.source_indices = source.indices.data();
                    target.source_vertices = source.vertices.data();
                } catch (...) { destroy(target); throw; }
            } else if (target.source_vertices != source.vertices.data() || target.source_indices != source.indices.data() ||
                       target.vertex_count != source.vertices.size() || target.index_count != source.indices.size()) {
                throw std::runtime_error("Original HUD immutable model cache identity changed");
            }
            if (!target.valid || target.revision != source.pose_revision) {
                upload_buffer(target.vertices,target.staging,source.vertices.data(),source.vertices.size_bytes(),
                              VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
                target.prepared_revision = source.pose_revision; target.pending = true;
            }
            VulkanGlobals globals{};
            globals.view_projection = source.view_projection;
            globals.world_to_view = source.lighting->world_to_view;
            globals.view_position = source.lighting->view_position;
            globals.ambient = source.lighting->global_ambient;
            globals.fog_color = source.lighting->fog_color; globals.fog_params = source.lighting->fog_params;
            const auto update = [](Buffer& buffer, const auto& value) {
                if (std::memcmp(buffer.mapped,&value,sizeof(value)) != 0)
                    std::memcpy(buffer.mapped,&value,sizeof(value));
            };
            auto material = vulkan_material_data(state);
            material.config[1] = 1; // The actual source HUD skin at entSkin1.
            material.config[3] = 0; // These source view entities have no tangent/UV1 stream.
            material.extra[2] = 0; // Original HUD never opts into native modern PBR.
            auto lighting = *source.lighting;
            lighting.material_params[1] = source.color_multiplier;
            update(target.globals,globals); update(target.material,material); update(target.lighting,lighting);
            if (target.image_index != source.image_index) {
                target.image_index = source.image_index; write_ui_mesh_sets(target);
            }
        }
    }
    void prepare(const FrameSnapshot& frame) {
        // The previous frame has completed. Abandoned/unsubmitted preparation is
        // retried from committed GPU revisions; no borrowed pixel data is retained.
        discard_uploads();
        try { prepare_frame(frame); prepared = true; }
        catch (...) { discard_uploads(); throw; }
    }
    void prepare_frame(const FrameSnapshot& frame) {
        if (!scene.framebuffer) throw std::runtime_error("Source composition prepare before resize");
        if (frame.overlay) {
            cache_images(ui_cache,frame.overlay->images,frame.overlay->image_revisions,true,true,frame.overlay->gpu_images);
            prepare_ui_meshes(*frame.overlay);
        }
        if (frame.effects) {
            const auto& effects = *frame.effects;
            // Original BMAP constructor requests exactly one GPU level, regardless
            // of world-texture quality or any lower levels in a decoded Image.
            cache_images(effect_cache,effects.textures,{},false,false);
            const auto selected = effects.source_anisotropy_raw != 0 && source_anisotropic_sampler
                ? source_anisotropic_sampler : linear_sampler;
            if (selected != effect_sampler_selected) {
                for (auto& texture : effect_cache.textures)
                    write_set(texture.sets[0],texture.image.view,selected);
                effect_sampler_selected = selected;
            }
            if (effects.vertices.size() > game::effect_vertex_capacity || effects.indices.size() > game::effect_index_capacity)
                throw std::runtime_error("Original effect geometry exceeds fixed particle pool");
            if (!effects_valid || effects_revision != effects.revision) {
                for (auto index : effects.indices) if (index >= effects.vertices.size())
                    throw std::runtime_error("Original effect vertex index out of range");
                upload_buffer(effect_vertices,effect_vertex_staging,effects.vertices.data(),effects.vertices.size_bytes(),VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
                upload_buffer(effect_indices,effect_index_staging,effects.indices.data(),effects.indices.size_bytes(),VK_ACCESS_INDEX_READ_BIT);
                prepared_effects_revision = effects.revision; effects_prepared = true;
            }
        }
        if (frame.gamma) {
            const auto& values = frame.gamma->corrected_active ? frame.gamma->corrected : frame.gamma->defaults;
            if (!gamma_valid || values != gamma_values) {
                auto* rgba = static_cast<std::uint16_t*>(gamma_staging.mapped);
                for (std::size_t i = 0; i < 256; ++i) {
                    rgba[i*4] = values[0][i]; rgba[i*4+1] = values[1][i]; rgba[i*4+2] = values[2][i]; rgba[i*4+3] = UINT16_MAX;
                }
                copy_image(gamma,gamma_staging,0,0,256,1,gamma_staging.size);
                prepared_gamma_values = values; gamma_pending = true;
            }
        }
        if (frame.bloom && frame.bloom->enabled) {
            const VkExtent2D size{frame.bloom->width,frame.bloom->height};
            if (!size.width || !size.height) throw std::runtime_error("Invalid original Bloom dimensions");
            if (bloom[0].image.width != size.width || bloom[0].image.height != size.height) {
                std::array<Target,3> replacement{};
                try { for (auto& target : replacement) create_target(target,size,color_pass); }
                catch (...) { for (auto& target : replacement) destroy(target); throw; }
                for (auto& target : bloom) destroy(target);
                bloom = replacement;
            }
        }
    }
    void begin_pass(VkCommandBuffer command, VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D size) {
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = pass; begin.framebuffer = framebuffer; begin.renderArea = {{0,0},size};
        vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0,0,float(size.width),float(size.height),0,1};
        const VkRect2D scissor{{0,0},size};
        vkCmdSetViewport(command,0,1,&viewport); vkCmdSetScissor(command,0,1,&scissor);
    }
    void bind(VkCommandBuffer command, VkPipeline pipeline, VkDescriptorSet set, const VulkanDraw& draw) {
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,c.pipeline_layout,0,1,&set,0,nullptr);
        vkCmdPushConstants(command,c.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(draw),&draw);
    }
    void fullscreen(VkCommandBuffer command, VkPipeline pipeline, VkDescriptorSet set) {
        bind(command,pipeline,set,VulkanDraw{}); vkCmdDraw(command,3,1,0,0); ++diag.post_passes;
    }
    void geometry(VkCommandBuffer command, const Buffer& vertices, const Buffer& indices) {
        const std::array<VkBuffer,2> buffers{vertices.handle,zero.handle};
        const std::array<VkDeviceSize,2> offsets{};
        vkCmdBindVertexBuffers(command,0,2,buffers.data(),offsets.data());
        vkCmdBindIndexBuffer(command,indices.handle,0,VK_INDEX_TYPE_UINT32);
    }
    void draw_effects(VkCommandBuffer command, const game::EffectFrame& frame) {
        if (effects_prepared ? prepared_effects_revision != frame.revision : !effects_valid || effects_revision != frame.revision)
            throw std::runtime_error("Original effects were not prepared");
        geometry(command,effect_vertices,effect_indices);
        VulkanDraw push; push.model = identity();
        for (const auto& batch : frame.batches) {
            if (batch.texture_index >= effect_cache.textures.size() || batch.first_index > frame.indices.size() ||
                batch.index_count > frame.indices.size() - batch.first_index)
                throw std::runtime_error("Original effect batch outside cached geometry/textures");
            if (!batch.index_count) continue;
            push.config[1] = batch.alpha_test ? 1 : 0;
            push.params[3] = 100.0f / 255.0f; // C4.wdl d3d_alpharef=100; D3DCMP_GREATEREQUAL.
            const auto variant = std::uint32_t(batch.transparent) | (std::uint32_t(batch.additive) << 1) | (std::uint32_t(batch.depth_write) << 2);
            bind(command,effect_pipelines[variant],effect_cache.textures[batch.texture_index].sets[0],push);
            vkCmdDrawIndexed(command,batch.index_count,1,batch.first_index,0,0); ++diag.effect_draw_calls;
        }
    }
    void draw_quad(VkCommandBuffer command, const ui::Quad& quad, const ui::Viewport& fit) {
        if (quad.image_index >= ui_cache.textures.size()) throw std::runtime_error("Original UI texture index out of range");
        if (quad.rect[2] <= 0 || quad.rect[3] <= 0) return;
        geometry(command,ui_vertices,ui_indices);
        const VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};
        vkCmdSetViewport(command,0,1,&viewport);
        const float width = float(extent.width), height = float(extent.height);
        const float co = std::cos(quad.rotation), si = std::sin(quad.rotation);
        const float w = quad.rect[2]*fit.scale, h = quad.rect[3]*fit.scale;
        const float cx = fit.x+(quad.rect[0]+quad.rect[2]*0.5f)*fit.scale;
        const float cy = fit.y+(quad.rect[1]+quad.rect[3]*0.5f)*fit.scale;
        VulkanDraw push;
        push.model[0] = 2*co*w/width; push.model[1] = 2*si*w/height;
        push.model[4] = -2*si*h/width; push.model[5] = 2*co*h/height;
        push.model[10] = push.model[15] = 1;
        push.model[12] = 2*cx/width-1-0.5f*(push.model[0]+push.model[4]);
        push.model[13] = 2*cy/height-1-0.5f*(push.model[1]+push.model[5]);
        push.diffuse = quad.color; push.uv = quad.uv;
        const auto& uv_scale = ui_cache.textures[quad.image_index].uv_scale;
        push.uv[0] *= uv_scale[0]; push.uv[2] *= uv_scale[0];
        push.uv[1] *= uv_scale[1]; push.uv[3] *= uv_scale[1];
        bind(command,ui_pipeline,ui_cache.textures[quad.image_index].sets[quad.filtered ? 0 : 1],push);
        vkCmdDrawIndexed(command,6,1,0,0,0); ++diag.ui_draw_calls;
    }
    void draw_ui_mesh(VkCommandBuffer command, const ui::OverlayMesh& mesh, const ui::Viewport& fit) {
        const auto cached = std::find_if(ui_meshes.begin(),ui_meshes.end(),
            [&](const UiMesh& candidate) { return candidate.source_vertices == mesh.vertices.data(); });
        if (cached == ui_meshes.end() || !mesh.material_state ||
            (cached->pending ? cached->prepared_revision != mesh.pose_revision : !cached->valid || cached->revision != mesh.pose_revision) ||
            cached->image_index != mesh.image_index || mesh.source_cull_mode < 1 || mesh.source_cull_mode > 3)
            throw std::runtime_error("Original HUD material/pose was not prepared");
        geometry(command,cached->vertices,cached->indices);
        const VkViewport viewport{fit.x,fit.y,fit.width,fit.height,0,1};
        vkCmdSetViewport(command,0,1,&viewport);
        const auto& state = *mesh.material_state;
        const auto& raster = state.raster;
        VulkanDraw push;
        push.model = mesh.model_to_view;
        const auto& uv_scale = ui_cache.textures[mesh.image_index].uv_scale;
        push.uv = {0,0,uv_scale[0],uv_scale[1]};
        push.diffuse = state.diffuse; push.config[2] = mesh.source_flags; push.config[3] = 1;
        push.config[1] = raster.color_key ? 2 : raster.alpha_comparison == AlphaComparison::GreaterEqual ? 1 : 0;
        push.params[2] = raster.blend ? state.entity_alpha : 1;
        push.params[3] = raster.alpha_reference;
        const auto variant = std::uint32_t(raster.blend)*3+mesh.source_cull_mode-1;
        bind(command,hud_pipelines[variant],cached->sets[mesh.filtered ? 0 : 1],push);
        vkCmdDrawIndexed(command,std::uint32_t(mesh.indices.size()),1,0,0,0);
        ++diag.ui_draw_calls; ++diag.ui_mesh_draw_calls;
    }
    void draw_ui(VkCommandBuffer command, const ui::UiFrame& frame) {
        if (!std::isfinite(frame.canvas_width) || !std::isfinite(frame.canvas_height) ||
            frame.canvas_width <= 0 || frame.canvas_height <= 0)
            throw std::runtime_error("Original UI requires a positive finite authored canvas");
        const auto fit = ui::Viewport::fit(int(extent.width),int(extent.height),
                                          frame.canvas_width,frame.canvas_height);
        const auto x = std::int32_t(std::floor(fit.x)), y = std::int32_t(std::floor(fit.y));
        const auto right = std::min(extent.width,std::uint32_t(std::ceil(fit.x+fit.width)));
        const auto bottom = std::min(extent.height,std::uint32_t(std::ceil(fit.y+fit.height)));
        const VkRect2D scissor{{x,y},{right-std::uint32_t(x),bottom-std::uint32_t(y)}};
        vkCmdSetScissor(command,0,1,&scissor);
        const auto before = [](const auto& a, const auto& b) noexcept {
            return a.layer < b.layer || (a.layer == b.layer && a.source_order > b.source_order);
        };
        for (std::size_t i=1; i<frame.quads.size(); ++i)
            if (before(frame.quads[i],frame.quads[i-1]))
                throw std::runtime_error("Original UI quads are not source-node ordered");
        for (std::size_t i=1; i<frame.meshes.size(); ++i)
            if (before(frame.meshes[i],frame.meshes[i-1]))
                throw std::runtime_error("Original HUD meshes are not source-node ordered");
        std::size_t quad = 0, mesh = 0;
        int last_mesh_layer{}; bool have_mesh_layer = false;
        while (quad < frame.quads.size() || mesh < frame.meshes.size()) {
            if (mesh < frame.meshes.size() &&
                (quad == frame.quads.size() || !before(frame.quads[quad],frame.meshes[mesh]))) {
                const auto& source = frame.meshes[mesh];
                if (!have_mesh_layer || last_mesh_layer != source.layer) {
                    VkClearAttachment clear{}; clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    clear.clearValue.depthStencil = {1.0f,0};
                    const VkClearRect rectangle{{{0,0},extent},0,1};
                    vkCmdClearAttachments(command,1,&clear,1,&rectangle);
                    last_mesh_layer = source.layer; have_mesh_layer = true;
                }
                draw_ui_mesh(command,source,fit); ++mesh;
            } else {
                draw_quad(command,frame.quads[quad],fit); ++quad;
            }
        }
    }
    void record(const FrameSnapshot& frame, VkCommandBuffer command, VkImageView base, VkImageLayout layout,
                VkFramebuffer swap_framebuffer, VkImageView position, VkImageView normal) {
        if (!scene.framebuffer || !base || !swap_framebuffer || !command || !uploads_recorded)
            throw std::runtime_error("Invalid source composition recording inputs");
        if (frame.gamma && !gamma_valid && !gamma_pending) throw std::runtime_error("Original gamma was not prepared");
        diag = {};
        diag.upload_buffer_copies = upload_buffer_copies; diag.upload_image_copies = upload_image_copies;
        diag.upload_bytes = upload_bytes; // Actual vkCmdCopy payload, not staging allocation/padding.
        diag.ui_bc_images = ui_image_counts[0]; diag.ui_packed16_images = ui_image_counts[1];
        diag.ui_rgba8_images = ui_image_counts[2]; diag.ui_precision_fallback_images = ui_image_counts[3];
        if (frame.effects) {
            diag.source_particle_anisotropy_raw = frame.effects->source_anisotropy_raw;
            const bool requested = frame.effects->source_anisotropy_raw != 0;
            if (requested) {
                const float source_value = float(double(frame.effects->source_anisotropy_raw)/1024.0);
                diag.source_particle_anisotropy_dword = std::bit_cast<std::uint32_t>(source_value);
                diag.source_particle_sampler_anisotropic = source_anisotropic_sampler != VK_NULL_HANDLE;
                diag.source_particle_sampler_anisotropy = source_anisotropic_sampler ? c.max_sampler_anisotropy : 1;
                if (!source_anisotropic_sampler) {
                    diag.source_effect_sampler_unresolved = true;
                    diag.source_effect_sampler_reason = "Original particle ANO requested but native samplerAnisotropy is unavailable/not enabled; CLAMP LINEAR fallback";
                } else {
                    // Original writes IEEE float bits as a huge DWORD. Native uses
                    // its legal capability bound; the observed equivalence is not
                    // a universal original-driver or internal tap-count assertion.
                    diag.source_effect_sampler_reason = "Original particle MAX request maps to the native device anisotropy cap; approved bounded driver observation, not universal source-device parity";
                }
            }
        }
        write_set(pass_sets[0],base,linear_sampler,layout);
        begin_pass(command,scene_pass,scene.framebuffer,extent);
        fullscreen(command,scene_copy,pass_sets[0]);
        if (frame.effects) draw_effects(command,*frame.effects);
        vkCmdEndRenderPass(command); scene.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Output input{scene.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0};
        if (postprocess) {
            input = postprocess(post_user,frame,command,scene.image.view,position,normal);
            if (!input.view || (input.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && input.layout != VK_IMAGE_LAYOUT_GENERAL))
                throw std::runtime_error("Native quality returned invalid composition output");
            diag.post_passes += input.post_passes;
        }
        const bool enabled = frame.bloom && frame.bloom->enabled;
        if (enabled) {
            write_set(pass_sets[1],input.view,linear_sampler,input.layout);
            write_set(pass_sets[2],bloom[0].image.view,linear_sampler);
            write_set(pass_sets[3],bloom[1].image.view,linear_sampler);
            const VkExtent2D size{frame.bloom->width,frame.bloom->height};
            for (std::size_t i=0; i<bloom.size(); ++i) {
                if (!bloom[i].framebuffer || bloom[i].image.width != size.width || bloom[i].image.height != size.height)
                    throw std::runtime_error("Original Bloom dimensions were not prepared");
                begin_pass(command,color_pass,bloom[i].framebuffer,size);
                fullscreen(command,compose_pipelines[i+1],pass_sets[i+1]);
                ++diag.source_bloom_passes;
                vkCmdEndRenderPass(command); bloom[i].image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        write_set(pass_sets[4],input.view,linear_sampler,input.layout,enabled ? bloom[2].image.view : VK_NULL_HANDLE);
        begin_pass(command,color_pass,final_scene.framebuffer,extent);
        fullscreen(command,compose_pipelines[enabled ? 4 : 0],pass_sets[4]);
        if (enabled) ++diag.source_bloom_passes;
        vkCmdEndRenderPass(command); final_scene.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (frame.overlay && (!frame.overlay->quads.empty() || !frame.overlay->meshes.empty())) {
            begin_pass(command,ui_pass,ui_framebuffer,extent);
            draw_ui(command,*frame.overlay);
            vkCmdEndRenderPass(command);
        }
        write_set(pass_sets[5],final_scene.image.view,linear_sampler);
        begin_pass(command,swap_pass,swap_framebuffer,extent);
        if (frame.gamma) {
            fullscreen(command,compose_pipelines[5],pass_sets[5]); ++diag.gamma_passes;
        } else {
            fullscreen(command,swap_copy,pass_sets[5]);
        }
        vkCmdEndRenderPass(command);
    }
    void cleanup() noexcept {
        if (!c.device) return;
        destroy_extent(); destroy(ui_cache); destroy(effect_cache);
        if (raster_vertex) vkDestroyShaderModule(c.device,raster_vertex,nullptr); raster_vertex = {};
        if (fullscreen_vertex) vkDestroyShaderModule(c.device,fullscreen_vertex,nullptr); fullscreen_vertex = {};
        if (raster_fragment) vkDestroyShaderModule(c.device,raster_fragment,nullptr); raster_fragment = {};
        if (compose_fragment) vkDestroyShaderModule(c.device,compose_fragment,nullptr); compose_fragment = {};
        for (auto& mesh : ui_meshes) destroy(mesh);
        destroy(zero); destroy(ui_vertices); destroy(ui_indices);
        destroy(effect_vertices); destroy(effect_indices); destroy(effect_vertex_staging); destroy(effect_index_staging);
        destroy(gamma); destroy(gamma_staging);
        if (pass_pool) vkDestroyDescriptorPool(c.device,pass_pool,nullptr); pass_pool = {};
        if (linear_sampler) vkDestroySampler(c.device,linear_sampler,nullptr); linear_sampler = {};
        if (nearest_sampler) vkDestroySampler(c.device,nearest_sampler,nullptr); nearest_sampler = {};
        if (source_anisotropic_sampler) vkDestroySampler(c.device,source_anisotropic_sampler,nullptr);
        source_anisotropic_sampler = effect_sampler_selected = {};
    }
};

SourceComposite::SourceComposite(const Context& context) : impl_(std::make_unique<Impl>(context)) {}
SourceComposite::~SourceComposite() = default;
void SourceComposite::release_extent() noexcept { impl_->destroy_extent(); }
void SourceComposite::resize(VkExtent2D extent, VkImageView depth, VkFormat depth_format,
                             VkFormat swap_format, VkRenderPass swap_pass, VkFormat composed_format) {
    impl_->resize(extent,depth,depth_format,swap_format,swap_pass,composed_format);
}
void SourceComposite::prepare(const FrameSnapshot& frame) { impl_->prepare(frame); }
void SourceComposite::record_uploads(VkCommandBuffer command) { impl_->record_uploads(command); }
void SourceComposite::submitted() noexcept { impl_->submitted(); }
void SourceComposite::record(const FrameSnapshot& frame, VkCommandBuffer command, VkImageView base,
                             VkImageLayout layout, VkFramebuffer swap, VkImageView position, VkImageView normal) {
    impl_->record(frame,command,base,layout,swap,position,normal);
}
void SourceComposite::set_postprocess(void* user, Postprocess postprocess) noexcept {
    impl_->post_user = user; impl_->postprocess = postprocess;
}
void SourceComposite::set_auxiliary_descriptors(VkDescriptorBufferInfo quality, VkDescriptorImageInfo depth,
                                               VkDescriptorBufferInfo shadow, VkDescriptorBufferInfo matrices,
                                               VkDescriptorBufferInfo lighting) {
    impl_->auxiliary_descriptors(quality,depth,shadow,matrices,lighting);
}
VkImageView SourceComposite::composed_view() const noexcept { return impl_->scene.image.view; }
VkFormat SourceComposite::composed_format() const noexcept { return impl_->scene_format; }
SourceComposite::Diagnostics SourceComposite::diagnostics() const noexcept { return impl_->diag; }

} // namespace c4::rendering
