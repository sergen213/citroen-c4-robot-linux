#pragma once

#include "GameState.hpp"
#include "physics/NativeNumeric.hpp"

namespace c4::game {
// GMA_turbo uses negative Euler rotation of both scaled absolute positions.
bool turbo_pickup_contains(Vec3 body_position, Vec3 body_angles, Vec3 pickup_position);

class CountdownPickups {
public:
    void initialize(GameContext& context);
    void reset(GameContext& context, bool first=false);
    void update(GameContext& context, const InputSnapshot& input, const FrameTiming& timing);
private:
    enum class Stage {
        idle, initial_waits, reset_wait, timing_wait, fade, music_wait,
        lower, lower_wait, red_wait, charge_wait, charge_samples, started
    };
    struct Pickup {
        EntityId entity{no_entity};
        physics::Var frame{};
        double deadline{};
        physics::Var hidden_waits{};
    };
    std::vector<Pickup> pickups_;
    std::array<EntityId,6> sharks_{no_entity,no_entity,no_entity,no_entity,no_entity,no_entity};
    Stage stage_{Stage::idle};
    int frame_waits_{}, red_skin_{}, charge_sample_{};
    physics::Var progress_{}, charge_{}, retract_progress_{};
    double deadline_{};
    bool boost_played_{}, retract_raising_{}, retract_done_{true};
    void countdown(GameContext&, const InputSnapshot&, const FrameTiming&);
    void start_boost(GameContext&, const InputSnapshot&, physics::Var time);
    void retract(GameContext&, physics::Var time);
    void rescale_sharks(GameContext&);
    void pickups(GameContext&, const FrameTiming&);
};
} // namespace c4::game
