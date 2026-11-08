#include "RobotController.hpp"
#include "VehicleController.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace c4;
using game::RobotController;
using Phase = RobotController::Phase;
using physics::number;
// Expected recurrences use independently arranged original 1/1024-var operands.
constexpr double phase_tolerance = 1e-6;
constexpr double vertex_tolerance = 1e-3;
physics::Var V(double value) { return physics::Var::literal(value); }
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
void near(double actual, double expected, std::string_view message, double tolerance = phase_tolerance) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void near(physics::Var actual, double expected, std::string_view message, double tolerance = phase_tolerance) {
    near(number(actual), expected, message, tolerance);
}
void same_pose(assets::AnimationSample actual, assets::AnimationSample expected) {
    check(actual.first_frame == expected.first_frame && actual.second_frame == expected.second_frame,
          "wrong original animation frames");
    near(actual.blend, expected.blend, "wrong original animation blend");
}
void pose(game::GameContext& c, std::string_view clip, double percentage, bool cycle = false) {
    for (auto id : {c.ids.p_robot_teker, c.ids.p_robot_mekanik, c.ids.p_robot_farcam,
                   c.ids.p_robot_fargovde, c.ids.p_robot_camlar, c.ids.p_robot_kaporta}) {
        const auto& group = c.group(id);
        check(!group.frames.empty() && !group.source_vertices.empty(), "missing original animated robot geometry");
        const auto sample = assets::animation_sample(group, clip, static_cast<float>(percentage), cycle);
        same_pose(c.visuals.at(id).sample, sample);
        const auto source = assets::sample_source_vertex(group, sample, 0);
        const auto& body = c.physics.body(id);
        // Boneless model affine has one final FLOAT store before _var(float) publication.
        const auto expected = physics::native(physics::source(physics::model_vertex_position(
            {source.x, source.y, source.z}, body.position, body.angles, body.scale)));
        const auto actual = c.physics.vec_for_vertex(id, 1, c.visuals.at(id).sample);
        near(actual.x, expected.x, "animated source vertex X mismatch");
        near(actual.y, expected.y, "animated source vertex Y mismatch");
        near(actual.z, expected.z, "animated source vertex Z mismatch");
    }
}
void tick(RobotController& robot, game::GameContext& c, game::InputSnapshot input = {}, double dt = .5) {
    const game::FrameTiming timing{V(dt), 0, 0};
    robot.update_auxiliary(c, timing);
    robot.update(c, input, timing);
}
void activate(RobotController& robot, game::GameContext& c, const game::SharedState& initial,
              const physics::Body& home) {
    c.state = initial;
    c.physics.body(c.ids.p_aracgovde) = home;
    c.state.ghost_reset_ok = V(0);
    c.state.ghost_yaris_start = V(1);
    game::VehicleController car;
    car.initialize(c);
    // The original D gate opens only after observing a released key.
    car.update(c, {}, game::FrameTiming{V(.5), 0, 0});
    game::InputSnapshot input;
    input.key_d = true;
    car.update(c, input, game::FrameTiming{V(.5), 0, 0});
    check(c.state.phase_request == game::PhaseRequest::robot, "source car did not activate robot");
    check(c.visuals.at(c.ids.p_arac_cam).invisible && !c.visuals.at(c.ids.p_robot_kaporta).invisible,
          "source conversion visibility not activated");
    c.state.phase_request = game::PhaseRequest::none;
    robot.initialize(c, game::FrameTiming{V(.5), 0, 0});
}
void finish_conversion(RobotController& robot, game::GameContext& c, bool forward) {
    game::InputSnapshot input;
    input.key_cuu = forward;
    for (int n = 0; n < 64 && robot.phase() == Phase::conversion; ++n) tick(robot, c, input);
    check(robot.phase() == Phase::running, "conversion did not enter running");
}
void conversion_and_reverse(game::GameContext& c, const game::SharedState& initial, const physics::Body& home) {
    RobotController robot;
    activate(robot, c, initial, home);
    bool fast = false, slow = false, deformed = false;
    auto expected_percentage = V(0);
    for (int n = 0; n < 64 && robot.percentage() <= V(100); ++n) {
        const double sampled = number(robot.percentage());
        near(sampled, expected_percentage.to_float(), "donus recurrence accumulated wrong percentage");
        check(robot.phase() == Phase::conversion, "conversion ended before its terminal sample");
        tick(robot, c);
        near(c.state.robot_frame_no, sampled, "donus must sample before increment");
        pose(c, "donus", sampled);
        const bool fast_segment = expected_percentage.raw < V(86.666).raw;
        expected_percentage = expected_percentage + V(6.25) / V(fast_segment ? .7 : .925) * V(.5);
        near(robot.percentage(), expected_percentage.to_float(), "wrong fixed-var donus boundary increment");
        fast |= fast_segment;
        slow |= !fast_segment;
        const auto& group = c.group(c.ids.p_robot_kaporta);
        const auto start = assets::animation_sample(group, "donus", 0);
        for (std::uint32_t v = 0; v < group.source_vertices.size() && !deformed; ++v) {
            const auto a = c.physics.vec_for_vertex(c.ids.p_robot_kaporta, v + 1, start);
            const auto b = c.physics.vec_for_vertex(c.ids.p_robot_kaporta, v + 1,
                                                   c.visuals.at(c.ids.p_robot_kaporta).sample);
            deformed |= physics::length(b - a) > vertex_tolerance;
        }
    }
    check(robot.percentage() > V(100), "donus progression stalled");
    check(fast && slow && deformed, "donus missed rate boundary or actual model deformation");
    check(robot.phase() == Phase::conversion, "donus must wait after sample exceeding 100");
    tick(robot, c);
    check(robot.phase() == Phase::running, "donus did not hand off to running on next update");
    bool coast = false, brake = false;
    for (int n = 0; n < 512 && robot.phase() != Phase::idle; ++n) {
        tick(robot, c);
        coast |= robot.phase() == Phase::coast;
        brake |= robot.phase() == Phase::brake;
    }
    check(coast && brake && robot.phase() == Phase::idle, "released running did not progress coast/brake/idle");
    const double entry_sample = number(robot.percentage());
    game::InputSnapshot d;
    d.key_d = true;
    tick(robot, c, d);
    check(robot.phase() == Phase::idle, "D skipped the source idle entry wait");
    pose(c, "bekle_basla", entry_sample);
    near(robot.percentage(), (V(entry_sample) + V(6.25) / V(.3) * V(.5)).to_float(),
         "idle entry increment mismatch");
    for (int n = 0; n < 32 && robot.phase() == Phase::idle; ++n) tick(robot, c);
    check(robot.phase() == Phase::reverse_blend, "held D during entry was not latched after release");
    check(c.state.phase_request == game::PhaseRequest::none, "D switched to car instantaneously");
    std::array<bool, 4> visited{};
    int frames = 0;
    for (; frames < 256 && robot.phase() != Phase::inactive; ++frames) {
        const auto phase = robot.phase();
        if (phase >= Phase::reverse_blend && phase <= Phase::reverse_inis)
            visited[static_cast<unsigned>(phase) - static_cast<unsigned>(Phase::reverse_blend)] = true;
        tick(robot, c);
    }
    check(frames > 1 && std::all_of(visited.begin(), visited.end(), [](bool v) { return v; }),
          "reverse omitted a source conversion phase");
    check(robot.phase() == Phase::inactive && c.state.phase_request == game::PhaseRequest::car,
          "reverse did not hand off to car");
    check(c.state.model_robot_ok == V(2) && c.state.car_settling_frame >= V(27.273),
          "reverse omitted car settling continuation");
    check(!c.visuals.at(c.ids.p_arac_cam).invisible && c.visuals.at(c.ids.p_robot_kaporta).invisible,
          "reverse did not restore car visibility");
}
void free_jump(game::GameContext& c, const game::SharedState& initial, const physics::Body& home) {
    RobotController robot;
    activate(robot, c, initial, home);
    finish_conversion(robot, c, true);
    // Move the real source body above the actual world: no invented collider/track.
    auto& body = c.physics.body(c.ids.p_aracgovde);
    body.position = home.position + physics::Vec3{0, 0, 100000};
    body.angles = {};
    c.state.arac_ivme = V(0);
    c.state.c4_guncel_havazaman = c.state.c4_eski_havazaman = c.state.c4_havadongu_no = V(0);
    game::InputSnapshot input;
    input.key_s = input.key_cuu = true;
    const double start_z = body.position.z;
    tick(robot, c, input, .4);
    check(robot.phase() == Phase::free_jump, "S far from original paths did not enter free_jump");
    check(body.position.z <= start_z + phase_tolerance, "S injected an upward ballistic impulse");
    pose(c, "bosa_zipla", 0);
    bool below = false, apex = false, above = false;
    input.key_s = false;
    for (int n = 0; n < 64 && robot.percentage() <= V(100); ++n) {
        const double sampled = number(robot.percentage()), before_z = body.position.z;
        tick(robot, c, input, .4);
        check(robot.phase() == Phase::free_jump, "free_jump exited before its final wait");
        const auto p = V(sampled);
        const bool ascending = p.raw < V(55).raw;
        const auto lift = ascending ? V(85.609) * p / V(55) : V(85.609) * (V(100) - p) / V(45);
        const auto coupling = ascending ? (V(55) - p) / V(55) : (p - V(55)) / V(45);
        near(c.state.cam_path_up, lift.to_float(), "wrong fixed-var free_jump camera lift");
        near(c.state.cam_path_ivme, coupling.to_float(), "wrong fixed-var free_jump camera coupling");
        near(robot.percentage(), (p + V(6.25) * V(.4)).to_float(), "wrong fixed-var free_jump increment");
        pose(c, "bosa_zipla", sampled);
        check(body.position.z <= before_z + phase_tolerance, "free_jump raised the physics body instead of morphing");
        below |= sampled < 55;
        apex |= std::abs(sampled - 55) < .1; // Quantized .4 ticks sample just below 55, never exactly 55.
        above |= sampled > 55;
    }
    check(robot.percentage() > V(100), "free_jump progression stalled");
    check(below && apex && above, "free_jump missed camera apex boundary at 55");
    // Zero elapsed time exposes running6's exact cooldown seed before its normal decrement.
    tick(robot, c, input, 0);
    check(robot.phase() == Phase::running, "free_jump did not enter running6");
    near(c.state.jump_delay_time, 100, "running6 must seed jump cooldown at 100");
    near(c.state.cam_path_up, 0, "free_jump camera not cleared");
    near(c.state.cam_path_ivme, 1, "free_jump camera coupling not restored");
    input.key_s = true;
    for (int n = 0; n < 13; ++n) {
        const double previous = number(c.state.jump_delay_time);
        check(previous > 0, "cooldown exhausted early");
        tick(robot, c, input);
        check(robot.phase() == Phase::running, "held S retriggered before cooldown test expired");
        const auto remaining = V(previous) - V(6.25) / V(.4) * V(.5);
        near(c.state.jump_delay_time, std::max(0., double(remaining.to_float())), "wrong fixed-var jump cooldown decrement");
    }
    near(c.state.jump_delay_time, 0, "jump cooldown did not expire");
    tick(robot, c, input);
    check(robot.phase() == Phase::free_jump, "held S failed to retrigger after cooldown expired");
}
void hanging_boundaries() {
    constexpr std::array<double, 8> endpoints{20880, 19780, 5830, 4715, -3190, -4370, -12435, -13900};
    for (std::size_t n = 0; n < endpoints.size(); ++n) {
        const auto endpoint = V(endpoints[n]);
        check(!RobotController::hanging_dismount_allowed(endpoint), "hanging dismount endpoint must be excluded");
        check(RobotController::hanging_dismount_allowed(endpoint + physics::Var{1}) == (n % 2 == 0),
              "hanging dismount one raw quantum above endpoint is wrong");
        check(RobotController::hanging_dismount_allowed(endpoint - physics::Var{1}) == (n % 2 != 0),
              "hanging dismount one raw quantum below endpoint is wrong");
    }
}
void source_path(game::GameContext& c, const game::SharedState& initial, const physics::Body& home) {
    RobotController robot;
    activate(robot, c, initial, home);
    finish_conversion(robot, c, true);
    for (int n = 1; n <= 23; ++n) {
        const auto id = c.ids.path_ent_dizi[n];
        check(id != game::no_entity && c.group(id).source_vertices.size() >= 4,
              "source bootstrap missing one of 23 original path models");
    }
    // Original path 1, Frame 40: vertex 4 anchor and vertex 1 tangent, as robot_fizik.wdl.
    const auto id = c.ids.path_ent_dizi[1];
    c.animate(id, "Frame", V(40));
    const auto sample = c.visuals.at(id).sample;
    const auto anchor = c.physics.vec_for_vertex(id, 4, sample);
    const auto tangent = c.physics.vec_for_vertex(id, 1, sample) - anchor;
    const double planar = std::hypot(tangent.x, tangent.y);
    check(planar > vertex_tolerance, "original path tangent is degenerate");
    auto& body = c.physics.body(c.ids.p_aracgovde);
    body.position = anchor - physics::Vec3{tangent.x / planar, tangent.y / planar, 0} * 100;
    body.angles = {std::atan2(tangent.y, tangent.x) * 180 / std::numbers::pi, 0, 0};
    c.state.arac_ivme = V(40);
    c.state.jump_delay_time = V(0);
    c.state.c4_guncel_havazaman = c.state.c4_eski_havazaman = c.state.c4_havadongu_no = V(0);
    game::InputSnapshot input;
    input.key_cuu = input.key_s = true;
    tick(robot, c, input, 0);
    check(robot.phase() == Phase::rail_entry || robot.phase() == Phase::hang_entry,
          "S near sampled original path did not acquire a path");
    check(std::find(c.ids.path_ent_dizi.begin() + 1, c.ids.path_ent_dizi.end(), c.state.selected_path)
              != c.ids.path_ent_dizi.end(), "path acquisition selected a non-source entity");
    check(c.state.selected_path != game::no_entity, "path acquisition lost actual entity handle");
    // The fine source scan increments a var literal .2 (204 raw), not a host double .2.
    const auto selected = c.visuals.at(c.state.selected_path).sample;
    bool fixed_grid = false;
    for (auto p = V(0); p.raw <= V(100).raw; p = p + V(.2)) {
        const auto expected = assets::animation_sample(c.group(c.state.selected_path), "Frame", p.to_float());
        fixed_grid |= selected.first_frame == expected.first_frame && selected.second_frame == expected.second_frame &&
                      std::abs(selected.blend - expected.blend) <= phase_tolerance;
    }
    check(fixed_grid, "selected source path pose was not sampled on the original fixed-var .2 grid");
}
void idle_progression(game::GameContext& c, const game::SharedState& initial, const physics::Body& home) {
    RobotController robot;
    activate(robot, c, initial, home);
    finish_conversion(robot, c, false);
    for (int n = 0; n < 512 && robot.phase() != Phase::idle; ++n) tick(robot, c);
    check(robot.phase() == Phase::idle, "released robot never reached idle");
    for (int n = 0; n < 32 && robot.percentage() <= V(100); ++n) tick(robot, c);
    check(robot.percentage() > V(100), "idle entry animation did not complete");
    tick(robot, c);
    pose(c, "bekle_loop", 0, true);
    const auto increment = V(6.25) / V(2) * V(.5);
    near(robot.percentage(), increment.to_float(), "idle loop did not restart after entry");
    const double sampled = number(robot.percentage());
    tick(robot, c);
    check(robot.phase() == Phase::idle, "no-input idle incorrectly changed phase");
    pose(c, "bekle_loop", sampled, true);
    near(robot.percentage(), (V(sampled) + increment).to_float(), "no-input idle loop did not advance");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: robot-phase-test ORIGINAL_ARCHIVE_ROOT\n";
        return 2;
    }
    try {
        const std::filesystem::path root(argv[1]);
        auto world = c4::assets::load_world(root / "kaynak_kap.rc4::bogazici.wmb");
        c4::game::SharedState state;
        std::vector<c4::game::VisualState> visuals;
        auto ids = c4::game::bootstrap_game(world, root, state, visuals);
        c4::physics::PhysicsWorld physics(world);
        c4::audio::Audio audio(48000, c4::audio::Mode::Offline);
        c4::game::GameRuntime runtime(world, physics, audio, state, ids, visuals, root);
        auto& context = runtime.context();
        const auto initial = state;
        const auto home = physics.body(ids.p_aracgovde);
        hanging_boundaries();
        conversion_and_reverse(context, initial, home);
        idle_progression(context, initial, home);
        free_jump(context, initial, home);
        source_path(context, initial, home);
        std::cout << "original-world robot phases, morphs, held-D, cooldown and source-path boundaries passed\n";
    } catch (const std::exception& error) {
        std::cerr << "robot-phase-test: " << error.what() << '\n';
        return 1;
    }
}
