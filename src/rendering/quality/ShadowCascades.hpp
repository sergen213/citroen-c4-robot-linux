#pragma once

#include <array>
#include <optional>

namespace c4::rendering::quality {

struct ShadowCascades {
    unsigned count{};
    std::array<std::array<float, 16>, 4> matrices{};
    std::array<float, 4> split_depths{};
    // Receiver-only constant/slope bias in sampled [0,1] depth, derived from
    // each cascade's world texel footprint, independent of depth padding.
    std::array<std::array<float, 2>, 4> depth_bias{};
};

// Equal logarithmic view-depth budgets preserve nearby caster precision without
// a uniform share of an extended far plane. Cascades cover the complete view.
inline constexpr float native_shadow_split_lambda = 1.0f;

// Input/output matrices are column-major. near/far are positive camera-view
// distances; camera_clip_near/far are their actual NDC endpoints (including
// reversed depth). sun_direction points along the light rays toward the scene.
// Each matrix maps world positions to light clip, default [-1,1] depth.
// tile_resolution is one cascade's atlas tile, not the whole atlas resolution.
// caster_depth_padding extends the fitted volume along the sun axis in world
// units; callers supply scene caster reach, not a fabricated scene dimension.
// Invalid/nonfinite/degenerate camera or light input returns nullopt.
std::optional<ShadowCascades> build_shadow_cascades(
    const std::array<float, 16>& inverse_view_projection,
    const std::array<float, 3>& sun_direction,
    float near_depth, float far_depth,
    float camera_clip_near, float camera_clip_far,
    unsigned cascade_count, unsigned tile_resolution, float split_lambda,
    float caster_depth_padding = 0.0f, bool light_clip_zero_to_one = false) noexcept;

} // namespace c4::rendering::quality
