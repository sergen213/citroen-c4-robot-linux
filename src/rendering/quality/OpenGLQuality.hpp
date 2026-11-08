#pragma once

#include "GraphicSettings.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace c4::rendering::quality {

// All hooks run on the owning current compatibility OpenGL context. Destroy
// before that context; Original never binds quality targets or executes passes.
class OpenGLQuality {
public:
    using Matrix = std::array<float, 16>;
    enum ShadowUniform : unsigned {
        AlphaTest, Technique, Textured, ModulateDiffuse, DiffuseAlpha, AlphaReference,
        SourceAmbient, Skills, ReflectionWeight, ViewPosition, SunDirection,
        ViewDirection, Skill5, TextureTransform, PassScale, ShadowUniformCount
    };
    struct ShadowPass {
        unsigned program{};
        int model_uniform{-1};
        Matrix light_view_projection{};
        unsigned cascade{};
        std::array<int, ShadowUniformCount> alpha_uniforms{};
    };
    using ShadowDraw = void (*)(void*, const ShadowPass&);
    struct Diagnostics {
        std::uint64_t scene_frames{}, resolve_passes{}, post_passes{}, ao_passes{},
                      bloom_passes{}, shadow_passes{}, attachments_created{};
        unsigned width{}, height{}, max_samples{}, active_samples{1};
        bool floating_targets{}, depth_texture{}, shadow_atlas{};
        std::string fallback_reason;
    };

    OpenGLQuality();
    ~OpenGLQuality();
    OpenGLQuality(const OpenGLQuality&) = delete;
    OpenGLQuality& operator=(const OpenGLQuality&) = delete;
    // Preparation is synchronous. Unsupported capabilities resolve down; failure
    // allocating an advertised selected target throws before candidate commit.
    void initialize(unsigned width, unsigned height);
    void resize(unsigned width, unsigned height);
    void set_settings(const GraphicSettings&);
    // True only when the owner's authored-material shader branch is connected.
    void set_material_support(bool pbr, bool shadows);
    // Owner reports only successfully created mip chains and connected samplers.
    void set_texture_support(bool trilinear, float max_anisotropy);
    const GraphicsCapabilities& capabilities() const noexcept;
    const ResolvedGraphics& resolved() const noexcept;
    // Borrowed from live requested settings; do not retain across set_settings.
    std::span<const ModernMaterialOverride> material_overrides() const noexcept;
    const Diagnostics& diagnostics() const noexcept;
    bool begin_scene();
    void end_scene(const Matrix& projection, const Matrix& inverse_projection);
    void finish();
    void draw_shadows(std::span<const Matrix> cascades, ShadowDraw, void* user);
    unsigned shadow_texture() const noexcept;
    // Offset.xy/scale.zw of the actually rendered region within each 2x2 cell.
    // Atlas dimensions are 2*capabilities().max_shadow_size; Low uses smaller regions.
    std::array<float, 4> shadow_tile(unsigned cascade) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4::rendering::quality
