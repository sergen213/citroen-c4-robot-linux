#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace c4::rendering::quality {

enum class Preset : std::uint8_t { Original, Enhanced, Ultra, Custom };
enum class RendererBackend : std::uint8_t { OpenGL, Vulkan };
struct ProfileDefaults {
    RendererBackend backend;
    std::uint32_t rt_effects;
};
enum class TextureFiltering : std::uint8_t { Original, Trilinear, Anisotropic };
enum class Denoising : std::uint8_t { Off, Standard, High };
enum class PostAA : std::uint8_t { Off, Fxaa };
enum class ShadowQuality : std::uint8_t { Off, Low, High };
enum class ToneMapping : std::uint8_t { Reinhard, Aces };

// Optional native approximation, never recovered original physical metadata.
struct ModernMaterialOverride {
    std::string material_name;
    float metallic{}, roughness{1.f};
    std::optional<float> reflectivity;
};

// Optional members are explicit user overrides, not capability promises.
struct GraphicSettings {
    Preset preset{Preset::Original};
    bool backend_override{};
    std::optional<TextureFiltering> texture_filtering;
    std::optional<Denoising> denoising;
    std::optional<unsigned> samples;
    std::optional<PostAA> post_aa;
    std::optional<bool> ssao;
    std::optional<ShadowQuality> shadows;
    std::optional<bool> pbr, internal_hdr, bloom;
    std::optional<float> exposure_ev;
    std::optional<ToneMapping> tone_mapping;
    std::vector<ModernMaterialOverride> material_overrides;
};

// Backends populate these only after both resource creation and shader/pass
// integration succeed. Extension presence alone is insufficient.
struct GraphicsCapabilities {
    unsigned sample_count_mask{1}; // OR of sample counts: 1 | 2 | 4 | 8.
    bool fxaa{}, ssao{}, pbr{}, internal_hdr{}, bloom{};
    unsigned max_shadow_cascades{}, max_shadow_size{};
    bool hdr_display_output{}; // Actual negotiated surface; not internal HDR.
    bool trilinear{};
    float max_anisotropy{1.f};
    bool temporal_denoise{}, high_quality_denoise{};
};

enum Fallback : std::uint32_t {
    SamplesUnavailable = 1u << 0, FxaaUnavailable = 1u << 1,
    SsaoUnavailable = 1u << 2, ShadowsUnavailable = 1u << 3,
    PbrUnavailable = 1u << 4, HdrUnavailable = 1u << 5,
    BloomUnavailable = 1u << 6, TextureFilteringUnavailable = 1u << 7,
    DenoisingUnavailable = 1u << 8
};
struct ResolvedGraphics {
    Preset requested_preset{Preset::Original}, effective_preset{Preset::Original};
    unsigned samples{1};
    PostAA post_aa{PostAA::Off};
    TextureFiltering texture_filtering{TextureFiltering::Original};
    float anisotropy{1.f};
    Denoising denoising{Denoising::Off};
    unsigned denoise_passes{};
    bool ssao{}, pbr{}, internal_hdr{}, bloom{}, hdr_display_output{};
    ShadowQuality shadows{ShadowQuality::Off};
    unsigned ssao_samples{}, shadow_cascades{}, shadow_size{};
    float exposure_ev{}, bloom_strength{}, ssao_radius{}, ssao_bias{};
    ToneMapping tone_mapping{ToneMapping::Reinhard};
    std::uint32_t fallbacks{};
    bool enhanced() const noexcept { return effective_preset != Preset::Original; }
};

// std140 vec4 schema shared by GL/Vulkan, no bool packing or implicit padding.
// effects: FXAA, SSAO sample count, PBR, internal HDR.
// post: exposure multiplier (2^EV), bloom strength, tone map (0 Reinhard/1 ACES), SSAO radius.
// sampling: inverse width, inverse height, SSAO bias, shadow cascade count.
struct alignas(16) QualityUniforms {
    std::array<float, 4> effects{}, post{}, sampling{};
};
static_assert(sizeof(QualityUniforms) == 48);

std::optional<ProfileDefaults> profile_defaults(Preset) noexcept;
// Requested label is capability-independent; the preset remains the quality base.
Preset requested_profile(const GraphicSettings&) noexcept;
ResolvedGraphics resolve_graphics(const GraphicSettings&, const GraphicsCapabilities&);
// Runtime diagnostics/display copies use the actual backend and enabled RT mask.
// Keep the resolved quality base separately for shaders and resource tuning.
Preset effective_profile(const ResolvedGraphics&, RendererBackend actual_backend,
                         std::uint32_t actual_rt_effects) noexcept;
QualityUniforms quality_uniforms(const ResolvedGraphics&, unsigned width, unsigned height);
std::string_view preset_name(Preset) noexcept;
std::string_view post_aa_name(PostAA) noexcept;
std::string_view shadow_quality_name(ShadowQuality) noexcept;
std::string_view tone_mapping_name(ToneMapping) noexcept;
std::string_view texture_filtering_name(TextureFiltering) noexcept;
std::string_view denoising_name(Denoising) noexcept;

} // namespace c4::rendering::quality
