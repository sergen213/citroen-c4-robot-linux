#pragma once
#include "GameState.hpp"
#include "core/render_scene.hpp"
#include <array>

namespace c4::game {
// Column-major original XYZ world -> D3D camera (right,up,positive depth).
// Native D3D XZY world inputs must permute the Y/Z columns explicitly.
std::array<float,16> source_view_matrix(Vec3 position, Vec3 angles);
// OpenGL clip convention shared by GL and Vulkan's explicit Y/depth conversion.
// near_plane and pixel_aspect are recovered effective engine parameters, not source clip_near=0.
rendering::CameraState perspective_camera(Vec3 position, Vec3 angles, double horizontal_arc,
    double near_plane, double far_plane, double pixel_aspect, int width, int height);
// Copy the selected raw source VIEW before world drawing; no native FLOAT narrowing.
SourceViewSnapshot capture_source_view(const SharedState& state);
rendering::CameraState source_camera(const SourceViewSnapshot& view, int width, int height);
rendering::CameraState source_camera(const SharedState& state, int width, int height);
// Source declared sky stage only; caller supplies captured world-draw admission.
std::optional<rendering::SourceSkyFrame> source_sky_frame(const rendering::SourceSkyBinding& sky,
    const SourceViewSnapshot& view, const rendering::CameraState& camera, bool draw_world);
// Empty cam_ibre VIEW defaults: deterministic immediate stores in original
// initializer 0x100267d0, corroborated by the actual SDK capture.
rendering::CameraState default_hud_camera(int width, int height);
inline constexpr double observed_empty_view_near=10463.0/1024;
// Original view entities are already camera-relative (+X depth,+Y left,+Z up).
std::array<double,3> project_view_entity(Vec3 position, double horizontal_arc,
    double near_plane, double far_plane, double pixel_aspect, int width, int height);
} // namespace c4::game
