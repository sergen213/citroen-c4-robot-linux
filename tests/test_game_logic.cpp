#include "RecoveredLogic.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using c4::game::RaceEventType;
using c4::game::RaceProgress;
using c4::game::transition_percent;

template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

void steering() {
    using c4::physics::Var;
    const auto zero = Var::literal(0);
    const auto full = Var::literal(100);
    const auto half = Var::literal(50);
    const auto low = Var::literal(25);
    const auto high = Var::literal(75);
    const Var raw_step{1};
    assert(transition_percent(zero) == zero);
    for (const int sign : {-1, 1}) {
        const auto direction = Var::literal(sign);
        assert(transition_percent(direction * low) == zero);
        assert(transition_percent(direction * (low - raw_step)) == zero);
        assert(transition_percent(direction * (low + raw_step)) == direction * half);
        assert(transition_percent(direction * half) == direction * half);
        assert(transition_percent(direction * (high - raw_step)) == direction * half);
        assert(transition_percent(direction * high) == direction * full);
        assert(transition_percent(direction * (high + raw_step)) == direction * full);
        assert(transition_percent(direction * (full - raw_step)) == direction * full);
        assert(transition_percent(direction * full) == direction * full);
        rejects([&] { transition_percent(direction * (full + raw_step)); });
    }
}

void gate_crossing() {
    for (int side = -3; side <= 3; ++side) {
        RaceProgress progress;
        const auto event = progress.observe(true, side, 0);
        assert(event.type == RaceEventType::checkpoint && event.checkpoint == 1);
        assert(!event.second_lap && event.elapsed_ticks == 0);
        assert(progress.next_gate == 1 && progress.previous_side == 0);
        assert(progress.split_ticks[0] == 0);
    }
    for (const int side : {-4, 4}) {
        RaceProgress progress;
        assert(progress.observe(true, side, 1).type == RaceEventType::none);
        assert(progress.previous_side == side && progress.next_gate == 0);
        assert(progress.observe(true, side, 2).type == RaceEventType::none);
        const auto crossing = progress.observe(true, -side, 3);
        assert(crossing.type == RaceEventType::checkpoint && crossing.checkpoint == 1);
        assert(progress.previous_side == 0 && progress.split_ticks[0] == 3);

        progress.reset();
        assert(progress.observe(true, side, 4).type == RaceEventType::none);
        assert(progress.observe(false, -side, 5).type == RaceEventType::none);
        assert(progress.previous_side == 0 && progress.next_gate == 0);
        assert(progress.observe(true, -side, 6).type == RaceEventType::none);
        assert(progress.next_gate == 0 && progress.previous_side == -side);
        assert(progress.observe(true, 0, 7).type == RaceEventType::checkpoint);
        assert(progress.previous_side == 0 && progress.split_ticks[0] == 7);
    }
}

void race_sequence() {
    RaceProgress progress;
    assert(progress.next_gate == 0 && progress.previous_side == 0);
    for (const auto& split : progress.split_ticks) assert(!split);
    for (int gate = 1; gate <= 9; ++gate) {
        const double ticks = gate * 16.0 + 0.25;
        const auto event = progress.observe(true, 0, ticks);
        assert(event.type == RaceEventType::checkpoint && event.checkpoint == gate);
        assert(event.second_lap == (gate == 5) && event.elapsed_ticks == ticks);
        assert(progress.next_gate == gate && progress.previous_side == 0);
        assert(progress.split_ticks[gate - 1] == ticks);
        for (int unvisited = gate; unvisited < 9; ++unvisited) assert(!progress.split_ticks[unvisited]);
    }
    const auto splits = progress.split_ticks;
    assert(progress.observe(true, 4, 160).type == RaceEventType::none);
    const auto finish = progress.observe(true, -4, 170.5);
    assert(finish.type == RaceEventType::finish && finish.checkpoint == 0);
    assert(!finish.second_lap && finish.elapsed_ticks == 170.5);
    assert(progress.next_gate == 0 && progress.previous_side == 0);
    assert(progress.split_ticks == splits); // No tenth split, and finish preserves the nine stamps.
    assert(progress.observe(true, 0, 1).type == RaceEventType::checkpoint);
    assert(progress.observe(true, -4, 2).type == RaceEventType::none);
    const auto updated = progress.split_ticks;
    progress.reset();
    assert(progress.next_gate == 0 && progress.previous_side == 0);
    assert(progress.split_ticks == updated && progress.split_ticks[0] == 1);
    for (int gate = 1; gate < 9; ++gate) assert(progress.split_ticks[gate] == splits[gate]);
}

void input_boundary() {
    RaceProgress progress;
    progress.observe(true, 0, 16);
    progress.observe(true, 4, 17);
    const auto splits = progress.split_ticks;
    for (const bool eligible : {false, true}) {
        rejects([&] { progress.observe(eligible, -5, 18); });
        rejects([&] { progress.observe(eligible, 5, 18); });
        rejects([&] { progress.observe(eligible, 0, -1); });
        rejects([&] { progress.observe(eligible, 0, std::numeric_limits<double>::infinity()); });
        rejects([&] { progress.observe(eligible, 0, -std::numeric_limits<double>::infinity()); });
        rejects([&] { progress.observe(eligible, 0, std::numeric_limits<double>::quiet_NaN()); });
        assert(progress.next_gate == 1 && progress.previous_side == 4);
        assert(progress.split_ticks == splits);
    }
}
} // namespace

int main() {
    steering();
    gate_crossing();
    race_sequence();
    input_boundary();
}
