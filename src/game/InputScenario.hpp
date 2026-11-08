#pragma once

#include "GameState.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace c4::game {
// Native deterministic recording, not a claim of original engine input/RNG parity.
// Each nonblank line is: frame key value. Frames are decimal, nondecreasing.
// Up/Down/Left/Right/D/S/Space/C/Esc/F1/MouseRight record physical 0 or 1;
// mouse_force_x takes a finite number in source Var range and is converted once
// through float-to-Q10 truncation. Play/Resume/Restart/Menu/Help/CloseHelp/
// Quit/SelectLocal/SelectWeb take literal 1. Names are case-sensitive; no aliases
// or comments. Events execute in file order; final assignments win, while
// keyboard press latches survive same-frame releases.
class InputScenario {
public:
    static constexpr std::uint64_t max_frame = 10'000'000;
    static constexpr std::size_t max_lines = 100'000;
    static constexpr std::size_t max_bytes = 4 * 1024 * 1024;
    static constexpr std::size_t max_line_bytes = 256;

    // Throws std::runtime_error for invalid/empty/oversized files or I/O errors.
    static InputScenario load(const std::filesystem::path& path);
    // Allocation-free for valid calls. Indices must be nondecreasing and bounded
    // by max_frame; otherwise throws std::invalid_argument. Repeating an index
    // is idempotent. Keyboard source fields publish physical held OR that
    // frame's press latch; MouseRight remains physical held only. Esc/F1 also
    // publish callback press pulses. UI/mouse force and all press latches expire
    // next frame. Jumps retain physical changes but discard old press pulses.
    InputSnapshot frame(std::uint64_t index);
    std::uint64_t last_frame() const noexcept { return events_.back().frame; }
    // The final event frame must be consumed before stopping playback.
    bool finished(std::uint64_t index) const noexcept { return index > last_frame(); }

private:
    struct Event {
        std::uint64_t frame{};
        bool InputSnapshot::* key{};
        std::uint32_t key_mask{};
        Var value{};
        UiCommand command{UiCommand::none};
    };
    InputScenario() = default;
    std::vector<Event> events_;
    std::size_t cursor_{};
    std::uint64_t frame_index_{};
    bool has_frame_{};
    std::uint32_t physical_keys_{};
    InputSnapshot input_{};
};
} // namespace c4::game
