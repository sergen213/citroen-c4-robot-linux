#include "GpuImage.hpp"
#include "GpuImageInternal.hpp"
#include "Animation.hpp"
#include "Resources.hpp"
#include "SkinImages.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace c4::assets {
namespace {
[[noreturn]] void invalid(const std::string& reason) {
    throw std::runtime_error("GPU image: " + reason);
}

void validate_dimensions(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > detail::max_image_dimension ||
        height > detail::max_image_dimension ||
        std::uint64_t(width) * height > detail::max_image_pixels)
        invalid("dimensions exceed 16384 per axis or 64M pixels, or are zero");
}

bool compressed(GpuImageFormat format) {
    return format == GpuImageFormat::Bc1Unorm || format == GpuImageFormat::Bc2Unorm ||
           format == GpuImageFormat::Bc3Unorm;
}

std::size_t unit_bytes(GpuImageFormat format) {
    switch (format) {
    case GpuImageFormat::Rgba8Unorm: return 4;
    case GpuImageFormat::Rgb565Unorm:
    case GpuImageFormat::Argb1555Unorm: return 2;
    case GpuImageFormat::Bc1Unorm: return 8;
    case GpuImageFormat::Bc2Unorm:
    case GpuImageFormat::Bc3Unorm: return 16;
    }
    invalid("unsupported pixel format");
}

void validate_gpu_storage(const GpuImage& image) {
    validate_dimensions(image.width, image.height);
    if (image.face_count != 1 && image.face_count != 6)
        invalid("face count must be one or six");
    if (image.face_count == 6 && image.width != image.height)
        invalid("cubemap faces must be square");
    if (image.texels.size() > detail::max_image_bytes ||
        image.authored_dds.size() > detail::max_image_bytes - image.texels.size() ||
        image.levels.empty() || image.levels.size() > 15)
        invalid("missing levels or aggregate storage exceeds image limits");
    const auto bytes = unit_bytes(image.format);
    auto width = image.width, height = image.height;
    std::size_t end = 0;
    for (std::size_t n = 0; n < image.levels.size(); ++n) {
        const auto& level = image.levels[n];
        if (level.width != width || level.height != height)
            invalid("mip dimensions do not match the image chain");
        const auto columns = compressed(image.format) ? (width + 3) / 4 : width;
        const auto rows = compressed(image.format) ? (height + 3) / 4 : height;
        if (level.row_pitch < std::size_t(columns) * bytes ||
            level.row_pitch > detail::max_image_bytes / rows ||
            level.face_stride < level.row_pitch * rows ||
            level.face_stride > detail::max_image_bytes / image.face_count ||
            level.byte_size != level.face_stride * image.face_count ||
            level.offset < end || level.offset > image.texels.size() ||
            level.byte_size > image.texels.size() - level.offset)
            invalid("invalid mip byte range, row pitch or face stride");
        end = level.offset + level.byte_size;
        if (n + 1 < image.levels.size() && width == 1 && height == 1)
            invalid("mip chain continues below one pixel");
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
}

std::uint16_t packed_pixel(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (std::uint16_t(bytes[1]) << 8));
}

void store_packed(std::uint8_t* bytes, std::uint16_t pixel) {
    bytes[0] = static_cast<std::uint8_t>(pixel);
    bytes[1] = static_cast<std::uint8_t>(pixel >> 8);
}

std::uint16_t to_1555(std::uint16_t pixel) {
    // Original D3DX_FILTER_NONE RGB565 -> A1R5G5B5 conversion.
    return static_cast<std::uint16_t>(0x8000 | ((pixel >> 1) & 0x7fe0) | (pixel & 0x1f));
}

void packed_overlay(GpuImage& image, std::span<const std::uint8_t> source) {
    // The key is the first TARGET 1555 texel, reused for every stored LOD.
    const auto key = to_1555(packed_pixel(source.data() + image.levels.front().offset));
    for (const auto& level : image.levels)
        for (std::size_t n = level.offset; n < level.offset + level.byte_size; n += 2) {
            auto pixel = to_1555(packed_pixel(source.data() + n));
            if (pixel == key) pixel &= 0x7fff;
            store_packed(image.texels.data() + n, pixel);
        }
    image.format = GpuImageFormat::Argb1555Unorm;
    image.color_key_applied = true;
}

GpuImageLevel tight_level(std::uint32_t width, std::uint32_t height,
                          std::uint32_t faces, std::size_t bytes, std::size_t offset) {
    const auto pitch = std::size_t(width) * bytes;
    const auto stride = pitch * height;
    return {width, height, offset, stride * faces, pitch, stride};
}
} // namespace

void validate_gpu_image(const GpuImage& image) {
    validate_gpu_storage(image);
}

GpuImageRuntimeContext cold_gpu_image_context(std::uint32_t actual_max_width,
                                             std::uint32_t actual_max_height,
                                             bool verified_mipmap_support) {
    if (!actual_max_width || !actual_max_height)
        invalid("cold runtime context requires positive verified texture maxima");
    if (!verified_mipmap_support)
        invalid("cold runtime context requires verified mipmap support; no unknown/zero-feature inference");
    const auto maximum = std::clamp(std::min({4096u, actual_max_width, actual_max_height}),
                                    256u, 4096u);
    return {0, 8, maximum, 0, true};
}

namespace {
GpuImage finalize_upload(GpuImage&& image, const GpuImageUploadOptions& options) {
    if (options.runtime)
        return derive_gpu_image_upload(std::move(image), *options.runtime, options.overlay);
    validate_gpu_image(image);
    return std::move(image);
}

GpuImage load_gpu_image_impl(const std::filesystem::path& resource, Image* cpu_diagnostic,
                             const GpuImageUploadOptions& options) {
    const auto name = resource.string();
    const auto separator = name.find("::");
    const auto selected = separator == std::string::npos ? name : name.substr(separator + 2);
    auto extension = std::filesystem::path(selected).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    ImageSourceFormat format;
    if (extension == ".dds") format = ImageSourceFormat::Dds;
    else if (extension == ".tga") format = ImageSourceFormat::Tga;
    else if (extension == ".bmp") format = ImageSourceFormat::Bmp;
    else invalid("unsupported image extension " + extension);
    try {
        auto bytes = read_resource(resource);
        if (format == ImageSourceFormat::Dds) {
            auto result = detail::gpu_dds_bytes(std::move(bytes), false, cpu_diagnostic,
                                                options.runtime.has_value());
            return finalize_upload(std::move(result), options);
        }
        auto image = detail::decode_image_bytes(bytes, format);
        auto result = cpu_diagnostic ? gpu_image_from_image(image, options)
                                     : gpu_image_from_image(std::move(image), options);
        if (cpu_diagnostic) *cpu_diagnostic = std::move(image);
        return result;
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(name + ": " + error.what());
    }
}
} // namespace

GpuImage load_gpu_image(const std::filesystem::path& resource,
                        const GpuImageUploadOptions& options) {
    return load_gpu_image_impl(resource, nullptr, options);
}

GpuImage load_gpu_image(const std::filesystem::path& resource, Image& cpu_diagnostic,
                        const GpuImageUploadOptions& options) {
    return load_gpu_image_impl(resource, &cpu_diagnostic, options);
}

namespace {
GpuImage gpu_image_from_image_impl(const Image& image, Image* consume,
                                   const GpuImageUploadOptions& options) {
    detail::validate_image_rgba(image);
    GpuImage out;
    out.width = image.width;
    out.height = image.height;
    out.face_count = image.face_count;
    out.source_format = image.source_format;
    out.source_bits_per_pixel = image.source_bits_per_pixel;
    out.source_alpha_known = image.source_alpha_known;
    out.source_alpha_flag = image.source_alpha_flag;
    out.implementation_precision_fallback = image.source_format == ImageSourceFormat::Dds ||
        (image.source_format == ImageSourceFormat::ModelSkin && image.source_bits_per_pixel == 16);
    const bool bmp16 = image.source_format == ImageSourceFormat::Bmp && options.texture_depth == 16 &&
        (image.source_bits_per_pixel == 8 || image.source_bits_per_pixel == 24);
    const bool tga16 = image.source_format == ImageSourceFormat::Tga &&
        image.source_bits_per_pixel == 24 && options.alpha_depth == 16;
    const bool key = options.overlay && image.source_format != ImageSourceFormat::Dds &&
                     !image.source_alpha_flag;
    if (key && image.face_count != 1) invalid("overlay key requires a one-face image");
    const std::size_t bytes = bmp16 || tga16 ? 2 : 4;
    out.format = bytes == 2 ? GpuImageFormat::Rgb565Unorm : GpuImageFormat::Rgba8Unorm;
    out.levels.reserve(image.mip_levels.size() + 1);
    out.levels.push_back(tight_level(image.width, image.height, image.face_count, bytes, 0));
    std::size_t total = out.levels.front().byte_size;
    for (const auto& mip : image.mip_levels) {
        out.levels.push_back(tight_level(mip.width, mip.height, image.face_count, bytes, total));
        total += out.levels.back().byte_size;
    }
    if (bytes == 2) {
        out.texels.resize(total);
        const bool reverse = image.source_format == ImageSourceFormat::Bmp && image.source_bits_per_pixel == 8;
        const auto convert = [&](std::span<const std::uint8_t> source, std::size_t offset) {
            for (std::size_t n = 0; n < source.size(); n += 4) {
                const auto r = source[n + (reverse ? 2 : 0)];
                const auto g = source[n + 1];
                const auto b = source[n + (reverse ? 0 : 2)];
                // Original BGR24 converter preserves G bit zero, not G bit two.
                const auto pixel = static_cast<std::uint16_t>(((r >> 3) << 11) |
                    ((g >> 3) << 6) | ((g & 1) << 5) | (b >> 3));
                store_packed(out.texels.data() + offset + n / 2, pixel);
            }
        };
        convert(image.rgba, 0);
        convert(image.mip_rgba, out.levels.front().byte_size);
        // Runtime TRIANGLE must read all six source green bits before target
        // 1555 quantization; conversion and keying both belong after resizing.
        if (key && !options.runtime) packed_overlay(out, out.texels);
    } else if (consume) {
        if (!image.mip_rgba.empty()) {
            consume->rgba.reserve(total);
            consume->rgba.insert(consume->rgba.end(), image.mip_rgba.begin(), image.mip_rgba.end());
        }
        out.texels = std::move(consume->rgba);
    } else {
        out.texels.resize(total);
        std::memcpy(out.texels.data(), image.rgba.data(), image.rgba.size());
        if (!image.mip_rgba.empty())
            std::memcpy(out.texels.data() + image.rgba.size(), image.mip_rgba.data(), image.mip_rgba.size());
    }
    if (key && bytes == 4 && !options.runtime) {
        const std::array<std::uint8_t, 3> rgb{out.texels[0], out.texels[1], out.texels[2]};
        for (std::size_t offset = 0; offset < out.texels.size(); offset += 4)
            if (out.texels[offset] == rgb[0] && out.texels[offset + 1] == rgb[1] &&
                out.texels[offset + 2] == rgb[2])
                out.texels[offset + 3] = 0;
        out.color_key_applied = true;
    }
    return finalize_upload(std::move(out), options);
}
} // namespace

GpuImage gpu_image_from_image(Image&& image, const GpuImageUploadOptions& options) {
    return gpu_image_from_image_impl(image, &image, options);
}

GpuImage gpu_image_from_image(const Image& image, const GpuImageUploadOptions& options) {
    return gpu_image_from_image_impl(image, nullptr, options);
}

namespace {
GpuImage gpu_image_from_skin_impl(const Skin& skin, Skin* consume,
                                 const GpuImageUploadOptions& options,
                                 std::span<const std::uint8_t> palette) {
    if (skin.format > 7 || (skin.flags & 7) != skin.format || (skin.flags & 0xc0))
        invalid("unsupported skin flags or format/type mismatch");
    const bool mipmaps = (skin.flags & 8) != 0;
    if (mipmaps && (skin.format == 0 || skin.format == 6 || skin.format == 7))
        invalid("unsupported skin mip flag for indexed, DDS or external data");
    if (skin.format == 7)
        invalid("skin external_reference: caller must safely resolve external_texture");
    if (skin.format == 6) {
        if (skin.height != 1 || skin.width != skin.payload.size())
            invalid("skin DDS descriptor must have width = payload bytes and height = 1");
        auto payload = consume ? std::move(consume->payload) : skin.payload;
        auto out = detail::gpu_dds_bytes(std::move(payload), true, nullptr,
                                         options.runtime.has_value());
        return finalize_upload(std::move(out), options);
    }
    if (skin.format == 0 && options.runtime) {
        if (palette.size() != 768)
            invalid("skin missing palette: exactly 768 original RGB palette bytes required");
        // Original P8 setup modifies a private GPU-only palette. Preserve index
        // identity until preparation; never remap the decoded CPU diagnostic.
        std::array<std::uint8_t, 768> prepared;
        std::copy(palette.begin(), palette.end(), prepared.begin());
        std::fill_n(prepared.begin(), 3, 0);
        for (std::size_t offset = 3; offset < prepared.size(); offset += 3)
            if (prepared[offset] < 8 && prepared[offset + 1] < 8 && prepared[offset + 2] < 8)
                std::fill_n(prepared.begin() + offset, 3, 8);
        if (options.runtime->palette_is_global)
            std::fill_n(prepared.end() - 3, 3, 255);
        return gpu_image_from_image(decode_skin_image(skin, prepared), options);
    }
    if (skin.format != 2)
        return gpu_image_from_image(decode_skin_image(skin, palette), options);
    validate_dimensions(skin.width, skin.height);
    if (mipmaps && ((skin.width & 7) || (skin.height & 7)))
        invalid("skin mip dimensions must be divisible by 8");
    GpuImage out;
    out.width = skin.width;
    out.height = skin.height;
    out.format = GpuImageFormat::Rgb565Unorm;
    out.source_format = ImageSourceFormat::ModelSkin;
    out.source_bits_per_pixel = 16;
    out.source_alpha_known = true;
    out.source_alpha_flag = false;
    out.levels.reserve(mipmaps ? 4 : 1);
    std::size_t total = 0;
    for (unsigned lod = 0; lod <= (mipmaps ? 3u : 0u); ++lod) {
        out.levels.push_back(tight_level(skin.width >> lod, skin.height >> lod, 1, 2, total));
        total += out.levels.back().byte_size;
    }
    if (total > detail::max_image_bytes || total != skin.payload.size())
        invalid("skin pixel payload size mismatch (base and all three mip levels required)");
    if (consume) {
        out.texels = std::move(consume->payload);
        if (options.overlay && !options.runtime) packed_overlay(out, out.texels);
    } else if (options.overlay && !options.runtime) {
        out.texels.resize(total);
        packed_overlay(out, skin.payload);
    } else {
        out.texels = skin.payload;
    }
    return finalize_upload(std::move(out), options);
}
} // namespace

GpuImage gpu_image_from_skin(Skin&& skin, const GpuImageUploadOptions& options,
                            std::span<const std::uint8_t> palette) {
    return gpu_image_from_skin_impl(skin, &skin, options, palette);
}

GpuImage gpu_image_from_skin(const Skin& skin, const GpuImageUploadOptions& options,
                            std::span<const std::uint8_t> palette) {
    return gpu_image_from_skin_impl(skin, nullptr, options, palette);
}

std::array<GpuImage, 6> gpu_sky_faces_from_image(
    const Image& strip, const GpuImageUploadOptions& options) {
    detail::validate_image_rgba(strip);
    if (strip.face_count != 1 || strip.source_format == ImageSourceFormat::Dds ||
        !strip.mip_levels.empty() || !strip.mip_rgba.empty() || strip.width < 6)
        invalid("declared sky requires a raw one-level horizontal six-face bitmap strip");
    const auto width = strip.width / 6;
    const auto row_bytes = std::size_t(width) * 4;
    auto face_options = options;
    // 1005fe90 propagates decoder alpha flags and adds the split-resource bit
    // to EACH bitmap before its first upload, not to an uploaded strip.
    if (face_options.runtime) face_options.runtime->bitmap_flags |= 0x20000000u;
    std::array<GpuImage, 6> faces;
    for (std::size_t index = 0; index < faces.size(); ++index) {
        Image face;
        face.width = width;
        face.height = strip.height;
        face.source_format = strip.source_format;
        face.source_bits_per_pixel = strip.source_bits_per_pixel;
        face.source_alpha_known = strip.source_alpha_known;
        face.source_alpha_flag = strip.source_alpha_flag;
        face.rgba.resize(row_bytes * strip.height);
        for (std::size_t row = 0; row < strip.height; ++row)
            std::memcpy(face.rgba.data() + row * row_bytes,
                        strip.rgba.data() + (row * strip.width + index * width) * 4,
                        row_bytes);
        faces[index] = gpu_image_from_image(std::move(face), face_options);
    }
    return faces;
}

GpuImage gpu_cubemap_from_image(GpuImage&& strip) {
    validate_gpu_image(strip);
    const auto logical_width = strip.runtime ? strip.runtime->written_width : strip.width;
    const auto logical_height = strip.runtime ? strip.runtime->written_height : strip.height;
    if (strip.runtime &&
        (strip.runtime->resize_filter != 1 ||
         logical_width != strip.runtime->source_width ||
         logical_height != strip.runtime->source_height ||
         strip.width != strip.runtime->nominal_width ||
         strip.height != strip.runtime->nominal_height))
        invalid("cubemap runtime strip requires unchanged NONE copy rectangles; cap/stretch layout is unproved");
    if (strip.face_count != 1 || !std::has_single_bit(logical_height) ||
        std::uint64_t(logical_height) * 6 != logical_width)
        invalid("cubemap requires six horizontal square power-of-two faces");
    if (compressed(strip.format)) {
        if (strip.runtime)
            invalid("cubemap runtime BC strip rearrangement is not recovered");
        // Compressed strip rectangles are not DDS cube faces. Explicitly decode
        // this unselected source case rather than invent a BC face-block layout.
        auto rgba = gpu_image_from_image(decode_gpu_image(strip));
        rgba.color_key_applied = strip.color_key_applied;
        rgba.implementation_precision_fallback = true;
        return gpu_cubemap_from_image(std::move(rgba));
    }
    GpuImage out;
    out.width = out.height = logical_height;
    out.face_count = 6;
    out.format = strip.format;
    out.source_format = strip.source_format;
    out.source_bits_per_pixel = strip.source_bits_per_pixel;
    out.source_alpha_known = strip.source_alpha_known;
    out.source_alpha_flag = strip.source_alpha_flag;
    out.color_key_applied = strip.color_key_applied;
    out.implementation_precision_fallback = strip.implementation_precision_fallback;
    // Runtime metadata continues to describe the source strip and its original
    // allocation; cube levels below describe the extracted physical faces.
    out.runtime = strip.runtime;
    const auto bytes = unit_bytes(strip.format);
    out.levels.reserve(strip.levels.size());
    std::size_t total = 0;
    auto face_dimension = logical_height;
    for (const auto& level : strip.levels) {
        if (out.levels.size() && out.levels.back().height == 1) break;
        if (strip.runtime) {
            if (std::uint64_t(face_dimension) * 6 > level.width || face_dimension > level.height)
                invalid("cubemap logical mip rectangle exceeds physical strip storage");
        } else if (std::uint64_t(level.height) * 6 != level.width) {
            invalid("cubemap lower mip is not a six-face strip");
        }
        out.levels.push_back(tight_level(face_dimension, face_dimension, 6, bytes, total));
        total += out.levels.back().byte_size;
        face_dimension = std::max(1u, face_dimension / 2);
    }
    if (total > detail::max_image_bytes ||
        strip.authored_dds.size() > detail::max_image_bytes - total)
        invalid("aggregate cubemap and authored DDS exceed 256 MiB");
    out.authored_dds = std::move(strip.authored_dds);
    if (out.runtime) {
        out.runtime->preserved_mip_levels = std::min(out.runtime->preserved_mip_levels,
                                                   static_cast<std::uint32_t>(out.levels.size()));
        out.runtime->generated_mip_levels = static_cast<std::uint32_t>(out.levels.size()) -
                                           out.runtime->preserved_mip_levels;
    }
    out.texels.resize(total);
    constexpr std::array<unsigned, 6> source_faces{2, 0, 5, 4, 1, 3};
    for (std::size_t lod = 0; lod < out.levels.size(); ++lod) {
        const auto& source = strip.levels[lod];
        const auto& target = out.levels[lod];
        for (std::size_t face = 0; face < source_faces.size(); ++face)
            for (std::size_t row = 0; row < target.height; ++row)
                std::memcpy(out.texels.data() + target.offset + face * target.face_stride + row * target.row_pitch,
                            strip.texels.data() + source.offset + row * source.row_pitch +
                                source_faces[face] * target.row_pitch, target.row_pitch);
    }
    validate_gpu_image(out);
    return out;
}

Image decode_gpu_image(const GpuImage& image) {
    if (compressed(image.format)) return detail::decode_dds_gpu(image);
    validate_gpu_image(image);
    std::size_t total = 0;
    for (const auto& level : image.levels) {
        const auto bytes = std::uint64_t(level.width) * level.height * image.face_count * 4;
        if (bytes > detail::max_image_bytes - total)
            invalid("aggregate decoded image exceeds 256 MiB");
        total += static_cast<std::size_t>(bytes);
    }
    Image out;
    out.width = image.width;
    out.height = image.height;
    out.face_count = image.face_count;
    out.source_format = image.source_format;
    out.source_bits_per_pixel = image.source_bits_per_pixel;
    out.source_alpha_known = image.source_alpha_known;
    out.source_alpha_flag = image.source_alpha_flag;
    const auto base_bytes = std::size_t(image.width) * image.height * image.face_count * 4;
    out.rgba.resize(base_bytes);
    out.mip_rgba.resize(total - base_bytes);
    out.mip_levels.reserve(image.levels.size() - 1);
    std::size_t mip_offset = 0;
    const auto bytes = unit_bytes(image.format);
    for (std::size_t lod = 0; lod < image.levels.size(); ++lod) {
        const auto& level = image.levels[lod];
        auto* destination = lod ? out.mip_rgba.data() + mip_offset : out.rgba.data();
        if (lod) out.mip_levels.push_back({level.width, level.height, mip_offset});
        for (std::size_t face = 0; face < image.face_count; ++face)
            for (std::size_t row = 0; row < level.height; ++row) {
                const auto* source = image.texels.data() + level.offset + face * level.face_stride + row * level.row_pitch;
                auto* target = destination + (face * level.height + row) * level.width * 4;
                if (image.format == GpuImageFormat::Rgba8Unorm) {
                    std::memcpy(target, source, std::size_t(level.width) * 4);
                    continue;
                }
                for (std::size_t column = 0; column < level.width; ++column) {
                    const auto pixel = packed_pixel(source + column * bytes);
                    const bool rgb565 = image.format == GpuImageFormat::Rgb565Unorm;
                    const auto r = (pixel >> (rgb565 ? 11 : 10)) & 31;
                    const auto g = (pixel >> 5) & (rgb565 ? 63 : 31);
                    const auto b = pixel & 31;
                    target[column * 4] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
                    target[column * 4 + 1] = static_cast<std::uint8_t>(rgb565 ? (g << 2) | (g >> 4) : (g << 3) | (g >> 2));
                    target[column * 4 + 2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
                    target[column * 4 + 3] = rgb565 || (pixel & 0x8000) ? 255 : 0;
                }
            }
        if (lod) mip_offset += std::size_t(level.width) * level.height * image.face_count * 4;
    }
    detail::validate_image_rgba(out);
    return out;
}
} // namespace c4::assets
