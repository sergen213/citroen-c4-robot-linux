#include "game/effects/ParticleCallbacks.hpp"
#include "game/GameState.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

namespace {
using namespace c4::game;
using c4::physics::Var;
Var v(double value) { return Var::literal(value); }

struct Fixture {
    c4::assets::WorldScene world;
    c4::physics::PhysicsWorld physics;
    c4::audio::Audio audio{48000, c4::audio::Mode::Offline};
    SharedState state;
    ComponentIds ids;
    std::vector<VisualState> visuals;
    std::filesystem::path root;
    SoundCache sounds;
    GameContext context{world, physics, audio, state, ids, visuals, root, sounds};
    Fixture() : world(make_world()), physics(world) {
        ids.p_aracgovde = ids.p_camera_target = 0;
        state.arac_ivme = v(96);
    }
    static c4::assets::WorldScene make_world() {
        c4::assets::WorldScene result;
        result.entities.emplace_back();
        return result;
    }
};

SourceParticle particle() {
    SourceParticle result;
    result.lifespan = v(80);
    result.size = v(4);
    result.alpha = v(50);
    return result;
}

void engine_lifecycle() {
    auto p = particle();
    p.move = true;
    p.velocity[2] = v(3);
    p.gravity = v(.8);
    p.lifespan = v(0);
    assert(!step_particle_engine(p, v(.5)));
    assert(p.position[2].raw == 0 && p.velocity[2].raw == v(3).raw);

    p.lifespan = v(.5);
    assert(step_particle_engine(p, v(.5)));
    assert(p.lifespan.raw == 0);
    assert(p.position[2].raw == v(1.5).raw);
    assert(p.velocity[2].raw == (v(3) - v(.8).scaled(v(.5))).raw);
    assert(!step_particle_engine(p, v(.5)));
    assert(p.position[2].raw == v(1.5).raw);

    p = particle();
    p.move = true;
    p.velocity[0] = Var{1};
    p.velocity[1] = Var{-1};
    p.lifespan = Var{1};
    assert(step_particle_engine(p, v(.5)));
    assert(p.lifespan.raw == -511);
    assert(p.position[0].raw == 1 && p.position[1].raw == 0);
    assert(!step_particle_engine(p, v(.5)));

    p = particle();
    p.velocity[2] = v(3);
    p.gravity = v(2);
    assert(step_particle_engine(p, v(1)));
    assert(p.position[2].raw == 0 && p.velocity[2].raw == v(3).raw);
}

void source_alpha_thresholds(Fixture& f) {
    auto p = particle();
    initialize_particle(f.context, p, "kivilcim_effect");
    p.alpha = v(2);
    assert(step_particle(f.context, p, v(1)));
    assert(p.alpha.raw == 0 && p.lifespan.raw == v(79).raw);
    const auto position = p.position;
    assert(!step_particle(f.context, p, v(1)));
    assert(p.alpha.raw == v(-2).raw && p.lifespan.raw == 0);
    for (unsigned i=0; i<3; ++i) assert(p.position[i].raw == position[i].raw);

    p = particle();
    initialize_particle(f.context, p, "kivilkay_effect");
    p.alpha = v(4);
    assert(step_particle(f.context, p, v(1)));
    assert(p.alpha.raw == 0);
    assert(!step_particle(f.context, p, v(1)));

    for (auto callback : {"toz_effect", "babatoz_effect", "robotoz_effect",
                          "babaduman_effect", "patitoz_effect", "karasmoke_effect"}) {
        p = particle();
        initialize_particle(f.context, p, callback);
        p.alpha = v(callback == std::string_view("robotoz_effect") ? 1.125 :
                    callback == std::string_view("babaduman_effect") ||
                    callback == std::string_view("karasmoke_effect") ? .6 : .9);
        const auto before = p.position;
        assert(!step_particle(f.context, p, v(1)));
        assert(p.alpha.raw == 0 && p.lifespan.raw == 0);
        for (unsigned i=0; i<3; ++i) assert(p.position[i].raw == before[i].raw);
    }
}

void anchor_and_callback_order(Fixture& f) {
    auto p = particle();
    initialize_particle(f.context, p, "robotoz_effect");
    p.alpha = p.initial_alpha = v(20);
    p.rise = v(2);
    p.acceleration = v(4);
    p.velocity = {};
    f.physics.body(0).position = {8, 4, 2};
    assert(step_particle(f.context, p, v(.5)));
    assert(p.position[0].raw == v(6).raw);
    assert(p.position[1].raw == v(3).raw);
    assert(p.position[2].raw == v(2.5).raw);
    assert(p.velocity[2].raw == v(2).raw);
    assert(p.alpha.raw == (v(20) - v(1.125)*v(.5)).raw);
    // A zero engine lifespan still invokes the source callback before removal.
    p.lifespan = v(0);
    const auto alpha = p.alpha;
    assert(!step_particle(f.context, p, v(.5)));
    assert(p.alpha.raw == (alpha - v(1.125)*v(.5)).raw);
}

void dust_initialization(Fixture& f) {
    auto expected=f.state;
    const auto x=source_random(expected,v(8))-v(4);
    const auto y=source_random(expected,v(8))-v(4);
    const auto z=v(1)+source_random(expected,v(2));
    const auto rise=source_random(expected,v(2));
    const auto acceleration=v(1)+source_random(expected,v(1));
    const auto size=v(32)+source_random(expected,v(32));
    const auto alpha=v(1.5)*(v(1.5)+source_random(expected,v(100))/v(200))*v(16);
    auto p=particle();
    initialize_particle(f.context,p,"toz_effect");
    assert(p.velocity[0].raw==x.raw && p.velocity[1].raw==y.raw && p.velocity[2].raw==z.raw);
    assert(p.rise.raw==rise.raw && p.acceleration.raw==acceleration.raw);
    assert(p.size.raw==size.raw && p.alpha.raw==alpha.raw && p.initial_alpha.raw==alpha.raw);
    assert(f.state.rng_state==expected.rng_state && f.state.rng_calls==expected.rng_calls);
}

void nitro_lifetime(Fixture& f) {
    NitroParticleContext nitro;
    nitro.active = true;
    nitro.start[0] = {v(10),v(20),v(30)};
    nitro.end[0] = {v(36),v(46),v(56)};
    nitro.start[1] = {v(-10),v(-20),v(-30)};
    nitro.end[1] = {v(16),v(6),v(-4)};
    for (auto callback : {"nitrospezial1", "nitrospezial2"}) {
        auto p = particle();
        p.ordinal = 13;
        const auto calls = f.state.rng_calls;
        auto expected=f.state;
        auto expected_alpha=v(40)*(v(13)/v(26))*((v(100)-source_random(expected,v(30)))/v(100));
        initialize_particle(f.context, p, callback, nitro);
        assert(f.state.rng_calls == calls+1);
        assert(p.alpha.raw==expected_alpha.raw);
        assert(p.size.raw==(v(15)-v(4)*(v(40)-expected_alpha)/v(40)).raw);
        const auto beam = callback == std::string_view("nitrospezial1") ? 0 : 1;
        assert(p.position[0].raw == (nitro.start[beam][0]+v(13)).raw);
        assert(!p.move && p.gravity.raw == 0 && p.lifespan.raw == v(80).raw);
        p.lifespan = v(0);
        expected=f.state;
        expected_alpha=v(40)*(v(13)/v(26))*((v(100)-source_random(expected,v(30)))/v(100));
        assert(step_particle(f.context, p, v(.5), nitro));
        assert(p.lifespan.raw == v(.5).raw && f.state.rng_calls == calls+2);
        assert(p.alpha.raw==expected_alpha.raw);
        // Callback renews life to one even after the previous engine subtraction hits zero.
        assert(step_particle(f.context, p, v(1), nitro));
        assert(p.lifespan.raw == 0 && f.state.rng_calls == calls+3);
        assert(step_particle(f.context, p, v(.5), nitro));
        nitro.freeze = 1;
        assert(!step_particle(f.context, p, v(.5), nitro));
        assert(p.lifespan.raw == 0 && f.state.rng_calls == calls+5);
        nitro.freeze = 0;
        nitro.active = false;
        assert(!step_particle(f.context, p, v(.5), nitro));
        assert(f.state.rng_calls == calls+6);
        nitro.active = true;
    }
}
void anchor_raw_precision(Fixture& f) {
    f.physics.body(0).position = {100000.+1./1024., 0, 0};
    auto p=particle();
    initialize_particle(f.context,p,"toz_effect");
    assert(p.previous_anchor[0].raw==102400001);
    p.alpha=p.initial_alpha=v(1);
    p.rise=p.acceleration=v(0);
    f.physics.body(0).position.x+=1./1024.;
    assert(step_particle(f.context,p,v(0)));
    assert(p.position[0].raw==1 && p.previous_anchor[0].raw==102400002);
}

} // namespace

int main() {
    engine_lifecycle();
    Fixture fixture;
    dust_initialization(fixture);
    source_alpha_thresholds(fixture);
    anchor_and_callback_order(fixture);
    nitro_lifetime(fixture);
    anchor_raw_precision(fixture);
}
