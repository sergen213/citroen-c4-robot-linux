#include "GpuImage.hpp"
#include "GpuImageInternal.hpp"
#include "GpuImageBc.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace c4::assets {
namespace {
[[noreturn]] void runtime_error(const char* reason) {
    throw std::runtime_error(std::string("GPU image runtime: ") + reason);
}

bool block_format(GpuImageFormat format) {
    return format == GpuImageFormat::Bc1Unorm || format == GpuImageFormat::Bc2Unorm ||
           format == GpuImageFormat::Bc3Unorm;
}

struct Channels {
    std::array<unsigned, 4> bits, shifts;
    std::size_t bytes;
};

Channels channels(GpuImageFormat format) {
    switch (format) {
    case GpuImageFormat::Rgba8Unorm: return {{8, 8, 8, 8}, {0, 8, 16, 24}, 4};
    case GpuImageFormat::Rgb565Unorm: return {{5, 6, 5, 0}, {11, 5, 0, 0}, 2};
    case GpuImageFormat::Argb1555Unorm: return {{5, 5, 5, 1}, {10, 5, 0, 15}, 2};
    default: runtime_error("packed channel conversion requires an uncompressed format");
    }
}

std::uint32_t read_pixel(const std::uint8_t* p, std::size_t bytes) {
    std::uint32_t value = p[0] | (std::uint32_t(p[1]) << 8);
    if (bytes == 4) value |= (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    return value;
}

void write_pixel(std::uint8_t* p, std::size_t bytes, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
    if (bytes == 4) {
        p[2] = static_cast<std::uint8_t>(value >> 16);
        p[3] = static_cast<std::uint8_t>(value >> 24);
    }
}


double tent_integral(double z) {
    if (z <= -1) return 0;
    if (z < 0) return .5 * (z + 1) * (z + 1);
    if (z < 1) return 1 - .5 * (1 - z) * (1 - z);
    return 1;
}

struct AxisRange { std::size_t offset; int first, count; };
struct AxisWeights {
    std::vector<AxisRange> ranges;
    std::vector<double> weights;
};

AxisWeights triangle_axis(std::uint32_t source, std::uint32_t destination) {
    AxisWeights axis;
    const auto scale = double(source) / destination;
    const auto stride = static_cast<std::size_t>(std::ceil(scale)) + 4;
    axis.ranges.reserve(destination);
    axis.weights.reserve(std::size_t(destination) * stride);
    for (std::uint32_t x = 0; x < destination; ++x) {
        const auto a = x * scale - .5, b = (x + 1) * scale - .5;
        const auto first = static_cast<int>(std::floor(a)) - 1;
        const auto count = static_cast<int>(std::ceil(b)) + 1 - first;
        axis.ranges.push_back({axis.weights.size(), first, count});
        for (int i = first; i < first + count; ++i)
            axis.weights.push_back((tent_integral(b - i) - tent_integral(a - i)) / scale);
    }
    return axis;
}

std::uint32_t wrapped(int value, std::uint32_t extent) {
    const auto result = value % static_cast<int>(extent);
    return static_cast<std::uint32_t>(result < 0 ? result + extent : result);
}

struct BcAxisWeights {
    std::vector<AxisRange> ranges;
    std::vector<std::uint32_t> indices;
    std::vector<double> weights;
};

BcAxisWeights bc_triangle_axis(std::uint32_t source, std::uint32_t destination) {
    auto raw = triangle_axis(source, destination);
    BcAxisWeights axis;
    axis.ranges.reserve(destination);
    axis.indices.reserve(raw.weights.size());
    axis.weights.reserve(raw.weights.size());
    std::vector<std::pair<std::uint32_t, double>> scratch;
    std::size_t maximum_count = 0;
    for (const auto& range : raw.ranges)
        maximum_count = std::max(maximum_count, std::size_t(range.count));
    scratch.reserve(maximum_count);
    for (const auto& range : raw.ranges) {
        scratch.clear();
        for (int i = 0; i < range.count; ++i)
            scratch.emplace_back(wrapped(range.first + i, source), raw.weights[range.offset + i]);
        std::sort(scratch.begin(), scratch.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        const auto offset = axis.weights.size();
        for (std::size_t i = 0; i < scratch.size();) {
            const auto index = scratch[i].first;
            double weight = 0;
            do { weight += scratch[i++].second; } while (i < scratch.size() && scratch[i].first == index);
            // Original coefficient builder drops tiny aggregate weights and
            // does not renormalize. Keep source-major accumulation order.
            if (weight <= std::bit_cast<float>(0x3727c5acu)) continue;
            axis.indices.push_back(index);
            axis.weights.push_back(weight);
        }
        axis.ranges.push_back({offset, 0, static_cast<int>(axis.weights.size() - offset)});
    }
    return axis;
}
// Original normalized converter constants, not an RGBA8 expansion of packed
// channels. Filter arithmetic remains explicitly portable-FP precision.
std::array<float, 4> decode_channels(std::uint32_t pixel, const Channels& format) {
    std::array<float, 4> out{};
    for (unsigned k = 0; k < 4; ++k) {
        const auto bits = format.bits[k];
        const auto reciprocal = bits == 8 ? std::bit_cast<float>(0x3b808081u) :
            bits == 6 ? std::bit_cast<float>(0x3c820821u) :
            bits == 5 ? std::bit_cast<float>(0x3d042108u) : 1.f;
        out[k] = bits ? float((pixel >> format.shifts[k]) & ((1u << bits) - 1)) * reciprocal : 1.f;
    }
    return out;
}

// Original writers set x87 RC=toward-zero for separate FLOAT scale/bias
// spills. Positive binary32 conversion is reproduced without changing caller
// FP state; double exactly represents these bounded float*integer operations.
float positive_float_toward_zero(double value) {
    const auto rounded = static_cast<float>(value);
    return double(rounded) > value ? std::nextafter(rounded, 0.f) : rounded;
}

std::uint32_t encode_channels(const std::array<float, 4>& pixel, const Channels& format,
                              std::uint32_t x, std::uint32_t y, std::uint32_t width,
                              bool dither) {
    static constexpr unsigned biases[4][4] = {
        {31, 15, 27, 11}, {7, 23, 3, 19}, {25, 9, 29, 13}, {1, 17, 5, 21}};
    // Table uses traversal position, not physical X: odd rows walk right-left.
    const auto traversal = (y & 1) ? width - 1 - x : x;
    const float bias = dither ? biases[y & 3][traversal & 3] / 32.f : .5f;
    std::uint32_t out = 0;
    for (unsigned k = 0; k < 4; ++k) if (format.bits[k]) {
        const auto maximum = (1u << format.bits[k]) - 1;
        const auto value = std::clamp(pixel[k], 0.f, 1.f);
        const auto scaled = positive_float_toward_zero(double(value) * maximum);
        const auto biased = positive_float_toward_zero(double(scaled) + bias);
        out |= std::min(static_cast<std::uint32_t>(biased), maximum) << format.shifts[k];
    }
    return out;
}
std::array<float, 4> decode_upload_pixel(std::uint32_t pixel, const Channels& format,
                                       bool opaque_black_key) {
    auto out = decode_channels(pixel, format);
    // Original API key compares exact normalized RGBA before filtering;
    // opaque black becomes FOUR zero components, unlike the manual RGB key.
    if (opaque_black_key && out == std::array<float, 4>{0, 0, 0, 1}) out = {};
    return out;
}

void copy_rectangle(const std::uint8_t* src, std::size_t src_pitch, const Channels& from,
                    std::uint8_t* dst, std::size_t dst_pitch, const Channels& to,
                    std::uint32_t width, std::uint32_t height, bool opaque_black_key = false) {
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* row = src + y * src_pitch;
        auto* target = dst + y * dst_pitch;
        if (from.bits == to.bits && !opaque_black_key)
            std::memcpy(target, row, std::size_t(width) * to.bytes);
        else for (std::uint32_t x = 0; x < width; ++x) {
            const auto pixel = decode_upload_pixel(
                read_pixel(row + x * from.bytes, from.bytes), from, opaque_black_key);
            write_pixel(target + x * to.bytes, to.bytes,
                        encode_channels(pixel, to, x, y, width, false));
        }
    }
}

void triangle_rectangle(const std::uint8_t* src, const GpuImageLevel& source,
                        const Channels& from, std::uint8_t* dst, const GpuImageLevel& target,
                        const Channels& to, std::uint32_t width, std::uint32_t height,
                        bool dither = false, bool opaque_black_key = false) {
    // Equal extents use the original dispatcher's copy/conversion path, even
    // when ordered dither is requested.
    if (source.width == width && source.height == height) {
        copy_rectangle(src, source.row_pitch, from, dst, target.row_pitch, to, width, height,
                       opaque_black_key);
        return;
    }
    // Share the bounded wrap/aggregate/threshold coefficient model with BC.
    // Sorted source indices reproduce source-y then source-x scatter order for
    // each destination accumulator, without allocating a full float image.
    const auto xs = bc_triangle_axis(source.width, width);
    const auto ys = bc_triangle_axis(source.height, height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto& yr = ys.ranges[y];
        auto* dst_row = dst + y * target.row_pitch;
        for (std::uint32_t x = 0; x < width; ++x) {
            std::array<float, 4> pixel{};
            const auto& xr = xs.ranges[x];
            for (int j = 0; j < yr.count; ++j) {
                const auto* row = src + ys.indices[yr.offset + j] * source.row_pitch;
                const auto wy = ys.weights[yr.offset + j];
                for (int i = 0; i < xr.count; ++i) {
                    const auto wx = xs.weights[xr.offset + i];
                    const auto sample = decode_upload_pixel(read_pixel(
                        row + xs.indices[xr.offset + i] * from.bytes, from.bytes), from, opaque_black_key);
                    for (unsigned k = 0; k < 4; ++k)
                        pixel[k] = static_cast<float>(double(pixel[k]) + sample[k] * (wx * wy));
                }
            }
            write_pixel(dst_row + x * to.bytes, to.bytes,
                        encode_channels(pixel, to, x, y, width, dither));
        }
    }
}

void apply_key(GpuImage& image, std::uint32_t supplied, std::uint32_t source_width,
               std::uint32_t source_height) {
    const auto format = channels(image.format);
    const auto alpha = image.format == GpuImageFormat::Rgba8Unorm ? 0xff000000u : 0x8000u;
    const auto key = read_pixel(image.texels.data() + image.levels.front().offset, format.bytes);
    for (std::uint32_t lod = 0; lod < supplied; ++lod) {
        const auto& level = image.levels[lod];
        // Original 1002dc5f..69 uses BMAP authored dimensions shifted by LOD,
        // not the resized destination rectangle or the whole allocation.
        for (std::uint32_t y = 0; y < source_height; ++y)
            for (std::uint32_t x = 0; x < source_width; ++x) {
                auto* p = image.texels.data() + level.offset + y * level.row_pitch + x * format.bytes;
                const auto pixel = read_pixel(p, format.bytes);
                write_pixel(p, format.bytes, pixel == key ? pixel & ~alpha : pixel | alpha);
            }
        source_width >>= 1;
        source_height >>= 1;
    }
    image.color_key_applied = true;
}

void box_level(GpuImage& image, std::uint32_t lod, const Channels& format) {
    const auto& source = image.levels[lod - 1];
    const auto& target = image.levels[lod];
    // Collapsed axes use the recovered generic normalized converter, not the
    // optimized integer 2x2 kernel. Its portable FP path remains explicit.
    const bool generic = source.width == 1 || source.height == 1;
    if (generic) image.implementation_precision_fallback = true;
    for (std::uint32_t y = 0; y < target.height; ++y)
        for (std::uint32_t x = 0; x < target.width; ++x) {
            std::array<std::uint32_t, 4> samples{};
            for (unsigned j = 0; j < 2; ++j) for (unsigned i = 0; i < 2; ++i)
                samples[2 * j + i] = read_pixel(image.texels.data() + source.offset +
                    std::min(2 * y + j, source.height - 1) * source.row_pitch +
                    std::min(2 * x + i, source.width - 1) * format.bytes, format.bytes);
            std::uint32_t pixel = 0;
            if (generic) {
                std::array<std::array<float, 4>, 4> decoded{};
                for (unsigned i = 0; i < 4; ++i) decoded[i] = decode_channels(samples[i], format);
                std::array<float, 4> filtered{};
                for (unsigned k = 0; k < 4; ++k)
                    filtered[k] = static_cast<float>((double(decoded[0][k]) + decoded[1][k] +
                                                     decoded[2][k] + decoded[3][k]) * .25);
                pixel = encode_channels(filtered, format, x, y, target.width, false);
            } else for (unsigned k = 0; k < 4; ++k) if (format.bits[k]) {
                const auto mask = (1u << format.bits[k]) - 1;
                unsigned sum = 0;
                for (const auto sample : samples) sum += (sample >> format.shifts[k]) & mask;
                pixel |= ((sum + 2) >> 2) << format.shifts[k];
            }
            write_pixel(image.texels.data() + target.offset + y * target.row_pitch +
                        x * format.bytes, format.bytes, pixel);
        }
}

void dimensions(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > detail::max_image_dimension || height > detail::max_image_dimension ||
        std::uint64_t(width) * height > detail::max_image_pixels)
        runtime_error("derived dimensions exceed 16384 per axis or 64M pixels, or are zero");
}

GpuImageLevel tight_level(std::uint32_t width, std::uint32_t height, std::size_t bytes,
                          std::size_t offset) {
    const auto pitch = std::size_t(width) * bytes;
    const auto size = pitch * height;
    return {width, height, offset, size, pitch, size};
}

std::span<const std::uint8_t> bc_bytes(std::span<const std::uint8_t> storage,
                                     const GpuImageLevel& level, std::uint32_t bx,
                                     std::uint32_t by, std::size_t bytes) {
    const auto relative = std::size_t(by) * level.row_pitch + std::size_t(bx) * bytes;
    if (relative > level.byte_size || bytes > level.byte_size - relative ||
        level.offset > storage.size() || relative > storage.size() - level.offset ||
        bytes > storage.size() - level.offset - relative)
        runtime_error("BC block lies outside the validated level span");
    return storage.subspan(level.offset + relative, bytes);
}

// Four decoded blocks cover every 8x8 source footprint for a 4x4 BOX target.
// This also keeps TRIANGLE sampling bounded without an image-sized FLOAT4 copy.
struct BcSamples {
    GpuImageFormat format;
    std::span<const std::uint8_t> storage;
    const GpuImageLevel& level;
    std::array<detail::BcBlock, 4> blocks{};
    std::array<std::size_t, 4> keys{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};

    const detail::BcPixel& at(std::uint32_t x, std::uint32_t y) {
        const auto bx = x / 4, by = y / 4;
        const auto key = std::size_t(by) * ((level.width + 3) / 4) + bx;
        const auto slot = (by & 1) * 2 + (bx & 1);
        if (keys[slot] != key) {
            detail::decode_bc_block(format, bc_bytes(storage, level, bx, by,
                detail::bc_block_bytes(format)), blocks[slot]);
            keys[slot] = key;
        }
        return blocks[slot][(y & 3) * 4 + (x & 3)];
    }
};


void bc_box_level(GpuImage& image, std::uint32_t lod) {
    const auto& source = image.levels[lod - 1];
    const auto& target = image.levels[lod];
    const auto bytes = detail::bc_block_bytes(image.format);
    BcSamples samples{image.format, image.texels, source};
    for (std::uint32_t by = 0; by < (target.height + 3) / 4; ++by)
        for (std::uint32_t bx = 0; bx < (target.width + 3) / 4; ++bx) {
            detail::BcBlock block;
            const auto offset = target.offset + std::size_t(by) * target.row_pitch + bx * bytes;
            // Original writer preserves unwritten texels by decoding EXISTING
            // destination storage, including authored small-mip padding.
            detail::decode_bc_block(image.format, bc_bytes(image.texels, target, bx, by, bytes), block);
            for (unsigned y = 0; y < 4 && by * 4 + y < target.height; ++y)
                for (unsigned x = 0; x < 4 && bx * 4 + x < target.width; ++x) {
                    auto& pixel = block[y * 4 + x];
                    for (unsigned k = 0; k < 4; ++k) {
                        // 4d5784..4d57e7 adds the four normalized FLOAT4
                        // samples successively before multiplying by 1/4.
                        const auto sx = (bx * 4 + x) * 2, sy = (by * 4 + y) * 2;
                        const auto x0 = std::min(sx, source.width - 1);
                        const auto x1 = std::min(sx + 1, source.width - 1);
                        const auto y0 = std::min(sy, source.height - 1);
                        const auto y1 = std::min(sy + 1, source.height - 1);
                        pixel[k] = (((samples.at(x0, y0)[k] + samples.at(x1, y0)[k]) +
                                     samples.at(x0, y1)[k]) + samples.at(x1, y1)[k]) * .25f;
                    }
                }
            detail::encode_bc_block(image.format, block,
                std::span<std::uint8_t>(image.texels).subspan(offset, bytes));
        }
}

void bc_triangle_rectangle(GpuImage& image, std::span<const std::uint8_t> authored,
                           const GpuImageLevel& source, const GpuImageLevel& target,
                           std::uint32_t width, std::uint32_t height, bool dither) {
    const auto bytes = detail::bc_block_bytes(image.format);
    const auto xs = bc_triangle_axis(source.width, width), ys = bc_triangle_axis(source.height, height);
    BcSamples samples{image.format, authored, source};
    for (std::uint32_t by = 0; by < (height + 3) / 4; ++by)
        for (std::uint32_t bx = 0; bx < (width + 3) / 4; ++bx) {
            detail::BcBlock block;
            detail::decode_bc_block(image.format, bc_bytes(image.texels, target, bx, by, bytes), block);
            for (unsigned y = 0; y < 4 && by * 4 + y < height; ++y)
                for (unsigned x = 0; x < 4 && bx * 4 + x < width; ++x) {
                    auto& pixel = block[y * 4 + x];
                    pixel = {};
                    const auto& yr = ys.ranges[by * 4 + y];
                    const auto& xr = xs.ranges[bx * 4 + x];
                    for (int j = 0; j < yr.count; ++j) for (int i = 0; i < xr.count; ++i) {
                        const auto wx = xs.weights[xr.offset + i], wy = ys.weights[yr.offset + j];
                        const auto& sample = samples.at(xs.indices[xr.offset + i],
                                                        ys.indices[yr.offset + j]);
                        for (unsigned k = 0; k < 4; ++k)
                            pixel[k] = static_cast<float>(double(pixel[k]) + sample[k] * (wx * wy));
                    }
                    for (auto& component : pixel) component = std::clamp(component, 0.f, 1.f);
                }
            const auto offset = target.offset + std::size_t(by) * target.row_pitch + bx * bytes;
            detail::encode_bc_block(image.format, block,
                std::span<std::uint8_t>(image.texels).subspan(offset, bytes), dither);
        }
}

GpuImage derive_bc_upload(GpuImage&& source, GpuImageRuntimeInfo info, std::uint32_t width,
                         std::uint32_t height, std::uint32_t requested, bool same_shape) {
    if (same_shape && !info.encoded_postfilter_attempted && requested == source.levels.size()) {
        source.runtime = info;
        return std::move(source);
    }
    GpuImage out = std::move(source);
    const auto bytes = detail::bc_block_bytes(out.format);
    auto authored_levels = std::move(out.levels);
    auto authored = std::move(out.texels);
    out.width = width; out.height = height;
    out.levels.clear();
    out.levels.reserve(requested);
    std::size_t total = 0;
    auto w = width, h = height;
    for (std::uint32_t lod = 0; lod < requested; ++lod) {
        const auto pitch = std::size_t((w + 3) / 4) * bytes;
        const auto size = pitch * ((h + 3) / 4);
        if (size > detail::max_image_bytes - total)
            runtime_error("aggregate derived BC upload exceeds 256 MiB");
        out.levels.push_back({w, h, total, size, pitch, size});
        total += size;
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    if (authored.size() > detail::max_image_bytes - total ||
        out.authored_dds.size() > detail::max_image_bytes - total - authored.size())
        runtime_error("aggregate derived BC and original DDS storage exceeds 256 MiB");
    out.texels.resize(total);
    info.original_padding_known = info.written_width == width && info.written_height == height;
    // Seed every available matching physical destination with its authored
    // bytes BEFORE SrcLevel0 regeneration overwrites the logical texels.
    for (std::uint32_t lod = 0; lod < std::min<std::size_t>(requested, authored_levels.size()); ++lod) {
        const auto& input = authored_levels[lod];
        const auto& target = out.levels[lod];
        if (input.width != target.width || input.height != target.height) continue;
        for (std::uint32_t row = 0; row < (target.height + 3) / 4; ++row)
            std::memcpy(out.texels.data() + target.offset + row * target.row_pitch,
                        authored.data() + input.offset + row * input.row_pitch, target.row_pitch);
    }
    if (!same_shape) {
        if (info.resize_filter == 4) {
            bc_triangle_rectangle(out, authored, authored_levels.front(), out.levels.front(),
                                  info.written_width, info.written_height, false);
        } else {
            const auto& input = authored_levels.front();
            const auto& target = out.levels.front();
            for (std::uint32_t row = 0; row < (input.height + 3) / 4; ++row)
                std::memcpy(out.texels.data() + target.offset + row * target.row_pitch,
                            authored.data() + input.offset + row * input.row_pitch, input.row_pitch);
        }
    }
    for (std::uint32_t lod = 1; lod < requested; ++lod) {
        const auto& level = out.levels[lod];
        if ((level.width < 4 || level.height < 4) &&
            (lod >= authored_levels.size() || authored_levels[lod].width != level.width ||
             authored_levels[lod].height != level.height)) info.original_padding_known = false;
        if (info.mip_filter == 5) bc_box_level(out, lod);
        // Original NPOT DEFAULT keeps initial SrcLevel0 for EVERY output;
        // only POT BOX recursively consumes the preceding generated level.
        else bc_triangle_rectangle(out, out.texels, out.levels.front(), level,
                                   level.width, level.height, true);
    }
    if (out.authored_dds.empty() && authored_levels.front().offset >= 128)
        out.authored_dds = std::move(authored);
    // Original x87 precision/spills and mutable metric-global runtime receipt
    // remain unknown: faithful recovered math is NOT a bit-identity claim.
    out.implementation_precision_fallback = true;
    out.runtime = info;
    validate_gpu_image(out);
    return out;
}
} // namespace

GpuImage derive_gpu_image_upload(GpuImage&& source, const GpuImageRuntimeContext& context,
                                 bool overlay) {
    validate_gpu_image(source);
    if (source.face_count != 1) runtime_error("runtime derivation requires a one-face 2D source before cube conversion");
    if (source.runtime) runtime_error("runtime derivation requires the authored source, not a previous derivative");
    if (!context.minimum_dimension || context.minimum_dimension > detail::max_image_dimension ||
        !context.maximum_dimension || context.maximum_dimension > detail::max_image_dimension)
        runtime_error("minimum and explicit maximum dimensions must be positive and at most 16384");
    const bool encoded = source.source_format == ImageSourceFormat::Dds;
    const bool palette_source = source.source_format == ImageSourceFormat::ModelSkin &&
        source.source_bits_per_pixel == 8;
    const bool overlay_target = !encoded && !source.source_alpha_flag &&
        !(context.bitmap_flags & (0x100u | 0x100000u)) &&
        (overlay || (context.bitmap_flags & 0x80000u));
    const bool key = overlay_target && context.automatic_transparency;
    // Indexed raw case1 retains fixed black even in automatic/manual mode.
    // Raw565/BGR24 cases2/3 suppress API black when automatic mode is nonzero.
    const bool api_black_key = overlay_target &&
        (palette_source || !context.automatic_transparency);
    if (overlay_target && source.color_key_applied)
        runtime_error("overlay requires unkeyed authored source for upload-before-key ordering");
    GpuImageRuntimeInfo info;
    info.source_width = source.width;
    info.source_height = source.height;
    info.nominal_width = std::max(context.minimum_dimension, std::bit_ceil(source.width));
    info.nominal_height = std::max(context.minimum_dimension, std::bit_ceil(source.height));
    info.authored_mip_levels = static_cast<std::uint32_t>(source.levels.size());
    auto width = info.nominal_width, height = info.nominal_height;
    info.written_width = source.width;
    info.written_height = source.height;
    if (std::max(width, height) > context.maximum_dimension &&
        (!encoded || (source.width == width && source.height == height))) {
        width = std::min(width, context.maximum_dimension);
        height = std::min(height, context.maximum_dimension);
        info.written_width = static_cast<std::uint32_t>(std::uint64_t(width) * source.width / info.nominal_width);
        info.written_height = static_cast<std::uint32_t>(std::uint64_t(height) * source.height / info.nominal_height);
        info.resize_filter = 4;
    }
    if (context.bitmap_flags & 0x10000u) {
        info.written_width = width;
        info.written_height = height;
        info.resize_filter = 4;
    }
    dimensions(width, height);
    if (!info.written_width || !info.written_height || info.written_width > width || info.written_height > height)
        runtime_error("unsupported empty or out-of-allocation original destination rectangle");
    const auto supplied = encoded ? 1u : info.authored_mip_levels;
    auto requested = encoded ? info.authored_mip_levels : context.requested_mip_levels;
    if (context.bitmap_flags & 0x40000u) requested = 4;
    requested = std::max(requested, supplied);
    const auto chain = std::bit_width(std::max(width, height));
    if (requested > chain) runtime_error("requested mip levels exceed the bounded physical mip chain");
    info.preserved_mip_levels = supplied;
    info.generated_mip_levels = requested - supplied;
    info.encoded_postfilter_attempted = encoded && requested > 1;
    info.mip_filter = std::has_single_bit(width) && std::has_single_bit(height) ? 5u : 0x80004u;
    const bool same_shape = width == source.width && height == source.height &&
        info.written_width == source.width && info.written_height == source.height;
    if (block_format(source.format))
        return derive_bc_upload(std::move(source), info, width, height, requested, same_shape);
    // Keep unchanged encoded storage and its original header in one owner.
    if (encoded && same_shape && !info.encoded_postfilter_attempted) {
        source.runtime = info;
        return std::move(source);
    }
    const auto from = channels(source.format);
    const auto target_format = palette_source ?
        (overlay_target ? GpuImageFormat::Argb1555Unorm : GpuImageFormat::Rgb565Unorm) :
        (overlay_target && source.format == GpuImageFormat::Rgb565Unorm ?
            GpuImageFormat::Argb1555Unorm : source.format);
    const auto to = channels(target_format);
    if (target_format != source.format) source.implementation_precision_fallback = true;
    auto rect_width = info.written_width, rect_height = info.written_height;
    for (std::uint32_t lod = 0; lod < supplied; ++lod) {
        if (!rect_width || !rect_height || !(source.width >> lod) || !(source.height >> lod))
            runtime_error("unsupported empty original supplied mip rectangle after arithmetic halving");
        if (key && ((source.width >> lod) > std::max(1u, width >> lod) ||
                    (source.height >> lod) > std::max(1u, height >> lod)))
            runtime_error("unsupported original manual-key authored rectangle exceeds physical mip surface after resize");
        rect_width >>= 1;
        rect_height >>= 1;
    }
    info.original_padding_known = info.written_width == width && info.written_height == height;
    if (!info.original_padding_known || api_black_key || (info.resize_filter == 4 && !same_shape))
        source.implementation_precision_fallback = true;

    // Raw identity uploads can keep every supplied range and append only the
    // missing suffix. Changed encoded uploads retain the original DDS by move.
    const bool reuse = !encoded && same_shape && from.bytes == to.bytes;
    GpuImage out = std::move(source);
    out.width = width;
    out.height = height;
    out.format = target_format;
    std::vector<std::uint8_t> old_texels;
    std::vector<GpuImageLevel> old_levels;
    const std::uint8_t* original = nullptr;
    if (!reuse) {
        old_texels = std::move(out.texels);
        out.texels.clear();
        old_levels = std::move(out.levels);
        if (encoded && out.authored_dds.empty() && old_levels.front().offset >= 128)
            out.authored_dds = std::move(old_texels);
        original = old_texels.empty() ? out.authored_dds.data() : old_texels.data();
        out.levels.clear();
    }
    out.levels.reserve(requested);
    std::size_t total = reuse ? out.texels.size() : 0;
    auto level_width = width, level_height = height;
    for (std::uint32_t lod = 0; lod < requested; ++lod) {
        if (!reuse || lod >= supplied) {
            const auto level = tight_level(level_width, level_height, to.bytes, total);
            if (level.byte_size > detail::max_image_bytes - total)
                runtime_error("aggregate derived upload exceeds 256 MiB");
            total += level.byte_size;
            out.levels.push_back(level);
        }
        level_width = std::max(1u, level_width / 2);
        level_height = std::max(1u, level_height / 2);
    }
    if (total > detail::max_image_bytes || out.authored_dds.size() > detail::max_image_bytes - total)
        runtime_error("aggregate derived and original DDS storage exceeds 256 MiB");
    out.texels.resize(total); // Defined native zero padding, not observed original contents.
    if (reuse && (from.bits != to.bits || api_black_key)) {
        for (std::uint32_t lod = 0; lod < supplied; ++lod) {
            const auto& level = out.levels[lod];
            auto* pixels = out.texels.data() + level.offset;
            copy_rectangle(pixels, level.row_pitch, from, pixels, level.row_pitch, to,
                           level.width, level.height, api_black_key);
        }
    } else if (!reuse) {
        rect_width = info.written_width;
        rect_height = info.written_height;
        for (std::uint32_t lod = 0; lod < supplied; ++lod) {
            const auto& input = old_levels[lod];
            const auto& output = out.levels[lod];
            if (info.resize_filter == 4)
                triangle_rectangle(original + input.offset, input, from,
                    out.texels.data() + output.offset, output, to, rect_width, rect_height,
                    false, api_black_key);
            else copy_rectangle(original + input.offset, input.row_pitch, from,
                    out.texels.data() + output.offset, output.row_pitch, to, rect_width, rect_height,
                    api_black_key);
            rect_width >>= 1;
            rect_height >>= 1;
        }
    }
    if (api_black_key) out.color_key_applied = true;
    if (key) apply_key(out, supplied, info.source_width, info.source_height);
    for (std::uint32_t lod = supplied; lod < requested; ++lod) {
        if (info.mip_filter == 5) box_level(out, lod, to);
        else {
            // Original NPOT DEFAULT always reads the SAME initial SrcLevel,
            // unlike recursively quantized POT BOX.
            const auto& initial = out.levels[supplied - 1];
            const auto& target = out.levels[lod];
            triangle_rectangle(out.texels.data() + initial.offset, initial, to,
                out.texels.data() + target.offset, target, to, target.width, target.height, true);
            out.implementation_precision_fallback = true;
        }
    }
    out.runtime = info;
    validate_gpu_image(out);
    return out;
}
} // namespace c4::assets
