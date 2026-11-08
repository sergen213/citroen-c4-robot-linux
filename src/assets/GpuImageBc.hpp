#pragma once

#include "GpuImage.hpp"

#include <array>
#include <span>

namespace c4::assets::detail {
using BcPixel = std::array<float, 4>;
using BcBlock = std::array<BcPixel, 16>;

// Original D3DX callbacks operate on normalized FLOAT4, not diagnostic RGBA8.
std::size_t bc_block_bytes(GpuImageFormat format);
void decode_bc_block(GpuImageFormat format, std::span<const std::uint8_t> bytes, BcBlock& pixels);
// DEFAULT POT BOX passes dither=0; NPOT DEFAULT passes block-local diffusion=1.
// Caller supplies normalized finite FLOAT4 and retains authored partial-block pixels.
void encode_bc_block(GpuImageFormat format, const BcBlock& pixels, std::span<std::uint8_t> bytes,
                     bool dither = false);
} // namespace c4::assets::detail
