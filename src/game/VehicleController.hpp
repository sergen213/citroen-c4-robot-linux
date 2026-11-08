#pragma once
#include "GameState.hpp"
#include "SourceMotion.hpp"
#include "physics/NativeNumeric.hpp"

namespace c4::game {
// Persistent source arac_fizik locals and independent arac_animate coroutine.
class VehicleController {
public:
    void initialize(GameContext&);
    void reset(GameContext&);
    void update(GameContext&, const InputSnapshot&, const FrameTiming&, bool advance_animation=true, bool advance_motion=true);
    void start_settling(GameContext&, physics::Var frame=physics::Var::literal(27.273), physics::Var speed=physics::Var::literal(1));
    void gear_animation(GameContext&);
private:
    struct SteeringRamp { physics::Var value{}, on{}, off{}, origin{}; void update(bool held,physics::Var time,physics::Var rise,physics::Var fall); };
    void animate_parts(GameContext&,std::string_view,physics::Var,bool cycle=false,bool include_steering=true);
    void update_animation(GameContext&,physics::Var time);
    void begin_animation(GameContext&,int type,physics::Var frame,physics::Var speed);
    void publish_pose(GameContext&,const InputSnapshot&,physics::Var time);
    void gears(GameContext&,const InputSnapshot&,physics::Var time);
    SourceMotion motion_;
    physics::ScriptVec pivot_correction_{};
    SteeringRamp left_, right_;
    physics::Var handbrake_{}, throttle_release_{}, slip_{}, forward_wait_{physics::Var::literal(100)}, reverse_wait_{};
    physics::Var front_steer_{}, wheel_frame_{}, skid_frame_{}, startup_frame_{}, startup_cycle_{};
    physics::Var enter_ok_{}, nitro_started_{};
    int animation_type_{-1};
    physics::Var animation_frame_{}, animation_speed_{}, animation_wait_{}, last_time_{physics::Var::literal(0.5)};
    physics::Var settling_dust_{};
};
} // namespace c4::game
