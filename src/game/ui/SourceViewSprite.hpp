#pragma once

#include "assets/Assets.hpp"

#include <array>
#include <cstdint>

namespace c4::ui {
// Original ordinary ENTITY sprite, in source-local axes at default Q10 scale 1024.
// Dimensions are the positive decoded image size; UVs address its unpadded domain.
std::array<assets::Vertex,4> original_view_sprite_vertices(float width, float height) noexcept;

// Original D3D triangle fan, with winding reversed by the source Y/Z axis swap.
inline constexpr std::array<std::uint32_t,6> original_view_sprite_indices{2,1,0,3,2,0};
} // namespace c4::ui
