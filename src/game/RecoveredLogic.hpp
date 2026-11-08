#pragma once

#include "physics/FixedPoint.hpp"

#include <array>
#include <optional>

namespace c4::game {
// Diagnostic source counterparts, not a world, collision system, clock, or game loop.
enum class RaceEventType { none, checkpoint, finish };

struct RaceEvent {
    RaceEventType type;
    int checkpoint;
    bool second_lap;
    double elapsed_ticks;
};

struct RaceProgress {
    int next_gate{};
    int previous_side{};
    std::array<std::optional<double>, 9> split_ticks{};

    // Original restart resets progression but retains existing split stamps.
    void reset();
    // Caller supplies current-gate eligibility, four world-vertex side signs summed
    // into [-4,4], and original normalized elapsed ticks (16 ticks per second).
    RaceEvent observe(bool eligible, int side_sum, double elapsed_ticks);
};

// Signed original loc_yon in [-100,100]; positive is left, negative is right.
// Numeric boundary violations in either primitive throw std::invalid_argument.
physics::Var transition_percent(physics::Var signed_percent);
} // namespace c4::game
