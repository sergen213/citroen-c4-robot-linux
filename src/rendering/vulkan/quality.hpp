#pragma once
#include "core/render_scene.hpp"
#include "rendering/quality/GraphicSettings.hpp"
#include "rendering/SourceLighting.hpp"
#include "VulkanShaderData.hpp"
#include "VulkanDispatch.hpp"
#include <array>
#include <filesystem>
#include <memory>

namespace c4::rendering {
// b13, std140. Matrices retain the source camera's GL clip convention;
// shadow raster converts Y/depth exactly like the ordinary raster vertex path.
struct alignas(16) VulkanShadowUniforms {
    // splits: positive native-camera view depths; view_forward/position: native
    // Z-up. params: active count, reciprocal tile size, unused, unused.
    // sun_direction: native D3DLIGHT9 rays TOWARD the scene, not vecSunDir.
    std::array<std::array<float,16>,4> matrices{};
    std::array<float,4> splits{}, view_forward{}, view_position{}, params{}, sun_direction{};
    // Per-cascade receiver biases in sampled [0,1] depth.
    std::array<float,4> constant_bias{}, slope_bias{};
};
static_assert(sizeof(VulkanShadowUniforms)==368);
struct QualityOutput { VkImage image{}; VkImageView view{}; VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; unsigned post_passes{}; };
struct QualityDiagnostics { bool fxaa{}, ssao{}, bloom{}, internal_hdr{}; unsigned shadow_cascades{}, shadow_size{}, post_passes{}; };
class QualityPasses {
public:
    // Borrowed cache: the scene keeps it alive through QualityPasses destruction.
    struct Context { VkPhysicalDevice physical{}; VkDevice device{}; std::filesystem::path shader_directory; VkPipelineCache pipeline_cache{}; };
    // Callback records actual visible alpha-aware casters inside the active pass.
    // Bind a depth-only pipeline compatible with pass, using raster.vert id4=true.
    // Use cascade as VulkanDraw.config[3]; shader id4 uses immutable b13 matrices.
    using ShadowDraw = void (*)(void*, VkCommandBuffer, unsigned cascade, const std::array<float,16>&, VkRenderPass, VkExtent2D);
    explicit QualityPasses(const Context&);
    ~QualityPasses();
    QualityPasses(const QualityPasses&)=delete;
    QualityPasses& operator=(const QualityPasses&)=delete;
    // Caller has waited the single in-flight fence before prepare/destruction.
    void prepare(VkExtent2D, const quality::ResolvedGraphics&);
    void record_shadows(VkCommandBuffer, const FrameSnapshot&, const SourceLightingState&, void*, ShadowDraw);
    // Color/normal are shader-readable; depth is the actual resolved hardware
    // depth view in DEPTH_STENCIL_READ_ONLY_OPTIMAL (owner transitions after
    // effects). Run before source bloom/UI/gamma, with genuine HDR color only.
    QualityOutput record_post(VkCommandBuffer, VkImageView color, VkImageView depth,
                              VkImageView world_normal, const CameraState&,
                              const VulkanGlobals&, bool input_hdr);
    VkImageView shadow_view() const noexcept;
    VkSampler shadow_sampler() const noexcept;
    VkBuffer shadow_uniform_buffer() const noexcept;
    VkRenderPass shadow_render_pass() const noexcept;
    const VulkanShadowUniforms& shadow_uniforms() const noexcept;
    // Own resources/pipelines only: Raster additionally gates SSAO on its real
    // sampled resolved depth and known camera, HDR on actual HDR color, and
    // cascades on the source-alpha caster pipeline. Not extension-only promises.
    quality::GraphicsCapabilities capabilities() const noexcept;
    QualityDiagnostics diagnostics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
