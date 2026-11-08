#include "GraphicSettings.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace c4::rendering::quality {

std::optional<ProfileDefaults> profile_defaults(Preset preset) noexcept {
    switch (preset) {
    case Preset::Original: return ProfileDefaults{RendererBackend::OpenGL, 0};
    case Preset::Enhanced: return ProfileDefaults{RendererBackend::Vulkan, 0};
    case Preset::Ultra: return ProfileDefaults{RendererBackend::Vulkan, 7};
    case Preset::Custom: return std::nullopt;
    }
    return std::nullopt;
}

Preset requested_profile(const GraphicSettings& settings) noexcept {
    if (settings.backend_override || settings.texture_filtering || settings.denoising || settings.samples ||
        settings.post_aa || settings.ssao || settings.shadows || settings.pbr ||
        settings.internal_hdr || settings.bloom || settings.exposure_ev || settings.tone_mapping ||
        !settings.material_overrides.empty())
        return Preset::Custom;
    return settings.preset;
}

Preset effective_profile(const ResolvedGraphics& graphics, RendererBackend actual_backend,
                         std::uint32_t actual_rt_effects) noexcept {
    const auto defaults = profile_defaults(graphics.effective_preset);
    if (graphics.fallbacks != 0 || !defaults || defaults->backend != actual_backend ||
        defaults->rt_effects != actual_rt_effects)
        return Preset::Custom;
    return graphics.effective_preset;
}

ResolvedGraphics resolve_graphics(const GraphicSettings& settings, const GraphicsCapabilities& caps) {
    if (static_cast<unsigned>(settings.preset) > static_cast<unsigned>(Preset::Custom))
        throw std::invalid_argument("invalid graphics preset");
    if (settings.samples && *settings.samples != 1 && *settings.samples != 2 &&
        *settings.samples != 4 && *settings.samples != 8)
        throw std::invalid_argument("graphics samples must be 1, 2, 4 or 8");
    if (settings.post_aa && static_cast<unsigned>(*settings.post_aa) > static_cast<unsigned>(PostAA::Fxaa))
        throw std::invalid_argument("invalid graphics post AA");
    if (settings.shadows && static_cast<unsigned>(*settings.shadows) > static_cast<unsigned>(ShadowQuality::High))
        throw std::invalid_argument("invalid graphics shadow quality");
    if (settings.tone_mapping && static_cast<unsigned>(*settings.tone_mapping) > static_cast<unsigned>(ToneMapping::Aces))
        throw std::invalid_argument("invalid graphics tone mapping");
    if (settings.texture_filtering && static_cast<unsigned>(*settings.texture_filtering) >
        static_cast<unsigned>(TextureFiltering::Anisotropic))
        throw std::invalid_argument("invalid graphics texture filtering");
    if (settings.denoising && static_cast<unsigned>(*settings.denoising) >
        static_cast<unsigned>(Denoising::High))
        throw std::invalid_argument("invalid graphics denoising");
    if (!std::isfinite(caps.max_anisotropy))
        throw std::invalid_argument("graphics maximum anisotropy must be finite");
    if (settings.exposure_ev && (!std::isfinite(*settings.exposure_ev) ||
        *settings.exposure_ev < -10.f || *settings.exposure_ev > 10.f))
        throw std::invalid_argument("graphics exposure EV must be finite and within [-10, 10]");
    for (const auto& material : settings.material_overrides) {
        const auto valid = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; };
        if (material.material_name.empty() || !valid(material.metallic) || !valid(material.roughness) ||
            (material.reflectivity && !valid(*material.reflectivity)))
            throw std::invalid_argument("native material overrides require an exact name and finite scalars within [0, 1]");
    }

    ResolvedGraphics result;
    result.requested_preset = settings.preset;
    result.effective_preset = requested_profile(settings);
    if (settings.preset == Preset::Enhanced || settings.preset == Preset::Ultra) {
        result.samples = settings.preset == Preset::Ultra ? 8 : 4;
        result.post_aa = PostAA::Fxaa;
        result.ssao = result.pbr = result.internal_hdr = result.bloom = true;
        result.shadows = settings.preset == Preset::Ultra ? ShadowQuality::High : ShadowQuality::Low;
        result.tone_mapping = ToneMapping::Aces;
        result.texture_filtering = TextureFiltering::Anisotropic;
    }
    if (settings.preset == Preset::Ultra)
        result.denoising = Denoising::High;
    else if (settings.preset == Preset::Custom && caps.temporal_denoise)
        result.denoising = Denoising::Standard;

    const unsigned requested_samples = settings.samples.value_or(result.samples);
    result.samples = 1;
    for (const unsigned count : {2u, 4u, 8u})
        if (count <= requested_samples && (caps.sample_count_mask & count)) result.samples = count;
    if (result.samples != requested_samples) result.fallbacks |= SamplesUnavailable;

    const auto requested_filtering = settings.texture_filtering.value_or(result.texture_filtering);
    result.texture_filtering = requested_filtering;
    if (requested_filtering == TextureFiltering::Anisotropic) {
        const float requested_anisotropy = settings.preset == Preset::Ultra ? 8.f : 4.f;
        if (caps.trilinear && caps.max_anisotropy > 1.f)
            result.anisotropy = std::min(requested_anisotropy, caps.max_anisotropy);
        else
            result.texture_filtering = caps.trilinear ? TextureFiltering::Trilinear : TextureFiltering::Original;
        if (result.anisotropy != requested_anisotropy)
            result.fallbacks |= TextureFilteringUnavailable;
    } else if (requested_filtering == TextureFiltering::Trilinear && !caps.trilinear) {
        result.texture_filtering = TextureFiltering::Original;
        result.fallbacks |= TextureFilteringUnavailable;
    }

    const auto requested_denoising = settings.denoising.value_or(result.denoising);
    result.denoising = requested_denoising;
    if (requested_denoising != Denoising::Off && !caps.temporal_denoise)
        result.denoising = Denoising::Off;
    else if (requested_denoising == Denoising::High && !caps.high_quality_denoise)
        result.denoising = Denoising::Standard;
    if (result.denoising != requested_denoising)
        result.fallbacks |= DenoisingUnavailable;
    result.denoise_passes = result.denoising == Denoising::High ? 3u :
        result.denoising == Denoising::Standard ? 1u : 0u;

    result.post_aa = settings.post_aa.value_or(result.post_aa);
    if (result.post_aa == PostAA::Fxaa && !caps.fxaa) {
        result.post_aa = PostAA::Off;
        result.fallbacks |= FxaaUnavailable;
    }
    result.ssao = settings.ssao.value_or(result.ssao);
    if (result.ssao && !caps.ssao) {
        result.ssao = false;
        result.fallbacks |= SsaoUnavailable;
    }
    if (result.ssao) {
        result.ssao_samples = settings.preset == Preset::Ultra ? 32 : 16;
        // Enhanced-only tuning: 16 source units is ~1/5 of the measured
        // 76.56-unit car width (work/reports/physics/original-engine-probes.json:87-104).
        result.ssao_radius = 16.f;
        result.ssao_bias = .025f;
    }

    result.shadows = settings.shadows.value_or(result.shadows);
    if (result.shadows != ShadowQuality::Off) {
        const bool requested_high = result.shadows == ShadowQuality::High;
        if (caps.max_shadow_cascades < 1 || caps.max_shadow_size < 256) {
            result.shadows = ShadowQuality::Off;
            result.fallbacks |= ShadowsUnavailable;
        } else {
            if (requested_high && (caps.max_shadow_cascades < 4 || caps.max_shadow_size < 2048)) {
                result.shadows = ShadowQuality::Low;
                result.fallbacks |= ShadowsUnavailable;
            }
            const unsigned cascades = result.shadows == ShadowQuality::High ? 4 : 2;
            const unsigned size = result.shadows == ShadowQuality::High ? 2048 : 1024;
            result.shadow_cascades = std::min(cascades, caps.max_shadow_cascades);
            result.shadow_size = std::min(size, caps.max_shadow_size);
            if (result.shadow_cascades != cascades || result.shadow_size != size)
                result.fallbacks |= ShadowsUnavailable;
        }
    }

    result.pbr = settings.pbr.value_or(result.pbr);
    if (result.pbr && !caps.pbr) {
        result.pbr = false;
        result.fallbacks |= PbrUnavailable;
    }
    const bool hdr_dependency = (settings.exposure_ev && *settings.exposure_ev != 0.f) ||
                                settings.bloom.value_or(false) || settings.tone_mapping.has_value();
    result.internal_hdr = settings.internal_hdr.value_or(result.internal_hdr || hdr_dependency);
    if ((result.internal_hdr && !caps.internal_hdr) || (hdr_dependency && !result.internal_hdr)) {
        result.internal_hdr = false;
        result.fallbacks |= HdrUnavailable;
    }
    result.bloom = settings.bloom.value_or(result.bloom);
    if (result.bloom && (!caps.bloom || !result.internal_hdr)) {
        result.bloom = false;
        result.fallbacks |= BloomUnavailable;
    }
    result.bloom_strength = result.bloom ? .08f : 0.f;
    result.exposure_ev = result.internal_hdr ? settings.exposure_ev.value_or(result.exposure_ev) : 0.f;
    result.tone_mapping = result.internal_hdr ? settings.tone_mapping.value_or(result.tone_mapping) : ToneMapping::Reinhard;
    result.hdr_display_output = caps.hdr_display_output;
    return result;
}

QualityUniforms quality_uniforms(const ResolvedGraphics& graphics, unsigned width, unsigned height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("graphics uniform dimensions must be nonzero");
    QualityUniforms result;
    result.effects = {graphics.post_aa == PostAA::Fxaa ? 1.f : 0.f,
                      static_cast<float>(graphics.ssao_samples), graphics.pbr ? 1.f : 0.f,
                      graphics.internal_hdr ? 1.f : 0.f};
    result.post = {std::exp2(graphics.exposure_ev), graphics.bloom_strength,
                   graphics.tone_mapping == ToneMapping::Aces ? 1.f : 0.f, graphics.ssao_radius};
    result.sampling = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height),
                       graphics.ssao_bias, static_cast<float>(graphics.shadow_cascades)};
    return result;
}

std::string_view preset_name(Preset preset) noexcept {
    switch (preset) {
    case Preset::Original: return "original";
    case Preset::Enhanced: return "enhanced";
    case Preset::Ultra: return "ultra";
    case Preset::Custom: return "custom";
    }
    return "unknown";
}

std::string_view post_aa_name(PostAA aa) noexcept {
    switch (aa) {
    case PostAA::Off: return "off";
    case PostAA::Fxaa: return "fxaa";
    }
    return "unknown";
}

std::string_view shadow_quality_name(ShadowQuality quality) noexcept {
    switch (quality) {
    case ShadowQuality::Off: return "off";
    case ShadowQuality::Low: return "low";
    case ShadowQuality::High: return "high";
    }
    return "unknown";
}

std::string_view tone_mapping_name(ToneMapping tone) noexcept {
    switch (tone) {
    case ToneMapping::Reinhard: return "reinhard";
    case ToneMapping::Aces: return "aces";
    }
    return "unknown";
}

std::string_view texture_filtering_name(TextureFiltering filtering) noexcept {
    switch (filtering) {
    case TextureFiltering::Original: return "original";
    case TextureFiltering::Trilinear: return "trilinear";
    case TextureFiltering::Anisotropic: return "anisotropic";
    }
    return "unknown";
}

std::string_view denoising_name(Denoising denoising) noexcept {
    switch (denoising) {
    case Denoising::Off: return "off";
    case Denoising::Standard: return "standard";
    case Denoising::High: return "high";
    }
    return "unknown";
}

} // namespace c4::rendering::quality
