#include "game/VehicleController.hpp"
#include "assets/World.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

// Uses the real imported original world/models/audio, not an invented flat test track.
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: test_vehicle_behavior ORIGINAL_ASSET_ROOT\n"; return 2; }
    const std::filesystem::path root(argv[1]);
    const std::array<std::string_view,1> names{"bogazici.wmb"};
    auto resources=c4::assets::resolve_resources(root/"C4.wrs",names);
    auto world = c4::assets::load_world(resources[0]);
    c4::game::SharedState state;
    std::vector<c4::game::VisualState> visuals;
    auto ids = c4::game::bootstrap_game(world, root, state, visuals);
    c4::physics::PhysicsWorld physics(world);
    c4::audio::Audio audio(48000, c4::audio::Mode::Offline);
    c4::game::GameRuntime runtime(world,physics,audio,state,ids,visuals,root);
    auto& ctx=runtime.context();
    const auto q=[](double v) { return c4::physics::Var::literal(v); };
    physics.on_entity=[&](const c4::physics::EntityEvent& event) { c4::game::SourceMotion::bounce(ctx,event); };
    state.ghost_reset_ok=q(0);
    c4::game::VehicleController car;
    car.initialize(ctx);
    c4::game::InputSnapshot input;
    c4::game::FrameTiming time{};
    // Proven compiler literals and opcode arithmetic: 3.827 * .3 * .8 gives raw938.
    // Checking the real controller here catches a final-only quantization shortcut.
    state.ghost_yaris_start=q(1);
    state.c4_eski_havazaman=q(0);
    input.key_cuu=true;
    time.originaltime_step_ticks=q(.3);
    car.update(ctx,input,time);
    assert(state.arac_ivme.raw==938);
    state.arac_ivme=q(0);
    car.reset(ctx);
    input.key_cuu=false;
    time.originaltime_step_ticks=q(.5);
    // Real left+Up frames exercise the front-wheel cap and its divide-before-multiply.
    input.key_cuu=true; input.key_cul=true;
    auto steer=q(0);
    for(int n=0;n<12;++n) {
        car.update(ctx,input,time);
        steer+=(q(10)-q(5)*state.arac_ivme/q(340))*time.originaltime_step_ticks;
        const auto limit=q(40)-q(16)*(state.arac_ivme/q(340));
        if(steer>limit)steer=limit;
        const auto expected=c4::assets::animation_sample(ctx.group(ids.p_arac_direx),"dondur",
            static_cast<float>(c4::physics::number(q(50)-steer*q(1.25))));
        const auto actual=visuals[ids.p_arac_direx].sample;
        assert(actual.first_frame==expected.first_frame && actual.second_frame==expected.second_frame && actual.blend==expected.blend);
        ++time.frame_index;
    }
    input.key_cuu=false; input.key_cul=false;
    state.arac_ivme=q(0);
    car.reset(ctx);
    // Down at zero enters reverse only after the source .25s direction wait.
    state.ghost_yaris_start=q(1);
    input.key_cud=true;
    for (int n=0;n<8;++n) { car.update(ctx,input,time); assert(state.arac_ivme.raw==0); ++time.frame_index; }
    car.update(ctx,input,time);
    assert(state.arac_ivme.raw<0 && state.arac_vites_no.raw==q(7).raw);
    // Crossing zero under throttle brakes, never jumps from reverse to forward.
    input.key_cud=false; input.key_cuu=true;
    state.arac_ivme=q(-1);
    car.update(ctx,input,time);
    assert(state.arac_ivme.raw==0);
    // Space clamps through zero; D is ignored before the race starts, then hands off.
    input.key_cuu=false; input.key_space=true;
    state.arac_ivme=q(1);
    car.update(ctx,input,time);
    assert(state.arac_ivme.raw==0);
    // Nitro is published after movement: its last charge accelerates the NEXT loop.
    input.key_space=false; input.key_cuu=true; input.key_s=true;
    state.arac_ivme=q(51); state.turbo_ekle_miktar=q(1);
    car.update(ctx,input,time);
    const auto first_delta=state.arac_ivme-q(51);
    assert(state.arac_turbo.raw==q(5).raw && state.turbo_ekle_miktar.raw==0);
    const auto first_speed=state.arac_ivme;
    car.update(ctx,input,time);
    assert((state.arac_ivme-first_speed).raw>first_delta.raw);
    assert(state.arac_turbo.raw==q(1).raw);
    input.key_cuu=false; input.key_s=false;
    input.key_space=false; input.key_d=true;
    state.ghost_yaris_start=q(0);
    car.update(ctx,input,time);
    assert(state.phase_request==c4::game::PhaseRequest::none);
    state.ghost_yaris_start=q(1);
    car.update(ctx,input,time);
    assert(state.phase_request==c4::game::PhaseRequest::robot);
    assert(state.model_robot_ok.raw==q(1).raw && state.arac_durum_tip.raw==q(1).raw);
    assert(visuals[ids.p_arac_cam].invisible && !visuals[ids.p_robot_kaporta].invisible);
    std::cout << "real-world reverse wait, zero clamps and conversion boundary passed\n";
}
