#pragma once

#include "assets/Assets.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace c4::ui {

// Silent, native video playback; the menu music has its own audio player.
class MenuVideo {
public:
    explicit MenuVideo(const std::filesystem::path& path);
    ~MenuVideo();
    MenuVideo(const MenuVideo&) = delete;
    MenuVideo& operator=(const MenuVideo&) = delete;

    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;

    // Begin a new menu-entry timeline at source time zero, retaining the decoder
    // and destination allocation. Pixels change only on the next update.
    void restart();

    // Absolute monotonic seconds since menu entry. An empty destination is
    // initialized once; subsequent calls retain its native-size RGBA allocation.
    // Returns true only when a newly selected source frame is written.
    bool update(double elapsed_seconds, assets::Image& destination);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4::ui
