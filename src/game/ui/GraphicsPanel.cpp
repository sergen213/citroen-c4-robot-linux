#include "GraphicsPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace c4::ui {
namespace {
namespace quality = rendering::quality;
constexpr std::array<std::string_view, 4> tabs{"QUALITY", "RAY EFFECTS", "DISPLAY", "STATUS"};
constexpr std::array<int, 7> frame_limits{0, 30, 60, 90, 120, 144, 240};
constexpr std::array<int,4> font_width{5,7,9,11}, font_height{10,14,19,23};
constexpr float row_top = 100, row_height = 22;
constexpr std::string_view on_off(bool value) { return value ? "on" : "off"; }

template<std::size_t N>
void copy(std::array<char, N>& out, std::string_view value) {
    const auto size = std::min(value.size(), N - 1);
    std::copy_n(value.data(), size, out.data());
    out[size] = '\0';
}
template<class T, class Allowed>
void cycle(std::optional<T>& value, unsigned count, int direction, Allowed allowed) {
    int index = value ? static_cast<int>(*value) + 1 : 0;
    for (unsigned n = 0; n <= count; ++n) {
        index = (index + direction + static_cast<int>(count) + 1) % (static_cast<int>(count) + 1);
        if (!index) { value.reset(); return; }
        const auto next = static_cast<T>(index - 1);
        if (allowed(next)) { value = next; return; }
    }
}
std::string_view bool_value(const std::optional<bool>& value) {
    return value ? on_off(*value) : "inherit";
}
}

GraphicsPanel::GraphicsPanel(Resources& resources)
    : resources_(resources),
      font_(resources.add("time_beyazfont_" + std::to_string(resources.tier()) + ".tga", true, true)),
      white_(resources.add_image(assets::Image{1, 1, {255, 255, 255, 255}})) {}

void GraphicsPanel::configure(const c4::Settings& active,
                              const quality::GraphicsCapabilities& caps,
                              const quality::ResolvedGraphics& effective,
                              std::uint32_t supported_rt_effects, std::uint32_t active_rt_effects,
                              std::string_view backend_name, std::string_view capability_reason,
                              std::string_view apply_error, std::optional<bool> observed_vsync) {
    active_ = active;
    if (!configured_ || apply_error.empty()) draft_ = active;
    caps_ = caps;
    effective_ = effective;
    observed_vsync_ = observed_vsync;
    supported_rt_effects_ = supported_rt_effects;
    active_rt_effects_ = active_rt_effects;
    backend_name_ = backend_name;
    capability_reason_ = capability_reason;
    apply_error_ = apply_error;
    resolutions_ = {{{active.width, active.height}, {640, 480}, {1280, 720}, {1920, 1080},
                     {2560, 1440}, {3440, 1440}, {3840, 2160}}};
    configured_ = true;
    pending_ = false;
    refresh_status();
    refresh_rows();
}
void GraphicsPanel::open() {
    if (!configured_) return;
    draft_ = active_;
    tab_ = status_page_ = 0;
    focus_ = 0;
    pressed_ = -1;
    open_ = true;
    refresh_rows();
}
void GraphicsPanel::close() noexcept { open_ = false; pressed_ = -1; }
void GraphicsPanel::custom() noexcept { draft_.graphics.backend_override = true; }
void GraphicsPanel::set_profile(quality::Preset preset) {
    if (const auto defaults = quality::profile_defaults(preset)) {
        draft_.graphics = {};
        draft_.graphics.preset = preset;
        draft_.renderer = defaults->backend == quality::RendererBackend::OpenGL ? "opengl" : "vulkan";
        draft_.rt_effects = defaults->rt_effects;
    } else custom();
}

void GraphicsPanel::refresh_rows() {
    row_count_ = 0;
    auto add = [&](Control control, std::string_view label, std::string_view requested,
                   std::string_view applied, bool supported = true) -> Row& {
        auto& row = rows_[row_count_++];
        row.control = control; row.label = label; row.supported = supported;
        copy(row.requested, requested); copy(row.effective, applied);
        return row;
    };
    const auto& g = draft_.graphics;
    const auto& e = effective_;
    switch (tab_) {
    case 0: {
        add(Control::Profile, "Profile", quality::preset_name(quality::requested_profile(g)),
            quality::preset_name(e.effective_preset));
        add(Control::Backend, "Renderer", draft_.renderer, backend_name_);
        add(Control::Filtering, "Texture filtering", g.texture_filtering ? quality::texture_filtering_name(*g.texture_filtering) : "inherit",
            quality::texture_filtering_name(e.texture_filtering), caps_.trilinear);
        auto& samples = add(Control::Samples, "MSAA samples", "inherit", "", (caps_.sample_count_mask & 14u) != 0);
        if (g.samples) std::snprintf(samples.requested.data(), samples.requested.size(), "%u", *g.samples);
        std::snprintf(samples.effective.data(), samples.effective.size(), "%u", e.samples);
        add(Control::PostAA, "Post AA", g.post_aa ? quality::post_aa_name(*g.post_aa) : "inherit", quality::post_aa_name(e.post_aa), caps_.fxaa);
        add(Control::Ssao, "Ambient occlusion", bool_value(g.ssao), on_off(e.ssao), caps_.ssao);
        add(Control::Shadows, "Raster shadows", g.shadows ? quality::shadow_quality_name(*g.shadows) : "inherit", quality::shadow_quality_name(e.shadows), caps_.max_shadow_cascades > 0 && caps_.max_shadow_size >= 256);
        add(Control::Pbr, "PBR materials", bool_value(g.pbr), on_off(e.pbr), caps_.pbr);
        add(Control::InternalHdr, "Internal HDR", bool_value(g.internal_hdr), on_off(e.internal_hdr), caps_.internal_hdr);
        add(Control::Bloom, "Bloom", bool_value(g.bloom), on_off(e.bloom), caps_.bloom && caps_.internal_hdr);
        auto& exposure = add(Control::Exposure, "Exposure EV", "inherit", "", caps_.internal_hdr);
        if (g.exposure_ev) std::snprintf(exposure.requested.data(), exposure.requested.size(), "%+.2f", static_cast<double>(*g.exposure_ev));
        std::snprintf(exposure.effective.data(), exposure.effective.size(), "%+.2f", static_cast<double>(e.exposure_ev));
        add(Control::ToneMapping, "Tone mapping", g.tone_mapping ? quality::tone_mapping_name(*g.tone_mapping) : "inherit", quality::tone_mapping_name(e.tone_mapping), caps_.internal_hdr);
        break;
    }
    case 1:
        add(Control::Reflections, "RT reflections", on_off((draft_.rt_effects & 1u) != 0), on_off((active_rt_effects_ & 1u) != 0), (supported_rt_effects_ & 1u) != 0);
        add(Control::RayShadows, "RT shadows", on_off((draft_.rt_effects & 2u) != 0), on_off((active_rt_effects_ & 2u) != 0), (supported_rt_effects_ & 2u) != 0);
        add(Control::GlobalIllumination, "RT global illumination", on_off((draft_.rt_effects & 4u) != 0), on_off((active_rt_effects_ & 4u) != 0), (supported_rt_effects_ & 4u) != 0);
        add(Control::Denoising, "RT denoising", g.denoising ? quality::denoising_name(*g.denoising) : "inherit",
            quality::denoising_name(e.denoising), draft_.rt_effects != 0 && caps_.temporal_denoise);
        break;
    case 2: {
        auto& resolution = add(Control::Resolution, "Resolution", "", "");
        std::snprintf(resolution.requested.data(), resolution.requested.size(), "%dx%d", draft_.width, draft_.height);
        std::snprintf(resolution.effective.data(), resolution.effective.size(), "%dx%d", active_.width, active_.height);
        add(Control::Fullscreen, "Fullscreen", on_off(draft_.fullscreen), on_off(active_.fullscreen));
        add(Control::Vsync, "V-sync", on_off(draft_.vsync),
            observed_vsync_ ? on_off(*observed_vsync_) : "unknown");
        auto& limit = add(Control::FrameLimit, "Frame limit", "unlimited", "unlimited");
        if (draft_.frame_limit) std::snprintf(limit.requested.data(), limit.requested.size(), "%d FPS", draft_.frame_limit);
        if (active_.frame_limit) std::snprintf(limit.effective.data(), limit.effective.size(), "%d FPS", active_.frame_limit);
        break;
    }
    default: {
        auto& page = add(Control::StatusPage, "Status page", "", "read-only runtime data");
        std::snprintf(page.requested.data(), page.requested.size(), "%u / %u", status_page_ + 1, status_pages_);
        break;
    }
    }
    std::snprintf(heading_.data(), heading_.size(), "GRAPHICS  |  REQUESTED: %s  |  BASE: %s",
                  quality::preset_name(quality::requested_profile(g)).data(), quality::preset_name(g.preset).data());
    if (pending_) copy(footer_, "Apply requested; awaiting runtime result. No success is assumed.");
    else if (!apply_error_.empty()) copy(footer_, "Apply error: see STATUS for the complete runtime message.");
    else copy(footer_, "Inherit uses the selected base. Limited controls: see STATUS for actual capabilities.");
    focus_ = std::clamp(focus_, 0, static_cast<int>(focus_count()) - 1);
}

void GraphicsPanel::refresh_status() {
    status_ = "Actual backend: ";
    status_ += backend_name_;
    status_ += '\n';
    std::array<char, 1024> summary{};
    std::snprintf(summary.data(), summary.size(),
        "Applied profile: %s | Requested base: %s\n"
        "RT supported mask: %u | RT active mask: %u\n"
        "MSAA mask: %u | FXAA: %s | SSAO: %s | PBR: %s\n"
        "Shadow cascades: %u | Shadow size: %u\n"
        "Trilinear: %s | Maximum anisotropy: %.2f | Applied anisotropy: %.2f\n"
        "Internal HDR: %s | Bloom: %s | Negotiated HDR display: %s\n"
        "Fallback bits: 0x%08x\n",
        quality::preset_name(effective_.effective_preset).data(),
        quality::preset_name(active_.graphics.preset).data(), supported_rt_effects_, active_rt_effects_,
        caps_.sample_count_mask, on_off(caps_.fxaa).data(), on_off(caps_.ssao).data(), on_off(caps_.pbr).data(),
        caps_.max_shadow_cascades, caps_.max_shadow_size, on_off(caps_.trilinear).data(),
        static_cast<double>(caps_.max_anisotropy), static_cast<double>(effective_.anisotropy),
        on_off(caps_.internal_hdr).data(), on_off(caps_.bloom).data(), on_off(caps_.hdr_display_output).data(), effective_.fallbacks);
    status_ += summary.data();
    std::snprintf(summary.data(), summary.size(), "RT denoising: %s | Actual passes: %u | Standard supported: %s | High supported: %s\n",
                  quality::denoising_name(effective_.denoising).data(), effective_.denoise_passes,
                  on_off(caps_.temporal_denoise).data(), on_off(caps_.high_quality_denoise).data());
    status_ += summary.data();
    constexpr std::array<std::string_view, 9> fallback_names{
        "MSAA samples unavailable", "FXAA unavailable", "SSAO unavailable", "Raster shadows unavailable",
        "PBR unavailable", "Internal HDR/exposure/tone mapping unavailable", "Bloom unavailable", "Texture filtering unavailable",
        "RT denoising unavailable"};
    for (unsigned i = 0; i < fallback_names.size(); ++i)
        if (effective_.fallbacks & (1u << i)) { status_.append(fallback_names[i]); status_ += '\n'; }
    status_ += "Capability reason:\n";
    status_.append(capability_reason_.empty() ? std::string_view("No additional runtime reason supplied.") : std::string_view(capability_reason_));
    status_ += "\nApply error:\n";
    status_.append(apply_error_.empty() ? std::string_view("None reported.") : std::string_view(apply_error_));
    status_ += "\nDisplay changes are validated by the native platform during Apply.\n"
               "Internal HDR is not a promise of HDR display output.\n"
               "Classic menu brightness remains unchanged.\n";
    status_lines_.clear();
    std::size_t begin = 0;
    while (begin < status_.size()) {
        status_lines_.push_back(begin);
        const auto newline = status_.find('\n', begin);
        const auto columns = static_cast<std::size_t>(550*resources_.scale_x()/.625f/font_width[resources_.tier()]);
        auto end = std::min(begin + columns, newline == std::string::npos ? status_.size() : newline + 1);
        while (end < status_.size() && end > begin && (static_cast<unsigned char>(status_[end]) & 0xc0u) == 0x80u) --end;
        begin = end;
    }
    status_lines_.push_back(status_.size());
    const auto count = status_lines_.size() - 1;
    status_pages_ = std::max(1u, static_cast<unsigned>((count + status_rows() - 1) / status_rows()));
    status_page_ = std::min(status_page_, status_pages_ - 1);
}

void GraphicsPanel::change(Control control, int direction) {
    auto& g = draft_.graphics;
    const bool changing_backend = draft_.renderer != active_.renderer;
    auto toggle = [&](std::optional<bool>& field, bool available) {
        cycle(field, 2, direction, [available, changing_backend](bool value) { return !value || available || changing_backend; });
    };
    switch (control) {
    case Control::Profile: {
        const int current = static_cast<int>(quality::requested_profile(g));
        set_profile(static_cast<quality::Preset>((current + direction + 4) % 4));
        break;
    }
    case Control::Backend: draft_.renderer = draft_.renderer == "vulkan" ? "opengl" : "vulkan"; custom(); break;
    case Control::Filtering:
        cycle(g.texture_filtering, 3, direction, [&](quality::TextureFiltering value) {
            return changing_backend || value == quality::TextureFiltering::Original ||
                   (caps_.trilinear && (value != quality::TextureFiltering::Anisotropic || caps_.max_anisotropy > 1.f));
        }); break;
    case Control::Samples: {
        constexpr std::array<unsigned, 5> samples{0, 1, 2, 4, 8};
        const auto it = std::find(samples.begin(), samples.end(), g.samples.value_or(0));
        int index = static_cast<int>(it - samples.begin());
        if (index == 5) index = 0;
        do { index = (index + direction + 5) % 5; } while (index && !changing_backend && !(caps_.sample_count_mask & samples[static_cast<unsigned>(index)]));
        if (index) g.samples = samples[static_cast<unsigned>(index)]; else g.samples.reset();
        break;
    }
    case Control::PostAA: cycle(g.post_aa, 2, direction, [&](quality::PostAA value) { return changing_backend || value == quality::PostAA::Off || caps_.fxaa; }); break;
    case Control::Ssao: toggle(g.ssao, caps_.ssao); break;
    case Control::Shadows:
        cycle(g.shadows, 3, direction, [&](quality::ShadowQuality value) {
            if (changing_backend || value == quality::ShadowQuality::Off) return true;
            return value == quality::ShadowQuality::High ? caps_.max_shadow_cascades >= 4 && caps_.max_shadow_size >= 2048 :
                   caps_.max_shadow_cascades >= 1 && caps_.max_shadow_size >= 256;
        }); break;
    case Control::Pbr: toggle(g.pbr, caps_.pbr); break;
    case Control::InternalHdr: toggle(g.internal_hdr, caps_.internal_hdr); break;
    case Control::Bloom: toggle(g.bloom, caps_.bloom && caps_.internal_hdr); break;
    case Control::Exposure:
        if (changing_backend || caps_.internal_hdr) g.exposure_ev = std::clamp(g.exposure_ev.value_or(0.f) + static_cast<float>(direction) * .25f, -10.f, 10.f);
        break;
    case Control::ToneMapping:
        cycle(g.tone_mapping, 2, direction, [&](quality::ToneMapping) { return changing_backend || caps_.internal_hdr; }); break;
    case Control::Reflections: case Control::RayShadows: case Control::GlobalIllumination: {
        const auto bit = 1u << (static_cast<unsigned>(control) - static_cast<unsigned>(Control::Reflections));
        if ((draft_.rt_effects & bit) || (supported_rt_effects_ & bit) || (changing_backend && draft_.renderer == "vulkan")) {
            draft_.rt_effects ^= bit; custom();
        }
        break;
    }
    case Control::Denoising:
        cycle(g.denoising, 3, direction, [&](quality::Denoising value) {
            return value == quality::Denoising::Off || (draft_.rt_effects != 0 &&
                   (changing_backend || (caps_.temporal_denoise && (value != quality::Denoising::High || caps_.high_quality_denoise))));
        }); break;
    case Control::Resolution: {
        int index = 0;
        for (unsigned i = 0; i < resolutions_.size(); ++i)
            if (resolutions_[i][0] == draft_.width && resolutions_[i][1] == draft_.height) { index = static_cast<int>(i); break; }
        index = (index + direction + static_cast<int>(resolutions_.size())) % static_cast<int>(resolutions_.size());
        draft_.width = resolutions_[static_cast<unsigned>(index)][0]; draft_.height = resolutions_[static_cast<unsigned>(index)][1];
        break;
    }
    case Control::Fullscreen: draft_.fullscreen = !draft_.fullscreen; break;
    case Control::Vsync: draft_.vsync = !draft_.vsync; break;
    case Control::FrameLimit: {
        auto it = std::find(frame_limits.begin(), frame_limits.end(), draft_.frame_limit);
        int index = it == frame_limits.end() ? 0 : static_cast<int>(it - frame_limits.begin());
        index = (index + direction + static_cast<int>(frame_limits.size())) % static_cast<int>(frame_limits.size());
        draft_.frame_limit = frame_limits[static_cast<unsigned>(index)]; break;
    }
    case Control::StatusPage:
        status_page_ = static_cast<unsigned>((static_cast<int>(status_page_) + direction + static_cast<int>(status_pages_)) % static_cast<int>(status_pages_)); break;
    case Control::Count: break;
    }
    refresh_rows();
}

void GraphicsPanel::apply() {
    if (pending_ || request_) return;
    // Capabilities belong to the initialized backend, not a prospective restart.
    // Core validates a different backend transaction before committing state.
    if ((draft_.graphics.backend_override && draft_.renderer == active_.renderer &&
         (draft_.rt_effects & ~supported_rt_effects_) != 0) ||
        (draft_.rt_effects && draft_.renderer != "vulkan")) {
        apply_error_ = draft_.rt_effects && draft_.renderer != "vulkan" ? "Ray effects require the Vulkan backend." : "Requested ray effects are not supported by the initialized runtime.";
        if (!capability_reason_.empty()) { apply_error_ += "\n"; apply_error_ += capability_reason_; }
        refresh_status(); refresh_rows();
        return;
    }
    request_ = draft_;
    pending_ = true;
    refresh_rows();
}
std::optional<c4::Settings> GraphicsPanel::take_request() {
    auto request = std::move(request_);
    request_.reset();
    return request;
}

GraphicsPanel::Hit GraphicsPanel::hit(float x, float y) const noexcept {
    x *= .625f/resources_.scale_x();
    y *= .625f/resources_.scale_y();
    if (x < 36 || x >= 604) return {};
    if (y >= 56 && y < 80) return {std::min(3, static_cast<int>((x - 36) / 142)), 1};
    if (y >= row_top && y < row_top + static_cast<float>(row_count_) * row_height)
        return {static_cast<int>(tab_count) + static_cast<int>((y - row_top) / row_height), x < 335 ? -1 : 1};
    if (y >= 416 && y < 440) {
        if (x < 216) return {static_cast<int>(tab_count + row_count_), 1};
        if (x < 404) return {static_cast<int>(tab_count + row_count_ + 1), 1};
        return {static_cast<int>(tab_count + row_count_ + 2), 1};
    }
    return {};
}
void GraphicsPanel::activate(int focus, int direction) {
    if (focus < static_cast<int>(tab_count)) { tab_ = static_cast<unsigned>(focus); refresh_rows(); return; }
    const auto index = static_cast<unsigned>(focus) - tab_count;
    if (index < row_count_) {
        if (!pending_ || rows_[index].control == Control::StatusPage) change(rows_[index].control, direction);
        return;
    }
    switch (index - row_count_) {
    case 0: apply(); break;
    case 1: if (!pending_) { draft_ = active_; refresh_rows(); } break;
    case 2: close(); break;
    default: break;
    }
}
void GraphicsPanel::input(const MenuInput& input) {
    if (!open_) return;
    if (input.escape_pressed) { close(); return; }
    if (input.tab) {
        const int count = static_cast<int>(focus_count());
        focus_ = (focus_ + (input.shift ? -1 : 1) + count) % count;
    }
    if (input.left || input.right) {
        const int direction = input.left ? -1 : 1;
        if (focus_ < static_cast<int>(tab_count)) {
            tab_ = static_cast<unsigned>((focus_ + direction + static_cast<int>(tab_count)) % static_cast<int>(tab_count));
            focus_ = static_cast<int>(tab_); refresh_rows();
        } else if (focus_ < static_cast<int>(tab_count + row_count_)) activate(focus_, direction);
    }
    if (input.enter) activate(focus_, 1);
    if (input.pointer_pressed) {
        const auto target = input.pointer_inside ? hit(input.x, input.y) : Hit{};
        pressed_ = target.focus; pressed_direction_ = target.direction;
        if (pressed_ >= 0) focus_ = pressed_;
    }
    if (input.pointer_released) {
        const auto target = input.pointer_inside ? hit(input.x, input.y) : Hit{};
        const int pressed = pressed_;
        pressed_ = -1;
        if (pressed >= 0 && target.focus == pressed) activate(pressed, pressed_direction_);
    }
}

unsigned GraphicsPanel::status_rows() const noexcept {
    return static_cast<unsigned>(240*resources_.scale_y()/.625f/font_height[resources_.tier()]);
}
void GraphicsPanel::text(Painter& painter, std::string_view value, float x, float y, float alpha) const {
    painter.bitmap_text(font_,font_width[resources_.tier()],font_height[resources_.tier()],value,
                        x*resources_.scale_x()/.625f,y*resources_.scale_y()/.625f,false,false,alpha);
}
void GraphicsPanel::draw(Painter& painter) {
    if (!open_) return;
    const auto old_layer = painter.layer;
    painter.set_layer(1000);
    auto solid = [&](float x, float y, float width, float height, std::array<float,4> color) {
        painter.solid(white_,x*resources_.scale_x()/.625f,y*resources_.scale_y()/.625f,
                      width*resources_.scale_x()/.625f,height*resources_.scale_y()/.625f,color);
    };
    solid(20,20,600,440,{.025f,.035f,.045f,.98f});
    solid(20,20,600,2,{.7f,.82f,.85f,1});
    text(painter, heading_.data(), 36, 34);
    for (unsigned i = 0; i < tab_count; ++i) {
        const float x = 36 + static_cast<float>(i) * 142;
        solid(x,56,138,24,i == tab_ ? std::array<float,4>{.12f,.25f,.30f,1} : std::array<float,4>{.07f,.09f,.11f,1});
        if (focus_ == static_cast<int>(i)) solid(x,78,138,2,{1,1,1,1});
        text(painter, tabs[i], x + 10, 63);
    }
    text(painter, "CONTROL", 40, 86); text(painter, "REQUESTED  < / >", 232, 86); text(painter, "APPLIED / ACTUAL", 425, 86);
    for (unsigned i = 0; i < row_count_; ++i) {
        const auto& row = rows_[i];
        const float y = row_top + static_cast<float>(i) * row_height;
        const bool focused = focus_ == static_cast<int>(tab_count + i);
        solid(36,y,568,row_height-1,focused ? std::array<float,4>{.12f,.22f,.27f,1} : std::array<float,4>{.055f,.07f,.085f,1});
        if (focused) text(painter, ">", 38, y + 6);
        text(painter, row.label, 48, y + 6);
        text(painter, row.requested.data(), 232, y + 6);
        text(painter, row.effective.data(), 425, y + 6);
        if (!row.supported) text(painter, "!", 592, y + 6);
    }
    if (tab_ == 3) {
        const auto begin = static_cast<std::size_t>(status_page_) * status_rows();
        const auto end = std::min(begin + status_rows(), status_lines_.size() - 1);
        for (auto line = begin; line < end; ++line) {
            auto value = std::string_view(status_).substr(status_lines_[line], status_lines_[line + 1] - status_lines_[line]);
            if (!value.empty() && value.back() == '\n') value.remove_suffix(1);
            text(painter,value,40,126+static_cast<float>(line-begin)*font_height[resources_.tier()]*.625f/resources_.scale_y());
        }
    } else if (tab_ == 1) {
        text(painter, "RT toggles are independent. Changing one retains the selected quality base.", 40, 204);
        text(painter, "Capability limits shown here describe the currently initialized backend.", 40, 224);
        text(painter, "A different backend is validated by Core during Apply; no support is assumed.", 40, 244);
        text(painter, "Advanced post controls: QUALITY tab. Actual runtime reasons: STATUS tab.", 40, 264);
    } else if (tab_ == 2) {
        text(painter, "Display requests are validated during the native runtime transaction.", 40, 222);
        text(painter, "A graphics backend restart preserves the current game state.", 40, 242);
        text(painter, "Classic menu brightness is separate from modern exposure EV.", 40, 262);
    }
    text(painter, footer_.data(), 40, 382);
    text(painter, "! = current-backend limit. A backend change requires runtime validation.", 40, 399);
    constexpr std::array<std::string_view, 3> buttons{"APPLY", "REVERT", "BACK"};
    for (unsigned i = 0; i < buttons.size(); ++i) {
        const float x = 36 + static_cast<float>(i) * 188;
        const bool focused = focus_ == static_cast<int>(tab_count + row_count_ + i);
        solid(x,416,180,24,{.12f,.18f,.21f,1});
        if (focused) solid(x,438,180,2,{1,1,1,1});
        text(painter, buttons[i], x + 65, 423, pending_ && i != 2 ? .5f : 1.f);
    }
    text(painter, "TAB/SHIFT-TAB focus | LEFT/RIGHT edit | ENTER select | ESC return", 40, 446);
    painter.set_layer(old_layer);
}

} // namespace c4::ui
