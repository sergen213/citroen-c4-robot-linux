#pragma once

#include "assets/Assets.hpp"
#include "assets/GpuImage.hpp"
#include "core/settings.hpp"
#include "physics/NativeNumeric.hpp"
#include "rendering/SourceLighting.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace c4::game {
struct GameContext;
struct FrameTiming;
struct SharedState;
enum class UiCommand;
}

namespace c4::ui {

struct UiSettings {
    int volume{100}, brightness{}, music{1}, color{}, ghost{1};
};
struct MenuInput {
    float x{}, y{};
    bool pointer_inside{}, pointer_pressed{}, pointer_released{};
    // Source engine samples the raw press edge and current held level in BOTH UI passes.
    bool pointer_down{};
    // Source mouse_mode != 0; disabled passes must not change button-held history.
    bool pointer_enabled{};
    bool tab{}, shift{}, enter{}, left{}, right{}, escape_pressed{}, escape_released{};
};

enum class UiPass { BeforeWorld, AfterWorld };

// Original WDL include order and declaration line identify a node, not its glyphs.
constexpr std::uint32_t source_ui_order(std::uint32_t include_rank, std::uint32_t line) noexcept {
    return (include_rank << 16) | line;
}

// Finite source PANEL pick metadata; TEXT/view entities use separate engine paths.
struct SourcePanelPickState {
    physics::Var x{}, y{}, sx{}, sy{};
    std::uint32_t width{}, height{};
    int layer{};
    std::uint32_t source_order{};
    bool visible{};
};

// Original panel coordinates; compositor fits this canvas without stretching.
struct Quad {
    std::uint32_t image_index{};
    std::array<float, 4> rect{}; // x, y, width, height, top-left pixels
    std::array<float, 4> uv{0, 0, 1, 1}; // u0, v0, u1, v1
    std::array<float, 4> color{1, 1, 1, 1}; // straight RGBA
    float rotation{}; // clockwise radians about rect center; original HUD needle
    bool filtered{true}; // source FILTER panels; bitmap TEXT defaults to nearest
    int layer{10};
    std::uint32_t source_order{}; // Equal source layers insert newer declarations first; 0 is unranked.
};

// Original camera-relative view entities, not world/RT geometry.
// Both matrices use OpenGL clip convention for the selected authored canvas;
// compositor applies Viewport::fit exactly once, then Vulkan converts Y/depth.
struct OverlayMesh {
    std::span<const assets::Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::array<float,16> model_to_view{};
    std::array<float,16> view_projection{};
    std::uint32_t image_index{};
    std::uint64_t pose_revision{};
    int layer{};
    const rendering::NativeMaterialState* material_state{};
    const rendering::SourceLightingUniforms* lighting{};
    std::uint32_t source_flags{};
    // Original D3DCULL values, mapped by each backend after its axis conversion.
    std::uint8_t source_cull_mode{};
    bool filtered{true};
    float color_multiplier{1}; // Recovered fixed-function texture COLOROP.
    std::uint32_t source_order{};
    bool source_effects_enabled{}; // Explicit private VIEW eligibility, not world/material shader state.
};

struct UiFrame {
    std::span<const assets::Image> images;
    std::span<const Quad> quads;
    float canvas_width{640};
    float canvas_height{480};
    std::span<const std::uint64_t> image_revisions; // empty: immutable; menu video changes only its slot
    std::span<const OverlayMesh> meshes; // stable source geometry, ordered by layer
    bool draw_world{true}; // Original blank menu/loading world, not an invented track preview.
    std::span<const OverlayMesh> prepared_meshes; // All selected assets, even when source visibility is off.
    std::span<const assets::GpuImage* const> gpu_images; // Aligned with images; null selects live/generated RGBA.
};

struct Viewport {
    float x{}, y{}, width{}, height{}, scale{};
    static Viewport fit(int width, int height, float canvas_width, float canvas_height) noexcept;
    bool map(float window_x, float window_y, float& canvas_x, float& canvas_y) const noexcept;
};

class NativeUi {
public:
    explicit NativeUi(const std::filesystem::path& original_root,
        const assets::GpuImageRuntimeContext& file_context, int launch_width, int launch_height);
    ~NativeUi();
    NativeUi(const NativeUi&) = delete;
    NativeUi& operator=(const NativeUi&) = delete;
    // Scheduler input passes surround WORLD; native keyboard/graphics edges run only in UI1.
    game::UiCommand input(const MenuInput& input, UiPass pass);
    // Flush callback effects without advancing source continuations or animation clocks.
    void apply_callbacks(game::GameContext& context);
    // Advance Menu source SCRIPT continuations once and drain only their deferred command.
    game::UiCommand advance_continuations(game::GameContext& context, game::FrameTiming timing);
    // Synchronize post-SIM bindings; tick HUD/media once, without resuming Menu continuations.
    void update(game::GameContext& context, game::FrameTiming timing);
    // Rebuild final overlays after UI2 without ticking logic or issuing callback effects.
    void compose(const game::SharedState& state);
    const UiFrame& frame() const noexcept;
    const UiSettings& settings() const noexcept;
    void set_selected_ghost(int choice);
    void configure_graphics(const Settings& active,
        const rendering::quality::GraphicsCapabilities& capabilities,
        const rendering::quality::ResolvedGraphics& effective,
        std::uint32_t supported_rt_effects, std::uint32_t active_rt_effects,
        std::string_view backend_name, std::string_view capability_reason,
        std::string_view apply_error, std::optional<bool> observed_vsync);
    std::optional<Settings> take_graphics_request();
    bool cursor_visible() const noexcept;
    bool captures_game_input() const noexcept;
    // Pure pre-SIM capture query; the actual graphics-opening callback remains in UI1.
    bool captures_game_input(const MenuInput& input) const noexcept;
    double needle_roll() const noexcept;
    double gauge_frame() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4::ui
