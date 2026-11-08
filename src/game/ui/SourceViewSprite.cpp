#include "SourceViewSprite.hpp"

namespace c4::ui {
std::array<assets::Vertex,4> original_view_sprite_vertices(float width, float height) noexcept {
    // Acknex 7.07: 1004a32d..1004a4d1 emits four vertices through 10047300.
    // IMAGE+44/+48 are padded texture dimensions. Preserve the original half-texel
    // endpoints after mapping that texture domain to our decoded, unpadded image.
    const float half_width = width * 0.5f;
    const float half_height = height * 0.5f;
    const float u0 = 0.5f / width, u1 = (width - 0.5f) / width;
    const float v0 = 0.5f / height, v1 = (height - 0.5f) / height;
    return {{{0, -half_width, half_height, u0, v0, 1, 0, 0},
             {0, half_width, half_height, u1, v0, 1, 0, 0},
             {0, half_width, -half_height, u1, v1, 1, 0, 0},
             {0, -half_width, -half_height, u0, v1, 1, 0, 0}}};
}
} // namespace c4::ui
