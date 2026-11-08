#pragma once

#include "UiResources.hpp"
#include "core/settings.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace c4::ui {

// Authored extension of the native menus. Only Apply emits a runtime request;
// configure supplies authoritative applied state after the runtime transaction.
class GraphicsPanel {
public:
    explicit GraphicsPanel(Resources& resources);
    void configure(const c4::Settings& active,
                   const rendering::quality::GraphicsCapabilities& caps,
                   const rendering::quality::ResolvedGraphics& effective,
                   std::uint32_t supported_rt_effects, std::uint32_t active_rt_effects,
                   std::string_view backend_name,
                   std::string_view capability_reason, std::string_view apply_error,
                   std::optional<bool> observed_vsync);
    void open();
    void close() noexcept;
    bool is_open() const noexcept { return open_; }
    void input(const MenuInput& input);
    void draw(Painter& painter);
    std::optional<c4::Settings> take_request();

private:
    enum class Control : unsigned char {
        Profile, Backend, Filtering, Samples, PostAA, Ssao, Shadows, Pbr,
        InternalHdr, Bloom, Exposure, ToneMapping,
        Reflections, RayShadows, GlobalIllumination, Denoising,
        Resolution, Fullscreen, Vsync, FrameLimit, StatusPage,
        Count
    };
    static constexpr unsigned tab_count = 4;
    static constexpr unsigned max_rows = 12;
    struct Row {
        Control control{};
        std::string_view label;
        std::array<char, 32> requested{}, effective{};
        bool supported{true};
    };
    struct Hit { int focus{-1}; int direction{1}; };
    void refresh_rows();
    void refresh_status();
    void change(Control control, int direction);
    void activate(int focus, int direction);
    void apply();
    Hit hit(float x, float y) const noexcept;
    unsigned focus_count() const noexcept { return tab_count + row_count_ + 3; }
    void custom() noexcept;
    void set_profile(rendering::quality::Preset preset);
    void text(Painter& painter, std::string_view value, float x, float y, float alpha = 1) const;
    unsigned status_rows() const noexcept;

    Resources& resources_;
    std::uint32_t font_{}, white_{};
    c4::Settings active_, draft_;
    rendering::quality::GraphicsCapabilities caps_;
    rendering::quality::ResolvedGraphics effective_;
    std::optional<bool> observed_vsync_;
    std::uint32_t supported_rt_effects_{}, active_rt_effects_{};
    std::string backend_name_, capability_reason_, apply_error_, status_;
    std::optional<c4::Settings> request_;
    std::array<Row, max_rows> rows_{};
    std::array<std::array<int, 2>, 7> resolutions_{};
    std::array<char, 112> heading_{}, footer_{};
    unsigned row_count_{}, tab_{}, status_page_{}, status_pages_{1};
    int focus_{}, pressed_{-1}, pressed_direction_{1};
    std::vector<std::size_t> status_lines_;
    bool configured_{}, open_{}, pending_{};
};

} // namespace c4::ui
