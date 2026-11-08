#pragma once

#include "assets/Assets.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace c4::game {
inline constexpr std::uint32_t effect_particle_capacity=10000;
inline constexpr std::uint32_t effect_vertex_capacity=effect_particle_capacity*4;
inline constexpr std::uint32_t effect_index_capacity=effect_particle_capacity*6;
struct EffectVertex {
    std::array<float,3> position;
    std::array<float,2> uv;
    std::array<float,4> color;
};
struct EffectBatch {
    std::uint32_t first_index{}, index_count{}, texture_index{};
    bool additive{}, bright{}, depth_write{}, transparent{}, alpha_test{};
};
struct EffectCounts {
    std::uint64_t emitted{}, expired{}, allocation_failures{}, recycled{};
    std::uint32_t active{};
};
// Images are decoded once. All spans remain immutable until the next source frame.
struct EffectFrame {
    std::span<const assets::Image> textures;
    std::span<const std::string_view> texture_sources;
    std::span<const EffectVertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const EffectBatch> batches;
    EffectCounts counts;
    std::uint64_t revision{};
    std::int32_t source_anisotropy_raw{};
};
} // namespace c4::game
