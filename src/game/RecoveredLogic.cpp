#include "RecoveredLogic.hpp"
#include "physics/NativeNumeric.hpp"

#include <cmath>
#include <stdexcept>

namespace c4::game {
physics::Var transition_percent(physics::Var signed_percent) {
    using physics::v;
    if (signed_percent < v(-100) || signed_percent > v(100))
        throw std::invalid_argument("steering percent must be in [-100,100]");
    // robot_fizik.wdl repeated steering-animation quantization, e.g. 2593-2607.
    const auto magnitude = physics::var_abs(signed_percent);
    const auto percent = magnitude <= v(25) ? v(0) : magnitude < v(75) ? v(50) : v(100);
    return signed_percent < v(0) ? -percent : percent;
}

void RaceProgress::reset() {
    // ghost_car.wdl 365-390: progression resets, previous split stamps do not.
    next_gate = 0;
    previous_side = 0;
}

RaceEvent RaceProgress::observe(bool eligible, int side_sum, double elapsed_ticks) {
    if (side_sum < -4 || side_sum > 4 || !std::isfinite(elapsed_ticks) || elapsed_ticks < 0)
        throw std::invalid_argument("checkpoint input requires side sum [-4,4] and finite nonnegative ticks");
    // ghost_car.wdl 219-279: straddling accepts even without a previous side.
    if (!eligible) {
        previous_side = 0;
        return {RaceEventType::none, 0, false, elapsed_ticks};
    }
    if (std::abs(side_sum) == 4 &&
        (previous_side == 0 || (previous_side < 0) == (side_sum < 0))) {
        previous_side = side_sum;
        return {RaceEventType::none, 0, false, elapsed_ticks};
    }
    if (next_gate == 9) {
        reset();
        return {RaceEventType::finish, 0, false, elapsed_ticks};
    }
    ++next_gate;
    previous_side = 0;
    split_ticks[next_gate - 1] = elapsed_ticks;
    return {RaceEventType::checkpoint, next_gate, next_gate == 5, elapsed_ticks};
}
} // namespace c4::game
