#pragma once

#include "assets/GpuImage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace c4::rendering::quality {

// Native approximation, not recovered original lighting: linear UNORM RGBA8
// limits radiance to [0,1] with 1/255 quantization. Upload without sRGB conversion.
struct ModernEnvironment {
    assets::Image irradiance, prefiltered;
};

namespace modern_environment_detail {
using Direction = std::array<float,3>;
constexpr unsigned sample_count = 64, prefilter_levels = 6;
constexpr float pi = 3.14159265358979323846f;

inline float dot(const Direction& a, const Direction& b) noexcept {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline Direction cross(const Direction& a, const Direction& b) noexcept {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline Direction normalized(Direction v) noexcept {
    const float scale = 1.f / std::sqrt(dot(v,v));
    for (float& component : v) component *= scale;
    return v;
}

// Same +X,-X,+Y,-Y,+Z,-Z mapping as the existing cube resolve shader;
// directly matches samplerCube, without a source-strip remap or rotation.
inline Direction face_direction(unsigned face, float u, float v) noexcept {
    switch (face) {
    case 0: return normalized({1.f,-v,-u});
    case 1: return normalized({-1.f,-v,u});
    case 2: return normalized({u,1.f,v});
    case 3: return normalized({u,-1.f,-v});
    case 4: return normalized({u,-v,1.f});
    default: return normalized({-u,-v,-1.f});
    }
}

struct Samples {
    std::array<Direction,sample_count> cosine{};
    std::array<std::array<Direction,sample_count>,prefilter_levels> ggx{};
    Samples() {
        for (unsigned i = 0; i < sample_count; ++i) {
            // Hammersley: i/64 and the base-two radical inverse (six bits).
            unsigned reversed = 0;
            for (unsigned bit = 0; bit < 6; ++bit)
                reversed = (reversed << 1) | ((i >> bit) & 1u);
            const float v = static_cast<float>(reversed) / sample_count;
            const float phi = 2.f * pi * static_cast<float>(i) / sample_count;
            const float cp = std::cos(phi), sp = std::sin(phi);
            const float radius = std::sqrt(v);
            cosine[i] = {radius*cp,radius*sp,std::sqrt(1.f-v)};
            for (unsigned level = 0; level < prefilter_levels; ++level) {
                const float roughness = static_cast<float>(level) / (prefilter_levels-1);
                const float alpha = roughness*roughness;
                const float z = std::sqrt((1.f-v)/(1.f+(alpha*alpha-1.f)*v));
                const float xy = std::sqrt(std::max(0.f,1.f-z*z));
                ggx[level][i] = {xy*cp,xy*sp,z};
            }
        }
    }
};

struct Basis {
    Direction normal, tangent, bitangent;
    explicit Basis(const Direction& n) : normal(n) {
        const Direction up = std::abs(n[2]) < .999f ? Direction{0,0,1} : Direction{1,0,0};
        tangent = normalized(cross(up,n));
        bitangent = cross(n,tangent);
    }
    Direction world(const Direction& local) const noexcept {
        Direction result{};
        for (unsigned axis = 0; axis < 3; ++axis)
            result[axis] = tangent[axis]*local[0] + bitangent[axis]*local[1] + normal[axis]*local[2];
        return result;
    }
};

inline Direction sample_cube(const std::vector<Direction>& linear, unsigned size,
                             const Direction& direction) noexcept {
    const float ax = std::abs(direction[0]), ay = std::abs(direction[1]), az = std::abs(direction[2]);
    unsigned face;
    float u, v, major;
    if (ax >= ay && ax >= az) {
        face = direction[0] >= 0.f ? 0 : 1;
        major = ax; u = face == 0 ? -direction[2] : direction[2]; v = -direction[1];
    } else if (ay >= az) {
        face = direction[1] >= 0.f ? 2 : 3;
        major = ay; u = direction[0]; v = face == 2 ? direction[2] : -direction[2];
    } else {
        face = direction[2] >= 0.f ? 4 : 5;
        major = az; u = face == 4 ? direction[0] : -direction[0]; v = -direction[1];
    }
    // ponytail: face-local bilinear clamps approximate seams; cross-face taps
    // are needed only if this native low-resolution seam ceiling is unacceptable.
    const float x = std::clamp((u/major+1.f)*.5f*size-.5f,0.f,static_cast<float>(size-1));
    const float y = std::clamp((v/major+1.f)*.5f*size-.5f,0.f,static_cast<float>(size-1));
    const unsigned x0 = static_cast<unsigned>(x), y0 = static_cast<unsigned>(y);
    const unsigned x1 = std::min(x0+1,size-1), y1 = std::min(y0+1,size-1);
    const float tx = x-x0, ty = y-y0;
    const std::size_t base = static_cast<std::size_t>(face)*size*size;
    const auto& a = linear[base+static_cast<std::size_t>(y0)*size+x0];
    const auto& b = linear[base+static_cast<std::size_t>(y0)*size+x1];
    const auto& c = linear[base+static_cast<std::size_t>(y1)*size+x0];
    const auto& d = linear[base+static_cast<std::size_t>(y1)*size+x1];
    Direction result{};
    for (unsigned channel = 0; channel < 3; ++channel)
        result[channel] = (a[channel]*(1.f-tx)+b[channel]*tx)*(1.f-ty) +
                          (c[channel]*(1.f-tx)+d[channel]*tx)*ty;
    return result;
}

inline void convolve_level(const std::vector<Direction>& linear, unsigned source_size,
                           unsigned size, bool irradiance, unsigned level,
                           std::uint8_t* destination) {
    static const Samples samples;
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                const auto normal = face_direction(face,2.f*(x+.5f)/size-1.f,2.f*(y+.5f)/size-1.f);
                Direction color{};
                float weight = 0.f;
                if (!irradiance && level == 0) {
                    // The 64 GGX directions coincide at roughness zero.
                    color = sample_cube(linear,source_size,normal);
                    weight = 1.f;
                } else {
                    const Basis basis(normal);
                    for (unsigned sample = 0; sample < sample_count; ++sample) {
                        Direction light = basis.world(irradiance ? samples.cosine[sample] : samples.ggx[level][sample]);
                        float contribution = 1.f;
                        if (!irradiance) {
                            // Importance-sampled GGX halfway vector; V=N.
                            const float twice_ndoth = 2.f*dot(normal,light);
                            for (unsigned axis = 0; axis < 3; ++axis)
                                light[axis] = twice_ndoth*light[axis]-normal[axis];
                            contribution = std::max(0.f,dot(normal,light));
                            if (contribution == 0.f) continue;
                        }
                        const auto radiance = sample_cube(linear,source_size,light);
                        for (unsigned channel = 0; channel < 3; ++channel)
                            color[channel] += radiance[channel]*contribution;
                        weight += contribution;
                    }
                }
                // Cosine-weighted expectation is irradiance/pi, not irradiance.
                const std::size_t offset = (static_cast<std::size_t>(face)*size*size+y*size+x)*4;
                for (unsigned channel = 0; channel < 3; ++channel)
                    destination[offset+channel] = static_cast<std::uint8_t>(
                        std::lround(std::clamp(color[channel]/weight,0.f,1.f)*255.f));
                destination[offset+3] = 255;
            }
}
} // namespace modern_environment_detail

// Scene owns caching by authored texture identity and both derived GPU images.
// No authored bytes/metadata are modified; lower authored mips are not renamed
// as IBL. Exactly one existing decode precedes linear-sRGB base-level sampling.
inline ModernEnvironment prepare_modern_environment(const assets::GpuImage& authored) {
    if (authored.face_count != 6 || authored.width == 0 || authored.height != authored.width ||
        authored.texels.empty() || authored.levels.empty())
        throw std::runtime_error("modern environment requires a nonempty square six-face authored cube");
    const assets::Image decoded = assets::decode_gpu_image(authored);
    using namespace modern_environment_detail;
    static const auto srgb_to_linear = [] {
        std::array<float,256> values{};
        for (unsigned i = 0; i < values.size(); ++i) {
            const float s = static_cast<float>(i)/255.f;
            values[i] = s <= .04045f ? s/12.92f : std::pow((s+.055f)/1.055f,2.4f);
        }
        return values;
    }();
    std::vector<Direction> linear(decoded.rgba.size()/4);
    for (std::size_t pixel = 0; pixel < linear.size(); ++pixel)
        for (unsigned channel = 0; channel < 3; ++channel)
            linear[pixel][channel] = srgb_to_linear[decoded.rgba[pixel*4+channel]];

    ModernEnvironment result;
    result.irradiance.width = result.irradiance.height = 8;
    result.irradiance.face_count = 6;
    result.irradiance.rgba.resize(8*8*4*6);
    convolve_level(linear,decoded.width,8,true,0,result.irradiance.rgba.data());
    result.prefiltered.width = result.prefiltered.height = 32;
    result.prefiltered.face_count = 6;
    result.prefiltered.rgba.resize(32*32*4*6);
    result.prefiltered.mip_levels.reserve(prefilter_levels-1);
    std::size_t mip_bytes = 0;
    for (unsigned level = 1, size = 16; level < prefilter_levels; ++level, size /= 2) {
        result.prefiltered.mip_levels.push_back({size,size,mip_bytes});
        mip_bytes += static_cast<std::size_t>(size)*size*4*6;
    }
    result.prefiltered.mip_rgba.resize(mip_bytes);
    convolve_level(linear,decoded.width,32,false,0,result.prefiltered.rgba.data());
    for (unsigned level = 1; level < prefilter_levels; ++level) {
        const auto& mip = result.prefiltered.mip_levels[level-1];
        convolve_level(linear,decoded.width,mip.width,false,level,
                       result.prefiltered.mip_rgba.data()+mip.offset);
    }
    return result;
}
} // namespace c4::rendering::quality
