#include "HudMeshes.hpp"
#include "SourceViewSprite.hpp"
#include "assets/GpuImage.hpp"
#include "assets/SkinImages.hpp"
#include "game/CameraProjection.hpp"
#include "game/GameState.hpp"

#include <algorithm>
#include <stdexcept>

namespace c4::ui {
namespace {
using physics::v;
using physics::number;
using physics::native;

std::array<float,16> entity_matrix(physics::ScriptVec position, physics::ScriptVec angles) {
    const auto axes = physics::rotation_axes(native(angles));
    const auto origin = native(position);
    return {float(axes[0].x), float(axes[0].y), float(axes[0].z), 0,
            float(axes[1].x), float(axes[1].y), float(axes[1].z), 0,
            float(axes[2].x), float(axes[2].y), float(axes[2].z), 0,
            float(origin.x), float(origin.y), float(origin.z), 1};
}

std::array<float,16> needle_matrix(physics::Var roll) {
    // ingame_panels.wdl:68-75,484,514-517 supplies XYZ/roll. Original
    // 1004f895..1004f909 uses the facing predicates to select cam.pan+180;
    // empty cam_ibre angles are zero, including the roll==0 branch's tilt.
    return entity_matrix({v(876), v(-401), v(-251)}, {v(180), v(0), roll});
}

void validate_fill(const assets::Model& model) {
    // kaynak_bit.rc4::hizpanel_dolum.mdl inventory: one actual group,
    // 102 source vertices, 100 indexed triangles, Frame0..Frame30, RGB888 skin.
    if (model.groups.size() != 1)
        throw std::runtime_error("Original HUD fill must contain exactly one model group");
    const auto& group = model.groups.front();
    if (group.source_vertices.size() != 102 || group.mesh.indices.size() != 300 ||
        group.source_indices.size() != group.mesh.vertices.size() ||
        group.frames.size() != 31 || group.clips.size() != 1 ||
        group.clips.front().name != "Frame" || group.clips.front().first_frame != 0 ||
        group.clips.front().frame_count != 31 || group.skins.size() != 1 ||
        group.triangle_materials.size() != 100 ||
        !std::all_of(group.triangle_materials.begin(), group.triangle_materials.end(),
                     [](auto material) { return material == 0; }))
        throw std::runtime_error("Original HUD fill topology, Frame scene or material binding is unsupported");
    const auto& skin = group.skins.front();
    if (skin.flags != 4 || skin.width != 256 || skin.height != 8 ||
        !skin.material.empty() || !skin.effect.empty() || !skin.external_texture.empty())
        throw std::runtime_error("Original HUD fill embedded RGB888 skin is unsupported");
}

bool same_pose(assets::AnimationSample a, assets::AnimationSample b) noexcept {
    return a.first_frame == b.first_frame && a.second_frame == b.second_frame && a.blend == b.blend;
}
} // namespace

// SpriteCtor 1005ffb1..1005ffbd adds TARGET4 when mip_sprites != 0.
// Its registered default is 2; C4.wdl:251's sole assignment is commented out.
HudMeshes::HudMeshes(Resources& resources)
    : resources_(resources), fill_model_(resources.add_model("hizpanel_dolum.mdl")),
      fill_image_(resources.add_image({})),
      needle_image_(resources.add("hizpanel_ibre2.tga", false, true, 0x40000u)) {}

void HudMeshes::initialize() {
    if (initialized_) return;
    const auto& model = resources_.models.at(fill_model_);
    validate_fill(model);
    fill_group_ = &model.groups.front();
    resources_.images.at(fill_image_) = assets::decode_skin_image(fill_group_->skins.front());
    // The canonical role-0 GPU factory preserves this original BGR888 skin's
    // precision; borrow its retained CPU decode without a skin copy or re-decode.
    // Raw skin kind4 has no supplied-mip flag: bitmap flags0, initial mip count0.
    // Reuse the caller's explicit hardware limit, never guess a texture maximum.
    resources_.set_gpu_image(fill_image_,
        assets::gpu_image_from_image(resources_.image(fill_image_), resources_.gpu_upload_options(false)));
    ++resources_.revisions.at(fill_image_);
    fill_vertices_.resize(fill_group_->mesh.vertices.size());
    auto& fill = meshes_[0];
    fill.vertices = fill_vertices_;
    fill.indices = fill_group_->mesh.indices;
    fill.image_index = fill_image_;
    fill.layer = 1;
    fill.source_order = source_ui_order(1, 80);
    // cam_ibre's captured default VIEW projector is owned/calibrated centrally;
    // never substitute gameplay camera motion, guessed near planes or panel pivots.
    fill.view_projection = game::default_hud_camera(resources_.canvas_width(), resources_.canvas_height()).view_projection;
    // ingame_panels.wdl:63-64,77-84,432-433. Per-tier offsets remain
    // Q10 script arithmetic before the SDK's native float transform.
    constexpr std::array<double,4> fill_y_offsets{0,-.123,0,0};
    constexpr std::array<double,4> fill_z_offsets{.125,.001,0,0};
    const auto tier = resources_.tier();
    fill.model_to_view = entity_matrix(
        {v(141.255), v(-63.234) + v(fill_y_offsets[tier]), v(-39.750) + v(fill_z_offsets[tier])},
        {v(0), v(180), v(180)});
    fill_pose_ = assets::animation_sample(*fill_group_, "Frame", float(number(v(0))), false);
    assets::sample_pose(*fill_group_, fill_pose_, fill_vertices_);
    fill.pose_revision = 1;
    const auto& image = resources_.image(needle_image_);
    if (image.width != 156 || image.height != 25 ||
        image.source_format != assets::ImageSourceFormat::Tga ||
        image.source_bits_per_pixel != 32)
        throw std::runtime_error("Original HUD needle must retain its authored 156x25 RGBA TGA");
    // Original 1004a280/10047300 ordinary sprite, not a panel rectangle:
    // centered source-local Y/Z plane, half-texel image UVs, original fan winding.
    needle_vertices_ = original_view_sprite_vertices(float(image.width), float(image.height));
    auto& needle = meshes_[1];
    needle.vertices = needle_vertices_;
    needle.indices = original_view_sprite_indices;
    needle.image_index = needle_image_;
    needle.layer = 5;
    needle.source_order = source_ui_order(1, 71);
    needle.view_projection = fill.view_projection;
    needle_roll_ = v(50);
    needle.model_to_view = needle_matrix(needle_roll_);
    needle.pose_revision = 1;
    // Both source entities have a null MATERIAL and no MDL material palette.
    // configure_material_state recovers mat_unlit's emissive 128/255, not white.
    // Original 10044d10/10044460 and the captured HUD draws prove these alpha
    // roles; source32 sprite alpha disables OVERLAY's generic alpha test.
    // Geometry, raster and sampler provenance: work/reports/ui-view-sprite-evidence.json.
    for (std::size_t i = 0; i < meshes_.size(); ++i) {
        auto& mesh = meshes_[i];
        auto& material = materials_[i];
        const bool sprite = i == 1;
        // Shared declaration finalizer 1001ee25 ORs08000000 into NODE+58
        // (ENTITY+34) for both. Named needle receipt corroborates that word;
        // its private sprite branch additionally ORs OVERLAY1000.
        mesh.source_flags = sprite ? 0x08001000u : 0x08000000u;
        // Fresh private cam_ibre flags0, corroborated by its named 148-byte
        // receipt in decoded-runtime-properties.json; no source member writes.
        // Original effect gate 100480b0 tests NOSHADER1000, not material/noFX.
        mesh.source_effects_enabled = true;
        mesh.source_cull_mode = sprite ? 1 : 3; // Actual D3DCULL_NONE / D3DCULL_CCW.
        mesh.filtered = true; // All 256 paired original sampler histories: MIN/MAG LINEAR.
        mesh.color_multiplier = float(number(v(2))); // Original COLOROP MODULATE2X.
        material.entity_skins[0] = mesh.image_index;
        rendering::configure_material_state(material, {}, mesh.source_flags, float(number(v(50))));
        material.entity_albedo_percent = float(number(v(0))); // ingame_panels.wdl:73,82.
        const rendering::OriginalModelAlphaDecision alpha{
            sprite, sprite, !sprite, rendering::BlendFactor::SourceAlpha,
            rendering::BlendFactor::InverseSourceAlpha, float(number(v(1)))};
        material.raster.blend = alpha.alpha_blend;
        material.raster.depth_test = true;
        material.raster.depth_write = alpha.depth_write;
        material.raster.source_blend = alpha.source_blend;
        material.raster.destination_blend = alpha.destination_blend;
        material.raster.alpha_comparison = rendering::AlphaComparison::Always;
        material.raster.color_key = false; // Opaque BGR skin / authored RGBA, never a pixel-key shader.
        material.raster.defaults_recovered = true;
        material.source_pass.raster = material.raster;
        material.passes[0].raster = material.raster;
        lighting_[i] = rendering::source_view_entity_uniforms(material, mesh.source_flags, &alpha);
        mesh.material_state = &material;
        mesh.lighting = &lighting_[i];
    }
    initialized_ = true;
}

void HudMeshes::update(const game::SharedState& state, const std::array<bool,16>& source_visibility) {
    if (!initialized_) throw std::logic_error("Original HUD geometry must initialize after resource loading");
    // Exact saved source INVISIBLE flags, not an aggregate panel/phase gate.
    // Slots12/13 are the fill/needle; either can be independently visible.
    const bool fill_visible = !source_visibility[12];
    const bool needle_visible = !source_visibility[13];
    mesh_first_ = fill_visible ? 0 : 1;
    mesh_count_ = std::size_t(fill_visible) + std::size_t(needle_visible);
    if (!mesh_count_) return;
    // SourceModelPercent crosses the SDK boundary only here, not in gameplay
    // state. Animation retains original scene/frame quantization and UV topology.
    if (fill_visible && state.hud_vites_frame != fill_percent_) {
        const float percentage = float(number(state.hud_vites_frame));
        const auto pose = assets::animation_sample(*fill_group_, "Frame", percentage, false);
        if (!same_pose(pose, fill_pose_)) {
            if (assets::sample_pose(*fill_group_, pose, fill_vertices_))
                ++meshes_[0].pose_revision;
            fill_pose_ = pose;
        }
        fill_percent_ = state.hud_vites_frame;
    }
    if (needle_visible && state.gauge_needle_roll != needle_roll_) {
        needle_roll_ = state.gauge_needle_roll;
        meshes_[1].model_to_view = needle_matrix(needle_roll_);
    }
}
} // namespace c4::ui
