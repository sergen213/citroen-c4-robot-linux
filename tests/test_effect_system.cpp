#include "game/effects/SourceEffects.hpp"
#include "game/GameState.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

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
    std::vector<VisualState> visuals{1};
    std::filesystem::path root;
    SoundCache sounds;
    GameContext context{world, physics, audio, state, ids, visuals, root, sounds};
    explicit Fixture(const std::filesystem::path& path, bool ambient_smoke=false)
        : world(make_world(ambient_smoke)), physics(world), root(path) {
        ids.p_aracgovde = ids.p_camera_target = 0;
        state.camera_pixel_aspect=1;
    }
    static c4::assets::WorldScene make_world(bool ambient_smoke) {
        c4::assets::WorldScene result;
        result.entities.emplace_back();
        if (ambient_smoke) result.entities[0].action="kara_smoke";
        return result;
    }
};

std::array<float,3> center(const EffectFrame& frame, std::size_t particle) {
    std::array<float,3> result{};
    for (unsigned corner=0; corner<4; ++corner)
        for (unsigned axis=0; axis<3; ++axis)
            result[axis] += frame.vertices[particle*4+corner].position[axis]*.25f;
    return result;
}
bool near(float actual, float expected) { return std::abs(actual-expected)<.002f; }

void update_and_publish(SourceEffects& effects, GameContext& context, FrameTiming timing) {
    effects.update(context,timing);
    effects.publish_frame(context);
}

void fractional_spark_and_single_consume(Fixture& f, SourceEffects& effects) {
    effects.clear();
    f.state.arac_ivme=v(21);
    const auto fractional=v(4)*f.state.particle_count*((v(21)-v(20))/v(40));
    assert(fractional.raw>0 && fractional.raw<v(1).raw);
    const EffectRequest request{"kivilci_cik_fx", {40,20,10}};
    auto expected=f.state;
    const auto magnitude=v(1)+source_random(expected,v(1));
    const auto pan=source_random(expected,v(300))+v(30);
    const auto tilt=v(20)+source_random(expected,v(50));
    const auto emitter_expected=expected;
    const std::array<Var,3> velocity{
        magnitude*c4::physics::var_cos(pan)*magnitude*c4::physics::var_cos(tilt),
        magnitude*c4::physics::var_sin(pan)*magnitude*c4::physics::var_cos(tilt),
        v(2)*magnitude*c4::physics::var_cos(tilt)};
    const auto alpha=v(35)+source_random(expected,v(10));
    const auto bitmap_alpha=static_cast<float>((static_cast<std::int64_t>(alpha.raw)*10)>>11)/255;
    const auto before=f.state;
    effects.emit(f.context,request);
    assert(f.state.rng_calls==before.rng_calls+3 && f.state.rng_state==emitter_expected.rng_state);
    // Initialization is deferred to the updater and must also remain frozen.
    f.state.freeze_mode=v(1);
    update_and_publish(effects, f.context, {v(100)});
    assert(f.state.rng_calls==emitter_expected.rng_calls && f.state.rng_state==emitter_expected.rng_state);
    f.state.freeze_mode=v(0);
    // Birth runs initialization then engine motion, not the alpha-decay callback.
    update_and_publish(effects, f.context, {v(1)});
    assert(effects.frame().counts.emitted==1 && effects.frame().counts.active==1);
    assert(f.state.rng_calls==before.rng_calls+4);
    assert(f.state.rng_calls==expected.rng_calls && f.state.rng_state==expected.rng_state);
    const auto position=center(effects.frame(),0);
    for (unsigned axis=0; axis<3; ++axis) {
        const auto target=Var::from_float((v(axis==0?40:axis==1?20:10)+velocity[axis]*v(6)).to_float());
        assert(near(position[axis],(target+velocity[axis]).to_float()));
    }
    assert(std::abs(effects.frame().vertices[0].color[3]-bitmap_alpha)<.000001f);
    assert(effects.frame().batches.size()==1 && effects.frame().batches[0].texture_index==2);
    assert(effects.frame().batches[0].additive && effects.frame().batches[0].bright);
    const auto& texture=effects.frame().textures[2];
    assert(texture.width>0 && (texture.width&(texture.width-1))==0);
    assert(texture.height>0 && (texture.height&(texture.height-1))==0);
    const float u_min=.5f/texture.width, u_max=(texture.width-.5f)/texture.width;
    const float v_min=.5f/texture.height, v_max=(texture.height-.5f)/texture.height;
    const std::array<std::array<float,2>,4> texel_centers{{
        {u_min,v_min}, {u_max,v_min}, {u_max,v_max}, {u_min,v_max}}};
    // Original TL/TR/BR/BL corners sample texel centers, never bitmap borders.
    for (unsigned corner=0; corner<4; ++corner)
        for (unsigned axis=0; axis<2; ++axis)
            assert(std::abs(effects.frame().vertices[corner].uv[axis]-texel_centers[corner][axis])<.000001f);

    effects.clear();
    f.state=before;
    f.context.source_effects=&effects;
    effects.update(f.context,{v(0)});
    // A first-pass UI emission is after simulation, before the world draw.
    f.context.effect(request.helper,request.position);
    assert(f.state.effect_count==1 && f.state.effects[0].helper==request.helper);
    assert(f.state.rng_calls==emitter_expected.rng_calls && f.state.rng_state==emitter_expected.rng_state);
    effects.publish_frame(f.context);
    assert(effects.frame().counts.active==1 && effects.frame().counts.emitted==1);
    assert(effects.frame().vertices.empty() && effects.frame().indices.empty() && effects.frame().batches.empty());
    assert(f.state.rng_calls==emitter_expected.rng_calls && f.state.rng_state==emitter_expected.rng_state);
    update_and_publish(effects, f.context, {v(0)});
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().counts.emitted==1 && effects.frame().counts.active==1);
    assert(f.state.rng_calls==expected.rng_calls && f.state.rng_state==expected.rng_state);
    assert(std::abs(effects.frame().vertices[0].color[3]-bitmap_alpha)<.000001f);
    f.context.source_effects=nullptr;
}

void freeze_and_hidden_expiration(Fixture& f, SourceEffects& effects) {
    effects.clear();
    f.state.freeze_mode=v(0);
    f.state.particle_count=v(1);
    f.physics.body(0).position={};
    const EffectRequest request{"robotkum_toz_cik", {100,20,10}};
    effects.emit(f.context,request);
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().counts.active==4 && effects.frame().counts.emitted==4);
    const std::vector<EffectVertex> frozen(effects.frame().vertices.begin(),effects.frame().vertices.end());
    const auto old_center=center(effects.frame(),0);
    const auto rng=f.state.rng_state;
    const auto calls=f.state.rng_calls;
    const auto* vertices=effects.frame().vertices.data();
    const auto* indices=effects.frame().indices.data();
    const auto* batches=effects.frame().batches.data();
    const auto* textures=effects.frame().textures.data();
    f.state.freeze_mode=v(1);
    f.physics.body(0).position={20,0,0};
    effects.emit(f.context,{"robotkum_toz_cik", {10000,0,0}});
    update_and_publish(effects, f.context, {v(100)});
    update_and_publish(effects, f.context, {v(100)});
    assert(f.state.rng_state==rng && f.state.rng_calls==calls);
    assert(effects.frame().counts.active==4 && effects.frame().counts.emitted==4);
    assert(effects.frame().counts.expired==0);
    assert(effects.frame().vertices.size()==frozen.size());
    for (std::size_t i=0; i<frozen.size(); ++i) {
        assert(effects.frame().vertices[i].position==frozen[i].position);
        assert(effects.frame().vertices[i].uv==frozen[i].uv);
        assert(effects.frame().vertices[i].color==frozen[i].color);
    }
    assert(effects.frame().vertices.data()==vertices && effects.frame().indices.data()==indices);
    assert(effects.frame().batches.data()==batches && effects.frame().textures.data()==textures);

    f.state.freeze_mode=v(0);
    update_and_publish(effects, f.context, {v(0)});
    assert(near(center(effects.frame(),0)[0],old_center[0]+15));
    assert(effects.frame().vertices[0].color==frozen[0].color);
    // Frozen callbacks must not update their cached anchor or consume lifetime.
    update_and_publish(effects, f.context, {v(1)});
    assert(effects.frame().counts.active==4 && effects.frame().counts.expired==0);
    const auto alpha=effects.frame().vertices[0].color[3];
    f.state.noparticle=true;
    update_and_publish(effects, f.context, {v(1)});
    assert(effects.frame().vertices.empty() && effects.frame().indices.empty() && effects.frame().batches.empty());
    assert(effects.frame().counts.active==4);
    f.state.noparticle=false;
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().vertices[0].color[3]<alpha);
    f.state.noparticle=true;
    update_and_publish(effects, f.context, {v(100)});
    assert(effects.frame().counts.active==0 && effects.frame().counts.expired==4);
    f.state.noparticle=false;
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().vertices.empty() && effects.frame().batches.empty());
    effects.clear();
    effects.emit(f.context,request);
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().counts.active==4 && effects.frame().counts.emitted==4);
    assert(effects.frame().vertices.data()==vertices && effects.frame().indices.data()==indices);
    assert(effects.frame().batches.data()==batches && effects.frame().textures.data()==textures);
}

void capacity_and_source_head_recycling(Fixture& f, SourceEffects& effects) {
    effects.clear();
    f.state.particle_count=v(2500);
    f.physics.body(0).position={};
    const auto calls=f.state.rng_calls;
    effects.emit(f.context,{"robotkum_toz_cik", {100,20,10}});
    update_and_publish(effects, f.context, {v(0)});
    assert(effects.frame().counts.emitted==9990 && effects.frame().counts.active==9990);
    assert(effects.frame().counts.allocation_failures==0 && effects.frame().counts.recycled==0);
    assert(f.state.rng_calls==calls+4+9990*7);
    const auto old_center=center(effects.frame(),0);
    std::array<float,10> tail_alpha{};
    for (unsigned i=0; i<10; ++i)
        tail_alpha[i]=effects.frame().vertices[(9980+i)*4].color[3];
    const auto* vertices=effects.frame().vertices.data();
    const auto* indices=effects.frame().indices.data();
    const auto* batches=effects.frame().batches.data();
    effects.emit(f.context,{"robotkum_toz_cik", {10000,20,10}});
    update_and_publish(effects, f.context, {v(0)});
    const auto& frame=effects.frame();
    assert(frame.counts.emitted==19980 && frame.counts.active==10000);
    assert(frame.counts.recycled==9980 && frame.counts.allocation_failures==0 && frame.counts.expired==0);
    assert(f.state.rng_calls==calls+2*(4+9990*7));
    assert(frame.vertices.size()==40000 && frame.indices.size()==60000 && frame.batches.size()==10000);
    const auto new_center=center(frame,0);
    assert(new_center[0]>=9995 && new_center[0]<10005);
    // Ten free records prepend; recycling walks the pre-batch head without
    // relinking it. Thus the ten OLD tail records survive, not the newest ten.
    for (std::size_t i=0; i<10000; ++i) {
        const auto actual=center(frame,i);
        const auto& expected=i<9990?new_center:old_center;
        for (unsigned axis=0; axis<3; ++axis) assert(near(actual[axis],expected[axis]));
        if (i>=9990) assert(frame.vertices[i*4].color[3]==tail_alpha[i-9990]);
        assert(frame.batches[i].first_index==i*6 && frame.batches[i].index_count==6);
        for (unsigned j=0; j<6; ++j) {
            constexpr unsigned corners[]{0,1,2,0,2,3};
            assert(frame.indices[i*6+j]==i*4+corners[j]);
        }
    }
    // The same preallocated GPU storage survives saturation and hidden frames.
    assert(frame.vertices.data()==vertices && frame.indices.data()==indices && frame.batches.data()==batches);
    f.state.noparticle=true;
    update_and_publish(effects, f.context, {v(0)});
    assert(frame.counts.active==10000 && frame.vertices.empty());
    assert(frame.vertices.data()==vertices && frame.indices.data()==indices && frame.batches.data()==batches);
    f.state.noparticle=false;
    update_and_publish(effects, f.context, {v(0)});
    assert(frame.vertices.size()==40000 && frame.counts.active==10000);
    assert(frame.vertices.data()==vertices && frame.indices.data()==indices && frame.batches.data()==batches);
    assert(near(center(frame,9999)[0],old_center[0]));
}

void source_process_pause_boundary(const std::filesystem::path& root) {
    Fixture f(root,true);
    SourceEffects effects(root);
    effects.initialize(f.context);
    f.physics.body(0).position={30,40,50};
    auto expected=f.state;
    const auto x=v(30)+source_random(expected,v(20))-v(10);
    const auto y=v(40)+source_random(expected,v(20))-v(10);
    effects.run_processes(f.context,{v(.5)});
    effects.publish_frame(f.context);
    assert(effects.frame().counts.emitted==2);
    assert(f.state.rng_calls==2 && f.state.rng_state==expected.rng_state);
    // Source processes run before the pause command; callbacks run after it.
    f.state.freeze_mode=v(1);
    f.state.noparticle=true;
    update_and_publish(effects, f.context, {v(100)});
    assert(f.state.rng_calls==2 && f.state.rng_state==expected.rng_state);
    assert(effects.frame().counts.active==2 && effects.frame().counts.expired==0);
    assert(effects.frame().vertices.empty());

    f.state.freeze_mode=v(0);
    f.state.noparticle=false;
    source_random(expected,v(3));
    source_random(expected,v(3));
    update_and_publish(effects, f.context, {v(0)});
    assert(f.state.rng_calls==4 && f.state.rng_state==expected.rng_state);
    assert(effects.frame().counts.active==2 && effects.frame().counts.emitted==2);
    for (unsigned i=0; i<2; ++i) {
        const auto position=center(effects.frame(),i);
        assert(near(position[0],Var::from_float(x.to_float()).to_float()));
        assert(near(position[1],Var::from_float(y.to_float()).to_float()));
        assert(near(position[2],50));
        assert(effects.frame().batches[i].texture_index==4);
        assert(std::abs(effects.frame().vertices[i*4].color[3]-50.f/255)<.000001f);
    }
    // Updating callbacks alone must neither reinitialize nor run ambient actions.
    update_and_publish(effects, f.context, {v(0)});
    assert(f.state.rng_calls==4 && f.state.rng_state==expected.rng_state);
    assert(effects.frame().counts.active==2 && effects.frame().counts.emitted==2);
    source_random(expected,v(20));
    source_random(expected,v(20));
    effects.run_processes(f.context,{v(.5)});
    assert(f.state.rng_calls==6 && f.state.rng_state==expected.rng_state);
    effects.publish_frame(f.context);
    assert(effects.frame().counts.emitted==4);
    source_random(expected,v(3));
    source_random(expected,v(3));
    update_and_publish(effects, f.context, {v(0)});
    assert(f.state.rng_calls==8 && f.state.rng_state==expected.rng_state);
    assert(effects.frame().counts.active==4 && effects.frame().counts.emitted==4);
}

void ui_resume_and_draw_camera_boundary(const std::filesystem::path& root) {
    Fixture f(root);
    SourceEffects effects(root);
    effects.initialize(f.context);
    f.state.arac_ivme=v(21);
    effects.emit(f.context,{"kivilci_cik_fx", {40,20,10}});
    update_and_publish(effects,f.context,{v(0)});
    const std::vector<EffectVertex> before_pause(effects.frame().vertices.begin(),effects.frame().vertices.end());
    const auto calls=f.state.rng_calls;
    const auto rng=f.state.rng_state;
    assert(before_pause.size()==4 && calls==4);
    f.state.freeze_mode=v(1);
    f.state.noparticle=true;
    effects.publish_frame(f.context);
    assert(effects.frame().vertices.empty());
    effects.update(f.context,{v(100)});
    // First-pass UI resume is later than the updater, earlier than world draw.
    f.state.freeze_mode=v(0);
    f.state.noparticle=false;
    effects.publish_frame(f.context);
    assert(f.state.rng_calls==calls && f.state.rng_state==rng);
    assert(effects.frame().counts.active==1 && effects.frame().counts.expired==0);
    assert(effects.frame().vertices.size()==before_pause.size());
    for (unsigned i=0; i<4; ++i) {
        assert(effects.frame().vertices[i].position==before_pause[i].position);
        assert(effects.frame().vertices[i].color==before_pause[i].color);
        assert(effects.frame().vertices[i].uv==before_pause[i].uv);
    }
    effects.update(f.context,{v(1)});
    // Simulation alone must not overwrite the last published GPU geometry.
    for (unsigned i=0; i<4; ++i) {
        assert(effects.frame().vertices[i].position==before_pause[i].position);
        assert(effects.frame().vertices[i].color==before_pause[i].color);
    }
    effects.publish_frame(f.context);
    const auto simulated_center=center(effects.frame(),0);
    const auto simulated_color=effects.frame().vertices[0].color;
    assert(simulated_color[3]<before_pause[0].color[3]);
    effects.update(f.context,{v(0)});
    f.state.camera_angles={v(90),v(0),v(0)};
    effects.publish_frame(f.context);
    assert(f.state.rng_calls==calls && f.state.rng_state==rng);
    assert(effects.frame().counts.active==1 && effects.frame().counts.expired==0);
    const auto drawn_center=center(effects.frame(),0);
    for (unsigned axis=0; axis<3; ++axis)
        assert(near(drawn_center[axis],simulated_center[axis]));
    for (unsigned i=0; i<4; ++i)
        assert(effects.frame().vertices[i].color==simulated_color);
    // Pan 90 rotates the four-unit horizontal edge from -Y to +X; up stays +Z.
    const auto& top_left=effects.frame().vertices[0].position;
    const auto& top_right=effects.frame().vertices[1].position;
    const auto& bottom_left=effects.frame().vertices[3].position;
    assert(near(top_right[0]-top_left[0],4));
    assert(near(top_right[1]-top_left[1],0) && near(top_right[2]-top_left[2],0));
    assert(near(top_left[0]-bottom_left[0],0) && near(top_left[1]-bottom_left[1],0));
    assert(near(top_left[2]-bottom_left[2],4));
}

void selected_view_particle_visibility(const std::filesystem::path& root) {
    Fixture f(root);
    SourceEffects effects(root);
    effects.initialize(f.context);
    f.state.particle_count=v(1);
    f.state.bloom_aktif=v(1);
    f.state.bloom_fx_ok=v(0);
    f.state.bloom_camera_angles=f.state.camera_angles;
    f.state.noparticle=false;
    f.state.bloom_noparticle=false;
    effects.emit(f.context,{"robotkum_toz_cik", {100,20,10}});
    update_and_publish(effects,f.context,{v(0)});
    const auto reference=effects.frame();
    assert(reference.counts.active==4 && reference.counts.emitted==4);
    assert(reference.counts.expired==0 && reference.counts.recycled==0 && reference.counts.allocation_failures==0);
    assert(reference.vertices.size()==16 && reference.indices.size()==24 && reference.batches.size()==4);
    const std::vector<EffectVertex> vertices(reference.vertices.begin(),reference.vertices.end());
    const std::vector<std::uint32_t> indices(reference.indices.begin(),reference.indices.end());
    const std::vector<EffectBatch> batches(reference.batches.begin(),reference.batches.end());
    const auto calls=f.state.rng_calls;
    const auto rng=f.state.rng_state;
    const auto diagnostics=effects.diagnostics();
    auto revision=reference.revision;
    const auto unchanged_counts=[&](const EffectCounts& expected) {
        const auto& actual=effects.frame().counts;
        assert(actual.active==expected.active && actual.emitted==expected.emitted);
        assert(actual.expired==expected.expired && actual.recycled==expected.recycled);
        assert(actual.allocation_failures==expected.allocation_failures);
        assert(f.state.rng_calls==calls && f.state.rng_state==rng);
    };
    const auto unchanged_snapshot=[&](bool hidden) {
        const auto& frame=effects.frame();
        assert(frame.revision==revision);
        unchanged_counts(reference.counts);
        assert(effects.diagnostics()==diagnostics);
        if (hidden) {
            assert(frame.vertices.empty() && frame.indices.empty() && frame.batches.empty());
            return;
        }
        assert(frame.vertices.size()==vertices.size() && frame.indices.size()==indices.size());
        assert(frame.batches.size()==batches.size());
        for (std::size_t i=0; i<vertices.size(); ++i) {
            assert(frame.vertices[i].position==vertices[i].position);
            assert(frame.vertices[i].uv==vertices[i].uv && frame.vertices[i].color==vertices[i].color);
        }
        for (std::size_t i=0; i<indices.size(); ++i) assert(frame.indices[i]==indices[i]);
        for (std::size_t i=0; i<batches.size(); ++i) {
            assert(frame.batches[i].first_index==batches[i].first_index && frame.batches[i].index_count==batches[i].index_count);
            assert(frame.batches[i].texture_index==batches[i].texture_index && frame.batches[i].additive==batches[i].additive);
            assert(frame.batches[i].bright==batches[i].bright && frame.batches[i].depth_write==batches[i].depth_write);
            assert(frame.batches[i].transparent==batches[i].transparent && frame.batches[i].alpha_test==batches[i].alpha_test);
        }
    };
    const auto publish=[&](bool hidden) {
        effects.publish_frame(f.context);
        ++revision;
        unchanged_snapshot(hidden);
    };
    // UI1 writes live VIEW flags; only explicit draw publication may replace the frame.
    f.state.noparticle=true;
    unchanged_snapshot(false);
    publish(true); // Primary selected and hidden; bloom is visible but not selected.
    f.state.bloom_fx_ok=v(1);
    unchanged_snapshot(true);
    publish(false); // Selected bloom remains visible despite the hidden primary.
    f.state.noparticle=false;
    f.state.bloom_noparticle=true;
    unchanged_snapshot(false);
    publish(true); // Selected bloom hides particles despite the visible primary.
    f.state.bloom_noparticle=false;
    unchanged_snapshot(true);
    publish(false); // The same initialized particles retain position, UV, color and counters.

    f.state.noparticle=true;
    f.state.camera_pixel_aspect=0;
    unchanged_snapshot(false);
    bool rejected=false;
    try {
        effects.publish_frame(f.context);
    } catch (const std::logic_error&) {
        rejected=true;
    }
    assert(rejected); // Primary hidden must not bypass selected-visible aspect validation.
    unchanged_snapshot(false);
    f.state.noparticle=false;
    f.state.bloom_noparticle=true;
    effects.update(f.context,{v(1)});
    unchanged_snapshot(false); // Hidden simulation advances without mutating the visible publication.
    publish(true); // Selected-hidden publication does not require a valid aspect.
    f.state.camera_pixel_aspect=1;
    f.state.bloom_noparticle=false;
    unchanged_snapshot(true);
    effects.publish_frame(f.context);
    assert(effects.frame().revision==++revision);
    unchanged_counts(reference.counts);
    assert(effects.frame().vertices.size()==vertices.size());
    for (std::size_t i=0; i<vertices.size(); ++i) {
        assert(effects.frame().vertices[i].uv==vertices[i].uv);
        assert(effects.frame().vertices[i].color[3]<vertices[i].color[3]);
    }
    const std::vector<EffectVertex> aged(effects.frame().vertices.begin(),effects.frame().vertices.end());
    f.state.bloom_noparticle=true;
    effects.update(f.context,{v(100)});
    assert(effects.frame().revision==revision);
    unchanged_counts(reference.counts);
    assert(effects.frame().vertices.size()==aged.size());
    for (std::size_t i=0; i<aged.size(); ++i) {
        assert(effects.frame().vertices[i].position==aged[i].position);
        assert(effects.frame().vertices[i].uv==aged[i].uv && effects.frame().vertices[i].color==aged[i].color);
    }
    effects.publish_frame(f.context);
    assert(effects.frame().revision==++revision);
    assert(effects.frame().vertices.empty() && effects.frame().indices.empty() && effects.frame().batches.empty());
    auto expired=reference.counts;
    expired.active=0;
    expired.expired=4;
    unchanged_counts(expired);
}

void ui2_clear_preserves_published_frame(const std::filesystem::path& root) {
    Fixture f(root,true);
    SourceEffects effects(root);
    effects.initialize(f.context);
    effects.emit(f.context,{"robotkum_toz_cik", {100,20,10}});
    update_and_publish(effects,f.context,{v(0)});
    const auto published=effects.frame();
    assert(published.counts.active==4 && published.counts.emitted==4);
    assert(published.vertices.size()==16 && published.indices.size()==24 && published.batches.size()==4);
    assert(published.vertices[0].color[3]>0 && published.batches[0].texture_index==0);
    assert(published.textures[0].width>0 && published.textures[0].height>0);
    const std::vector<EffectVertex> vertices(published.vertices.begin(),published.vertices.end());
    const std::vector<std::uint32_t> indices(published.indices.begin(),published.indices.end());
    const std::vector<EffectBatch> batches(published.batches.begin(),published.batches.end());
    const auto unchanged=[&] {
        const auto& frame=effects.frame();
        assert(frame.revision==published.revision && frame.source_anisotropy_raw==published.source_anisotropy_raw);
        assert(frame.counts.active==published.counts.active && frame.counts.emitted==published.counts.emitted);
        assert(frame.counts.expired==published.counts.expired && frame.counts.recycled==published.counts.recycled);
        assert(frame.counts.allocation_failures==published.counts.allocation_failures);
        assert(frame.vertices.data()==published.vertices.data() && frame.vertices.size()==vertices.size());
        assert(frame.indices.data()==published.indices.data() && frame.indices.size()==indices.size());
        assert(frame.batches.data()==published.batches.data() && frame.batches.size()==batches.size());
        assert(frame.textures.data()==published.textures.data() && frame.textures.size()==published.textures.size());
        assert(frame.texture_sources.data()==published.texture_sources.data());
        assert(frame.texture_sources.size()==published.texture_sources.size());
        for (std::size_t i=0; i<vertices.size(); ++i) {
            assert(frame.vertices[i].position==vertices[i].position);
            assert(frame.vertices[i].uv==vertices[i].uv && frame.vertices[i].color==vertices[i].color);
        }
        for (std::size_t i=0; i<indices.size(); ++i) assert(frame.indices[i]==indices[i]);
        for (std::size_t i=0; i<batches.size(); ++i) {
            assert(frame.batches[i].first_index==batches[i].first_index && frame.batches[i].index_count==batches[i].index_count);
            assert(frame.batches[i].texture_index==batches[i].texture_index && frame.batches[i].additive==batches[i].additive);
            assert(frame.batches[i].bright==batches[i].bright && frame.batches[i].depth_write==batches[i].depth_write);
            assert(frame.batches[i].transparent==batches[i].transparent && frame.batches[i].alpha_test==batches[i].alpha_test);
        }
    };
    // UI2 clears simulation after UI1 has selected this real, visible draw.
    effects.clear();
    unchanged();
    assert(effects.diagnostics()=="effects active=0 emitted=0 expired=0 recycled=0 allocation_failures=0");
    effects.run_processes(f.context,{v(0)});
    effects.update(f.context,{v(0)});
    effects.update(f.context,{v(100)});
    unchanged();
    assert(effects.diagnostics()=="effects active=0 emitted=2 expired=2 recycled=0 allocation_failures=0");
    f.state.particle_count=v(2500);
    effects.emit(f.context,{"robotkum_toz_cik", {10000,20,10}});
    effects.emit(f.context,{"robotkum_toz_cik", {20000,20,10}});
    effects.update(f.context,{v(0)});
    f.state.camera_angles={v(90),v(0),v(0)};
    f.state.d3d_anisotropy=v(8);
    unchanged();
    assert(effects.diagnostics().starts_with("effects active=10000 emitted=19982 expired=2 recycled=9980 allocation_failures=0"));
    const auto calls=f.state.rng_calls;
    const auto rng=f.state.rng_state;
    effects.publish_frame(f.context);
    const auto& replacement=effects.frame();
    assert(replacement.revision==published.revision+1 && replacement.source_anisotropy_raw==v(8).raw);
    assert(replacement.counts.active==10000 && replacement.counts.emitted==19982 && replacement.counts.expired==2);
    assert(replacement.counts.recycled==9980 && replacement.counts.allocation_failures==0);
    assert(replacement.vertices.size()==40000 && replacement.indices.size()==60000 && replacement.batches.size()==10000);
    assert(replacement.vertices.data()==published.vertices.data() && replacement.indices.data()==published.indices.data());
    assert(replacement.batches.data()==published.batches.data() && replacement.textures.data()==published.textures.data());
    assert(center(replacement,0)[0]>=19995 && center(replacement,0)[0]<20005);
    assert(replacement.vertices[0].color[3]>0);
    assert(f.state.rng_calls==calls && f.state.rng_state==rng);
    const auto revision=replacement.revision;
    effects.clear();
    assert(replacement.revision==revision && replacement.counts.active==10000);
    effects.publish_frame(f.context);
    assert(replacement.revision==revision+1);
    assert(replacement.vertices.empty() && replacement.indices.empty() && replacement.batches.empty());
    assert(replacement.counts.active==0 && replacement.counts.emitted==0 && replacement.counts.expired==0);
    assert(replacement.counts.recycled==0 && replacement.counts.allocation_failures==0);
    assert(replacement.vertices.data()==published.vertices.data() && replacement.indices.data()==published.indices.data());
    assert(replacement.batches.data()==published.batches.data() && replacement.textures.data()==published.textures.data());
}
} // namespace

int main(int argc, char** argv) {
    if (argc!=2) {
        std::cerr << "usage: test_effect_system ORIGINAL_ASSET_ROOT\n";
        return 2;
    }
    Fixture fixture(argv[1]);
    SourceEffects effects(fixture.root);
    effects.initialize(fixture.context);
    assert(effects.frame().textures.size()==5 && effects.frame().texture_sources.size()==5);
    fractional_spark_and_single_consume(fixture,effects);
    freeze_and_hidden_expiration(fixture,effects);
    capacity_and_source_head_recycling(fixture,effects);
    source_process_pause_boundary(fixture.root);
    ui_resume_and_draw_camera_boundary(fixture.root);
    selected_view_particle_visibility(fixture.root);
    ui2_clear_preserves_published_frame(fixture.root);
}
