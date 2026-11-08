#pragma once

#include "core/render_scene.hpp"
#include "rendering/quality/GraphicSettings.hpp"
#include "VulkanDispatch.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace c4::rendering {

// All features below must actually have been enabled at VkDevice creation.
// Missing capability leaves Original raster untouched; no CPU tracing fallback.
struct RayTracingContext {
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queue_family{};
    VkCommandPool command_pool{};
    bool buffer_device_address{}, acceleration_structure{}, ray_query{};
    bool runtime_descriptor_array{}, sampled_image_nonuniform_indexing{};
    // Dedicated capability query prepares real pipelines without enabling dispatch.
    bool initialize_for_capabilities{};
    // Enabled descriptorBindingSampledImageUpdateAfterBind for large source arrays.
    bool sampled_image_update_after_bind{};
    // Borrowed scene-owned cache; the scene outlives all RT pipelines.
    VkPipelineCache pipeline_cache{};
};
struct RayTracingMesh {
    VkBuffer vertices{}, indices{};
    VkDeviceAddress vertex_address{}, index_address{};
    std::uint32_t vertex_count{}, index_count{}, vertex_stride{};
    std::uint32_t position_offset{}, uv_offset{}, normal_offset{};
    // Same source/generated tangent cache used by Original raster shading.
    VkBuffer tangent_buffer{};
    VkDeviceAddress tangent_address{};
    std::uint32_t tangent_stride{}, tangent_offset{};
    VkBuffer uv1_buffer{};
    VkDeviceAddress uv1_address{};
    std::uint32_t uv1_stride{}, uv1_offset{};
};
// Sampled 2D views retain Original source-key/alpha preprocessing and sampling.
// The owner supplies a compatible 2D view for every descriptor, including cubes.
struct RayTracingTexture { VkImageView view{}; VkSampler sampler{}; };
struct RayTracingGBuffer {
    VkImageView color{}, world_position{}, normal{};
    // Native receiver vec4(metallic, roughness, reflectivity, discrete material ID).
    // Selected-SUN contribution only, in the same encoding as color.
    VkImageView material_receiver{}, sun_direct{};
    // Raster transitions all five to SHADER_READ_ONLY_OPTIMAL before record().
};
struct RayTracingEnvironment {
    VkImageView irradiance{}, prefiltered{};
    VkSampler sampler{};
    float max_lod{};
};
struct RayTracingOutput {
    VkImageView view{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::uint32_t effects{};
};
class RayTracing {
public:
    // Construct from stable source mesh buffers/topology, including a cold cache.
    // Actual source draw coverage gates readiness and the first recorded AS build.
    // The explicit asset-free capability query builds only a real zero-primitive TLAS.
    // Mesh buffers require AS_BUILD_INPUT_READ_ONLY + SHADER_DEVICE_ADDRESS +
    // STORAGE_BUFFER usage and DEVICE_ADDRESS memory. Source pose revisions refit.
    // Coverage consumes actual Instance.original_alpha and authored FX overrides:
    // opaque final alpha remains intersectable even when blending is enabled.
    RayTracing(RayTracingContext context, const RenderScene& scene,
               std::span<const RayTracingMesh> meshes,
               std::span<const RayTracingTexture> textures,
               std::filesystem::path shader_directory, std::uint32_t effects);
    ~RayTracing();
    RayTracing(const RayTracing&) = delete;
    RayTracing& operator=(const RayTracing&) = delete;
    // Call only after the frame fence. Extent/input-view changes invalidate history.
    void resize(VkExtent2D extent, RayTracingGBuffer inputs);
    // After the frame fence, refresh replacement views/samplers at stable indices.
    // Descriptor count is immutable; changed bindings reject history, not geometry.
    void update_textures(std::span<const RayTracingTexture> textures);
    // After the frame fence: prepared authored, linear-radiance cube views.
    // Empty views select a native approximation and internally valid fallback descriptors.
    void update_environment(RayTracingEnvironment environment);
    // Camera cuts can explicitly reject history; call before recording the cut.
    void invalidate_history() noexcept;
    // Called after previous frame fence and before recording this frame. Copies
    // real completed GPU counters and clears them through the coherent mapping.
    void frame_completed();
    // Records dirty-only TLAS builds, hardware queries and temporal/spatial filter.
    // camera_position is actual native world eye. HDR is an explicit optional
    // source-RGB decode; Original never acquires implicit gamma/tone mapping.
    // Off returns raw output; Standard dispatches one temporal/bilateral pass;
    // High dispatches temporal/bilateral plus two variance-aware a-trous passes.
    // high_ray_quality independently selects bounded 2/4/2 vs 1/1/1 ray budgets.
    RayTracingOutput record(const FrameSnapshot& frame, VkCommandBuffer command,
                            std::array<float, 3> camera_position,
                            quality::Denoising denoising = quality::Denoising::Standard,
                            bool high_ray_quality = false, bool input_hdr = false);
    bool supported() const noexcept;
    std::uint32_t enabled_effects() const noexcept;
    std::uint32_t available_effects() const noexcept;
    bool high_quality() const noexcept;
    bool input_hdr() const noexcept;
    std::uint32_t ray_budget_per_valid_pixel() const noexcept;
    std::uint32_t spatial_radius() const noexcept;
    // Configured blend weight, not measured history acceptance.
    float temporal_history_weight() const noexcept;
    quality::Denoising denoising_mode() const noexcept;
    std::uint32_t denoise_passes() const noexcept;
    bool temporal_denoise_available() const noexcept;
    bool high_quality_denoise_available() const noexcept;
    // Counters below describe completed work after frame_completed(), not estimates.
    std::uint64_t completed_queries() const noexcept;
    std::uint64_t blas_updates() const noexcept;
    std::uint64_t tlas_updates() const noexcept;
    // Current TLAS submitted triangle/instance counts, not the full hidden cache.
    std::uint64_t triangles() const noexcept;
    std::uint64_t instances() const noexcept;
    std::string_view fallback_reason() const noexcept;
    std::string diagnostics_json() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace c4::rendering
