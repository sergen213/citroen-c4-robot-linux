#include "QualityDiagnostics.hpp"

#include <cmath>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace c4::rendering::quality {

std::string capabilities_json(const GraphicsCapabilities& caps) {
    if (!std::isfinite(caps.max_anisotropy))
        throw std::invalid_argument("graphics capability diagnostic float members must be finite");

    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "{\"sample_counts\":[1";
    for (const unsigned count : {2u, 4u, 8u})
        if (caps.sample_count_mask & count) json << ',' << count;
    json << "],\"fxaa\":" << caps.fxaa
         << ",\"ssao\":" << caps.ssao
         << ",\"pbr\":" << caps.pbr
         << ",\"internal_hdr\":" << caps.internal_hdr
         << ",\"bloom\":" << caps.bloom
         << ",\"max_shadow_cascades\":" << caps.max_shadow_cascades
         << ",\"max_shadow_size\":" << caps.max_shadow_size
         << ",\"hdr_display_output\":" << caps.hdr_display_output
         << ",\"trilinear\":" << caps.trilinear
         << ",\"max_anisotropy\":" << caps.max_anisotropy
         << ",\"temporal_denoise\":" << caps.temporal_denoise
         << ",\"high_quality_denoise\":" << caps.high_quality_denoise << '}';
    return std::move(json).str();
}

std::string resolved_graphics_json(const ResolvedGraphics& graphics) {
    if (!std::isfinite(graphics.ssao_radius) || !std::isfinite(graphics.ssao_bias) ||
        !std::isfinite(graphics.exposure_ev) || !std::isfinite(graphics.bloom_strength) ||
        !std::isfinite(graphics.anisotropy))
        throw std::invalid_argument("graphics diagnostic float members must be finite");

    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "{\"requested_preset\":\"" << preset_name(graphics.requested_preset)
         << "\",\"effective_preset\":\"" << preset_name(graphics.effective_preset)
         << "\",\"samples\":" << graphics.samples
         << ",\"post_aa\":\"" << post_aa_name(graphics.post_aa)
         << "\",\"ssao\":" << graphics.ssao
         << ",\"ssao_samples\":" << graphics.ssao_samples
         << ",\"ssao_radius\":" << graphics.ssao_radius
         << ",\"ssao_bias\":" << graphics.ssao_bias
         << ",\"shadows\":\"" << shadow_quality_name(graphics.shadows)
         << "\",\"shadow_cascades\":" << graphics.shadow_cascades
         << ",\"shadow_size\":" << graphics.shadow_size
         << ",\"pbr\":" << graphics.pbr
         << ",\"internal_hdr\":" << graphics.internal_hdr
         << ",\"exposure_ev\":" << graphics.exposure_ev
         << ",\"bloom\":" << graphics.bloom
         << ",\"bloom_strength\":" << graphics.bloom_strength
         << ",\"tone_mapping\":\"" << tone_mapping_name(graphics.tone_mapping)
         << "\",\"hdr_display_output\":" << graphics.hdr_display_output
         << ",\"texture_filtering\":\"" << texture_filtering_name(graphics.texture_filtering)
         << "\",\"anisotropy\":" << graphics.anisotropy
         << ",\"denoising\":\"" << denoising_name(graphics.denoising)
         << "\",\"denoise_passes\":" << graphics.denoise_passes
         << ",\"fallbacks\":" << graphics.fallbacks << '}';
    return std::move(json).str();
}

} // namespace c4::rendering::quality
