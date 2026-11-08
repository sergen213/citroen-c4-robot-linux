#pragma once

#include "UiResources.hpp"
#include "physics/FixedPoint.hpp"

#include <array>
#include <span>
#include <vector>

namespace c4::game { struct SharedState; }

namespace c4::ui {
// ingame_panels.wdl:68-84 camera-relative entities. Geometry remains outside
// world acceleration structures; NativeUi owns this cache for every UiFrame span.
class HudMeshes {
public:
    explicit HudMeshes(Resources& resources);
    HudMeshes(const HudMeshes&) = delete;
    HudMeshes& operator=(const HudMeshes&) = delete;
    void initialize(); // After Resources::load(), before backend image uploads.
    void update(const game::SharedState& state, const std::array<bool,16>& source_visibility);
    std::span<const OverlayMesh> meshes() const noexcept {
        return std::span<const OverlayMesh>(meshes_).subspan(mesh_first_, mesh_count_);
    }
    // Eager backend preparation also sees hidden HUD geometry; no resume-time allocation.
    std::span<const OverlayMesh> prepared_meshes() const noexcept {
        return initialized_ ? std::span<const OverlayMesh>(meshes_) : std::span<const OverlayMesh>{};
    }
private:
    Resources& resources_;
    std::uint32_t fill_model_, fill_image_, needle_image_;
    const assets::ModelGroup* fill_group_{};
    std::vector<assets::Vertex> fill_vertices_;
    std::array<assets::Vertex,4> needle_vertices_{};
    std::array<OverlayMesh,2> meshes_{};
    std::array<rendering::NativeMaterialState,2> materials_{};
    std::array<rendering::SourceLightingUniforms,2> lighting_{};
    assets::AnimationSample fill_pose_{};
    physics::Var fill_percent_{}, needle_roll_{};
    std::size_t mesh_first_{}, mesh_count_{};
    bool initialized_{};
};
} // namespace c4::ui
