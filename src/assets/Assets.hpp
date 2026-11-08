#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace c4::assets {
// Images are top-left RGBA with straight alpha; cubemaps store +X,-X,+Y,-Y,+Z,-Z planes.
// Cubemap width/height are per face; original normalized MDL UVs are retained.
// Meshes retain original local axes, UVs and normals. Animation is exposed separately.
struct Vertex { float x, y, z, u, v; float nx{}, ny{}, nz{}; };
enum class ImageSourceFormat : std::uint8_t { Unknown, Dds, Tga, Bmp, ModelSkin };
struct ImageMipLevel { std::uint32_t width{}, height{}; std::size_t offset{}; };
struct Image {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
    std::uint32_t face_count{1};
    ImageSourceFormat source_format{ImageSourceFormat::Unknown};
    std::uint8_t source_bits_per_pixel{};
    // Stored lower mip levels only; offsets address mip_rgba, not base rgba.
    std::vector<std::uint8_t> mip_rgba{};
    std::vector<ImageMipLevel> mip_levels{};
    bool source_alpha_known{}, source_alpha_flag{};
};
enum class TangentProvenance : std::uint8_t { Unknown, ImportedSource, SourceAlgorithmNativePort };
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::array<float, 2>> secondary_uv;
    std::vector<std::array<float, 4>> tertiary_uv;
    TangentProvenance tangent_provenance{TangentProvenance::Unknown};
};

// Paths may select an original WRS3 member as archive-path::member-name.
// Unsupported formats and malformed input throw std::runtime_error; no fallback assets.
Image load_image(const std::filesystem::path& path);
// Original ARGB8 OVERLAY upload primitive: caller selects an original non-alpha bitmap
// and supplies a role-owned copy if opaque uses must retain the unmodified image.
// Clears alpha for exact base upper-left RGB matches in base and all stored lower
// levels, preserving RGB and other alpha in place. Requires one-face RGBA8 data.
// No source-format inference or implicit DDS keying; caller bypasses authored DDS.
// Does not allocate, copy or generate mips; apply before filtering/mip generation.
// Malformed dimensions, offsets or byte counts throw before any pixels are changed.
void apply_upper_left_color_key(Image& image);
// Convert an original six-horizontal-face strip, without resampling.
Image load_cubemap(const std::filesystem::path& path);
Image cubemap_from_image(const Image& strip);
Mesh load_mesh(const std::filesystem::path& path);
// Native port of the recovered stock indexed-corner producer, not proven runtime
// bit parity. Input p/n use original axes; output TEXCOORD2 uses D3D (x,z,y).
// Original face order and UV0 are retained; input normals are not normalized.
// Reuses output capacity. Orphan xyz becomes zero; existing w is retained, while
// new w is native-initialized zero (original allocation contents are unknown).
// Malformed triples/indices or non-finite input, arithmetic or output throw.
void generate_source_tangents(std::span<const Vertex> vertices,
                              std::span<const std::uint32_t> original_indices,
                              std::vector<std::array<float, 4>>& output);
} // namespace c4::assets
