#include "Assets.hpp"
#include "Resources.hpp"
#include "Animation.hpp"
#include "SkinImages.hpp"
#include "GpuImageInternal.hpp"
#include "GpuImageBc.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace c4::assets {
namespace {
using Bytes = std::span<const std::uint8_t>;
using Color = std::array<std::uint8_t, 4>;
using Pixels = std::span<std::uint8_t>;

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("image: " + message);
}

void require(Bytes data, std::size_t offset, std::size_t count) {
    if (offset > data.size() || count > data.size() - offset)
        invalid("truncated pixel data or header");
}

std::uint16_t u16(Bytes data, std::size_t offset) {
    require(data, offset, 2);
    return static_cast<std::uint16_t>(data[offset] | (std::uint16_t(data[offset + 1]) << 8));
}

std::uint32_t u32(Bytes data, std::size_t offset) {
    require(data, offset, 4);
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
           (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
}

std::size_t pixel_count(std::uint32_t width, std::uint32_t height) {
    const auto count = std::uint64_t(width) * height;
    if (!width || !height || width > detail::max_image_dimension || height > detail::max_image_dimension ||
        count > detail::max_image_pixels || count > std::numeric_limits<std::size_t>::max() / 4)
        invalid("dimensions exceed 16384 per axis or 64M pixels, or are zero");
    return static_cast<std::size_t>(count);
}

void require_decoded_bytes(std::uint64_t base_bytes, std::uint64_t lower_bytes) {
    if (base_bytes > detail::max_image_bytes || lower_bytes > detail::max_image_bytes - base_bytes)
        invalid("aggregate decoded image exceeds 256 MiB");
}

void validate_2d_rgba(const Image& image) {
    if (image.face_count != 1)
        invalid("2D image requires one face with an exact RGBA8 base payload");
    detail::validate_image_rgba(image);
}

Image image(std::uint32_t width, std::uint32_t height, ImageSourceFormat format, std::uint8_t bits) {
    return {width, height, std::vector<std::uint8_t>(pixel_count(width, height) * 4), 1, format, bits};
}

void put(Image& out, std::size_t index, const Color& color) {
    std::copy(color.begin(), color.end(), out.rgba.begin() + index * 4);
}

void put(Pixels out, std::size_t index, const Color& color) {
    std::copy(color.begin(), color.end(), out.begin() + index * 4);
}

// Independently implemented from the published TGA header/RLE specification:
// https://raw.githubusercontent.com/DEAKSoftware/Truevision-TGA/master/truevision-tga.pdf
Image tga(Bytes data) {
    require(data, 0, 18);
    const auto type = data[2];
    const auto bits = data[16];
    const auto descriptor = data[17];
    if (type != 2 && type != 10) invalid("unsupported TGA image type " + std::to_string(type));
    if (bits != 24 && bits != 32) invalid("unsupported TGA pixel depth " + std::to_string(bits));
    if ((descriptor & 0xc0) || (bits == 24 && (descriptor & 15)) ||
        (bits == 32 && (descriptor & 15) != 0 && (descriptor & 15) != 8))
        invalid("unsupported TGA interleaving or attribute bits");
    std::size_t offset = 18 + data[0];
    if (data[1] == 1) {
        const auto map_bits = data[7];
        if (map_bits != 15 && map_bits != 16 && map_bits != 24 && map_bits != 32)
            invalid("unsupported TGA auxiliary color map depth");
        offset += std::size_t(u16(data, 5)) * ((map_bits + 7) / 8);
    } else if (data[1] != 0) {
        invalid("unsupported TGA color map type");
    }
    require(data, 0, offset);
    const auto width = u16(data, 12), height = u16(data, 14);
    const auto count = pixel_count(width, height);
    const std::size_t bytes = bits / 8;
    if (type == 2) require(data, offset, count * bytes);
    else require(data, offset, ((count + 127) / 128) * (bytes + 1));
    auto out = image(width, height, ImageSourceFormat::Tga, bits);
    out.source_alpha_known = true;
    out.source_alpha_flag = bits == 32;
    const auto read_color = [&]() {
        require(data, offset, bytes);
        Color color{data[offset + 2], data[offset + 1], data[offset], 255};
        if (bits == 32 && (descriptor & 15) == 8) color[3] = data[offset + 3];
        offset += bytes;
        return color;
    };
    const auto emit = [&](std::size_t index, const Color& color) {
        auto x = index % width, y = index / width;
        if (descriptor & 0x10) x = width - 1 - x;
        if (!(descriptor & 0x20)) y = height - 1 - y;
        put(out, y * width + x, color);
    };
    for (std::size_t index = 0; index < count;) {
        std::size_t run = 1;
        bool repeated = false;
        if (type == 10) {
            require(data, offset, 1);
            const auto packet = data[offset++];
            run = (packet & 127) + 1;
            repeated = (packet & 128) != 0;
            if (run > count - index) invalid("TGA RLE packet exceeds image dimensions");
        }
        if (repeated) {
            const auto color = read_color();
            for (std::size_t n = 0; n < run; ++n) emit(index++, color);
        } else {
            for (std::size_t n = 0; n < run; ++n) emit(index++, read_color());
        }
    }
    // TGA 2.0 metadata/footer is not image data and is deliberately left intact.
    return out;
}

struct Masks {
    std::array<std::uint32_t, 4> mask;
    std::array<unsigned, 4> shift{};
    std::array<std::uint32_t, 4> maximum{};

    Masks(std::array<std::uint32_t, 4> values, unsigned bits) : mask(values) {
        std::uint32_t used = 0;
        for (unsigned c = 0; c < 4; ++c) {
            if (!mask[c]) {
                if (c < 3) invalid("RGB channel mask is missing");
                continue;
            }
            if ((used & mask[c]) || (bits < 32 && (mask[c] >> bits)))
                invalid("overlapping or out-of-range channel masks");
            shift[c] = std::countr_zero(mask[c]);
            maximum[c] = mask[c] >> shift[c];
            if ((std::uint64_t(maximum[c]) & (std::uint64_t(maximum[c]) + 1)) != 0)
                invalid("noncontiguous channel mask");
            used |= mask[c];
        }
    }

    Color decode(std::uint32_t pixel) const {
        Color out{0, 0, 0, 255};
        for (unsigned c = 0; c < 4; ++c)
            if (mask[c]) {
                const auto value = (pixel & mask[c]) >> shift[c];
                out[c] = static_cast<std::uint8_t>((std::uint64_t(value) * 255 + maximum[c] / 2) / maximum[c]);
            }
        return out;
    }
};

std::uint32_t packed_pixel(Bytes data, std::size_t offset, unsigned bytes) {
    require(data, offset, bytes);
    std::uint32_t value = 0;
    for (unsigned n = 0; n < bytes; ++n) value |= std::uint32_t(data[offset + n]) << (8 * n);
    return value;
}

Color rgb565(std::uint16_t value) {
    const auto r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)),
            static_cast<std::uint8_t>((g << 2) | (g >> 4)),
            static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255};
}

// DDS/BC1/BC2/BC3 layouts and interpolation are specified by Microsoft:
// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide
// https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression
struct DdsLayout {
    std::uint32_t width{}, height{}, fourcc{}, bits{};
    unsigned block_bytes{};
    std::optional<Masks> masks;
    GpuImageFormat gpu_format{GpuImageFormat::Rgba8Unorm};
    bool source_alpha_known{}, source_alpha_flag{};
    std::vector<GpuImageLevel> levels;
};

DdsLayout dds_layout(Bytes data, bool exact) {
    if (data.size() > detail::max_image_bytes)
        invalid("encoded image exceeds 256 MiB");
    require(data, 0, 128);
    if (u32(data, 0) != 0x20534444 || u32(data, 4) != 124 || u32(data, 76) != 32)
        invalid("invalid DDS magic or header sizes");
    const auto flags = u32(data, 8), caps = u32(data, 108), caps2 = u32(data, 112);
    if ((caps & ~0x401008u) || caps2 || u32(data, 116) || u32(data, 120))
        invalid("unsupported DDS surface caps (only single 2D textures are supported)");
    if ((flags & 0x800000) || u32(data, 24) > 1)
        invalid("unsupported DDS volume texture");
    const auto width = u32(data, 16), height = u32(data, 12);
    pixel_count(width, height);
    const auto format = u32(data, 80), fourcc = u32(data, 84), bits = u32(data, 88);
    unsigned block_bytes = 0;
    if (format & 4) {
        if (fourcc == 0x31545844) block_bytes = 8; // DXT1
        else if (fourcc == 0x33545844 || fourcc == 0x35545844) block_bytes = 16;
        else invalid("unsupported DDS FourCC " + std::to_string(fourcc));
    } else if (!(format & 0x40) || (format & ~0x41u) || (bits != 16 && bits != 24 && bits != 32)) {
        invalid("unsupported DDS pixel format/depth");
    }
    const auto levels = std::max(1u, u32(data, 28));
    const auto max_levels = std::bit_width(std::max(width, height));
    if (levels > max_levels) invalid("DDS mip count exceeds dimensions");
    const std::size_t tight = std::size_t(width) * (bits / 8);
    const std::size_t pitch = !block_bytes && (flags & 8) ? u32(data, 20) : tight;
    if (!block_bytes && pitch < tight) invalid("DDS row pitch is shorter than pixel width");
    const bool aligned_mips = !block_bytes && pitch != tight && pitch == ((tight + 3) & ~std::size_t(3));
    std::size_t offset = 128;
    DdsLayout layout;
    layout.width = width; layout.height = height;
    layout.fourcc = fourcc; layout.bits = bits; layout.block_bytes = block_bytes;
    layout.levels.reserve(levels);
    auto w = width, h = height;
    for (unsigned level = 0; level < levels; ++level) {
        std::size_t row = block_bytes ? std::size_t((w + 3) / 4) * block_bytes :
                                       std::size_t(w) * (bits / 8);
        if (!block_bytes) {
            if (!level) row = pitch;
            else if (aligned_mips) row = (row + 3) & ~std::size_t(3);
        }
        const auto size = std::uint64_t(row) * (block_bytes ? (h + 3) / 4 : h);
        if (size > std::numeric_limits<std::size_t>::max()) invalid("DDS surface size overflow");
        require(data, offset, static_cast<std::size_t>(size));
        layout.levels.push_back({w, h, offset, static_cast<std::size_t>(size),
                                 row, static_cast<std::size_t>(size)});
        offset += static_cast<std::size_t>(size);
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    if (exact && offset != data.size()) invalid("DDS payload has trailing bytes");
    if (!block_bytes) {
        layout.masks.emplace(std::array<std::uint32_t, 4>{u32(data, 92), u32(data, 96), u32(data, 100),
                                                        (format & 1) ? u32(data, 104) : 0u}, bits);
        if ((format & 1) && !u32(data, 104)) invalid("DDS alpha flag has no alpha mask");
        const auto& m = layout.masks->mask;
        if (bits == 16 && m == std::array<std::uint32_t, 4>{0xf800, 0x7e0, 0x1f, 0}) {
            layout.gpu_format = GpuImageFormat::Rgb565Unorm;
            layout.source_alpha_known = true; // D3DFMT_R5G6B5 (23)
        } else if (bits == 16 && m == std::array<std::uint32_t, 4>{0x7c00, 0x3e0, 0x1f, 0x8000}) {
            layout.gpu_format = GpuImageFormat::Argb1555Unorm;
            layout.source_alpha_known = true; // D3DFMT_A1R5G5B5 (25), predicate false
        } else if (bits == 16 && m == std::array<std::uint32_t, 4>{0xf00, 0xf0, 0xf, 0xf000}) {
            layout.source_alpha_known = layout.source_alpha_flag = true; // A4R4G4B4 (26)
        } else if (bits == 32 && m == std::array<std::uint32_t, 4>{0xff0000, 0xff00, 0xff, 0xff000000}) {
            layout.source_alpha_known = layout.source_alpha_flag = true; // A8R8G8B8 (21)
        } else if (bits == 32 &&
                   (m == std::array<std::uint32_t, 4>{0xff0000, 0xff00, 0xff, 0} ||
                    m == std::array<std::uint32_t, 4>{0xff, 0xff00, 0xff0000, 0xff000000})) {
            layout.source_alpha_known = true; // X8R8G8B8 (22), A8B8G8R8 (32)
        } else if (format == 0x40 && bits == 24 &&
                   m == std::array<std::uint32_t, 4>{0xff0000, 0xff00, 0xff, 0}) {
            // Original D3DX DDS table 0x62a758 selects format20; Acknex 1002ce40 leaves BMAP0x100 clear.
            layout.source_alpha_known = true;
        }
    } else {
        layout.gpu_format = fourcc == 0x31545844 ? GpuImageFormat::Bc1Unorm :
                            fourcc == 0x33545844 ? GpuImageFormat::Bc2Unorm : GpuImageFormat::Bc3Unorm;
        layout.source_alpha_known = true;
        layout.source_alpha_flag = fourcc != 0x31545844;
    }
    return layout;
}

void decode_dds_level(Bytes data, const DdsLayout& layout, const GpuImageLevel& level, Pixels out) {
    const auto width = level.width, height = level.height;
    auto offset = level.offset;
    const auto pitch = level.row_pitch;
    const auto bits = layout.bits, fourcc = layout.fourcc;
    const auto block_bytes = layout.block_bytes;
    const auto& masks = layout.masks;
        if (!block_bytes) {
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                    put(out, std::size_t(y) * width + x,
                        masks->decode(packed_pixel(data, offset + std::size_t(y) * pitch +
                                                   std::size_t(x) * (bits / 8), bits / 8)));
            return;
        }
        for (std::uint32_t by = 0; by < (height + 3) / 4; ++by)
            for (std::uint32_t bx = 0; bx < (width + 3) / 4; ++bx, offset += block_bytes) {
                const auto color_offset = offset + (block_bytes == 8 ? 0 : 8);
                const auto c0 = u16(data, color_offset), c1 = u16(data, color_offset + 2);
                std::array<Color, 4> colors{rgb565(c0), rgb565(c1), Color{}, Color{}};
                const bool transparent = block_bytes == 8 && c0 <= c1;
                for (unsigned c = 0; c < 3; ++c) {
                    colors[2][c] = static_cast<std::uint8_t>(transparent ? (unsigned(colors[0][c]) + colors[1][c]) / 2 :
                        (2u * colors[0][c] + colors[1][c]) / 3);
                    colors[3][c] = transparent ? 0 : static_cast<std::uint8_t>((unsigned(colors[0][c]) + 2u * colors[1][c]) / 3);
                }
                colors[2][3] = 255; colors[3][3] = transparent ? 0 : 255;
                const auto indices = u32(data, color_offset + 4);
                std::array<std::uint8_t, 8> alpha{};
                std::uint64_t alpha_indices = 0;
                if (fourcc == 0x35545844) {
                    alpha[0] = data[offset]; alpha[1] = data[offset + 1];
                    const unsigned denominator = alpha[0] > alpha[1] ? 7 : 5;
                    for (unsigned n = 1; n < denominator; ++n)
                        alpha[n + 1] = static_cast<std::uint8_t>(((denominator - n) * alpha[0] + n * alpha[1]) / denominator);
                    if (denominator == 5) { alpha[6] = 0; alpha[7] = 255; }
                    for (unsigned n = 0; n < 6; ++n) alpha_indices |= std::uint64_t(data[offset + 2 + n]) << (8 * n);
                }
                for (unsigned n = 0; n < 16; ++n) {
                    const auto x = bx * 4 + n % 4, y = by * 4 + n / 4;
                    if (x >= width || y >= height) continue;
                    auto color = colors[(indices >> (2 * n)) & 3];
                    if (fourcc == 0x33545844) color[3] = static_cast<std::uint8_t>(((data[offset + n / 2] >> (4 * (n % 2))) & 15) * 17);
                    else if (fourcc == 0x35545844) color[3] = alpha[(alpha_indices >> (3 * n)) & 7];
                    put(out, std::size_t(y) * width + x, color);
                }
            }
}

Image decode_dds_layout(Bytes data, const DdsLayout& layout) {
    const auto base_bytes = pixel_count(layout.width, layout.height) * 4;
    std::uint64_t lower_bytes = 0;
    for (std::size_t level = 1; level < layout.levels.size(); ++level)
        lower_bytes += pixel_count(layout.levels[level].width, layout.levels[level].height) * 4;
    require_decoded_bytes(base_bytes, lower_bytes);
    auto out = image(layout.width, layout.height, ImageSourceFormat::Dds,
                     layout.block_bytes ? 0 : static_cast<std::uint8_t>(layout.bits));
    out.source_alpha_known = layout.source_alpha_known;
    out.source_alpha_flag = layout.source_alpha_flag;
    out.mip_rgba.resize(static_cast<std::size_t>(lower_bytes));
    out.mip_levels.reserve(layout.levels.size() - 1);
    std::size_t destination = 0;
    for (std::size_t index = 0; index < layout.levels.size(); ++index) {
        const auto& level = layout.levels[index];
        const auto bytes = pixel_count(level.width, level.height) * 4;
        Pixels pixels = out.rgba;
        if (index) {
            out.mip_levels.push_back({level.width, level.height, destination});
            pixels = Pixels(out.mip_rgba).subspan(destination, bytes);
            destination += bytes;
        }
        decode_dds_level(data, layout, level, pixels);
    }
    return out;
}

Image dds(Bytes data, bool exact = false) {
    return decode_dds_layout(data, dds_layout(data, exact));
}

Image bmp(Bytes data) {
    require(data, 0, 54);
    if (u16(data, 0) != 0x4d42) invalid("invalid BMP signature");
    const auto file_size = u32(data, 2), start = u32(data, 10), header = u32(data, 14);
    if (file_size > data.size() || file_size < 54) invalid("invalid or truncated BMP file size");
    data = data.first(file_size);
    if (header != 40 && header != 52 && header != 56 && header != 108 && header != 124)
        invalid("unsupported BMP DIB header size " + std::to_string(header));
    require(data, 14, header);
    const auto signed_width = std::bit_cast<std::int32_t>(u32(data, 18));
    const auto signed_height = std::bit_cast<std::int32_t>(u32(data, 22));
    if (signed_width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min())
        invalid("invalid BMP signed dimensions");
    const auto width = static_cast<std::uint32_t>(signed_width);
    const auto height = static_cast<std::uint32_t>(signed_height < 0 ? -signed_height : signed_height);
    pixel_count(width, height);
    if (u16(data, 26) != 1) invalid("invalid BMP plane count");
    const auto bits = u16(data, 28);
    const auto compression = u32(data, 30);
    if ((bits != 8 && bits != 16 && bits != 24 && bits != 32) || (compression != 0 && compression != 3) ||
        (compression == 3 && (bits == 8 || bits == 24)))
        invalid("unsupported BMP compression/depth " + std::to_string(compression) + "/" + std::to_string(bits));
    std::array<std::uint32_t, 4> values = bits == 16 ?
        std::array<std::uint32_t, 4>{0x7c00, 0x3e0, 0x1f, 0} :
        std::array<std::uint32_t, 4>{0xff0000, 0xff00, 0xff, 0};
    std::size_t minimum_start = 14 + header;
    const auto palette_count = bits == 8 ? (u32(data, 46) ? u32(data, 46) : 256u) : 0u;
    if (palette_count > 256) invalid("BMP palette exceeds 256 entries");
    if (bits == 8) minimum_start += std::size_t(palette_count) * 4;
    if (compression == 3) {
        if (header == 40) minimum_start += 12;
        require(data, 54, 12);
        values = {u32(data, 54), u32(data, 58), u32(data, 62), header >= 56 ? u32(data, 66) : 0u};
    }
    if (start < minimum_start) invalid("BMP pixels overlap header or masks");
    const std::size_t stride = (std::size_t(width) * (bits / 8) + 3) & ~std::size_t(3);
    require(data, start, stride * height);
    if (bits == 8) {
        auto out = image(width, height, ImageSourceFormat::Bmp, static_cast<std::uint8_t>(bits));
        out.source_alpha_known = true;
        const std::size_t palette = 14 + header;
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto source_y = signed_height < 0 ? y : height - 1 - y;
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto index = data[start + std::size_t(source_y) * stride + x];
                if (index >= palette_count) invalid("BMP pixel references a missing palette entry");
                const auto entry = palette + std::size_t(index) * 4;
                put(out, std::size_t(y) * width + x, {data[entry + 2], data[entry + 1], data[entry], 255});
            }
        }
        return out;
    }
    const Masks masks(values, bits);
    auto out = image(width, height, ImageSourceFormat::Bmp, static_cast<std::uint8_t>(bits));
    out.source_alpha_known = bits == 24;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto source_y = signed_height < 0 ? y : height - 1 - y;
        for (std::uint32_t x = 0; x < width; ++x)
            put(out, std::size_t(y) * width + x,
                masks.decode(packed_pixel(data, start + std::size_t(source_y) * stride + std::size_t(x) * (bits / 8), bits / 8)));
    }
    return out;
}
} // namespace

namespace detail {

void validate_image_rgba(const Image& image) {
    if (image.face_count != 1 && image.face_count != 6)
        invalid("image requires one or six faces");
    const auto base_bytes = std::uint64_t(pixel_count(image.width, image.height)) * 4 * image.face_count;
    require_decoded_bytes(base_bytes, image.mip_rgba.size());
    if (image.rgba.size() != base_bytes)
        invalid("image requires an exact RGBA8 base payload");
    std::size_t lower_bytes = 0;
    auto width = image.width, height = image.height;
    for (const auto& mip : image.mip_levels) {
        if ((width == 1 && height == 1) ||
            mip.width != std::max(1u, width / 2) ||
            mip.height != std::max(1u, height / 2) || mip.offset != lower_bytes)
            invalid("image lower mip dimensions or offset mismatch");
        const auto bytes = std::uint64_t(pixel_count(mip.width, mip.height)) * 4 * image.face_count;
        require_decoded_bytes(base_bytes, std::uint64_t(lower_bytes) + bytes);
        lower_bytes += static_cast<std::size_t>(bytes);
        width = mip.width;
        height = mip.height;
    }
    if (lower_bytes != image.mip_rgba.size())
        invalid("image lower mip pixel payload size mismatch");
}

Image decode_image_bytes(Bytes bytes, ImageSourceFormat format) {
    if (bytes.size() > max_image_bytes) invalid("encoded image exceeds 256 MiB");
    switch (format) {
    case ImageSourceFormat::Dds: return dds(bytes);
    case ImageSourceFormat::Tga: return tga(bytes);
    case ImageSourceFormat::Bmp: return bmp(bytes);
    default: invalid("unsupported source image format");
    }
}

GpuImage gpu_dds_bytes(std::vector<std::uint8_t>&& input, bool exact,
                       Image* cpu_diagnostic, bool retain_encoded) {
    auto bytes = std::move(input);
    auto layout = dds_layout(bytes, exact);
    GpuImage out;
    out.width = layout.width; out.height = layout.height;
    out.format = layout.gpu_format;
    out.source_format = ImageSourceFormat::Dds;
    out.source_bits_per_pixel = layout.block_bytes ? 0 : static_cast<std::uint8_t>(layout.bits);
    out.source_alpha_known = layout.source_alpha_known;
    out.source_alpha_flag = layout.source_alpha_flag;
    if (out.format != GpuImageFormat::Rgba8Unorm) {
        if (cpu_diagnostic) *cpu_diagnostic = decode_dds_layout(bytes, layout);
        out.texels = std::move(bytes);
        out.levels = std::move(layout.levels);
        return out;
    }
    std::uint64_t decoded_bytes = 0;
    for (const auto& level : layout.levels)
        decoded_bytes += pixel_count(level.width, level.height) * 4;
    require_decoded_bytes(decoded_bytes, 0);
    if (retain_encoded && bytes.size() > max_image_bytes - decoded_bytes)
        invalid("aggregate retained DDS and decoded image exceeds 256 MiB");
    out.implementation_precision_fallback = true;
    out.texels.resize(static_cast<std::size_t>(decoded_bytes));
    out.levels.reserve(layout.levels.size());
    std::size_t destination = 0;
    for (const auto& level : layout.levels) {
        const auto size = pixel_count(level.width, level.height) * 4;
        decode_dds_level(bytes, layout, level, Pixels(out.texels).subspan(destination, size));
        out.levels.push_back({level.width, level.height, destination, size,
                              std::size_t(level.width) * 4, size});
        destination += size;
    }
    if (cpu_diagnostic) {
        const auto base_end = out.texels.begin() + out.levels.front().byte_size;
        Image cpu{out.width, out.height, std::vector<std::uint8_t>(out.texels.begin(), base_end),
                  1, out.source_format, out.source_bits_per_pixel};
        cpu.source_alpha_known = out.source_alpha_known;
        cpu.source_alpha_flag = out.source_alpha_flag;
        cpu.mip_rgba.assign(base_end, out.texels.end());
        cpu.mip_levels.reserve(out.levels.size() - 1);
        const auto base_bytes = out.levels.front().byte_size;
        for (std::size_t index = 1; index < out.levels.size(); ++index) {
            const auto& level = out.levels[index];
            cpu.mip_levels.push_back({level.width, level.height, level.offset - base_bytes});
        }
        *cpu_diagnostic = std::move(cpu);
    }
    if (retain_encoded) out.authored_dds = std::move(bytes);
    return out;
}

namespace {

void decode_runtime_bc_level(const GpuImage& image, const GpuImageLevel& level, Pixels output) {
    const auto block_bytes = bc_block_bytes(image.format);
    const Bytes source = image.texels;
    BcBlock block;
    for (std::size_t face = 0; face < image.face_count; ++face)
        for (std::uint32_t by = 0; by < (level.height + 3) / 4; ++by)
            for (std::uint32_t bx = 0; bx < (level.width + 3) / 4; ++bx) {
                const auto offset = level.offset + face * level.face_stride +
                                    std::size_t(by) * level.row_pitch + std::size_t(bx) * block_bytes;
                decode_bc_block(image.format, source.subspan(offset, block_bytes), block);
                for (unsigned pixel = 0; pixel < block.size(); ++pixel) {
                    const auto x = bx * 4 + pixel % 4, y = by * 4 + pixel / 4;
                    if (x >= level.width || y >= level.height) continue;
                    const auto target = ((face * level.height + y) * level.width + x) * 4;
                    for (unsigned channel = 0; channel < 4; ++channel)
                        output[target + channel] = static_cast<std::uint8_t>(
                            std::clamp(block[pixel][channel], 0.f, 1.f) * 255.f + .5f);
                }
            }
}

Image decode_runtime_bc_image(const GpuImage& image) {
    std::uint64_t total_bytes = 0;
    for (const auto& level : image.levels) {
        const auto bytes = std::uint64_t(pixel_count(level.width, level.height)) * image.face_count * 4;
        require_decoded_bytes(total_bytes, bytes);
        total_bytes += bytes;
    }
    const auto base_bytes = pixel_count(image.width, image.height) * std::size_t(image.face_count) * 4;
    Image out{image.width, image.height, std::vector<std::uint8_t>(base_bytes), image.face_count,
              image.source_format, image.source_bits_per_pixel};
    out.source_alpha_known = image.source_alpha_known;
    out.source_alpha_flag = image.source_alpha_flag;
    out.mip_rgba.resize(static_cast<std::size_t>(total_bytes) - base_bytes);
    out.mip_levels.reserve(image.levels.size() - 1);
    std::size_t destination = 0;
    for (std::size_t index = 0; index < image.levels.size(); ++index) {
        const auto& level = image.levels[index];
        const auto bytes = pixel_count(level.width, level.height) * std::size_t(image.face_count) * 4;
        Pixels pixels = out.rgba;
        if (index) {
            out.mip_levels.push_back({level.width, level.height, destination});
            pixels = Pixels(out.mip_rgba).subspan(destination, bytes);
            destination += bytes;
        }
        // Explicit normalized-FLOAT4 to RGBA8 diagnostics, not original CPU DDS expansion.
        decode_runtime_bc_level(image, level, pixels);
    }
    return out;
}

} // namespace

Image decode_dds_gpu(const GpuImage& image) {
    validate_gpu_image(image);
    if (image.runtime && image.levels.front().offset == 0)
        return decode_runtime_bc_image(image);
    const auto layout = dds_layout(image.texels, false);
    if (image.source_format != ImageSourceFormat::Dds || image.face_count != 1 ||
        image.width != layout.width || image.height != layout.height ||
        image.format != layout.gpu_format || image.levels.size() != layout.levels.size())
        invalid("DDS GPU image descriptor does not match its source");
    for (std::size_t index = 0; index < layout.levels.size(); ++index) {
        const auto& expected = layout.levels[index];
        const auto& actual = image.levels[index];
        if (actual.width != expected.width || actual.height != expected.height ||
            actual.offset != expected.offset || actual.byte_size != expected.byte_size ||
            actual.row_pitch != expected.row_pitch || actual.face_stride != expected.face_stride)
            invalid("DDS GPU image level does not match its source");
    }
    return decode_dds_layout(image.texels, layout);
}

} // namespace detail

Image decode_skin_image(const Skin& skin, std::span<const std::uint8_t> palette) {
    const auto format = skin.format;
    if (format > 7 || (skin.flags & 7) != format || (skin.flags & 0xc0))
        invalid("unsupported skin flags or format/type mismatch");
    const bool mipmaps = (skin.flags & 8) != 0;
    if (mipmaps && (format == 0 || format == 6 || format == 7))
        invalid("unsupported skin mip flag for indexed, DDS or external data");
    if (format == 7)
        invalid("skin external_reference: caller must safely resolve external_texture");
    const Bytes data = skin.payload;
    if (format == 6) {
        if (skin.height != 1 || skin.width != data.size())
            invalid("skin DDS descriptor must have width = payload bytes and height = 1");
        return dds(data, true);
    }
    if (format != 0 && format != 2 && format != 3 && format != 4 && format != 5)
        invalid("unsupported skin pixel format " + std::to_string(format));
    const auto count = pixel_count(skin.width, skin.height);
    const unsigned bytes = format == 0 ? 1 : format <= 3 ? 2 : format - 1;
    std::uint64_t expected = std::uint64_t(count) * bytes;
    if (mipmaps) {
        if ((skin.width & 7) || (skin.height & 7))
            invalid("skin mip dimensions must be divisible by 8");
        for (unsigned level = 1; level <= 3; ++level)
            expected += std::uint64_t(skin.width >> level) * (skin.height >> level) * bytes;
    }
    const auto lower_bytes = (expected / bytes - count) * 4;
    require_decoded_bytes(count * 4, lower_bytes);
    if (expected > std::numeric_limits<std::size_t>::max() || expected != data.size())
        invalid("skin pixel payload size mismatch (base and all three mip levels required)");
    if (format == 0 && palette.size() != 768)
        invalid("skin missing palette: exactly 768 original RGB palette bytes required");
    auto out = image(skin.width, skin.height, ImageSourceFormat::ModelSkin,
                     static_cast<std::uint8_t>(bytes * 8));
    // Original raw-skin initializer 10060e40 sets alpha100 only for kinds 3/5.
    out.source_alpha_known = true;
    out.source_alpha_flag = format == 3 || format == 5;
    const auto decode_pixels = [&](Bytes data, Pixels out) {
        const auto count = out.size() / 4;
        switch (format) {
        case 0:
            for (std::size_t n = 0; n < count; ++n) {
                const auto entry = std::size_t(data[n]) * 3;
                put(out, n, {palette[entry], palette[entry + 1], palette[entry + 2], 255});
            }
            break;
        case 2:
            for (std::size_t n = 0; n < count; ++n)
                put(out, n, rgb565(u16(data, n * 2)));
            break;
        case 3:
            for (std::size_t n = 0; n < count; ++n) {
                const auto pixel = u16(data, n * 2);
                put(out, n, {static_cast<std::uint8_t>(((pixel >> 8) & 15) * 17),
                             static_cast<std::uint8_t>(((pixel >> 4) & 15) * 17),
                             static_cast<std::uint8_t>((pixel & 15) * 17),
                             static_cast<std::uint8_t>((pixel >> 12) * 17)});
            }
            break;
        case 4:
            for (std::size_t n = 0; n < count; ++n)
                put(out, n, {data[n * 3 + 2], data[n * 3 + 1], data[n * 3], 255});
            break;
        case 5:
            for (std::size_t n = 0; n < count; ++n)
                put(out, n, {data[n * 4 + 2], data[n * 4 + 1], data[n * 4], data[n * 4 + 3]});
            break;
        }
    };
    decode_pixels(data.first(count * bytes), out.rgba);
    if (mipmaps) {
        out.mip_rgba.resize(static_cast<std::size_t>(lower_bytes));
        out.mip_levels.reserve(3);
        std::size_t source_offset = count * bytes, destination = 0;
        for (unsigned level = 1; level <= 3; ++level) {
            const auto width = skin.width >> level, height = skin.height >> level;
            const auto pixels = pixel_count(width, height);
            out.mip_levels.push_back({width, height, destination});
            decode_pixels(data.subspan(source_offset, pixels * bytes),
                          Pixels(out.mip_rgba).subspan(destination, pixels * 4));
            source_offset += pixels * bytes;
            destination += pixels * 4;
        }
    }
    return out;
}

Image load_image(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".dds" && extension != ".tga" && extension != ".bmp")
        invalid("unsupported image extension " + extension);
    const auto data = read_resource(path);
    try {
        return detail::decode_image_bytes(data, extension == ".dds" ? ImageSourceFormat::Dds :
                                         extension == ".tga" ? ImageSourceFormat::Tga : ImageSourceFormat::Bmp);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

void apply_upper_left_color_key(Image& image) {
    validate_2d_rgba(image);

    const std::array<std::uint8_t, 3> key{image.rgba[0], image.rgba[1], image.rgba[2]};
    const auto clear_matching_alpha = [&](Pixels pixels) {
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
            if (pixels[offset] == key[0] && pixels[offset + 1] == key[1] &&
                pixels[offset + 2] == key[2])
                pixels[offset + 3] = 0;
    };
    clear_matching_alpha(image.rgba);
    clear_matching_alpha(image.mip_rgba);
}

Image cubemap_from_image(const Image& strip) {
    const auto count = pixel_count(strip.width, strip.height);
    const auto size = strip.height;
    if (strip.face_count != 1 || !std::has_single_bit(size) ||
        std::uint64_t(size) * 6 != strip.width)
        invalid("cubemap requires six horizontal square power-of-two faces");
    if (strip.rgba.size() != count * 4)
        invalid("cubemap pixel payload size mismatch");
    std::size_t lower_bytes = 0;
    auto previous_height = size;
    for (const auto& mip : strip.mip_levels) {
        const auto pixels = pixel_count(mip.width, mip.height);
        if (previous_height <= 1 || mip.height != previous_height / 2 ||
            std::uint64_t(mip.height) * 6 != mip.width || mip.offset != lower_bytes)
            invalid("cubemap lower mip dimensions or offset mismatch");
        lower_bytes += pixels * 4;
        previous_height = mip.height;
    }
    if (lower_bytes != strip.mip_rgba.size())
        invalid("cubemap lower mip pixel payload size mismatch");
    require_decoded_bytes(count * 4, lower_bytes);
    Image out{size, size, std::vector<std::uint8_t>(strip.rgba.size()), 6,
              strip.source_format, strip.source_bits_per_pixel};
    out.source_alpha_known = strip.source_alpha_known;
    out.source_alpha_flag = strip.source_alpha_flag;
    out.mip_rgba.resize(lower_bytes);
    out.mip_levels.reserve(strip.mip_levels.size());
    // Original acknex.dll 1002f980 (bmap_convert mode 3) copies these rectangles
    // to D3D faces +X,-X,+Y,-Y,+Z,-Z with D3DX_FILTER_NONE and no rotation.
    constexpr std::array<unsigned, 6> source_face{2, 0, 5, 4, 1, 3};
    const auto copy_faces = [&](Bytes source, Pixels destination, std::uint32_t size) {
        const auto row_bytes = std::size_t(size) * 4;
        const auto face_bytes = row_bytes * size;
        for (std::size_t face = 0; face < source_face.size(); ++face)
            for (std::size_t row = 0; row < size; ++row)
                std::copy_n(source.data() + row * row_bytes * 6 + source_face[face] * row_bytes,
                            row_bytes, destination.data() + face * face_bytes + row * row_bytes);
    };
    copy_faces(strip.rgba, out.rgba, size);
    for (const auto& mip : strip.mip_levels) {
        const auto bytes = std::size_t(mip.width) * mip.height * 4;
        out.mip_levels.push_back({mip.height, mip.height, mip.offset});
        copy_faces(Bytes(strip.mip_rgba).subspan(mip.offset, bytes),
                   Pixels(out.mip_rgba).subspan(mip.offset, bytes), mip.height);
    }
    return out;
}

Image load_cubemap(const std::filesystem::path& path) {
    const auto strip = load_image(path);
    try {
        return cubemap_from_image(strip);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
} // namespace c4::assets
