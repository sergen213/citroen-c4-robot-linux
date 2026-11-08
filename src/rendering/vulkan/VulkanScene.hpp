#pragma once
#include "core/render_scene.hpp"
#include "rendering/quality/GraphicSettings.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <stdexcept>
#include <string_view>
struct SDL_Window;
namespace c4::rendering {
// Only loader/device/surface availability permits an implicit backend preference to fall back.
class VulkanUnavailableError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct VulkanBackendDiagnostics {
    std::string device_name, driver_name, driver_info, present_mode, rt_fallback_reason;
    std::string quality_fallback_reason, source_material_fallback_reason;
    std::string_view source_sprite_fallback_reason{};
    std::uint64_t sprite_private_flags_unknown{}, sprite_cached_euler_unknown{};
    std::uint32_t vendor_id{}, device_id{}, api_version{}, driver_version{}, rt_requested{}, rt_enabled{};
    std::uint32_t rt_supported_effects{};
    std::uint32_t rt_rays_per_valid_pixel{}, rt_denoise_radius{};
    bool rt_high_quality{};
    quality::Denoising rt_denoising{quality::Denoising::Off};
    std::uint32_t rt_denoise_passes{};
    bool rt_supported{}, validation_enabled{};
    std::uint64_t world_draw_calls{}, mesh_draw_calls{}, image_draw_calls{};
    // Last recorded frame: all raster draws, including shadows, composition and UI.
    std::uint64_t draw_calls{}, rt_triangles{}, rt_instances{}, rt_queries{}, validation_errors{}, frames{};
    bool gpu_timing_available{};
    std::uint64_t gpu_frame_ns{}, gpu_total_ns{}, gpu_timed_frames{}, ui_draw_calls{};
    std::uint64_t gpu_median_ns{}, gpu_p95_ns{}, gpu_distribution_samples{};
    std::uint64_t upload_submissions{}, upload_completed{}, upload_bytes{}, upload_backpressure_waits{}, upload_readiness_waits{}, upload_peak_pending{}, upload_peak_bytes{};
    std::uint64_t upload_gpu_total_ns{}, upload_gpu_samples{}, upload_gpu_median_ns{}, upload_gpu_p95_ns{};
    std::uint64_t frame_upload_copies{}, frame_upload_bytes{};
    std::uint64_t composition_upload_waits{};
    std::string pipeline_cache_path, pipeline_cache_load_status, pipeline_cache_save_status, pipeline_cache_error;
    std::uint64_t pipeline_cache_loaded_bytes{}, pipeline_cache_saved_bytes{}, pipeline_cache_load_ns{}, pipeline_cache_save_ns{};
    std::uint64_t pipeline_creation_median_ns{}, pipeline_creation_p95_ns{};
    std::uint64_t rt_blas_updates{}, rt_tlas_updates{};
    std::uint64_t msaa_resolves{}, material_draw_calls{}, incomplete_material_draw_calls{}, pbr_draw_calls{}, particle_draw_calls{}, shadow_draw_calls{}, post_passes{}, source_bloom_passes{}, gamma_passes{}, ui_mesh_draw_calls{};
    std::uint64_t generated_mip_images{}, generated_mip_levels{};
    std::uint64_t native_texture_images{}, bc_texture_images{}, packed_texture_images{}, texture_precision_fallbacks{};
    std::uint64_t incomplete_shadow_casters{};
    std::uint64_t ui_bc_images{}, ui_packed16_images{}, ui_rgba8_images{}, ui_precision_fallback_images{};
    std::int32_t source_particle_anisotropy_raw{};
    std::uint32_t source_particle_anisotropy_dword{};
    float source_particle_sampler_anisotropy{1};
    bool source_particle_sampler_anisotropic{}, source_effect_sampler_unresolved{};
    std::uint32_t color_attachment_format{}, depth_resolve_mode{};
};
class VulkanScene {
public:
    VulkanScene(SDL_Window*, bool vsync, std::uint32_t rt_effects = 0, bool capability_probe = false);
    ~VulkanScene();
    VulkanScene(const VulkanScene&) = delete;
    VulkanScene& operator=(const VulkanScene&) = delete;
    std::uint32_t max_texture_dimension() const noexcept;
    // Selected-device sampled RGBA8 mip support, not an observed legacy D3D CAPS word.
    bool source_mipmaps_supported() const noexcept;
    // The scene must outlive this renderer; retained same-queue uploads use this device.
    void prepare_scene(const RenderScene&);
    bool draw(const FrameSnapshot&, const std::filesystem::path& capture = {});
    void resize();
    bool set_vsync(bool);
    // Prepares real GPU resources synchronously; throws before a caller commits settings.
    // Does not draw, present, or advance the source scene. A zero drawable extent is an error.
    void configure_graphics(const quality::GraphicSettings&);
    const quality::GraphicsCapabilities& graphics_capabilities() const noexcept;
    const quality::ResolvedGraphics& resolved_quality() const noexcept;
    std::uint32_t ray_tracing_capabilities() const noexcept;
    const VulkanBackendDiagnostics& diagnostics() const noexcept;
    std::uint64_t draw_calls() const noexcept;
    std::string diagnostics_json() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
