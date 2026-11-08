#pragma once

#include "Assets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <optional>
#include <vector>

namespace c4::assets {

struct Skin;

enum class GpuImageFormat {
    Rgba8Unorm,
    Rgb565Unorm,
    Argb1555Unorm,
    Bc1Unorm,
    Bc2Unorm,
    Bc3Unorm
};

// All levels, including LOD0. Dimensions describe one face; byte_size includes
// all faces. Offsets and pitches are bytes into GpuImage::texels. Uncompressed
// faces are row-major; cube faces use +X,-X,+Y,-Y,+Z,-Z order.
struct GpuImageLevel {
    std::uint32_t width{}, height{};
    std::size_t offset{}, byte_size{}, row_pitch{}, face_stride{};
};

// Explicit original bitmap upload state. The hardware maximum is REQUIRED:
// startup defaults are not evidence for the active device's texture limit.
struct GpuImageRuntimeContext {
    std::uint32_t bitmap_flags{};
    std::uint32_t minimum_dimension{8};
    std::uint32_t maximum_dimension{};
    std::uint32_t requested_mip_levels{};
    // Original d3d_autotransparency selector at first upload, not an overlay
    // role substitute. False selects the recovered API fixed-black key path.
    bool automatic_transparency{true};
    // Proven use of the original global palette, not a caller/custom palette.
    // Only that global producer forces entry 255 to white.
    bool palette_is_global{};
};

// Cold native capability bridge, not an observed Windows receipt or the
// mutable per-frame original TEXLIMIT. [INFERENCE] Verified mip support maps
// the known nonzero cold seed through the recovered legacy cap classifier.
// Positive maxima and verified mip support are required. The original scalar
// lower clamp is 256 even for smaller caps; physical device validation remains
// distinct. General runtime contexts retain their caller-specified maximum.
GpuImageRuntimeContext cold_gpu_image_context(std::uint32_t actual_max_width,
                                             std::uint32_t actual_max_height,
                                             bool verified_mipmap_support);

struct GpuImageRuntimeInfo {
    std::uint32_t source_width{}, source_height{}, nominal_width{}, nominal_height{};
    std::uint32_t written_width{}, written_height{};
    std::uint32_t authored_mip_levels{}, preserved_mip_levels{}, generated_mip_levels{};
    std::uint32_t resize_filter{1}, mip_filter{5};
    bool original_padding_known{true};
    bool encoded_postfilter_attempted{};
    bool postfilter_output_known{true};
};

struct GpuImage {
    // Physical upload dimensions; runtime source/nominal/written rectangles
    // remain distinct metadata and never change the retained CPU diagnostic.
    std::uint32_t width{}, height{}, face_count{1};
    GpuImageFormat format{GpuImageFormat::Rgba8Unorm};
    ImageSourceFormat source_format{ImageSourceFormat::Unknown};
    std::uint8_t source_bits_per_pixel{};
    bool source_alpha_known{}, source_alpha_flag{};
    bool color_key_applied{};
    // True for decoded precision, native deterministic padding, or a resampling
    // model not proved bit-identical to the original implementation.
    bool implementation_precision_fallback{};
    // Direct BC/packed DDS views own the original DDS including its header.
    // Runtime derivatives own upload bytes here; upload only level ranges.
    std::vector<std::uint8_t> texels;
    std::vector<GpuImageLevel> levels;
    // Original encoded DDS bytes survive a changed runtime upload by ownership
    // transfer, not a second copy. Direct DDS views keep them in texels instead.
    std::vector<std::uint8_t> authored_dds;
    std::optional<GpuImageRuntimeInfo> runtime;
};

// Validate owned storage, every stored level, dimensions, faces and byte
// pitches before backend upload. Throws on malformed input; never mutates it.
void validate_gpu_image(const GpuImage& image);

// Explicit caller state at FIRST upload of the original bitmap, not a new
// per-role cache identity. Defaults are recovered engine registration values;
// a caller with changed original texture depths must pass those actual values.
struct GpuImageUploadOptions {
    bool overlay{};
    std::uint8_t texture_depth{16}, alpha_depth{32};
    std::optional<GpuImageRuntimeContext> runtime;
};

// Derive physical upload storage without modifying the CPU/source bitmap.
// BC derivatives use recovered normalized block decoding/endpoint-solver
// mathematics with explicit FP precision fallback, retaining authored DDS
// bytes immutably. Unsupported derivation contexts fail, never decode silently.
// Runtime-disabled factories retain the authored upload view.
GpuImage derive_gpu_image_upload(GpuImage&& source,
                                 const GpuImageRuntimeContext& context, bool overlay);

// By default original DDS blocks are retained without RGBA decoding and other
// formats reuse the bounded CPU readers. Optional runtime derivation records
// authored/preserved/generated levels separately and requires explicit caps.
GpuImage load_gpu_image(const std::filesystem::path& resource,
                        const GpuImageUploadOptions& options = {});
// Read once while explicitly retaining a CPU bitmap interpretation/diagnostic.
// Non-DDS CPU pixels remain unmodified by upload conversion or overlay keying.
// DDS diagnostic expansion is additional requested work; native GPU blocks and
// their original ownership are preserved. The default overload does no such decode.
GpuImage load_gpu_image(const std::filesystem::path& resource, Image& cpu_diagnostic,
                        const GpuImageUploadOptions& options = {});
// A borrowed CPU image is preserved and converted directly into owned GPU
// storage, without a temporary CPU image copy. An rvalue can transfer its RGBA
// storage. Already-decoded DDS remains an explicit precision fallback.
GpuImage gpu_image_from_image(const Image& image,
                             const GpuImageUploadOptions& options = {});
GpuImage gpu_image_from_image(Image&& image,
                             const GpuImageUploadOptions& options = {});
// Borrowed skins retain their payload and metadata. Only GPU texel storage is
// copied or converted; rvalues can transfer native packed/DDS payload storage.
GpuImage gpu_image_from_skin(const Skin& skin,
                            const GpuImageUploadOptions& options = {},
                            std::span<const std::uint8_t> palette = {});
GpuImage gpu_image_from_skin(Skin&& skin,
                            const GpuImageUploadOptions& options = {},
                            std::span<const std::uint8_t> palette = {});
// Declared SKY CUBE resources use six independent bitmap uploads, in original
// left-to-right strip order (1005fe90), NOT the reflection-cubemap remap.
// Split the decoded raw bitmap before per-face depth/key/runtime conversion.
// Width is divided by six as in the original; trailing remainder is discarded.
// DDS/cubemaps and authored mip strips are not this raw resource path.
std::array<GpuImage, 6> gpu_sky_faces_from_image(
    const Image& strip, const GpuImageUploadOptions& options = {});
// NONE-copied runtime strips use their logical written rectangle and physical
// row pitch. Cap/stretch and compressed runtime strip rearrangement are not
// recovered and are rejected rather than inventing face placement.
GpuImage gpu_cubemap_from_image(GpuImage&& strip);

// Explicit CPU diagnostic/device-capability fallback. BC and packed UNORM
// expansion to RGBA8 is honest implementation precision, not source GPU parity.
Image decode_gpu_image(const GpuImage& image);

} // namespace c4::assets
