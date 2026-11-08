#pragma once

#include "GpuImage.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace c4::assets::detail {

inline constexpr std::uint32_t max_image_dimension = 16384;
inline constexpr std::uint64_t max_image_pixels = 64ull * 1024 * 1024;
inline constexpr std::uint64_t max_image_bytes = 256ull * 1024 * 1024;

Image decode_image_bytes(std::span<const std::uint8_t> bytes, ImageSourceFormat format);
// Optional CPU diagnostics reuse this layout and the original CPU channel expansion.
// Retained masked DDS plus derived texels share one owned 256 MiB bound.
GpuImage gpu_dds_bytes(std::vector<std::uint8_t>&& bytes, bool exact = false,
                       Image* cpu_diagnostic = nullptr, bool retain_encoded = false);
Image decode_dds_gpu(const GpuImage& image);
void validate_image_rgba(const Image& image);

} // namespace c4::assets::detail
