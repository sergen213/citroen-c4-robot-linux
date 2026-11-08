#include "rendering/quality/GraphicSettings.hpp"
#include "rendering/quality/QualityDiagnostics.hpp"
#include "rendering/quality/ShadowCascades.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>

using namespace c4::rendering::quality;

namespace {
GraphicsCapabilities full_capabilities() {
    GraphicsCapabilities caps;
    caps.sample_count_mask = 1 | 2 | 4 | 8;
    caps.fxaa = caps.ssao = caps.pbr = caps.internal_hdr = caps.bloom = true;
    caps.max_shadow_cascades = 4;
    caps.max_shadow_size = 2048;
    caps.hdr_display_output = true;
    caps.trilinear = true;
    caps.max_anisotropy = 16.f;
    caps.temporal_denoise = caps.high_quality_denoise = true;
    return caps;
}

template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

void check_off(const ResolvedGraphics& graphics) {
    assert(graphics.samples == 1 && graphics.post_aa == PostAA::Off);
    assert(!graphics.ssao && !graphics.pbr && !graphics.internal_hdr && !graphics.bloom);
    assert(graphics.shadows == ShadowQuality::Off);
    assert(graphics.ssao_samples == 0 && graphics.shadow_cascades == 0 && graphics.shadow_size == 0);
    assert(graphics.exposure_ev == 0 && graphics.bloom_strength == 0);
    assert(graphics.tone_mapping == ToneMapping::Reinhard);
    assert(graphics.texture_filtering == TextureFiltering::Original && graphics.anisotropy == 1.f);
    assert(graphics.denoising == Denoising::Off && graphics.denoise_passes == 0);
}

class DiagnosticLocale : public std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

void check_diagnostics(const GraphicsCapabilities& caps, const ResolvedGraphics& custom,
                       const ResolvedGraphics& fallback) {
    const std::string supported =
        R"({"sample_counts":[1,2,4,8],"fxaa":true,"ssao":true,"pbr":true,"internal_hdr":true,"bloom":true,"max_shadow_cascades":4,"max_shadow_size":2048,"hdr_display_output":true,"trilinear":true,"max_anisotropy":16,"temporal_denoise":true,"high_quality_denoise":true})";
    assert(capabilities_json(caps) == supported);
    GraphicsCapabilities unavailable;
    unavailable.sample_count_mask = 0;
    assert(capabilities_json(unavailable) ==
        R"({"sample_counts":[1],"fxaa":false,"ssao":false,"pbr":false,"internal_hdr":false,"bloom":false,"max_shadow_cascades":0,"max_shadow_size":0,"hdr_display_output":false,"trilinear":false,"max_anisotropy":1,"temporal_denoise":false,"high_quality_denoise":false})");
    unavailable.sample_count_mask = 2 | 8 | 16;
    assert(capabilities_json(unavailable).find(R"("sample_counts":[1,2,8])") != std::string::npos);
    unavailable.sample_count_mask = 16;
    assert(capabilities_json(unavailable).find(R"("sample_counts":[1])") != std::string::npos);
    assert(resolved_graphics_json(fallback) ==
        R"({"requested_preset":"ultra","effective_preset":"ultra","samples":1,"post_aa":"off","ssao":false,"ssao_samples":0,"ssao_radius":0,"ssao_bias":0,"shadows":"off","shadow_cascades":0,"shadow_size":0,"pbr":false,"internal_hdr":false,"exposure_ev":0,"bloom":false,"bloom_strength":0,"tone_mapping":"reinhard","hdr_display_output":false,"texture_filtering":"original","anisotropy":1,"denoising":"off","denoise_passes":0,"fallbacks":511})");

    auto fractional = custom;
    fractional.ssao_radius = .5f;
    fractional.ssao_bias = .25f;
    fractional.exposure_ev = -.125f;
    fractional.bloom_strength = .75f;
    const std::string actual =
        R"({"requested_preset":"custom","effective_preset":"custom","samples":2,"post_aa":"fxaa","ssao":true,"ssao_samples":16,"ssao_radius":0.5,"ssao_bias":0.25,"shadows":"high","shadow_cascades":4,"shadow_size":2048,"pbr":true,"internal_hdr":true,"exposure_ev":-0.125,"bloom":true,"bloom_strength":0.75,"tone_mapping":"aces","hdr_display_output":true,"texture_filtering":"original","anisotropy":1,"denoising":"standard","denoise_passes":1,"fallbacks":0})";
    assert(resolved_graphics_json(fractional) == actual);
    const auto previous_locale = std::locale();
    std::locale::global(std::locale(previous_locale, new DiagnosticLocale));
    const auto localized_caps = capabilities_json(caps);
    const auto localized_graphics = resolved_graphics_json(fractional);
    std::locale::global(previous_locale);
    assert(localized_caps == supported && localized_graphics == actual);

    for (const auto member : {&ResolvedGraphics::ssao_radius, &ResolvedGraphics::ssao_bias,
                              &ResolvedGraphics::exposure_ev, &ResolvedGraphics::bloom_strength,
                              &ResolvedGraphics::anisotropy}) {
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity()}) {
            auto nonfinite = custom;
            nonfinite.*member = invalid;
            rejects([&] { resolved_graphics_json(nonfinite); });
        }
    }
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()}) {
        auto nonfinite = caps;
        nonfinite.max_anisotropy = invalid;
        rejects([&] { capabilities_json(nonfinite); });
    }
}

void check_texture_filtering(const GraphicsCapabilities& caps) {
    for (const auto preset : {Preset::Original, Preset::Enhanced, Preset::Ultra, Preset::Custom}) {
        GraphicSettings settings;
        settings.preset = preset;
        for (const auto filtering : {TextureFiltering::Original, TextureFiltering::Trilinear,
                                      TextureFiltering::Anisotropic}) {
            settings.texture_filtering = filtering;
            const auto resolved = resolve_graphics(settings, caps);
            assert(resolved.texture_filtering == filtering && resolved.effective_preset == Preset::Custom);
            const float expected = filtering == TextureFiltering::Anisotropic ?
                (preset == Preset::Ultra ? 8.f : 4.f) : 1.f;
            assert(resolved.anisotropy == expected && resolved.fallbacks == 0);
        }
        settings.texture_filtering.reset();
        settings.backend_override = true;
        const auto external = resolve_graphics(settings, caps);
        settings.backend_override = false;
        const auto base = resolve_graphics(settings, caps);
        assert(external.effective_preset == Preset::Custom);
        assert(external.samples == base.samples && external.ssao_samples == base.ssao_samples);
        assert(external.shadows == base.shadows && external.tone_mapping == base.tone_mapping);
        assert(external.texture_filtering == base.texture_filtering && external.anisotropy == base.anisotropy);
    }
    for (const auto preset : {Preset::Enhanced, Preset::Ultra}) {
        GraphicSettings settings;
        settings.preset = preset;
        const float requested = preset == Preset::Ultra ? 8.f : 4.f;
        for (const bool trilinear : {false, true}) {
            for (const float maximum : {-4.f, 0.f, .5f, 1.f, 1.5f, 2.f, 4.f, 8.f, 16.f}) {
                auto limited = caps;
                limited.trilinear = trilinear;
                limited.max_anisotropy = maximum;
                const auto resolved = resolve_graphics(settings, limited);
                const auto expected_filtering = !trilinear ? TextureFiltering::Original :
                    maximum <= 1.f ? TextureFiltering::Trilinear : TextureFiltering::Anisotropic;
                const float expected_anisotropy = trilinear && maximum > 1.f ?
                    (maximum < requested ? maximum : requested) : 1.f;
                assert(resolved.texture_filtering == expected_filtering && resolved.anisotropy == expected_anisotropy);
                assert(resolved.fallbacks == (expected_anisotropy < requested ? TextureFilteringUnavailable : 0u));
                assert(effective_profile(resolved, RendererBackend::Vulkan, preset == Preset::Ultra ? 7u : 0u) ==
                       (resolved.fallbacks ? Preset::Custom : preset));
                const auto source = resolve_graphics({}, limited);
                check_off(source);
                assert(source.fallbacks == 0 && source.effective_preset == Preset::Original);
                settings.texture_filtering = TextureFiltering::Trilinear;
                const auto linear = resolve_graphics(settings, limited);
                assert(linear.texture_filtering == (trilinear ? TextureFiltering::Trilinear : TextureFiltering::Original));
                assert(linear.anisotropy == 1.f && linear.fallbacks == (trilinear ? 0u : TextureFilteringUnavailable));
                settings.texture_filtering = TextureFiltering::Original;
                const auto original = resolve_graphics(settings, limited);
                assert(original.texture_filtering == TextureFiltering::Original && original.anisotropy == 1.f);
                assert(original.fallbacks == 0);
                settings.texture_filtering.reset();
            }
        }
    }
}

void check_denoising(const GraphicsCapabilities& caps) {
    for (const auto preset : {Preset::Original, Preset::Enhanced, Preset::Ultra, Preset::Custom}) {
        GraphicSettings settings;
        settings.preset = preset;
        for (const bool temporal : {false, true}) {
            for (const bool high : {false, true}) {
                auto limited = caps;
                limited.temporal_denoise = temporal;
                limited.high_quality_denoise = high;
                const auto base = resolve_graphics(settings, limited);
                const auto default_mode = preset == Preset::Ultra ? Denoising::High :
                    preset == Preset::Custom && temporal ? Denoising::Standard : Denoising::Off;
                const auto supported_mode = !temporal ? Denoising::Off :
                    default_mode == Denoising::High && !high ? Denoising::Standard : default_mode;
                assert(base.denoising == supported_mode);
                assert(base.denoise_passes == (supported_mode == Denoising::High ? 3u :
                       supported_mode == Denoising::Standard ? 1u : 0u));
                assert(base.fallbacks == (supported_mode != default_mode ? DenoisingUnavailable : 0u));
                for (const auto requested : {Denoising::Off, Denoising::Standard, Denoising::High}) {
                    settings.denoising = requested;
                    const auto resolved = resolve_graphics(settings, limited);
                    const auto expected = requested == Denoising::Off || !temporal ? Denoising::Off :
                        requested == Denoising::High && !high ? Denoising::Standard : requested;
                    assert(resolved.denoising == expected && resolved.effective_preset == Preset::Custom);
                    assert(resolved.denoise_passes == (expected == Denoising::High ? 3u :
                           expected == Denoising::Standard ? 1u : 0u));
                    assert(resolved.fallbacks == (expected != requested ? DenoisingUnavailable : 0u));
                    assert(resolved.samples == base.samples && resolved.ssao_samples == base.ssao_samples);
                    assert(resolved.anisotropy == base.anisotropy && resolved.shadows == base.shadows);
                }
                settings.denoising.reset();
            }
        }
    }
}
void check_material_overrides() {
    const auto caps = full_capabilities();
    GraphicSettings settings;
    settings.preset = Preset::Original;
    settings.material_overrides.push_back({"Exact Material.Name", 0.f, 1.f, std::nullopt});
    for (const float metallic : {0.f, 1.f}) {
        for (const float roughness : {0.f, 1.f}) {
            for (const auto reflectivity : {std::optional<float>{}, std::optional<float>{0.f},
                                            std::optional<float>{1.f}}) {
                settings.material_overrides.front() =
                    {"Exact Material.Name", metallic, roughness, reflectivity};
                check_off(resolve_graphics(settings, caps));
                assert(settings.preset == Preset::Original && settings.material_overrides.size() == 1);
                const auto& source = settings.material_overrides.front();
                assert(source.material_name == "Exact Material.Name");
                assert(source.metallic == metallic && source.roughness == roughness);
                assert(source.reflectivity == reflectivity);
            }
        }
    }
    settings.material_overrides.front().material_name.clear();
    rejects([&] { resolve_graphics(settings, caps); });
    assert(settings.material_overrides.front().material_name.empty());
    settings.material_overrides.front().material_name = "Exact Material.Name";
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity(), -.01f, 1.01f}) {
        for (unsigned field = 0; field < 3; ++field) {
            auto& source = settings.material_overrides.front();
            source = {"Exact Material.Name", 0.f, 1.f, std::nullopt};
            if (field == 0) source.metallic = invalid;
            else if (field == 1) source.roughness = invalid;
            else source.reflectivity = invalid;
            rejects([&] { resolve_graphics(settings, caps); });
            assert(settings.preset == Preset::Original && settings.material_overrides.size() == 1);
            assert(source.material_name == "Exact Material.Name");
            const float retained = field == 0 ? source.metallic :
                field == 1 ? source.roughness : source.reflectivity.value();
            assert(std::isnan(invalid) ? std::isnan(retained) : retained == invalid);
            assert(field == 0 || source.metallic == 0.f);
            assert(field == 1 || source.roughness == 1.f);
            assert(field == 2 || !source.reflectivity);
        }
    }
}

void check_shadow_depth_bias() {
    // A finite camera volume; scene caster padding changes only its light-depth fit.
    const std::array<float, 16> inverse{100,0,0,0, 0,100,0,0,
                                       0,0,495,0, 0,0,505,1};
    for (const bool zero_to_one : {false, true}) {
        const auto tight = build_shadow_cascades(inverse, {0,-.8660254f,-.5f},
            10, 1000, -1, 1, 4, 2048, .5f, 0, zero_to_one);
        const auto padded = build_shadow_cascades(inverse, {0,-.8660254f,-.5f},
            10, 1000, -1, 1, 4, 2048, .5f, 300000, zero_to_one);
        assert(tight && padded && tight->split_depths == padded->split_depths);
        for (unsigned cascade = 0; cascade < tight->count; ++cascade) {
            const auto normalized_depth_scale = [&](const ShadowCascades& fit) {
                const auto& m = fit.matrices[cascade];
                return std::hypot(m[2], m[6], m[10]) * (zero_to_one ? 1.f : .5f);
            };
            const auto& m = tight->matrices[cascade];
            const float world_texel = 2.f / (2048.f * std::hypot(m[0], m[4], m[8]));
            for (unsigned component = 0; component < 2; ++component) {
                const float a = tight->depth_bias[cascade][component] / normalized_depth_scale(*tight);
                const float b = padded->depth_bias[cascade][component] / normalized_depth_scale(*padded);
                const float expected = world_texel * (component == 0 ? .5f : 1.f);
                assert(std::abs(a - expected) < expected * 1.e-5f);
                assert(std::abs(a - b) < expected * 1.e-5f);
                // Padding must not swallow a nearby two-texel caster separation.
                assert(2.f * world_texel * normalized_depth_scale(*padded) >
                       padded->depth_bias[cascade][component]);
            }
        }
    }
}

void check_near_shadow_footprint() {
    // Perspective camera with the source's 60-degree horizontal arc at 4:3,
    // expanded to 16:9, including its recovered 1.005 aperture correction.
    // Keep full CGM3 visibility, not a shadow-distance clamp.
    const auto fit = [](float far_depth, float lambda) {
        const float y_tangent = std::tan(3.14159265358979323846f / 6.f) * .75f * 1.005f;
        const float x_tangent = y_tangent * (16.f / 9.f);
        const std::array<float, 16> inverse{x_tangent,0,0,0, 0,y_tangent,0,0,
            0,0,0,-(far_depth-1.f)/(2.f*far_depth),
            0,0,-1,(far_depth+1.f)/(2.f*far_depth)};
        return build_shadow_cascades(inverse, {0,-.8660254f,-.5f},
            1,far_depth,-1,1,4,2048,lambda,300000);
    };
    const auto old_minimum = fit(55000, .5f);
    const auto old_extended = fit(280000, .5f);
    const auto modern = fit(280000, native_shadow_split_lambda);
    assert(old_minimum && old_extended && modern);
    const auto texel = [](const ShadowCascades& fit) {
        const auto& m = fit.matrices[0];
        return 2.f / (2048.f * std::hypot(m[0],m[4],m[8]));
    };
    assert(texel(*old_extended) > 4.f * texel(*old_minimum));
    assert(texel(*modern) < 1.f);
    assert(modern->count == 4 && modern->split_depths[3] == 280000);
    // Equal logarithmic view-depth budgets keep the near cascade dense even
    // when the whole camera range grows; the last split remains exactly far.
    float previous = 1.f;
    const float ratio = std::pow(280000.f, .25f);
    for (unsigned i = 0; i < modern->count; ++i) {
        assert(std::abs(modern->split_depths[i] / previous - ratio) < ratio * 1.e-5f);
        previous = modern->split_depths[i];
    }
    assert(modern->split_depths[0] < 25.f);
    // A five-unit fence band was sub-texel in the extended old fit; it now
    // spans at least one actual shadow texel, with unchanged tile resolution.
    const auto projected_texels = [](const ShadowCascades& fit, float width) {
        return std::abs(fit.matrices[0][0] * width) * .5f * 2048.f;
    };
    assert(projected_texels(*old_extended,5) < 1);
    assert(projected_texels(*modern,5) > 1);
    for (const float near_depth : {1.f,10.f}) {
        for (const float far_depth : {1000.f,280000.f}) {
            const std::array<float,16> inverse{100,0,0,0, 0,100,0,0,
                0,0,(far_depth-near_depth)*.5f,0, 0,0,(far_depth+near_depth)*.5f,1};
            for (unsigned count = 1; count <= 4; ++count) {
                for (const unsigned resolution : {256u,2048u}) {
                    const auto logarithmic = build_shadow_cascades(inverse,{0,-.8660254f,-.5f},
                        near_depth,far_depth,-1,1,count,resolution,native_shadow_split_lambda);
                    const auto uniform = build_shadow_cascades(inverse,{0,-.8660254f,-.5f},
                        near_depth,far_depth,-1,1,count,resolution,0.f);
                    assert(logarithmic && uniform && logarithmic->count == count);
                    assert(logarithmic->split_depths[count-1] == far_depth);
                    for (unsigned cascade = 0; cascade < count; ++cascade) {
                        const float fraction = static_cast<float>(cascade+1)/count;
                        const float expected = near_depth*std::pow(far_depth/near_depth,fraction);
                        assert(std::abs(logarithmic->split_depths[cascade]-expected) < expected*1.e-5f);
                        const float uniform_expected = near_depth+(far_depth-near_depth)*fraction;
                        assert(std::abs(uniform->split_depths[cascade]-uniform_expected) < uniform_expected*1.e-5f);
                    }
                    // Exact far split must also fit every far-plane corner,
                    // rather than merely retaining a far value in the metadata.
                    const auto& matrix = logarithmic->matrices[count-1];
                    for (const float x : {-100.f,100.f}) {
                        for (const float y : {-100.f,100.f}) {
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                const float clip = matrix[axis]*x + matrix[axis+4]*y
                                    + matrix[axis+8]*far_depth + matrix[axis+12];
                                assert(std::abs(clip) <= 1.f+1.e-5f);
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    check_shadow_depth_bias();
    check_near_shadow_footprint();
    const auto original_bundle = profile_defaults(Preset::Original);
    const auto enhanced_bundle = profile_defaults(Preset::Enhanced);
    const auto ultra_bundle = profile_defaults(Preset::Ultra);
    assert(original_bundle && original_bundle->backend == RendererBackend::OpenGL && original_bundle->rt_effects == 0);
    assert(enhanced_bundle && enhanced_bundle->backend == RendererBackend::Vulkan && enhanced_bundle->rt_effects == 0);
    assert(ultra_bundle && ultra_bundle->backend == RendererBackend::Vulkan && ultra_bundle->rt_effects == 7);
    assert(!profile_defaults(Preset::Custom));
    assert(!profile_defaults(static_cast<Preset>(255)));
    const auto caps = full_capabilities();
    check_texture_filtering(caps);
    check_denoising(caps);
    check_material_overrides();
    GraphicSettings settings;
    assert(requested_profile(settings) == Preset::Original);
    const auto original = resolve_graphics(settings, caps);
    check_off(original);
    assert(original.requested_preset == Preset::Original && original.effective_preset == Preset::Original);
    assert(!original.enhanced() && original.fallbacks == 0);
    assert(original.hdr_display_output);

    settings.preset = Preset::Enhanced;
    const auto enhanced = resolve_graphics(settings, caps);
    assert(enhanced.requested_preset == Preset::Enhanced && enhanced.effective_preset == Preset::Enhanced);
    assert(enhanced.samples == 4 && enhanced.post_aa == PostAA::Fxaa);
    assert(enhanced.ssao && enhanced.ssao_samples == 16);
    assert(enhanced.ssao_radius == 16 && enhanced.ssao_bias == .025f);
    assert(enhanced.shadows == ShadowQuality::Low && enhanced.shadow_cascades == 2 && enhanced.shadow_size == 1024);
    assert(enhanced.pbr && enhanced.internal_hdr && enhanced.bloom);
    assert(enhanced.bloom_strength == .08f && enhanced.exposure_ev == 0);
    assert(enhanced.tone_mapping == ToneMapping::Aces && enhanced.fallbacks == 0);
    assert(enhanced.texture_filtering == TextureFiltering::Anisotropic && enhanced.anisotropy == 4.f);
    assert(enhanced.denoising == Denoising::Off && enhanced.denoise_passes == 0);

    settings.preset = Preset::Ultra;
    assert(requested_profile(settings) == Preset::Ultra);
    const auto ultra = resolve_graphics(settings, caps);
    assert(ultra.samples == 8 && ultra.ssao_samples == 32);
    assert(ultra.shadows == ShadowQuality::High && ultra.shadow_cascades == 4 && ultra.shadow_size == 2048);
    assert(ultra.post_aa == enhanced.post_aa && ultra.ssao_radius == enhanced.ssao_radius);
    assert(ultra.ssao_bias == enhanced.ssao_bias && ultra.bloom_strength == enhanced.bloom_strength);
    assert(ultra.pbr && ultra.internal_hdr && ultra.bloom && ultra.tone_mapping == ToneMapping::Aces);
    assert(ultra.effective_preset == Preset::Ultra && ultra.fallbacks == 0);
    assert(ultra.texture_filtering == TextureFiltering::Anisotropic && ultra.anisotropy == 8.f);
    assert(ultra.denoising == Denoising::High && ultra.denoise_passes == 3);
    assert(effective_profile(original, RendererBackend::OpenGL, 0) == Preset::Original);
    assert(effective_profile(enhanced, RendererBackend::Vulkan, 0) == Preset::Enhanced);
    assert(effective_profile(ultra, RendererBackend::Vulkan, 7) == Preset::Ultra);
    for (const std::uint32_t mask : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u})
        assert(effective_profile(ultra, RendererBackend::Vulkan, mask) == Preset::Custom);
    assert(effective_profile(original, RendererBackend::Vulkan, 0) == Preset::Custom);
    assert(effective_profile(original, RendererBackend::OpenGL, 7) == Preset::Custom);
    assert(effective_profile(enhanced, RendererBackend::OpenGL, 0) == Preset::Custom);
    assert(effective_profile(enhanced, RendererBackend::Vulkan, 1) == Preset::Custom);
    assert(effective_profile(ultra, RendererBackend::OpenGL, 7) == Preset::Custom);

    settings.backend_override = true;
    assert(requested_profile(settings) == Preset::Custom);
    const auto external_override = resolve_graphics(settings, caps);
    assert(external_override.requested_preset == Preset::Ultra && external_override.effective_preset == Preset::Custom);
    assert(external_override.samples == 8 && external_override.ssao_samples == 32);
    assert(external_override.shadow_cascades == 4 && external_override.shadow_size == 2048);
    assert(external_override.texture_filtering == TextureFiltering::Anisotropic && external_override.anisotropy == 8.f);
    assert(external_override.pbr && external_override.internal_hdr && external_override.bloom);
    assert(external_override.tone_mapping == ToneMapping::Aces && external_override.fallbacks == 0);
    assert(external_override.denoising == Denoising::High && external_override.denoise_passes == 3);
    assert(effective_profile(external_override, RendererBackend::Vulkan, 7) == Preset::Custom);

    // Even a value-preserving or false override changes the effective preset.
    for (const auto preset : {Preset::Original, Preset::Enhanced, Preset::Ultra}) {
        settings = {};
        settings.preset = preset;
        const auto inherited = resolve_graphics(settings, caps);
        settings.samples = inherited.samples;
        auto result = resolve_graphics(settings, caps);
        assert(result.requested_preset == preset && result.effective_preset == Preset::Custom);
        assert(result.ssao_samples == inherited.ssao_samples && result.shadows == inherited.shadows);
        settings = {};
        settings.preset = preset;
        settings.bloom = false;
        assert(requested_profile(settings) == Preset::Custom);
        result = resolve_graphics(settings, caps);
        assert(result.effective_preset == Preset::Custom && !result.bloom && result.bloom_strength == 0);
        assert(result.samples == inherited.samples && result.ssao_samples == inherited.ssao_samples);
    }
    // Every optional control is live and counts as an override independently.
    std::array<GraphicSettings, 12> overrides{};
    overrides[0].post_aa = PostAA::Off;
    overrides[1].ssao = false;
    overrides[2].shadows = ShadowQuality::Off;
    overrides[3].pbr = false;
    overrides[4].internal_hdr = false;
    overrides[5].bloom = false;
    overrides[6].exposure_ev = 0;
    overrides[7].tone_mapping = ToneMapping::Reinhard;
    overrides[8].samples = 1;
    overrides[9].texture_filtering = TextureFiltering::Original;
    overrides[10].backend_override = true;
    overrides[11].denoising = Denoising::Off;
    for (const auto& override : overrides) {
        assert(requested_profile(override) == Preset::Custom);
        assert(resolve_graphics(override, caps).effective_preset == Preset::Custom);
    }

    settings = {};
    settings.preset = Preset::Enhanced;
    settings.samples = 1;
    settings.post_aa = PostAA::Off;
    settings.ssao = settings.pbr = settings.internal_hdr = settings.bloom = false;
    settings.shadows = ShadowQuality::Off;
    settings.texture_filtering = TextureFiltering::Original;
    settings.exposure_ev = 3;
    settings.tone_mapping = ToneMapping::Aces;
    const auto disabled = resolve_graphics(settings, caps);
    check_off(disabled);
    assert(disabled.effective_preset == Preset::Custom && disabled.fallbacks == HdrUnavailable);

    settings = {};
    settings.preset = Preset::Custom;
    settings.denoising = Denoising::Off;
    check_off(resolve_graphics(settings, caps));
    settings.denoising.reset();
    settings.samples = 2;
    settings.post_aa = PostAA::Fxaa;
    settings.ssao = true;
    settings.shadows = ShadowQuality::High;
    settings.pbr = settings.internal_hdr = settings.bloom = true;
    settings.exposure_ev = 2;
    settings.tone_mapping = ToneMapping::Aces;
    const auto custom = resolve_graphics(settings, caps);
    assert(custom.samples == 2 && custom.post_aa == PostAA::Fxaa && custom.ssao_samples == 16);
    assert(custom.shadows == ShadowQuality::High && custom.pbr && custom.internal_hdr && custom.bloom);
    assert(custom.exposure_ev == 2 && custom.tone_mapping == ToneMapping::Aces);
    assert(custom.texture_filtering == TextureFiltering::Original && custom.anisotropy == 1.f);
    assert(custom.denoising == Denoising::Standard && custom.denoise_passes == 1);
    assert(effective_profile(custom, RendererBackend::Vulkan, 7) == Preset::Custom);

    // Dependent controls activate floating HDR unless explicitly disabled.
    for (const auto preset : {Preset::Original, Preset::Custom}) {
        std::array<GraphicSettings, 4> dependent{};
        dependent[0].exposure_ev = 2;
        dependent[1].bloom = true;
        dependent[2].tone_mapping = ToneMapping::Aces;
        dependent[3].tone_mapping = ToneMapping::Reinhard;
        for (auto request : dependent) {
            request.preset = preset;
            const auto active = resolve_graphics(request, caps);
            assert(active.internal_hdr && active.effective_preset == Preset::Custom && active.fallbacks == 0);
            assert(active.exposure_ev == request.exposure_ev.value_or(0.f));
            assert(active.bloom == request.bloom.value_or(false));
            assert(active.tone_mapping == request.tone_mapping.value_or(ToneMapping::Reinhard));
            auto no_hdr = caps;
            no_hdr.internal_hdr = false;
            const auto unavailable_hdr = resolve_graphics(request, no_hdr);
            assert(!unavailable_hdr.internal_hdr && unavailable_hdr.exposure_ev == 0 && !unavailable_hdr.bloom);
            assert(unavailable_hdr.tone_mapping == ToneMapping::Reinhard);
            std::uint32_t expected = HdrUnavailable;
            if (request.bloom.value_or(false)) expected |= BloomUnavailable;
            assert(unavailable_hdr.fallbacks == expected);
            request.internal_hdr = false;
            const auto explicitly_off = resolve_graphics(request, caps);
            assert(!explicitly_off.internal_hdr && explicitly_off.exposure_ev == 0 && !explicitly_off.bloom);
            assert(explicitly_off.tone_mapping == ToneMapping::Reinhard && explicitly_off.fallbacks == expected);
        }
    }

    settings = {};
    settings.preset = Preset::Ultra;
    GraphicsCapabilities unavailable;
    const auto fallback = resolve_graphics(settings, unavailable);
    check_off(fallback);
    assert(fallback.requested_preset == Preset::Ultra && fallback.effective_preset == Preset::Ultra);
    assert(fallback.fallbacks == (SamplesUnavailable | FxaaUnavailable | SsaoUnavailable |
                                ShadowsUnavailable | PbrUnavailable | HdrUnavailable | BloomUnavailable |
                                TextureFilteringUnavailable | DenoisingUnavailable));
    assert(!fallback.hdr_display_output);
    assert(effective_profile(fallback, RendererBackend::Vulkan, 7) == Preset::Custom);
    auto filtered_fallback = ultra;
    filtered_fallback.fallbacks = TextureFilteringUnavailable;
    assert(effective_profile(filtered_fallback, RendererBackend::Vulkan, 7) == Preset::Custom);
    check_diagnostics(caps, custom, fallback);
    assert(resolved_graphics_json(original).find(R"("requested_preset":"original")") != std::string::npos);
    assert(resolved_graphics_json(enhanced).find(R"("effective_preset":"enhanced")") != std::string::npos);
    assert(resolved_graphics_json(enhanced).find(R"("shadows":"low")") != std::string::npos);

    // Missing mask bits never imply support; baseline one sample is unconditional.
    for (const unsigned mask : {0u, 1u, 2u, 4u, 8u, 2u | 8u, 4u | 8u, 16u}) {
        auto limited = caps;
        limited.sample_count_mask = mask;
        for (const unsigned requested : {1u, 2u, 4u, 8u}) {
            settings.samples = requested;
            const auto result = resolve_graphics(settings, limited);
            unsigned expected = 1;
            for (const unsigned count : {2u, 4u, 8u})
                if (count <= requested && (mask & count)) expected = count;
            assert(result.samples == expected);
            assert(bool(result.fallbacks & SamplesUnavailable) == (expected != requested));
        }
    }
    settings.samples.reset();
    auto limited = caps;
    limited.max_shadow_cascades = 3;
    auto result = resolve_graphics(settings, limited);
    assert(result.shadows == ShadowQuality::Low && result.shadow_cascades == 2 && result.shadow_size == 1024);
    assert(result.fallbacks == ShadowsUnavailable);
    limited = caps;
    limited.max_shadow_size = 512;
    result = resolve_graphics(settings, limited);
    assert(result.shadows == ShadowQuality::Low && result.shadow_cascades == 2 && result.shadow_size == 512);
    assert(result.fallbacks == ShadowsUnavailable);
    limited.max_shadow_cascades = 1;
    result = resolve_graphics(settings, limited);
    assert(result.shadows == ShadowQuality::Low && result.shadow_cascades == 1);
    for (const unsigned size : {0u, 255u, 256u}) {
        limited.max_shadow_size = size;
        result = resolve_graphics(settings, limited);
        assert((result.shadows == ShadowQuality::Off) == (size < 256));
        assert(result.fallbacks == ShadowsUnavailable);
    }
    limited.max_shadow_cascades = 0;
    limited.max_shadow_size = 2048;
    result = resolve_graphics(settings, limited);
    assert(result.shadows == ShadowQuality::Off && result.shadow_cascades == 0 && result.shadow_size == 0);

    // Bloom capability alone is insufficient without floating-point HDR attachments.
    limited = caps;
    limited.internal_hdr = false;
    settings.exposure_ev = 3;
    result = resolve_graphics(settings, limited);
    assert(!result.internal_hdr && !result.bloom && result.exposure_ev == 0);
    assert(result.tone_mapping == ToneMapping::Reinhard);
    assert(result.fallbacks == (HdrUnavailable | BloomUnavailable));
    settings.internal_hdr = false;
    result = resolve_graphics(settings, caps);
    assert(!result.internal_hdr && !result.bloom && result.exposure_ev == 0);
    assert(result.tone_mapping == ToneMapping::Reinhard && result.fallbacks == (HdrUnavailable | BloomUnavailable));
    settings.internal_hdr = true;
    limited = caps;
    limited.bloom = false;
    result = resolve_graphics(settings, limited);
    assert(result.internal_hdr && !result.bloom && result.exposure_ev == 3);
    assert(result.tone_mapping == ToneMapping::Aces && result.fallbacks == BloomUnavailable);

    for (const unsigned samples : {0u, 3u, 5u, 16u}) {
        settings = {};
        settings.samples = samples;
        rejects([&] { resolve_graphics(settings, caps); });
    }
    settings = {};
    settings.preset = static_cast<Preset>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    settings = {};
    settings.post_aa = static_cast<PostAA>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    settings = {};
    settings.shadows = static_cast<ShadowQuality>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    settings = {};
    settings.tone_mapping = static_cast<ToneMapping>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    settings = {};
    settings.texture_filtering = static_cast<TextureFiltering>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    settings = {};
    settings.denoising = static_cast<Denoising>(255);
    rejects([&] { resolve_graphics(settings, caps); });
    for (const float invalid : {std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid_caps = caps;
        invalid_caps.max_anisotropy = invalid;
        rejects([&] { resolve_graphics({}, invalid_caps); });
    }
    for (const float ev : {-10.01f, 10.01f, std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        settings = {};
        settings.exposure_ev = ev;
        rejects([&] { resolve_graphics(settings, caps); });
    }
    for (const float ev : {-10.f, 10.f}) {
        settings = {};
        settings.internal_hdr = true;
        settings.exposure_ev = ev;
        assert(resolve_graphics(settings, caps).exposure_ev == ev);
        assert(quality_uniforms(resolve_graphics(settings, caps), 1, 1).post[0] == std::exp2(ev));
    }

    const auto uniforms = quality_uniforms(custom, 1920, 1080);
    assert((uniforms.effects == std::array<float, 4>{1, 16, 1, 1}));
    assert((uniforms.post == std::array<float, 4>{4, .08f, 1, 16}));
    assert((uniforms.sampling == std::array<float, 4>{1.f / 1920, 1.f / 1080, .025f, 4}));
    // Policy has no frame-rate/simulation input, and resolution changes only texel size.
    const auto resized = quality_uniforms(custom, 640, 480);
    assert(resized.effects == uniforms.effects && resized.post == uniforms.post);
    assert(resized.sampling[2] == uniforms.sampling[2] && resized.sampling[3] == uniforms.sampling[3]);
    assert(resized.sampling[0] == 1.f / 640 && resized.sampling[1] == 1.f / 480);
    rejects([&] { quality_uniforms(custom, 0, 1080); });
    rejects([&] { quality_uniforms(custom, 1920, 0); });
    rejects([&] { quality_uniforms(custom, 0, 0); });
    const auto off = quality_uniforms(original, 1, 1);
    assert((off.effects == std::array<float, 4>{0, 0, 0, 0}));
    assert((off.post == std::array<float, 4>{1, 0, 0, 0}));
    assert((off.sampling == std::array<float, 4>{1, 1, 0, 0}));

    assert(preset_name(Preset::Original) == "original" && preset_name(Preset::Enhanced) == "enhanced");
    assert(preset_name(Preset::Ultra) == "ultra" && preset_name(Preset::Custom) == "custom");
    assert(post_aa_name(PostAA::Off) == "off" && post_aa_name(PostAA::Fxaa) == "fxaa");
    assert(shadow_quality_name(ShadowQuality::Off) == "off" && shadow_quality_name(ShadowQuality::Low) == "low");
    assert(shadow_quality_name(ShadowQuality::High) == "high");
    assert(tone_mapping_name(ToneMapping::Reinhard) == "reinhard" && tone_mapping_name(ToneMapping::Aces) == "aces");
    assert(texture_filtering_name(TextureFiltering::Original) == "original");
    assert(texture_filtering_name(TextureFiltering::Trilinear) == "trilinear");
    assert(texture_filtering_name(TextureFiltering::Anisotropic) == "anisotropic");
    assert(texture_filtering_name(static_cast<TextureFiltering>(255)) == "unknown");
    assert(denoising_name(Denoising::Off) == "off");
    assert(denoising_name(Denoising::Standard) == "standard");
    assert(denoising_name(Denoising::High) == "high");
    assert(denoising_name(static_cast<Denoising>(255)) == "unknown");
}
