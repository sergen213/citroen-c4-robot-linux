#include "GpuImageBc.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace c4::assets::detail {
namespace {
using Rgb = std::array<float, 3>;
constexpr float f(std::uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr Rgb metric{f(0x3e981530), 1.f, f(0x3dce6734)};
constexpr Rgb inverse_metric{f(0x4057762e), 1.f, f(0x411ec1dd)};
constexpr float r31 = f(0x3d042108), r63 = f(0x3c820821), r255 = f(0x3b808081);
constexpr std::array<float, 4> rgb4_up{0.f, f(0x3eaaaaab), f(0x3f2aaaab), 1.f};
constexpr std::array<float, 4> rgb4_down{1.f, f(0x3f2aaaab), f(0x3eaaaaab), 0.f};
constexpr std::array<float, 3> rgb3_up{0.f, .5f, 1.f}, rgb3_down{1.f, .5f, 0.f};
// 4078c8 really contains 8/7, not 1: preserve the original optimizer table.
constexpr std::array<float, 8> alpha8_up{0.f, f(0x3e124925), f(0x3e924925),
    f(0x3edb6db7), f(0x3f124925), f(0x3f36db6e), f(0x3f5b6db7), f(0x3f924925)};
constexpr std::array<float, 8> alpha8_down{1.f, f(0x3f5b6db7), f(0x3f36db6e),
    f(0x3f124925), f(0x3edb6db7), f(0x3e924925), f(0x3e124925), 0.f};
constexpr std::array<float, 6> alpha6_up{0.f, .2f, .4f, .6f, .8f, 1.f};
constexpr std::array<float, 6> alpha6_down{1.f, .8f, .6f, .4f, .2f, 0.f};
constexpr std::array<unsigned, 4> rgb4_map{0, 2, 3, 1};
constexpr std::array<unsigned, 3> rgb3_map{0, 2, 1};
constexpr std::array<unsigned, 8> alpha8_map{0, 2, 3, 4, 5, 6, 7, 1};
constexpr std::array<unsigned, 6> alpha6_map{0, 2, 3, 4, 5, 1};

void diffuse(std::array<float, 16>& errors, unsigned i, float residual) {
    const auto x = i & 3;
    if (x != 3) errors[i + 1] += residual * (7.f / 16.f);
    if (i < 12) {
        if (x) errors[i + 3] += residual * (3.f / 16.f);
        errors[i + 4] += residual * (5.f / 16.f);
        if (x != 3) errors[i + 5] += residual * (1.f / 16.f);
    }
}

std::uint16_t word(const std::uint8_t* p) { return p[0] | (std::uint16_t(p[1]) << 8); }
void put_word(std::uint8_t* p, std::uint16_t v) { p[0] = v; p[1] = v >> 8; }
BcPixel rgb565(std::uint16_t v) {
    return {float(v >> 11) * r31, float((v >> 5) & 63) * r63, float(v & 31) * r31, 1.f};
}
std::uint16_t quant565(const Rgb& color) {
    return (std::uint16_t(std::clamp(color[0], 0.f, 1.f) * 31.f + .5f) << 11) |
           (std::uint16_t(std::clamp(color[1], 0.f, 1.f) * 63.f + .5f) << 5) |
            std::uint16_t(std::clamp(color[2], 0.f, 1.f) * 31.f + .5f);
}
float norm2(const Rgb& v) { return (v[2] * v[2] + v[1] * v[1]) + v[0] * v[0]; }

// 4d8de9: four signed bounding-box axes, then at most eight residual updates.
void solve_rgb(const std::array<Rgb, 16>& pixels, unsigned count, Rgb& low, Rgb& high) {
    low = metric;
    high = {};
    for (const auto& p : pixels) for (unsigned k = 0; k < 3; ++k) {
        low[k] = std::min(low[k], p[k]);
        high[k] = std::max(high[k], p[k]);
    }
    Rgb delta{}, axis{}, center{};
    for (unsigned k = 0; k < 3; ++k) delta[k] = high[k] - low[k];
    const float span = norm2(delta);
    if (span < std::numeric_limits<float>::min()) return;
    for (unsigned k = 0; k < 3; ++k) {
        axis[k] = delta[k] / span;
        center[k] = (high[k] + low[k]) * .5f;
    }
    std::array<float, 4> scores{};
    for (const auto& p : pixels) {
        const float r = (p[0] - center[0]) * axis[0];
        const float g = (p[1] - center[1]) * axis[1];
        const float b = (p[2] - center[2]) * axis[2];
        const std::array<float, 4> projections{(g + b) + r, (g + r) - b,
                                               (r - g) + b, (r - g) - b};
        for (unsigned i = 0; i < 4; ++i) scores[i] += projections[i] * projections[i];
    }
    unsigned best = 0;
    for (unsigned i = 1; i < 4; ++i) if (scores[i] > scores[best]) best = i;
    if (best & 2) std::swap(low[1], high[1]);
    if (best & 1) std::swap(low[2], high[2]);
    if (span < f(0x39800000)) return;
    const auto up = count == 3 ? std::span<const float>(rgb3_up) : std::span<const float>(rgb4_up);
    const auto down = count == 3 ? std::span<const float>(rgb3_down) : std::span<const float>(rgb4_down);
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        std::array<Rgb, 4> palette{};
        for (unsigned i = 0; i < count; ++i) for (unsigned k = 0; k < 3; ++k)
            palette[i][k] = low[k] * down[i] + high[k] * up[i];
        for (unsigned k = 0; k < 3; ++k) delta[k] = high[k] - low[k];
        const float length = norm2(delta);
        if (length < f(0x39800000)) break;
        const float scale = float(count - 1) / length;
        for (unsigned k = 0; k < 3; ++k) axis[k] = delta[k] * scale;
        Rgb low_gradient{}, high_gradient{};
        float low_denominator = 0, high_denominator = 0;
        for (const auto& p : pixels) {
            const float projection = ((p[1] - low[1]) * axis[1] +
                (p[0] - low[0]) * axis[0]) + (p[2] - low[2]) * axis[2];
            const auto index = projection <= 0 ? 0u : projection >= float(count - 1)
                ? count - 1 : static_cast<unsigned>(projection + .5f);
            const float a = down[index] * .125f, b = up[index] * .125f;
            low_denominator += a * down[index];
            high_denominator += b * up[index];
            for (unsigned k = 0; k < 3; ++k) {
                const float residual = palette[index][k] - p[k];
                low_gradient[k] += a * residual;
                high_gradient[k] += b * residual;
            }
        }
        if (low_denominator > 0) for (unsigned k = 0; k < 3; ++k)
            low[k] += low_gradient[k] * (-1.f / low_denominator);
        if (high_denominator > 0) for (unsigned k = 0; k < 3; ++k)
            high[k] += high_gradient[k] * (-1.f / high_denominator);
        bool converged = true;
        for (unsigned k = 0; k < 3; ++k)
            converged &= low_gradient[k] * low_gradient[k] < f(0x37800000) &&
                         high_gradient[k] * high_gradient[k] < f(0x37800000);
        if (converged) break;
    }
}

void encode_rgb(const BcBlock& pixels, bool transparency, std::uint8_t* bytes, bool dither) {
    unsigned transparent = 0;
    if (transparency) for (const auto& p : pixels) transparent += p[3] < .5f;
    if (transparent == 16) {
        put_word(bytes, 0); put_word(bytes + 2, 0xffff);
        std::fill_n(bytes + 4, 4, 0xff);
        return;
    }
    const unsigned count = transparent ? 3 : 4;
    std::array<Rgb, 16> quantized{};
    std::array<std::array<float, 16>, 3> errors{};
    for (unsigned i = 0; i < 16; ++i) for (unsigned k = 0; k < 3; ++k) {
        const float value = pixels[i][k] + (dither ? errors[k][i] : 0.f);
        const float quantized_value = float(int(value * (k == 1 ? 63.f : 31.f) + .5f)) *
                                      (k == 1 ? r63 : r31);
        quantized[i][k] = quantized_value * metric[k];
        if (dither) diffuse(errors[k], i, value - quantized_value);
    }
    Rgb low{}, high{};
    solve_rgb(quantized, count, low, high);
    for (unsigned k = 0; k < 3; ++k) { low[k] *= inverse_metric[k]; high[k] *= inverse_metric[k]; }
    auto a = quant565(low), b = quant565(high);
    if (count == 4 && a == b) {
        put_word(bytes, a); put_word(bytes + 2, b); std::fill_n(bytes + 4, 4, 0);
        return;
    }
    if ((count == 3) != (b >= a)) std::swap(a, b);
    put_word(bytes, a); put_word(bytes + 2, b);
    const auto first = rgb565(a), last = rgb565(b);
    Rgb origin{}, axis{}, delta{};
    for (unsigned k = 0; k < 3; ++k) {
        origin[k] = first[k] * metric[k];
        delta[k] = last[k] * metric[k] - origin[k];
    }
    const float scale = a == b ? 0.f : float(count - 1) / norm2(delta);
    for (unsigned k = 0; k < 3; ++k) axis[k] = delta[k] * scale;
    errors = {};
    std::array<Rgb, 4> palette{origin, {}, {}, {}};
    for (unsigned k = 0; k < 3; ++k) {
        palette[1][k] = last[k] * metric[k];
        palette[2][k] = delta[k] * (count == 3 ? .5f : f(0x3eaaaaab)) + origin[k];
        if (count == 4) palette[3][k] = delta[k] * f(0x3f2aaaab) + origin[k];
    }
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned index = 3;
        if (!transparent || pixels[i][3] >= .5f) {
            Rgb corrected{};
            for (unsigned k = 0; k < 3; ++k)
                corrected[k] = pixels[i][k] * metric[k] + (dither ? errors[k][i] : 0.f);
            const float projection = ((corrected[2] - origin[2]) * axis[2] +
                (corrected[1] - origin[1]) * axis[1]) + (corrected[0] - origin[0]) * axis[0];
            if (projection <= 0) index = 0;
            else if (projection >= float(count - 1)) index = 1;
            else {
                const auto interior = static_cast<unsigned>(projection + .5f);
                index = count == 3 ? rgb3_map[interior] : rgb4_map[interior];
            }
            if (dither) for (unsigned k = 0; k < 3; ++k)
                diffuse(errors[k], i, corrected[k] - palette[index][k]);
        }
        bits |= index << (2 * i);
    }
    for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = bits >> (8 * i);
}

void solve_alpha(const std::array<float, 16>& pixels, unsigned count, float& low, float& high) {
    low = 1; high = 0;
    for (const auto p : pixels) {
        if (count == 8 || p > 0) low = std::min(low, p);
        if (count == 8 || p < 1) high = std::max(high, p);
    }
    if (count == 6 && low == high) high = 1;
    const auto up = count == 6 ? std::span<const float>(alpha6_up) : std::span<const float>(alpha8_up);
    const auto down = count == 6 ? std::span<const float>(alpha6_down) : std::span<const float>(alpha8_down);
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        if (high - low < f(0x3b800000)) break;
        const float scale = float(count - 1) / (high - low);
        std::array<float, 8> palette{};
        for (unsigned i = 0; i < count; ++i) palette[i] = high * up[i] + low * down[i];
        float low_gradient = 0, high_gradient = 0, low_denominator = 0, high_denominator = 0;
        for (const auto p : pixels) {
            const float projection = (p - low) * scale;
            unsigned index;
            if (projection <= 0) {
                if (count == 6 && low * .5f >= p) continue;
                index = 0;
            } else if (projection >= float(count - 1)) {
                if (count == 6 && (high + 1.f) * .5f <= p) continue;
                index = count - 1;
            } else index = static_cast<unsigned>(projection + .5f);
            const float residual = p - palette[index];
            low_gradient += residual * down[index];
            low_denominator += down[index] * down[index];
            high_gradient += residual * up[index];
            high_denominator += up[index] * up[index];
        }
        // Original 4d8d08/24 uses endpoint - residual/denominator (not RGB's sign).
        if (low_denominator > 0) low -= low_gradient / low_denominator;
        if (high_denominator > 0) high -= high_gradient / high_denominator;
        if (low > high) std::swap(low, high);
        if (low_gradient * low_gradient < 1.f / 64.f &&
            high_gradient * high_gradient < 1.f / 64.f) break;
    }
    low = std::clamp(low, 0.f, 1.f); high = std::clamp(high, 0.f, 1.f);
}

void encode_alpha3(const BcBlock& pixels, std::uint8_t* bytes, bool dither) {
    std::array<float, 16> quantized{};
    std::array<float, 16> errors{};
    float minimum = pixels[0][3], maximum = pixels[0][3];
    for (unsigned i = 0; i < 16; ++i) {
        const float corrected = pixels[i][3] + (dither ? errors[i] : 0.f);
        quantized[i] = float(int(corrected * 255.f + .5f)) * r255;
        if (dither) diffuse(errors, i, corrected - quantized[i]);
        minimum = std::min(minimum, quantized[i]); maximum = std::max(maximum, quantized[i]);
    }
    if (minimum == 1.f) { bytes[0] = bytes[1] = 255; std::fill_n(bytes + 2, 6, 0); return; }
    const unsigned count = minimum != 0.f && maximum != 1.f ? 8 : 6;
    float low, high;
    solve_alpha(quantized, count, low, high);
    const auto a = static_cast<std::uint8_t>(low * 255.f + .5f);
    const auto b = static_cast<std::uint8_t>(high * 255.f + .5f);
    if (count == 8 && a == b) { bytes[0] = a; bytes[1] = b; std::fill_n(bytes + 2, 6, 0); return; }
    bytes[0] = count == 6 ? a : b; bytes[1] = count == 6 ? b : a;
    const float first = float(bytes[0]) * r255, last = float(bytes[1]) * r255;
    const float scale = first == last ? 0.f : float(count - 1) / (last - first);
    std::array<float, 8> palette{first, last};
    for (unsigned i = 1; i < count - 1; ++i)
        palette[i + 1] = (float(count - 1 - i) * first + float(i) * last) *
                         (count == 6 ? .2f : f(0x3e124925));
    if (count == 6) { palette[6] = 0; palette[7] = 1; }
    errors = {};
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const float p = pixels[i][3] + (dither ? errors[i] : 0.f);
        const float projection = (p - first) * scale;
        unsigned index;
        if (projection <= 0) index = count == 6 && first * .5f >= p ? 6 : 0;
        else if (projection >= float(count - 1)) index = count == 6 && (last + 1.f) * .5f <= p ? 7 : 1;
        else {
            const auto interior = static_cast<unsigned>(projection + .5f);
            index = count == 6 ? alpha6_map[interior] : alpha8_map[interior];
        }
        bits |= std::uint64_t(index) << (3 * i);
        if (dither) diffuse(errors, i, p - palette[index]);
    }
    for (unsigned i = 0; i < 6; ++i) bytes[2 + i] = bits >> (8 * i);
}
} // namespace

std::size_t bc_block_bytes(GpuImageFormat format) {
    switch (format) {
    case GpuImageFormat::Bc1Unorm: return 8;
    case GpuImageFormat::Bc2Unorm: case GpuImageFormat::Bc3Unorm: return 16;
    default: throw std::runtime_error("GPU BC codec: non-BC format");
    }
}

void decode_bc_block(GpuImageFormat format, std::span<const std::uint8_t> bytes, BcBlock& pixels) {
    if (bytes.size() < bc_block_bytes(format)) throw std::runtime_error("GPU BC codec: truncated block");
    const auto* color = bytes.data() + (format == GpuImageFormat::Bc1Unorm ? 0 : 8);
    const auto a = word(color), b = word(color + 2);
    std::array<BcPixel, 4> palette{rgb565(a), rgb565(b), {}, {}};
    if (a > b) for (unsigned k = 0; k < 4; ++k) {
        const float delta = palette[1][k] - palette[0][k];
        palette[2][k] = delta * f(0x3eaaaaab) + palette[0][k];
        palette[3][k] = delta * f(0x3f2aaaab) + palette[0][k];
    } else for (unsigned k = 0; k < 4; ++k)
        palette[2][k] = (palette[1][k] - palette[0][k]) * .5f + palette[0][k];
    std::uint32_t color_bits = 0;
    for (unsigned i = 0; i < 4; ++i) color_bits |= std::uint32_t(color[4 + i]) << (8 * i);
    for (unsigned i = 0; i < 16; ++i) pixels[i] = palette[(color_bits >> (2 * i)) & 3];
    if (format == GpuImageFormat::Bc2Unorm) for (unsigned i = 0; i < 16; ++i)
        pixels[i][3] = float((bytes[i / 2] >> (4 * (i & 1))) & 15) * f(0x3d888889);
    if (format == GpuImageFormat::Bc3Unorm) {
        std::array<float, 8> alpha{float(bytes[0]) * r255, float(bytes[1]) * r255};
        const unsigned count = bytes[0] > bytes[1] ? 8 : 6;
        const float reciprocal = count == 8 ? f(0x3e124925) : .2f;
        for (unsigned i = 1; i < count - 1; ++i)
            alpha[i + 1] = (float(count - 1 - i) * alpha[0] + float(i) * alpha[1]) * reciprocal;
        if (count == 6) { alpha[6] = 0; alpha[7] = 1; }
        std::uint64_t bits = 0;
        for (unsigned i = 0; i < 6; ++i) bits |= std::uint64_t(bytes[2 + i]) << (8 * i);
        for (unsigned i = 0; i < 16; ++i) pixels[i][3] = alpha[(bits >> (3 * i)) & 7];
    }
}

void encode_bc_block(GpuImageFormat format, const BcBlock& pixels, std::span<std::uint8_t> bytes,
                     bool dither) {
    if (bytes.size() < bc_block_bytes(format)) throw std::runtime_error("GPU BC codec: truncated target block");
    if (format == GpuImageFormat::Bc1Unorm && dither) {
        auto corrected = pixels;
        std::array<float, 16> errors{};
        for (unsigned i = 0; i < 16; ++i) {
            const float alpha = pixels[i][3] + errors[i];
            corrected[i][3] = float(int(alpha + .5f));
            diffuse(errors, i, alpha - corrected[i][3]);
        }
        encode_rgb(corrected, true, bytes.data(), true);
        return;
    }
    if (format == GpuImageFormat::Bc2Unorm) {
        std::array<float, 16> errors{};
        for (unsigned i = 0; i < 16; ++i) {
            const float alpha = pixels[i][3] + (dither ? errors[i] : 0.f);
            const auto q = static_cast<int>(alpha * 15.f + .5f);
            if (!(i & 1)) bytes[i / 2] = static_cast<std::uint8_t>(q);
            else bytes[i / 2] |= static_cast<unsigned>(q) << 4;
            if (dither) diffuse(errors, i, alpha - float(q) * f(0x3d888889));
        }
    } else if (format == GpuImageFormat::Bc3Unorm) encode_alpha3(pixels, bytes.data(), dither);
    encode_rgb(pixels, format == GpuImageFormat::Bc1Unorm,
               bytes.data() + (format == GpuImageFormat::Bc1Unorm ? 0 : 8), dither);
}
} // namespace c4::assets::detail
