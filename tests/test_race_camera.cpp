#include "game/RaceCamera.hpp"
#include "game/CameraProjection.hpp"
#include "game/Ghost.hpp"
#include "game/SourceTime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

namespace {
using namespace c4::game;
Var q(double value) { return Var::literal(value); }
void source_time() {
    using c4::physics::Var;
    assert(source_time_roundtrip(Var{705}).raw==704);
    assert(source_str_for_num(Var{705})=="0.688");
    assert(source_str_for_num(Var{64})=="0.063");
    assert(source_str_for_num(Var{-64})=="-0.063");
    assert(source_str_for_num(Var{320})=="0.313");
    std::string text;
    source_time_text(text,Var::literal(16),"ZAMAN ");
    assert(text=="ZAMAN 00:01.000");
    source_time_text(text,Var{705},"ZAMAN ");
    assert(text=="ZAMAN 00:00.043");
}
void projection() {
    // Actual selected-camera D3D matrix after requested_camera_after_four_frames,
    // work/reports/physics/projection-original/observations.jsonl:220.
    const auto measured=perspective_camera({},{},60,1,55000,1,640,480);
    const auto& matrix=measured.view_projection;
    assert(std::bit_cast<std::uint32_t>(-matrix[4])==1071421818u);
    assert(std::bit_cast<std::uint32_t>(matrix[9])==1074991356u);
    // Undo the common OpenGL depth mapping exactly as the Vulkan shader does.
    assert(std::bit_cast<std::uint32_t>((matrix[2]+matrix[3])*.5f)==1065353369u);
    assert(std::bit_cast<std::uint32_t>((matrix[14]+matrix[15])*.5f)==3212837017u);
    assert(measured.projection_known&&measured.clip_near==1.f&&measured.clip_far==55000.f);
    constexpr int width=1280,height=720;
    // Explicit caller-owned test calibration, not guessed original-engine defaults.
    constexpr double arc=60,near_plane=4,far_plane=112500,pixel_aspect=1.25;
    auto project=[](const c4::rendering::CameraState& camera,Vec3 point,int viewport_width=1280,int viewport_height=720) {
        const auto& m=camera.view_projection;
        const double w=m[3]*point.x+m[7]*point.y+m[11]*point.z+m[15];
        assert(w>0);
        return std::array<double,3>{
            (1+(m[0]*point.x+m[4]*point.y+m[8]*point.z+m[12])/w)*viewport_width*.5,
            (1-(m[1]*point.x+m[5]*point.y+m[9]*point.z+m[13])/w)*viewport_height*.5,
            (m[2]*point.x+m[6]*point.y+m[10]*point.z+m[14])/w};
    };
    auto matches=[](const auto& actual,const auto& expected) {
        assert(std::abs(actual[0]-expected[0])<.001&&std::abs(actual[1]-expected[1])<.001);
        assert(std::abs(actual[2]-expected[2])<1e-5);
    };
    const Vec3 position{123,-456,789},angles{73,-24,19};
    const auto origin=perspective_camera({},{},arc,near_plane,far_plane,pixel_aspect,width,height);
    const auto transformed=perspective_camera(position,angles,arc,near_plane,far_plane,pixel_aspect,width,height);
    for(const Vec3 point:std::array<Vec3,2>{{{876,-401,-251},{141.255,-63.234,-39.750}}}) {
        const auto expected=project_view_entity(point,arc,near_plane,far_plane,pixel_aspect,width,height);
        assert(expected[0]>width*.5&&expected[1]>height*.5);
        matches(project(origin,point),expected);
        matches(project(transformed,position+c4::physics::rotate_vector(point,angles)),expected);
    }
    SharedState state;
    state.camera_effective_near=0;
    state.camera_pixel_aspect=0;
    auto rejects=[&] {
        bool rejected=false;
        try { (void)source_camera(state,width,height); }
        catch(const std::invalid_argument&) { rejected=true; }
        assert(rejected);
    };
    rejects();
    state.camera_effective_near=near_plane;
    rejects();
    state.camera_pixel_aspect=pixel_aspect;
    state.camera_effective_near=0;
    rejects();
    state.camera_effective_near=near_plane;
    state.camera_arc=q(arc);
    state.camera_clip_far=q(far_plane);
    const auto reference=source_camera(state,640,480);
    matches(project(reference,{876,-401,-251},640,480),
        project_view_entity({876,-401,-251},arc,near_plane,far_plane,pixel_aspect,640,480));
    const auto ultra=source_camera(state,3440,1440);
    assert(ultra.view_projection[9]==reference.view_projection[9]); // Preserve original vertical coverage.
    assert(std::abs(ultra.view_projection[4])<std::abs(reference.view_projection[4]));
    assert(ultra.view_projection[2]==reference.view_projection[2]&&ultra.view_projection[14]==reference.view_projection[14]);
    assert(state.camera_arc.raw==q(arc).raw&&state.camera_clip_far.raw==q(far_plane).raw);
    for(const auto* camera:{&origin,&transformed,&reference,&ultra}) {
        assert(camera->projection_known);
        assert(camera->clip_near>0&&camera->clip_far>camera->clip_near);
        assert(camera->clip_near==near_plane&&camera->clip_far==far_plane);
        for(int column=0;column<4;++column)for(int row=0;row<4;++row) {
            float product=0;
            for(int k=0;k<4;++k)
                product+=camera->projection[k*4+row]*camera->inverse_projection[column*4+k];
            assert(std::abs(product-(row==column?1.f:0.f))<1e-5f);
        }
    }
    const Vec3 forward=position+c4::physics::rotate_vector({100,0,0},angles);
    const auto& view=transformed.view;
    assert(std::abs(view[0]*forward.x+view[4]*forward.y+view[8]*forward.z+view[12])<1e-3);
    assert(std::abs(view[1]*forward.x+view[5]*forward.y+view[9]*forward.z+view[13])<1e-3);
    assert(std::abs(view[2]*forward.x+view[6]*forward.y+view[10]*forward.z+view[14]+100)<1e-3);
    assert(state.camera_position.x.raw==0&&state.camera_position.y.raw==0&&state.camera_position.z.raw==0);
    assert(state.camera_angles.x.raw==0&&state.camera_angles.y.raw==0&&state.camera_angles.z.raw==0);
    // effect_define.wdl:432–435,456–458: compile bloom alone does not select camView.
    state.camera_effective_near=1;
    state.camera_pixel_aspect=1;
    state.camera_arc=state.bloom_camera_arc=q(60);
    state.camera_clip_far=q(55000);
    state.bloom_aktif=q(1);
    state.bloom_fx_ok=q(0);
    assert(source_camera(state,640,480).clip_near==1.f);
    state.bloom_fx_ok=q(1);
    assert(source_camera(state,640,480).clip_near==float(observed_empty_view_near));
    state.bloom_fx_ok=q(0);
    assert(source_camera(state,640,480).clip_near==1.f);
    auto same_camera=[](const auto& actual,const auto& expected) {
        auto same_bits=[](const auto& a,const auto& b) {
            for(std::size_t i=0;i<a.size();++i)
                assert(std::bit_cast<std::uint32_t>(a[i])==std::bit_cast<std::uint32_t>(b[i]));
        };
        same_bits(actual.view_position,expected.view_position);
        same_bits(actual.view,expected.view);
        same_bits(actual.projection,expected.projection);
        same_bits(actual.inverse_projection,expected.inverse_projection);
        same_bits(actual.view_projection,expected.view_projection);
        assert(actual.clip_near==expected.clip_near&&actual.clip_far==expected.clip_far);
        assert(actual.projection_known==expected.projection_known&&actual.provenance==expected.provenance);
        assert(actual.source_view_flags==expected.source_view_flags);
        assert(actual.source_view_flags_known_mask==expected.source_view_flags_known_mask);
    };
    for(const bool bloom:{false,true}) {
        auto later=state;
        later.camera_position={q(123),q(-456),q(789)};
        later.bloom_camera_position={q(-12),q(34),q(56)};
        later.camera_angles=later.bloom_camera_angles={q(73),q(-24),q(19)};
        later.bloom_fx_ok=q(bloom);
        later.noshader=true;
        later.bloom_noparticle=true;
        later.camera_ambient=Var{1023};
        later.bloom_camera_ambient=Var{7*1024+1023};
        const auto captured=capture_source_view(later);
        assert(captured.clip_near.raw==(bloom?10463:later.camera_clip_near.raw));
        assert(captured.no_shader==!bloom&&captured.no_particles==bloom);
        assert(captured.flags_known_mask==0x00831410u);
        assert(captured.flags_value==(bloom?0x800000u:0x1000u));
        assert(captured.effective_ambient_known&&captured.effective_ambient==(bloom?7:0));
        const auto expected=source_camera(later,width,height);
        assert(expected.source_view_flags==captured.flags_value&&expected.source_view_flags_known_mask==captured.flags_known_mask);
        const auto hud=default_hud_camera(width,height);
        assert(hud.source_view_flags==0u&&hud.source_view_flags_known_mask==0x00821410u);
        same_camera(source_camera(captured,width,height),expected);
        later.camera_position=later.bloom_camera_position={};
        later.camera_angles=later.bloom_camera_angles={};
        later.camera_arc=later.bloom_camera_arc=q(45);
        later.camera_clip_near=q(7);
        later.camera_clip_far=q(60000);
        later.camera_effective_near=7;
        later.camera_pixel_aspect=1.25;
        later.bloom_fx_ok=q(!bloom);
        later.noshader=later.bloom_noshader=false;
        later.noparticle=later.bloom_noparticle=false;
        later.camera_ambient=later.bloom_camera_ambient=Var{128*1024};
        assert(!capture_source_view(later).effective_ambient_known);
        later.camera_ambient=later.bloom_camera_ambient=Var{-1};
        assert(!capture_source_view(later).effective_ambient_known);
        later.camera_ambient=later.bloom_camera_ambient=Var{127*1024+1023};
        const auto high_ambient=capture_source_view(later);
        assert(high_ambient.effective_ambient_known&&high_ambient.effective_ambient==127);
        assert(captured.effective_ambient_known&&captured.effective_ambient==(bloom?7:0));
        assert(captured.no_shader==!bloom&&captured.no_particles==bloom);
        assert(captured.flags_known_mask==0x00831410u&&captured.flags_value==(bloom?0x800000u:0x1000u));
        same_camera(source_camera(captured,width,height),expected);
        assert(source_camera(later,width,height).view_projection!=expected.view_projection);
    }
}
void declared_sky(const std::filesystem::path& root, const c4::assets::WorldScene& world) {
    using namespace c4;
    for (std::uint8_t mtq = 0; mtq != 3; ++mtq) {
        auto profile = assets::original_source_profile;
        profile.mtq = mtq;
        const std::string_view name = mtq == 0 ? "sky_blue_a+6.tga" : "sky_blue_c+6.tga";
        const std::array<std::string_view, 1> names{name};
        const auto resource = assets::resolve_resources(root / "C4.wrs", names, profile).at(0);
        assert(resource.string().find("kaynak_mtq" + std::to_string(mtq) + ".rc4::") != std::string::npos);
        assert(resource.string().ends_with(name));
        const auto strip = assets::load_image(resource);
        const auto faces = assets::gpu_sky_faces_from_image(strip);
        assert(strip.width == 6 * strip.height);
        for (const auto& face : faces) {
            assert(face.face_count == 1 && face.width == strip.width / 6 && face.height == strip.height);
            assert(face.source_alpha_known == strip.source_alpha_known);
            assert(face.source_alpha_flag == strip.source_alpha_flag);
        }
        rendering::SourceSkyBinding binding;
        binding.first_face_width = strip.width / 6;
        binding.first_face_height = strip.height;
        binding.stage_enabled = binding.stage_enabled_known = world.source_empty_collision_root_known;
        SharedState state;
        state.camera_effective_near = state.camera_pixel_aspect = 1;
        state.camera_arc = q(60);
        state.camera_clip_far = q(55000);
        state.camera_position = {q(123), q(-456), q(789)};
        const auto view = capture_source_view(state);
        const auto normal = source_camera(view, 1280, 720);
        const auto unchanged = normal;
        const auto sky = source_sky_frame(binding, view, normal, true);
        assert(sky && sky->camera.clip_far == 2 * normal.clip_far);
        assert(normal.projection == unchanged.projection && normal.view_projection == unchanged.view_projection);
        assert(sky->camera.view == normal.view && sky->camera.projection[0] == normal.projection[0]
            && sky->camera.projection[5] == normal.projection[5] && sky->camera.clip_near == normal.clip_near);
        const std::array<std::array<float, 3>, 6> offsets{{
            {-1,0,0}, {0,1,0}, {1,0,0}, {0,-1,0}, {0,0,-1}, {0,0,1}}};
        const std::array<std::array<float, 3>, 6> local_y{{
            {0,1,0}, {1,0,0}, {0,-1,0}, {-1,0,0}, {1,0,0}, {1,0,0}}};
        const std::array<std::array<float, 3>, 6> local_z{{
            {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}, {0,1,0}, {0,-1,0}}};
        const auto scale = Var::from_float(float(double(normal.clip_far) * 2 / binding.first_face_width)).to_float();
        state.camera_position = {};
        const auto origin_view = capture_source_view(state);
        const auto origin_camera = source_camera(origin_view, 1280, 720);
        const auto origin_sky = source_sky_frame(binding, origin_view, origin_camera, true);
        assert(origin_sky);
        for (std::size_t face = 0; face < 6; ++face) {
            const auto& matrix = sky->model_to_world[face];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                assert(matrix[12 + axis] == normal.view_position[axis] + offsets[face][axis] * normal.clip_far);
                assert(std::abs(matrix[axis] + offsets[face][axis]) < 1e-6); // All normals face the eye.
                assert(std::abs(matrix[4 + axis] / scale - local_y[face][axis]) < 1e-6);
                assert(std::abs(matrix[8 + axis] / scale - local_z[face][axis]) < 1e-6);
            }
            // Eye-relative geometry projects identically after a camera translation.
            for (std::size_t row = 0; row < 4; ++row) {
                float moved = 0, origin = 0;
                for (std::size_t k = 0; k < 4; ++k) {
                    moved += sky->camera.view_projection[k * 4 + row] * matrix[12 + k];
                    origin += origin_sky->camera.view_projection[k * 4 + row] * origin_sky->model_to_world[face][12 + k];
                }
                assert(std::abs(moved - origin) < .02);
            }
        }
        assert(!source_sky_frame(binding, view, normal, false));
        binding.stage_enabled_known = false;
        assert(!source_sky_frame(binding, view, normal, true));
        binding.stage_enabled_known = true;
        binding.stage_enabled = false;
        assert(!source_sky_frame(binding, view, normal, true));
        binding.stage_enabled = true;
        auto unknown = view;
        unknown.flags_known_mask &= ~0x10000u;
        assert(!source_sky_frame(binding, unknown, normal, true));
        unknown = view;
        unknown.flags_value |= 0x10000u; // Original WIREFRAME consumer suppresses the sky stage.
        assert(!source_sky_frame(binding, unknown, normal, true));
    }
}
constexpr std::array<std::array<double,4>,5> gates{{
    {16811.617,52681.492,16126.169,53152.059},
    {-19825.926,45132.727,-19085.248,45219.867},
    {-43974.715,-30874.541,-45091.395,-31609.875},
    {-6221.184,-22568.174,-6782.038,-22119.979},
    {27881.670,-32164.383,27565.885,-32102.354}}};
void same(Vec3 a,Vec3 b) { assert(a.x==b.x&&a.y==b.y&&a.z==b.z); }
void same(ScriptVec a,ScriptVec b) { assert(a.x.raw==b.x.raw&&a.y.raw==b.y.raw&&a.z.raw==b.z.raw); }
struct GatePose { Vec3 center,normal; double low,high; };
GatePose gate_pose(GameContext& context,int gate) {
    const auto& g=gates.at(gate%5);
    const Vec3 a{g[0],g[1],0},b{g[2],g[3],0};
    const double length=std::hypot(b.x-a.x,b.y-a.y);
    const Vec3 normal{-(b.y-a.y)/length,(b.x-a.x)/length,0};
    auto& body=context.physics.body(context.ids.p_camera_target);
    body.angles={};
    body.position=(a+b)*.5;
    const auto& group=context.group(context.ids.p_camera_target);
    assert(group.source_vertices.size()>=4);
    double low=0,high=0;
    for (std::uint32_t vertex=1;vertex<=4;++vertex) {
        const auto actual=context.physics.vec_for_vertex(context.ids.p_camera_target,vertex,context.visuals.at(context.ids.p_camera_target).sample);
        const Vec3 offset=actual-body.position;
        const double projection=offset.x*normal.x+offset.y*normal.y;
        if(vertex==1)low=high=projection;
        else { low=std::min(low,projection);high=std::max(high,projection); }
    }
    assert(high>low);
    // Clearance comes from the original model's published source vertices.
    assert(std::max(std::abs(low),std::abs(high))+16<500);
    return {body.position,normal,low,high};
}
GateObservation place(GameContext& context,int gate,const GatePose& pose,double offset) {
    context.physics.body(context.ids.p_camera_target).position=pose.center+pose.normal*offset;
    return observe_race_gate(context,gate);
}
void geometry(GameContext& context) {
    const auto saved=context.physics.body(context.ids.p_camera_target);
    RaceProgress progress;
    for(int gate=0;gate<10;++gate) {
        const auto pose=gate_pose(context,gate);
        auto observation=place(context,gate,pose,-pose.low+16);
        assert(observation.eligible&&observation.side_sum==4);
        assert(progress.observe(observation.eligible,observation.side_sum,gate*4+1).type==RaceEventType::none);
        assert(progress.previous_side==4);
        observation=place(context,gate,pose,2000);
        assert(!observation.eligible&&observation.side_sum==0);
        assert(progress.observe(observation.eligible,observation.side_sum,gate*4+2).type==RaceEventType::none);
        assert(progress.previous_side==0&&progress.next_gate==gate);
        observation=place(context,gate,pose,-pose.high-16);
        assert(observation.eligible&&observation.side_sum==-4);
        assert(progress.observe(observation.eligible,observation.side_sum,gate*4+3).type==RaceEventType::none);
        assert(progress.previous_side==-4); // Leaving eligibility cannot create a crossing.
        observation=place(context,gate,pose,-pose.low+16);
        assert(observation.eligible&&observation.side_sum==4);
        const auto event=progress.observe(observation.eligible,observation.side_sum,gate*4+4);
        if(gate==9) {
            assert(event.type==RaceEventType::finish&&progress.next_gate==0&&progress.previous_side==0);
        } else {
            assert(event.type==RaceEventType::checkpoint&&event.checkpoint==gate+1);
            assert(event.second_lap==(gate==4));
            assert(progress.split_ticks.at(gate)==gate*4+4);
        }
    }
    assert(progress.split_ticks[0]==4&&progress.split_ticks[8]==36);
    const auto pose=gate_pose(context,0);
    const auto straddling=place(context,0,pose,-(pose.low+pose.high)*.5);
    assert(straddling.eligible&&std::abs(straddling.side_sum)<4);
    assert(progress.observe(straddling.eligible,straddling.side_sum,40).type==RaceEventType::checkpoint);
    progress.reset();
    assert(progress.next_gate==0&&progress.previous_side==0&&progress.split_ticks[0]==40);
    context.physics.body(context.ids.p_camera_target)=saved;
}
void camera_and_clock(GameContext& context) {
    RaceCamera camera;
    camera.initialize(context);
    assert(context.state.camera_clip_far.raw==q(55000).raw);
    camera.reset(context);
    auto& state=context.state;
    const auto initial_mouse_mode=state.mouse_mode;
    state.phase=GamePhase::intro;
    state.mouse_mode=q(2);
    camera.update_early(context,{},FrameTiming{q(.5),0,0});
    assert(state.mouse_mode.raw==0&&state.phase==GamePhase::intro);
    state.mouse_mode=q(1);
    camera.update_early(context,{},FrameTiming{q(.5),1,1.0/32});
    assert(state.mouse_mode.raw==q(1).raw);
    state.mouse_mode=initial_mouse_mode;
    state.phase=GamePhase::countdown;
    camera.reset(context);
    auto& body=context.physics.body(context.ids.p_aracgovde);
    state.ghost_reset_ok=q(0);
    state.camera_tipno=q(3);
    camera.update_late(context,{},FrameTiming{q(.5),0,0});
    assert(state.t_oyun_zaman.raw==0&&!state.ghost_yaris_start);
    assert(camera.diagnostics().find("race_records=1 ")!=std::string::npos);
    camera.update_late(context,{},FrameTiming{q(.5),1,1.0/32});
    assert(state.t_oyun_zaman.raw==0&&state.camera_tipno.raw==q(3).raw);
    assert(state.hud_speed_visible&&!state.noparticle&&state.min_ghost_dist.raw==q(200).raw);
    assert(!state.bloom_noparticle);
    assert(context.visuals.at(context.ids.p_ic_panel).invisible&&context.visuals.at(context.ids.p_arac_kaporta).invisible);
    assert(camera.diagnostics().find("race_records=1 ")!=std::string::npos);
    camera.reset(context);
    state.phase=GamePhase::race;
    state.ghost_reset_ok=q(0);
    state.ghost_yaris_start=q(1);
    state.camera_tipno=q(2);
    state.bloom_aktif=q(1);
    state.selected_ghost=q(0);
    body.angles.x=179;
    const auto outside=gate_pose(context,0);
    assert(!place(context,0,outside,2000).eligible);
    InputSnapshot input;
    FrameTiming time{q(.5),0,0};
    auto step=[&] { camera.update_late(context,input,time);++time.frame_index;time.seconds_deadlines+=1.0/32; };
    step(); // Source camera wait(1), while the running race clock still advances.
    assert(state.t_oyun_zaman.raw==q(.5).raw);
    step();
    assert(state.camera_tipno.raw==q(2).raw&&state.noparticle&&!state.hud_speed_visible&&state.min_ghost_dist.raw==q(230).raw);
    assert(state.bloom_noparticle);
    assert(!context.visuals.at(context.ids.p_ic_panel).invisible&&context.visuals.at(context.ids.p_arac_kaporta).invisible);
    body.angles.x=-179;
    assert(c4::physics::var_ang(q(2)).raw==2047);
    assert(c4::physics::var_ang(q(-358)).raw==2053); // Signed16 truncation is direction-sensitive.
    step();
    assert(state.camera_angles.x.raw==185143); // Q10 gain applied to the wrapped raw2053 delta.
    same(state.bloom_camera_position,state.camera_position);
    same(state.bloom_camera_angles,state.camera_angles);
    assert(state.bloom_camera_arc.raw==state.camera_arc.raw);
    body.angles.x=179;
    assert(c4::physics::var_ang(Var{-1847}).raw==-1845);
    step();
    assert(state.camera_angles.x.raw==183483); // Signed16 reverse delta and Q10 multiplication.
    input.key_c=true;
    step();
    assert(state.camera_tipno.raw==q(3).raw&&!state.noparticle&&state.hud_speed_visible&&state.min_ghost_dist.raw==q(200).raw);
    assert(!state.bloom_noparticle);
    assert(context.visuals.at(context.ids.p_ic_panel).invisible);
    for(int n=0;n<3;++n)step();
    assert(state.camera_tipno.raw==q(3).raw); // Held C advances once, not once per frame.
    input.key_c=false;step();
    input.key_c=true;step();
    assert(state.camera_tipno.raw==0&&state.min_ghost_dist.raw==q(100).raw);
    assert(!state.noparticle&&!state.bloom_noparticle);
    assert(!context.visuals.at(context.ids.p_arac_kaporta).invisible);
    input.key_c=false;step();input.key_c=true;step();
    assert(state.camera_tipno.raw==q(1).raw);
    assert(!state.noparticle&&!state.bloom_noparticle);
    input.key_c=false;step();input.key_c=true;step();
    assert(state.camera_tipno.raw==q(2).raw);
    state.model_robot_ok=q(1);
    step();
    assert(state.camera_tipno.raw==0&&state.camrec_regno.raw==q(2).raw&&!state.noparticle&&state.hud_speed_visible);
    assert(!state.bloom_noparticle);
    step();
    assert(state.camera_tipno.raw==0&&state.camrec_regno.raw==q(2).raw); // C is ignored in robot form.
    input.key_c=false;
    state.model_robot_ok=q(0);
    step();
    assert(state.camera_tipno.raw==q(2).raw&&state.camrec_regno.raw==0&&state.noparticle&&!state.hud_speed_visible);
    assert(state.bloom_noparticle);
    assert(!context.visuals.at(context.ids.p_ic_panel).invisible&&context.visuals.at(context.ids.p_arac_kaporta).invisible);
    const auto paused_clock=state.t_oyun_zaman;
    const auto paused_position=state.camera_position,paused_angles=state.camera_angles;
    const auto paused_arc=state.camera_arc;
    state.phase=GamePhase::paused;
    state.freeze_mode=q(1);
    input.key_c=true;
    for(int n=0;n<3;++n) {
        time.seconds_deadlines+=100;
        camera.update_early(context,input,time);
        step();
    }
    assert(state.t_oyun_zaman.raw==paused_clock.raw&&state.camera_tipno.raw==q(2).raw);
    same(state.camera_position,paused_position);same(state.camera_angles,paused_angles);
    assert(state.camera_arc.raw==paused_arc.raw);
    state.freeze_mode=q(0);
    state.phase=GamePhase::race;
    input.key_c=false;
    step();
    assert(state.t_oyun_zaman.raw==(paused_clock+q(.5)).raw); // No paused wall-clock catch-up.
    const auto pose=gate_pose(context,0);
    const auto before=state.t_oyun_zaman;
    place(context,0,pose,-pose.low+16);step();
    assert(state.checkpoint_no.raw==0&&state.checkpoint_oldframe.raw==q(4).raw);
    place(context,0,pose,-pose.high-16);step();
    assert(state.checkpoint_no.raw==q(1).raw&&state.checkpoint_oldframe.raw==0);
    assert(state.t_point_zaman[0].raw==(before+q(1)).raw&&!state.game_over_ok);
    camera.reset(context);
    assert(state.t_oyun_zaman.raw==0&&state.race_lap.raw==q(1).raw&&!state.finish_buttons_visible);
    step();
    assert(state.t_oyun_zaman.raw==0);
}
void countdown_sharks(GameContext& context) {
    constexpr std::array<std::string_view,6> actions{
        "fish_m_shark1a","fish_m_shark1b","fish_m_shark1c",
        "fish_m_shark2a","fish_m_shark2b","fish_m_shark2c"};
    std::array<EntityId,6> sharks;
    sharks.fill(no_entity);
    for(EntityId id=0;id<context.world.entities.size();++id)
        for(std::size_t index=0;index<actions.size();++index)
            if(context.world.entities[id].action==actions[index])sharks[index]=id;
    for(const auto id:sharks)assert(id!=no_entity);
    RaceCamera camera;
    camera.initialize(context);
    FrameTiming time{q(.5),0,0};
    auto step=[&] {
        camera.update_early(context,{},time);
        ++time.frame_index;
        time.seconds_deadlines+=1.0/32;
    };
    auto green=[&] {
        camera.reset(context);
        for(int frame=0;frame<600&&!context.state.ghost_yaris_start;++frame)step();
        assert(context.state.ghost_yaris_start.raw==q(1).raw&&context.state.phase==GamePhase::race);
        assert(context.visuals.at(context.ids.p_zamanlama).skin==6);
    };
    auto expected_scales=[&] {
        const auto seed=context.state.rng_state;
        const auto calls=context.state.rng_calls;
        std::array<double,6> scales;
        for(auto& scale:scales)
            scale=c4::physics::number(q(7)*(q(100)+context.random(q(160)))/q(100));
        context.state.rng_state=seed;
        context.state.rng_calls=calls;
        return scales;
    };
    auto scales_match=[&](const auto& scales) {
        for(std::size_t index=0;index<sharks.size();++index)
            same(context.physics.body(sharks[index]).scale,{scales[index],scales[index],scales[index]});
    };
    green();
    auto calls=context.state.rng_calls;
    auto scales=expected_scales();
    for(int frame=0;frame<64&&context.state.rng_calls==calls;++frame)step();
    assert(context.state.rng_calls==calls+6);
    scales_match(scales); // Six ordered original fish actions, one isotropic Q10 scale each.
    for(int frame=0;frame<3;++frame)step();
    assert(context.state.rng_calls==calls+6);
    green();
    calls=context.state.rng_calls;
    scales=expected_scales();
    camera.reset(context); // Cancel the live post-green lift; its source tail still runs.
    step();
    assert(context.state.rng_calls==calls+6&&!context.state.ghost_yaris_start);
    scales_match(scales);
}
void actual_finish_hud_boundary(GameContext& context) {
    // A genuine finish runs real persistence. Keep its files away from user records.
    struct RecordSandbox {
        std::filesystem::path root;
        std::string previous;
        bool had_previous{};
        RecordSandbox() {
            const auto* old=std::getenv("XDG_DATA_HOME");
            had_previous=old!=nullptr;
            if(old)previous=old;
            char name[]="/tmp/c4-race-finish-XXXXXX";
            const auto* directory=::mkdtemp(name);
            assert(directory);
            root=directory;
            assert(::setenv("XDG_DATA_HOME",root.c_str(),1)==0);
        }
        ~RecordSandbox() {
            if(had_previous)assert(::setenv("XDG_DATA_HOME",previous.c_str(),1)==0);
            else assert(::unsetenv("XDG_DATA_HOME")==0);
            std::error_code error;
            std::filesystem::remove_all(root,error);
            assert(!error);
        }
    } sandbox;
    auto& state=context.state;
    const auto baseline=state.hud_off_event_counter;
    RaceCamera camera;
    camera.initialize(context);
    state.phase=GamePhase::countdown;
    camera.reset(context);
    const auto outside=gate_pose(context,0);
    assert(!place(context,0,outside,2000).eligible);
    FrameTiming time{q(.5),0,0};
    auto step=[&] {
        camera.update_early(context,{},time);
        camera.update_late(context,{},time);
        ++time.frame_index;
        time.seconds_deadlines+=1.0/32;
    };
    for(int frame=0;frame<600&&!state.ghost_yaris_start;++frame)step();
    assert(state.ghost_yaris_start.raw==q(1).raw&&state.phase==GamePhase::race);
    for(int gate=0;gate<10;++gate) {
        const auto pose=gate_pose(context,gate);
        const auto positive=place(context,gate,pose,-pose.low+16);
        assert(positive.eligible&&positive.side_sum==4);
        step();
        assert(!state.game_over_ok&&state.hud_off_event_counter==baseline);
        const auto negative=place(context,gate,pose,-pose.high-16);
        assert(negative.eligible&&negative.side_sum==-4);
        step();
        if(gate<9)assert(state.checkpoint_no.raw==q(gate+1).raw&&!state.game_over_ok);
    }
    assert(state.game_over_ok.raw==q(1).raw&&state.hud_off_event_counter==baseline);
    assert(!state.local_ghost_available||state.t_oyun_zaman<state.local_ghost_ticks);
    const double deadline=time.seconds_deadlines-1.0/32+3;
    while(time.seconds_deadlines<deadline) {
        step();
        assert(state.gameover_panel_acik.raw==0&&state.hud_off_event_counter==baseline);
    }
    step(); // Three real seconds open the panel, but do not turn the HUD off yet.
    assert(state.gameover_panel_acik.raw==q(1).raw&&state.hud_off_event_counter==baseline);
    step(); // First of the two source frame waits.
    assert(state.hud_off_event_counter==baseline);
    step(); // The actual oyunici_panel_off call, not a phase-derived approximation.
    assert(state.hud_off_event_counter==baseline+1&&!state.hud_visible&&!state.hud_speed_visible);
    assert(state.record_error.empty());
    const auto persisted=load_ghost(local_ghost_path());
    assert(persisted&&persisted->name==state.oyuncu_ismi&&persisted->record_ticks==state.t_oyun_zaman);
    for(int frame=0;frame<40;++frame) {
        step();
        assert(state.hud_off_event_counter==baseline+1);
    }
    assert(state.phase==GamePhase::results&&state.finish_buttons_visible);
    camera.reset(context);
    assert(state.hud_off_event_counter==baseline+1);
    step();
    assert(state.hud_off_event_counter==baseline+1);
}
} // namespace
int main(int argc,char** argv) {
    if(argc!=2) { std::cerr<<"usage: test_race_camera ORIGINAL_ASSET_ROOT\n";return 2; }
    source_time();
    projection();
    const std::filesystem::path root(argv[1]);
    const std::array<std::string_view,1> names{"bogazici.wmb"};
    const auto resources=c4::assets::resolve_resources(root/"C4.wrs",names);
    auto world=c4::assets::load_world(resources[0]);
    declared_sky(root, world);
    SharedState state;
    std::vector<VisualState> visuals;
    auto ids=bootstrap_game(world,root,state,visuals);
    c4::physics::PhysicsWorld physics(world);
    c4::audio::Audio audio(48000,c4::audio::Mode::Offline);
    GameRuntime runtime(world,physics,audio,state,ids,visuals,root);
    auto& context=runtime.context();
    geometry(context);
    camera_and_clock(context);
    countdown_sharks(context);
    actual_finish_hud_boundary(context);
    std::cout<<"original gate vertices, race eligibility, camera latch/restore and paused clock assertions passed\n";
}
