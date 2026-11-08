#pragma once

#include <cstdint>

namespace c4::assets {
// Actual original compile selectors, independent of native GPU/backend presets.
// C4.wdl:24-104; game_textures.wdl selects ATQ/MTQ/STQ resource declarations.
struct SourceProfile {
    std::uint8_t atq{}, mtq{}, stq{}, cgm{}, shq{}, ano{}, bloom{};
    bool operator==(const SourceProfile&) const = default;
};
inline constexpr SourceProfile original_source_profile{};
// Prior authored high asset inspection; unrelated source selectors stay Original.
// This is not an observed original launcher preset.
inline constexpr SourceProfile high_source_profile{2, 2, 3, 0, 2, 0, 0};
constexpr bool valid_source_profile(const SourceProfile& profile) noexcept {
    return profile.atq <= 2 && profile.mtq <= 2 && profile.stq <= 3
        && profile.cgm <= 3 && profile.shq <= 2 && profile.ano <= 1 && profile.bloom <= 1;
}
} // namespace c4::assets
