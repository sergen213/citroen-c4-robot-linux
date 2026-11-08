#pragma once

#include "Assets.hpp"

#include <span>

namespace c4::assets {
struct Skin;
// format is the low three type bits; flags retains the full original type byte.
// Types 0/2/3/4/5 decode original pixels; 0 requires the 768-byte RGB level palette.
// 0x08 adds three mip levels (dimensions divisible by 8); 0x10/0x20 are metadata.
// DDS type 6 uses width = payload bytes, height = 1 and its own mip descriptors.
// External type 7 throws external_reference; the caller must safely resolve its path.
// Malformed/unsupported input throws std::runtime_error; output is base top-left RGBA.
Image decode_skin_image(const Skin& skin, std::span<const std::uint8_t> palette = {});
} // namespace c4::assets
