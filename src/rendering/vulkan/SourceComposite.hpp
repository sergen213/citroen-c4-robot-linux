#pragma once

#include "core/render_scene.hpp"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace c4::rendering {

// Borrowed handles outlive the helper. Resource changes/destruction require the
// previous frame fence. prepare() copies borrowed CPU data before frame recording.
class SourceComposite {
public:
    struct Context {
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkPipelineCache pipeline_cache{};
        VkDescriptorSetLayout descriptor_set_layout{}; // Shared source/quality shader ABI.
        VkPipelineLayout pipeline_layout{}; // Shared VulkanDraw push constants (128 bytes).
        VkBuffer globals_buffer{};
        VkDeviceSize globals_range{288};
        VkBuffer default_material_buffer{};
        VkDeviceSize default_material_offset{}, default_material_range{320};
        VkImageView neutral_2d_view{};
        VkSampler neutral_2d_sampler{};
        VkImageView neutral_cube_view{};
        VkSampler neutral_cube_sampler{};
        std::filesystem::path shader_directory;
        VkDescriptorBufferInfo quality_uniform{}; // b11; caller supplies valid disabled state in Original.
        VkDescriptorImageInfo shadow_depth{}; // b12; sampled depth-array, including neutral Original binding.
        VkDescriptorBufferInfo shadow_uniform{}; // b13.
        VkDescriptorBufferInfo quality_matrices{}; // b14.
        VkDescriptorBufferInfo lighting_uniform{}; // b15.
        bool bc_images_enabled{}; // Actual enabled VkDevice textureCompressionBC feature.
        bool sampler_anisotropy_enabled{}; // Actual enabled logical-device core feature.
        float max_sampler_anisotropy{1}; // Selected physical-device limit, never a preset number.
    };
    struct Output {
        VkImageView view{};
        VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::uint64_t post_passes{};
    };
    // Optional native-quality adapter. Input is composed_view(), already containing
    // source effects. Its returned color feeds Original Bloom, then UI and gamma.
    using Postprocess = Output (*)(void*, const FrameSnapshot&, VkCommandBuffer,
                                   VkImageView, VkImageView, VkImageView);
    struct Diagnostics {
        std::uint64_t ui_draw_calls{}, effect_draw_calls{}, post_passes{};
        std::uint64_t source_bloom_passes{}, gamma_passes{}, ui_mesh_draw_calls{};
        std::uint64_t ui_bc_images{}, ui_packed16_images{}, ui_rgba8_images{}, ui_precision_fallback_images{};
        std::uint64_t upload_buffer_copies{}, upload_image_copies{}, upload_bytes{}, upload_waits{};
        bool source_effect_sampler_unresolved{};
        std::string_view source_effect_sampler_reason{};
        std::int32_t source_particle_anisotropy_raw{};
        std::uint32_t source_particle_anisotropy_dword{}; // Encoded source request for nonzero raw, not driver readback.
        float source_particle_sampler_anisotropy{1};
        bool source_particle_sampler_anisotropic{};
    };

    explicit SourceComposite(const Context& context);
    ~SourceComposite();
    SourceComposite(const SourceComposite&) = delete;
    SourceComposite& operator=(const SourceComposite&) = delete;
    // After the fence, before destroying borrowed swap/depth attachments. Keeps
    // immutable textures, video staging, HUD poses and effect geometry cached.
    void release_extent() noexcept;
    void resize(VkExtent2D extent, VkImageView world_depth_view, VkFormat depth_format,
                VkFormat swap_format, VkRenderPass swap_render_pass,
                VkFormat composed_format = VK_FORMAT_R8G8B8A8_UNORM);
    // Success owns all transfer bytes; failure submits nothing and commits no GPU
    // revisions. A later prepare retries discarded/unsubmitted uploads.
    void prepare(const FrameSnapshot& frame);
    // Outside render passes, before any frame draws. Uses the caller's frame fence;
    // retained staging must not be changed until that fence signals.
    void record_uploads(VkCommandBuffer command);
    // Call only after the caller successfully submits that frame command.
    void submitted() noexcept;
    void record(const FrameSnapshot& frame, VkCommandBuffer command,
                VkImageView source_base_view, VkImageLayout source_base_layout,
                VkFramebuffer swap_framebuffer, VkImageView world_position_view,
                VkImageView world_normal_view);
    void set_postprocess(void* user, Postprocess postprocess) noexcept;
    // Refresh borrowed quality/shadow resources after their fenced resize. Updates
    // cached image descriptors as well as subsequently recorded fullscreen passes.
    void set_auxiliary_descriptors(VkDescriptorBufferInfo quality_uniform,
                                  VkDescriptorImageInfo shadow_depth,
                                  VkDescriptorBufferInfo shadow_uniform,
                                  VkDescriptorBufferInfo quality_matrices,
                                  VkDescriptorBufferInfo lighting_uniform);
    VkImageView composed_view() const noexcept;
    VkFormat composed_format() const noexcept;
    Diagnostics diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4::rendering
