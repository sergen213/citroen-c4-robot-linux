#pragma once

#include "GameState.hpp"
#include "physics/NativeNumeric.hpp"
#include <array>

namespace c4::game {
// Source operations share globals through GameContext; wait-preserved car locals
// belong to each controller's persistent SourceMotion instance.
class SourceMotion {
public:
    void reset();
    static void bounce(GameContext& ctx, const physics::EntityEvent& event);
    physics::Var collision(GameContext& ctx, physics::Var time, bool robot=false);
    physics::Var gravity(GameContext& ctx, physics::Var time, physics::Var collision_result);
    void airborne_angles(GameContext& ctx, physics::Var time);
    void remember_angle_scratch(physics::ScriptVec value) { angle_scratch_=value; }
    void ground_angles(GameContext& ctx, physics::Var time);
    void robot_ground_angles(GameContext& ctx, physics::Var time, physics::Var yaw, bool snap=false);
    void shadows(GameContext& ctx);
    bool consume_landing_animation();

    enum class WheelEffect { none, asphalt_smoke, wheelspin_dust, road_dust };
    struct WheelContact {
        physics::Hit hit;
        WheelEffect effect{WheelEffect::none};
    };
    const std::array<WheelContact,4>& wheel_contacts() const { return wheel_contacts_; }
private:
    physics::Var gravity_return_{}, landing_airtime_{};
    physics::ScriptVec angle_scratch_{};
    bool landing_animation_{};
    std::array<WheelContact,4> wheel_contacts_{};
    physics::Var contact_height(GameContext& ctx, std::uint32_t vertex, bool effects,
                               WheelContact& contact);
    void contact_angles(GameContext& ctx, physics::Var time, bool robot, physics::Var yaw, bool snap);
};
} // namespace c4::game
