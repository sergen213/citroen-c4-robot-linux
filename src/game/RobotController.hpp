#pragma once
#include "GameState.hpp"
#include "SourceMotion.hpp"

namespace c4::game {
// Persistent source coroutines; percentages are sampled before each wait(1).
class RobotController {
public:
    enum class Phase { inactive, conversion, running, slide, coast, brake, idle,
        reverse_blend, reverse_fold, reverse_donus, reverse_inis,
        free_jump, rail_entry, rail_slide, rail_ground, rail_exit,
        hang_entry, hang_loop, hang_exit };
    void initialize(GameContext&, FrameTiming);
    void update(GameContext&, const InputSnapshot&, FrameTiming);
    void update_auxiliary(GameContext&, FrameTiming);
    void reset(GameContext&);
    Phase phase() const noexcept { return phase_; }
    Var percentage() const noexcept { return i_; }
    static bool hanging_dismount_allowed(Var y) noexcept;
private:
    SourceMotion contact_;
    Phase phase_{Phase::inactive};
    int entry_{}, direction_{1}, selected_no_{}, step_{}, titre_direction_{1};
    Var i_{}, turn_{}, entry_turn_{}, titre_{}, turn_time_{}, turn_release_{}, coast_wait_{};
    bool entry_active_{}, recenter_{}, first_cycle_{true}, camera_stop_{};
    Var path_frame_{}, path_length_{}, travel_time_{}, stop_time_{}, lateral_start_{}, reverse_start_{}, reverse_speed_{Var::literal(1)}, in_pan_{};
    bool entry_d_latched_{}, locomotion_ready_{};
    ScriptVec alignment_position_{}, alignment_angles_{};
    ScriptVec ground_probe_angles_{};
    audio::Handle skid_{};
    Var shadow_percent_{};
    Var current_tick_{};
    bool shadow_active_{}, shadow_to_robot_{true};
    void enter(GameContext&, Phase, int entry=0, Var turn={});
    void locomotion(GameContext&, const InputSnapshot&, Var time);
    void reverse(GameContext&, Var time);
    void jump(GameContext&, const InputSnapshot&, Var time);
    void acquire_path(GameContext&);
    void pose(GameContext&, std::string_view clip, Var percent, bool cycle=false, bool target=false);
    void blend(GameContext&, std::string_view clip, Var percent, Var blend_percent);
    void move(GameContext&, Var time, Var yaw={}, Vec3 relative={}, bool slip=false, bool ground_done=false);
    void ghost_run(GameContext&, Var time, bool advance=true);
    void skid(GameContext&, bool rail=false);
    void stop_skid(GameContext&);
    enum class PathAngleDestination { target, tip, anchor };
    void path_pose(GameContext&, PathAngleDestination destination=PathAngleDestination::target);
    void advance_path(Var time);
    void camera_offsets(GameContext&, Var numerator, Var denominator, bool hanging);
    void clear_camera(GameContext&);
    void footsteps(GameContext&, Var percent);
    void dust(GameContext&, int foot);
    void sparks(GameContext&, int foot);
    void begin_shadow(GameContext&, bool robot);
};
} // namespace c4::game
