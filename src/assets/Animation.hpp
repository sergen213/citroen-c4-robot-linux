#pragma once

#include "Assets.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace c4::assets {
struct Skin {
    std::uint8_t format{}, flags{};
    std::uint32_t width{}, height{};
    std::string name;
    std::string external_texture;
    std::vector<std::uint8_t> payload;
    // Original diffuse, ambient, specular, emissive RGBA and power; no defaults invented.
    std::vector<float> material;
    std::string effect;
};
struct VertexEdit { std::uint32_t index{}; Vertex vertex{}; };
struct AnimationFrame { std::string name; std::vector<VertexEdit> edits; };
struct AnimationClip { std::string name; std::uint32_t first_frame{}, frame_count{}; };
struct ModelGroup {
    std::string name;
    Mesh mesh;
    std::vector<Vertex> source_vertices;
    std::vector<std::uint32_t> source_indices;
    std::vector<Skin> skins;
    // 0xffffffff means no original skin/material; runtime morphskin can supply one.
    std::vector<std::uint32_t> triangle_materials;
    // Runtime-ordered frames: MDL7 prepends the engine "_base" label with no
    // edits only when its first disk frame edits vertices; otherwise the
    // first disk frame labels the base alias. Stored labels remain untouched.
    // Legacy MDL4 frame zero already aliases its decoded source base.
    std::vector<AnimationFrame> frames;
    std::vector<AnimationClip> clips;
};
struct Model {
    std::vector<ModelGroup> groups;
    // Original optional entity/editor metadata, retained without inferred attachments.
    std::vector<std::uint8_t> entity_data, editor_data;
    // False for manual/unclassified models; true only after validated ordinary MDL disk
    // decoding: source 10061070 cannot activate 10050570's RESOURCE1000-linked
    // descriptor brightness writer. Copies/moves preserve this fact, not global brightness neutrality.
    bool source_no_linked_brightness_known{};
};
struct AnimationSample {
    std::uint32_t first_frame{}, second_frame{};
    float blend{};
    // Original entity FRAME/NEXT_FRAME properties, Q10ONE = 1024.
    // Producers encode FRAME = (first_frame + 1) * Q10ONE + fractionRaw.
    // ent_animate (1003d880) leaves NEXT_FRAME at zero for implicit next;
    // only cycle coordinate > clip last sets (clip first + 1) * Q10ONE.
    // robot_fizik.wdl:885 transitions explicitly set the target one-based frame.
    // Manual three-field render samples leave both metadata properties zero.
    std::int32_t frame_raw{}, next_frame_raw{};
};

// Original MDL4/MDL7 coordinates/normals, UVs, group/frame labels and skins.
Model load_model(const std::filesystem::path& path);
Model decode_model(std::span<const std::uint8_t> bytes);
AnimationSample animation_sample(const ModelGroup& group, std::string_view clip,
                                 float percentage, bool cycle = false);
// Original robot_fizik.wdl transition: quantize the old fraction to Q10,
// choose nearest old frame (strict > raw 512; half ties down), floor target
// to an integer one-based engine frame, and multiply blend_percent by the
// source Var literal .01 (raw 10), not floating-point percentage / 100.
AnimationSample animation_transition(const ModelGroup& group, AnimationSample previous,
                                     std::string_view target_clip, float target_percentage,
                                     float blend_percent);
// Output must contain initialized retained vertices, sized to group.mesh.vertices.
// Writes the sampled pose and mesh UVs; returns true iff any XYZ/normal/UV bytes changed.
bool sample_pose(const ModelGroup& group, AnimationSample pose, std::span<Vertex> output);
Vertex sample_source_vertex(const ModelGroup& group, AnimationSample pose, std::uint32_t index);
// Reuse group.mesh.vertices-sized output across samples. Never changes UV/index topology.
void sample_animation(const ModelGroup& group, std::string_view clip, float percentage,
                      bool cycle, std::span<Vertex> output);
// robot_fizik.wdl:4050-4077 one-shot donus recurrence, in original 1/16-second ticks.
// Start at zero; sample before advancing. Completion is returned percentage > 100.
// Uses recovered Q10 script arithmetic; scheduler/time-step postprocessing is separate.
float advance_donus_percentage(float percentage, float normalized_ticks);
} // namespace c4::assets
