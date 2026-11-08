#include "game/RaceLifecycle.hpp"
#include "physics/NativeNumeric.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using namespace c4::physics;
using c4::game::turbo_pickup_contains;

bool inside_box(ScriptVec delta) {
    return var_abs(delta.x) < v(.1) && var_abs(delta.y) < v(.06) &&
           var_abs(delta.z) < v(.06);
}

bool source_oracle(Vec3 body, Vec3 angles, Vec3 pickup) {
    // Body properties already publish raw Q10; do not narrow them through float.
    const auto scaled_body = var_vec_scale(body_source(body), v(.001));
    const auto scaled_pickup = var_vec_scale(body_source(pickup), v(.001));
    if (!(var_vec_length(scaled_body - scaled_pickup) < v(.250))) return false;
    const auto euler = body_source(angles);
    const ScriptVec negative{-euler.x, -euler.y, -euler.z};
    return inside_box(var_vec_rotate(scaled_body, negative) -
                      var_vec_rotate(scaled_pickup, negative));
}

ScriptVec inverse_body_rotation(ScriptVec value, Vec3 angles) {
    // A genuine inverse reverses pan/tilt/roll order; GMA_turbo does not.
    const auto euler = body_source(angles);
    value = var_vec_rotate(value, {-euler.x, v(0), v(0)});
    value = var_vec_rotate(value, {v(0), -euler.y, v(0)});
    return var_vec_rotate(value, {v(0), v(0), -euler.z});
}

Vec3 undo_source_rotation(Vec3 value, Vec3 angles) {
    // Native doubles only generate SDK input fixtures; the oracle uses Var.
    value = rotate_vector(value, {angles.x, 0, 0});
    value = rotate_vector(value, {0, angles.y, 0});
    return rotate_vector(value, {0, 0, angles.z});
}

void restart_music_lifetime() {
    using namespace c4;
    using namespace game;
    // Generated check-only PCM and animation, independent of imported assets.
    auto clip = std::make_shared<audio::Clip>();
    clip->sample_rate = 8000;
    clip->channels = 1;
    clip->samples.assign(16, .05F);
    assets::WorldScene world;
    auto model = std::make_shared<assets::Model>();
    model->groups.emplace_back();
    model->groups.back().frames.push_back({"indir", {}});
    model->groups.back().source_vertices.emplace_back();
    model->groups.back().clips.push_back({"indir", 0, 1});
    world.models.emplace_back();
    world.models.back().model = model;
    world.entities.emplace_back();
    world.entities.back().model_index = 0;
    for (auto action : {"fish_m_shark1a", "fish_m_shark1b", "fish_m_shark1c",
                        "fish_m_shark2a", "fish_m_shark2b", "fish_m_shark2c"}) {
        world.entities.emplace_back();
        world.entities.back().action = action;
    }
    PhysicsWorld physics(world);
    audio::Audio audio(8000, audio::Mode::Offline);
    SharedState state;
    ComponentIds ids;
    ids.p_zamanlama = ids.p_aracgovde = ids.p_arac_kaporta = ids.p_robot_kaporta = 0;
    std::vector<VisualState> visuals(world.entities.size());
    const std::filesystem::path root;
    SoundCache sounds{{"muzik_oyun_ses", clip}};
    GameContext context{world, physics, audio, state, ids, visuals, root, sounds};
    CountdownPickups countdown;
    countdown.initialize(context);
    const auto unrelated = audio.play(clip, 5, 0, true);
    state.music_handle = context.sound("muzik_oyun_ses", v(75), true);
    const auto old_music = state.music_handle;
    std::array<float, 30> pcm{};
    audio.render(pcm);
    assert(audio.playing(old_music) && audio.state(old_music).frame > 0);
    context.stop_music(); // Shared physical fade-tail/resumed Restart boundary.
    assert(!audio.playing(old_music) && state.music_handle == 0);
    assert(audio.playing(unrelated));
    countdown.reset(context, false);
    for (std::uint64_t frame = 1; frame < 100 && state.music_handle == 0; ++frame) {
        countdown.update(context, {}, {v(.5), frame, static_cast<double>(frame) / 32});
        audio.render(pcm);
        assert(!audio.playing(old_music) && audio.playing(unrelated));
    }
    assert(audio.playing(state.music_handle));
    assert(audio.state(state.music_handle).loop && audio.state(state.music_handle).volume == 75);
    assert(audio.active_voices() == 2); // New countdown loop and the unrelated owned voice only.
}

void countdown_materials(const std::filesystem::path& root) {
    using namespace c4::game;
    const std::array<std::string_view, 9> names{
        "bogazici.wmb", "ses_checkpoint", "muzik_oyun_ses", "ses_zaman_mekanik1",
        "ses_zaman_red", "ses_zaman_green", "ses_zaman_mekanik2", "turboal_ses_wav",
        "ses_checkfinish"
    };
    const auto resources = c4::assets::resolve_resources(root / "C4.wrs", names);
    auto world = c4::assets::load_world(resources[0]);
    SharedState state;
    std::vector<VisualState> visuals;
    auto ids = bootstrap_game(world, root, state, visuals);
    PhysicsWorld physics(world);
    c4::audio::Audio audio(48000, c4::audio::Mode::Offline);
    SoundCache sounds;
    for (std::size_t index = 1; index < names.size(); ++index)
        sounds.emplace(std::string(names[index]), c4::audio::load_clip(resources[index]));
    GameContext context{world, physics, audio, state, ids, visuals, root, sounds};
    initialize_game_physics(context);
    CountdownPickups countdown;
    countdown.initialize(context);
    const InputSnapshot input{};
    std::uint64_t frame{};
    const auto step = [&] {
        countdown.update(context, input, FrameTiming{v(.5), frame, frame / 120.});
        ++frame;
    };
    const std::array<Var, 11> colors{
        v(0), v(1), v(2), v(3), v(4), v(5), v(6), v(7), v(8), v(9), v(4.5)
    };
    struct MaterialSnapshot {
        std::string_view material_identity;
        bool material_identity_known;
    };
    const auto snapshot = [&](EntityId id) {
        const auto& visual = visuals.at(id);
        return MaterialSnapshot{visual.material_identity, visual.material_identity_known};
    };
    for (Var color : colors) {
        for (bool first : {true, false}) {
            state.c4_texture_rengi = color;
            const bool black = color == v(4) || color == v(8);
            const bool grey = color == v(2);
            const std::string_view car_material = black ? "Mtl_siyah_car" :
                grey ? "Mtl_kul_car" : "Mtl_car3";
            const std::string_view robot_material = black ? "Mtl_siyah_robot" :
                grey ? "Mtl_kul_robot" : "Mtl_robot_kaporta";
            // Seed genuine alternate materials so an early assignment is observable.
            context.set_material(ids.p_arac_kaporta, black ? "Mtl_car3" : "Mtl_siyah_car");
            context.set_material(ids.p_robot_kaporta, black ? "Mtl_robot_kaporta" : "Mtl_siyah_robot");
            const auto before_car = snapshot(ids.p_arac_kaporta);
            const auto before_robot = snapshot(ids.p_robot_kaporta);
            const auto before_mechanical = snapshot(ids.p_robot_mekanik);
            const auto assert_material = [&](EntityId id, const MaterialSnapshot& before) {
                const auto& current = visuals.at(id);
                assert(current.material_identity == before.material_identity);
                assert(current.material_identity_known == before.material_identity_known);
            };
            countdown.reset(context, first);
            assert_material(ids.p_arac_kaporta, before_car);
            assert_material(ids.p_robot_kaporta, before_robot);
            assert_material(ids.p_robot_mekanik, before_mechanical);
            assert(state.ghost_reset_ok == v(1));
            const int waits = first ? 7 : 5;
            for (int wait = 0; wait < waits; ++wait) {
                step();
                assert_material(ids.p_arac_kaporta, before_car);
                assert_material(ids.p_robot_kaporta, before_robot);
                assert_material(ids.p_robot_mekanik, before_mechanical);
                assert(state.ghost_reset_ok == (wait + 1 < waits ? v(1) : v(0)));
                assert(state.phase == GamePhase::countdown);
            }
            step(); // Resume reset_wait after the release frame's wait(1).
            assert(visuals.at(ids.p_arac_kaporta).material_identity_known);
            assert(visuals.at(ids.p_arac_kaporta).material_identity == car_material);
            assert(visuals.at(ids.p_robot_kaporta).material_identity_known);
            assert(visuals.at(ids.p_robot_kaporta).material_identity == robot_material);
            assert_material(ids.p_robot_mekanik, before_mechanical);
            assert(state.ghost_reset_ok == v(0));
            assert(state.phase == GamePhase::countdown);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: test_race_lifecycle [ORIGINAL_ASSET_ROOT]\n";
        return 2;
    }
    restart_music_lifetime();
    const Vec3 origin{};
    assert(turbo_pickup_contains(origin, origin, origin));
    assert(v(.001).raw == 1);
    assert(v(.1).raw == 102);
    assert(v(.06).raw == 61);
    assert(!turbo_pickup_contains(origin, origin, {256, 0, 0}));
    assert(!turbo_pickup_contains(origin, origin, {257, 0, 0}));

    const std::array<Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<std::int32_t, 3> limits{v(.1).raw, v(.06).raw, v(.06).raw};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const auto positive_tie = limits[axis] * 1024 - 512;
        const auto negative_tie = -limits[axis] * 1024 + 512;
        const auto contains_raw = [&](std::int32_t raw) {
            return turbo_pickup_contains(origin, origin, axes[axis] * (raw / 1024.));
        };
        // vec_scale rounds half-ties toward +infinity, including negative inputs.
        assert(contains_raw(positive_tie - 1));
        assert(!contains_raw(positive_tie));
        assert(!contains_raw(positive_tie + 1));
        assert(!contains_raw(negative_tie - 1));
        assert(contains_raw(negative_tie));
        assert(contains_raw(negative_tie + 1));
        assert(!contains_raw(limits[axis] * 1024));
        assert(!contains_raw(-limits[axis] * 1024));
    }
    assert(turbo_pickup_contains(origin, origin, {100, 60, 60}));
    assert(!turbo_pickup_contains(origin, origin, {102, 60, 60}));
    assert(!turbo_pickup_contains(origin, origin, {100, 61, 60}));
    assert(!turbo_pickup_contains(origin, origin, {100, 60, 61}));

    // Subtracting before vec_scale loses the source's independently rounded tie.
    const Vec3 shifted_body{1. / 1024., 0, 0};
    const Vec3 tied_pickup{101.5, 0, 0};
    assert(!source_oracle(shifted_body, origin, tied_pickup));
    assert(inside_box(var_vec_scale(body_source(tied_pickup) - body_source(shifted_body), v(.001))));
    assert(!turbo_pickup_contains(shifted_body, origin, tied_pickup));

    // A float SDK import erases the raw unit just below this high-position tie.
    const Vec3 high_body{1000000, 0, 0};
    const Vec3 subfloat_pickup{1000000 + 101.5 - 1. / 1024., 0, 0};
    assert(body_source(subfloat_pickup).x.raw != source(subfloat_pickup).x.raw);
    assert(source_oracle(high_body, origin, subfloat_pickup));
    assert(!inside_box(var_vec_scale(source(subfloat_pickup), v(.001)) -
                       var_vec_scale(source(high_body), v(.001))));
    assert(turbo_pickup_contains(high_body, origin, subfloat_pickup));

    // Mixed-axis rotations distinguish negative Euler angles from an inverse quaternion.
    const Vec3 angles{90, 30, 45};
    const Vec3 pickup{75, 0, 0};
    assert(!source_oracle(origin, angles, pickup));
    assert(inside_box(inverse_body_rotation(var_vec_scale(body_source(pickup), v(.001)), angles)));
    assert(!turbo_pickup_contains(origin, angles, pickup));

    const std::array<Vec3, 3> translations{{
        {0, 0, 0}, {123456.25, -987654.5, 456789.75},
        {1000000, -1500000, 750000}
    }};
    const std::array<Vec3, 3> orientations{{
        {0, 0, 0}, {37, -23, 61}, {-71, 42, -19}
    }};
    for (Vec3 translation : translations) {
        for (Vec3 orientation : orientations) {
            for (std::size_t axis = 0; axis < axes.size(); ++axis) {
                for (std::int32_t sign : {-1, 1}) {
                    const std::int32_t tie = sign * (limits[axis] * 1024 - 512);
                    const std::array<std::int32_t, 5> samples{
                        sign * (limits[axis] - 1) * 1024,
                        tie - 1, tie, tie + 1,
                        sign * (limits[axis] + 1) * 1024
                    };
                    for (std::int32_t sample : samples) {
                        const Vec3 candidate = translation +
                            undo_source_rotation(axes[axis] * (sample / 1024.), orientation);
                        assert(turbo_pickup_contains(translation, orientation, candidate) ==
                               source_oracle(translation, orientation, candidate));
                    }
                }
            }
        }
    }
    if (argc == 2) countdown_materials(argv[1]);
    return 0;
}
