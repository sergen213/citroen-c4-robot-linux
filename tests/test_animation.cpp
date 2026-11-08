#include "Animation.hpp"
#include "Resources.hpp"
#include "core/render_scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Bytes = std::vector<std::uint8_t>;
using c4::assets::Vertex;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void u16(Bytes& b, std::uint16_t n) { b.push_back(n & 255); b.push_back(n >> 8); }
void u32(Bytes& b, std::uint32_t n) { for (int i = 0; i < 4; ++i) b.push_back((n >> (8*i)) & 255); }
void put16(Bytes& b, std::size_t at, std::uint16_t n) {
    b.at(at) = n & 255; b.at(at+1) = n >> 8;
}
void put32(Bytes& b, std::size_t at, std::uint32_t n) {
    for (int i = 0; i < 4; ++i) b.at(at+i) = (n >> (8*i)) & 255;
}
void f32(Bytes& b, float n) { u32(b, std::bit_cast<std::uint32_t>(n)); }
void label(Bytes& b, std::size_t at, const std::string& name) {
    require(name.size() < 16, "test label too long");
    std::copy(name.begin(), name.end(), b.begin()+at);
}
void save(const std::filesystem::path& p, const Bytes& b) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    require(bool(f), "cannot write animation test input");
}
template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid animation input accepted");
}
void near(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual-expected) <= 0.0001f, message);
}
void pose(const Vertex& actual, const Vertex& expected) {
    near(actual.x, expected.x, "sample x mismatch");
    near(actual.y, expected.y, "sample y mismatch");
    near(actual.z, expected.z, "sample z mismatch");
    near(actual.nx, expected.nx, "sample normal x mismatch");
    near(actual.ny, expected.ny, "sample normal y mismatch");
    near(actual.nz, expected.nz, "sample normal z mismatch");
}
// EnginePhysics: shipped acknex.dll vec_for_vertex 100502b5..100502fa
// subtracts next-first in x87, multiplies the FLOAT blend, adds first, with
// no intermediate FLOAT stores, then stores XYZ once as FLOAT. This oracle
// implements nominal 53-bit incoming x87 precision, not recovered steady game
// precision control: CreateDevice flags 0x54 omit FPU_PRESERVE and incoming
// control-word policy remains unresolved. Volatile separates the double
// product from addition so the nominal recipe cannot contract into an FMA.
float source_coordinate(float first, float next, float weight) {
    const double delta = static_cast<double>(next)-static_cast<double>(first);
    const volatile double product = delta*static_cast<double>(weight);
    const double result = static_cast<double>(first)+product;
    return static_cast<float>(result);
}
Vertex mix(const Vertex& a, const Vertex& b, float weight) {
    return {source_coordinate(a.x,b.x,weight), source_coordinate(a.y,b.y,weight), source_coordinate(a.z,b.z,weight),
            a.u, a.v, a.nx+(b.nx-a.nx)*weight, a.ny+(b.ny-a.ny)*weight,
            a.nz+(b.nz-a.nz)*weight};
}
void vertex(Bytes& b, const Vertex& v, std::uint16_t index) {
    f32(b,v.x); f32(b,v.y); f32(b,v.z); u16(b,index);
    f32(b,v.nx); f32(b,v.ny); f32(b,v.nz);
}
struct Temp {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("c4-animation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { require(std::filesystem::create_directory(dir), "animation test temp directory unavailable"); }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
struct Offsets {
    std::size_t group{}, external_skin{}, embedded_skin{}, uv{}, triangles{}, vertices{};
    std::array<std::size_t,5> frames{}, edits{};
};
struct Input { Bytes bytes; std::array<Offsets,2> offsets; };
Input mdl() {
    Input input; auto& b = input.bytes;
    b.insert(b.end(), {'M','D','L','7'});
    u32(b,0); u32(b,0); u32(b,2); u32(b,0); u32(b,0); u32(b,0);
    for (auto n : {36,28,16,68,8,16,26,26,68,24}) u16(b,static_cast<std::uint16_t>(n));
    for (std::size_t g=0; g<2; ++g) {
        auto& at = input.offsets[g]; const float shift = float(g)*1000;
        at.group = b.size(); b.resize(b.size()+44); b[at.group] = 1;
        label(b,at.group+8,g ? "second group" : "first group");
        for (auto [offset,count] : {std::pair{24,2}, {28,4}, {32,2}, {36,3}, {40,5}})
            put32(b,at.group+offset,count);
        at.external_skin = b.size(); b.resize(b.size()+28); b[at.external_skin] = 7;
        put32(b,at.external_skin+4,6); put32(b,at.external_skin+8,1);
        label(b,at.external_skin+12,"external skin");
        b.insert(b.end(), {'a','.','d','d','s',0});
        at.embedded_skin = b.size(); b.resize(b.size()+28); b[at.embedded_skin] = 0x14;
        put32(b,at.embedded_skin+4,1); put32(b,at.embedded_skin+8,1);
        label(b,at.embedded_skin+12,"embedded skin"); b.insert(b.end(), {17,34,51});
        for (int n=0;n<17;++n) f32(b,float(n)/16);
        at.uv = b.size();
        for (auto n : {0.f,0.f,1.f,0.f,0.f,1.f,0.25f,0.75f}) f32(b,n);
        at.triangles = b.size();
        for (auto n : {0,1,2,0,1,2}) u16(b,static_cast<std::uint16_t>(n)); u32(b,0);
        for (auto n : {0,2,1,3,2,1}) u16(b,static_cast<std::uint16_t>(n)); u32(b,1);
        at.vertices = b.size();
        vertex(b,{shift,0,0,0,0,0,0,1},0xffff);
        vertex(b,{shift+1,0,0,0,0,0,0,1},0xffff);
        vertex(b,{shift,1,0,0,0,0,0,1},0xffff);
        const std::array<std::string,5> names{"stand 0","stand 1","donus 0","donus 1","donus 2"};
        const std::array<Vertex,5> edits{{
            {shift+10,0,0,0,0,0,1,0}, {shift+20,0,0,0,0,1,0,0},
            {shift+100,0,0,0,0,1,0,0}, {shift+1,200,0,0,0,0,1,0},
            {shift+300,0,0,0,0,0,0,1}}};
        for (std::size_t frame=0;frame<5;++frame) {
            at.frames[frame] = b.size(); b.resize(b.size()+24); label(b,at.frames[frame],names[frame]);
            put32(b,at.frames[frame]+16,1); at.edits[frame] = b.size();
            vertex(b,edits[frame],frame==1 || frame==3 ? 1 : 0);
        }
        put32(b,at.group+4,static_cast<std::uint32_t>(b.size()-at.group));
    }
    put32(b,16,static_cast<std::uint32_t>(b.size())); return input;
}
void exact_morph_rounding(const c4::assets::ModelGroup& original) {
    struct Case {
        float weight;
        std::array<std::uint32_t,3> first, next, expected;
    };
    // Binary32 inputs found by scalar Python analysis; expected bits qualify
    // the nominal 53-bit incoming x87 recipe, not recovered steady game
    // precision control (CreateDevice flags 0x54 omit FPU_PRESERVE; incoming
    // policy is unresolved). All three axes differ from float-intermediate
    // XYZ. The last Z also distinguishes std::lerp's opposite-sign path
    // (0x40893b64) from both the nominal recipe and old float math.
    const std::array<Case,2> cases{{
        {0.25f, {0x41df49ba,0xc012d0e5,0x41b1126f}, {0x410c353f,0xc2a0570a,0xc2264396},
                {0x41b8fdf3,0xc1ae1a9f,0x40c6b021}}, // Old: 41b8fdf4 c1ae1aa0 40c6b020.
        {0.75f, {0x42b7bf7d,0xc0395810,0x4292d1ec}, {0x40ba45a2,0xc2c3ad91,0xc1960419},
                {0x41daac8b,0xc29434dd,0x40893b65}}  // Old: 41daac8c c29434dc 40893b60.
    }};
    auto group = original;
    group.frames[2].edits = group.frames[1].edits; // Both frames edit source 0, including its UV seam.
    auto& first = group.frames[1].edits[0].vertex;
    auto& next = group.frames[2].edits[0].vertex;
    auto output = group.mesh.vertices;
    for (const auto& test : cases) {
        first = {std::bit_cast<float>(test.first[0]),std::bit_cast<float>(test.first[1]),
                 std::bit_cast<float>(test.first[2]),0,0,0,0.5f,-0.5f};
        next = {std::bit_cast<float>(test.next[0]),std::bit_cast<float>(test.next[1]),
                std::bit_cast<float>(test.next[2]),0,0,1,-0.5f,0.5f};
        const auto expected = mix(first,next,test.weight);
        const std::array<float,3> expected_xyz{expected.x,expected.y,expected.z};
        for (std::size_t axis=0;axis<3;++axis)
            require(std::bit_cast<std::uint32_t>(expected_xyz[axis])==test.expected[axis],
                    "independent source coordinate oracle bits mismatch");
        const c4::assets::AnimationSample sample{1,2,test.weight};
        require(sample.frame_raw==0 && sample.next_frame_raw==0,
                "manual render sample fabricated entity frame properties");
        const auto source = c4::assets::sample_source_vertex(group,sample,0);
        require(c4::assets::sample_pose(group,sample,output), "exact morph not reported changed");
        const std::array<float,3> expected_normals{
            std::lerp(first.nx,next.nx,test.weight),std::lerp(first.ny,next.ny,test.weight),
            std::lerp(first.nz,next.nz,test.weight)};
        for (const auto& value : {source,output[0],output[3]}) {
            const std::array<float,3> xyz{value.x,value.y,value.z};
            const std::array<float,3> normals{value.nx,value.ny,value.nz};
            for (std::size_t axis=0;axis<3;++axis) {
                require(std::bit_cast<std::uint32_t>(xyz[axis])==test.expected[axis],
                        "source/split/seam exact morph coordinate bits mismatch");
                require(std::bit_cast<std::uint32_t>(normals[axis])==
                        std::bit_cast<std::uint32_t>(expected_normals[axis]),
                        "exact morph changed normal interpolation");
            }
        }
        require(std::bit_cast<std::uint32_t>(source.u)==std::bit_cast<std::uint32_t>(first.u) &&
                std::bit_cast<std::uint32_t>(source.v)==std::bit_cast<std::uint32_t>(first.v),
                "exact source morph changed UVs");
        for (std::size_t i=0;i<output.size();++i) {
            require(std::bit_cast<std::uint32_t>(output[i].u)==
                    std::bit_cast<std::uint32_t>(group.mesh.vertices[i].u) &&
                    std::bit_cast<std::uint32_t>(output[i].v)==
                    std::bit_cast<std::uint32_t>(group.mesh.vertices[i].v),
                    "exact split morph changed retained UVs");
            if (group.source_indices[i]!=0)
                require(std::bit_cast<std::array<std::uint32_t,8>>(output[i])==
                        std::bit_cast<std::array<std::uint32_t,8>>(group.mesh.vertices[i]),
                        "exact morph changed unedited source vertex");
        }
        require(!c4::assets::sample_pose(group,sample,output), "repeated exact morph reported changed");
    }
}
void sampling(const c4::assets::ModelGroup& group, float shift) {
    const auto indices = group.mesh.indices;
    require(indices==std::vector<std::uint32_t>({0,1,2,3,2,1}), "seam index topology mismatch");
    require(group.source_indices==std::vector<std::uint32_t>({0,1,2,0}), "seam source mapping mismatch");
    require(group.mesh.secondary_uv.size()==group.mesh.vertices.size(), "MDL7 seam UV1 missing");
    for (std::size_t i=0;i<group.mesh.vertices.size();++i)
        require(group.mesh.secondary_uv[i]==std::array<float,2>{group.mesh.vertices[i].u,group.mesh.vertices[i].v},
                "MDL7 UV1 lost split vertex UV0");
    require(group.mesh.secondary_uv[0]!=group.mesh.secondary_uv[3], "MDL7 UV1 seam was welded");
    std::vector<Vertex> output(group.mesh.vertices.size());
    require(c4::assets::sample_pose(group,{},output), "initial base pose not reported changed");
    require(!c4::assets::sample_pose(group,{},output), "identical base pose reported changed");
    for (std::uint32_t source=0;source<group.source_vertices.size();++source)
        pose(c4::assets::sample_source_vertex(group,{},source),group.source_vertices[source]);
    for (std::size_t i=0;i<output.size();++i)
        require(std::bit_cast<std::array<std::uint32_t,8>>(output[i])==
                std::bit_cast<std::array<std::uint32_t,8>>(group.mesh.vertices[i]),
                "default sample does not preserve source base and seam UVs");
    const auto base_sample = c4::assets::animation_sample(group,"_base",100,true);
    require(base_sample.first_frame==0 && base_sample.second_frame==0 && base_sample.blend==0 &&
            base_sample.frame_raw==1024 && base_sample.next_frame_raw==0,
            "engine baseline frame/properties mismatch");
    require(!c4::assets::sample_pose(group,base_sample,output), "named base differs from default sample");
    require(c4::assets::sample_pose(group,{1,1,0},output), "different pose not reported changed");
    require(c4::assets::sample_pose(group,base_sample,output), "named base did not reset edited pose");
    require(!c4::assets::sample_pose(group,{},output), "base reset differs from default sample");
    require(c4::assets::sample_pose(group,{1,1,0},output), "edited pose after reset not reported changed");
    require(!c4::assets::sample_pose(group,{1,1,0},output), "repeated different pose reported changed");
    auto identical_frames = group;
    identical_frames.frames[2].edits = identical_frames.frames[1].edits;
    c4::assets::sample_pose(identical_frames,{1,1,0},output);
    require(!c4::assets::sample_pose(identical_frames,{1,2,0.5f},output),
            "distinct sample states with identical vertices reported changed");
    auto& normal_edit = identical_frames.frames[2].edits[0].vertex;
    normal_edit.nx = 0.5f;
    require(c4::assets::sample_pose(identical_frames,{2,2,0},output),
            "normal-only edit not reported changed");
    near(output[0].nx,0.5f,"normal-only edit not written");
    near(output[3].nx,0.5f,"normal-only seam edit not written");
    require(!c4::assets::sample_pose(identical_frames,{2,2,0},output),
            "repeated normal-only pose reported changed");
    output[3].u = -1;
    output[3].v = -2;
    require(c4::assets::sample_pose(identical_frames,{2,2,0},output),
            "retained UV mismatch not reported changed");
    near(output[3].u,group.mesh.vertices[3].u,"retained u not restored");
    near(output[3].v,group.mesh.vertices[3].v,"retained v not restored");
    require(!c4::assets::sample_pose(identical_frames,{2,2,0},output),
            "restored UVs reported changed");
    auto sample = [&](const char* clip, float percentage, bool cycle) {
        c4::assets::sample_animation(group,clip,percentage,cycle,output);
        require(group.mesh.indices==indices, "animation changed indices");
        for (std::size_t i=0;i<output.size();++i) {
            near(output[i].u,group.mesh.vertices[i].u,"animation changed u");
            near(output[i].v,group.mesh.vertices[i].v,"animation changed v");
            require(group.mesh.secondary_uv[i]==std::array<float,2>{output[i].u,output[i].v},
                    "MDL7 posed UV1 differs from retained source UV0");
        }
        pose(output[0],output[3]); // Both corners of the UV seam follow the same source vertex.
        const auto boundary = c4::assets::animation_sample(group,clip,percentage,cycle);
        pose(c4::assets::sample_source_vertex(group,boundary,0),output[0]);
        pose(c4::assets::sample_source_vertex(group,boundary,1),output[1]);
    };
    auto boundary = [&](const char* clip, float percentage, bool cycle,
                        std::uint32_t first, std::uint32_t second, float blend,
                        std::int32_t next_frame_raw = 0) {
        const auto actual = c4::assets::animation_sample(group,clip,percentage,cycle);
        require(actual.first_frame==first && actual.second_frame==second, "frame boundary mismatch");
        near(actual.blend,blend,"frame interpolation weight mismatch");
        require(actual.frame_raw==static_cast<std::int32_t>((first+1)*1024)+
                                  static_cast<std::int32_t>(blend*1024),
                "original one-based Q10 FRAME mismatch");
        require(actual.next_frame_raw==next_frame_raw, "original explicit NEXT_FRAME mismatch");
    };
    boundary("DONUS",0,false,3,4,0);
    boundary("don",25,false,3,4,0.5f);
    boundary("donus",75,false,4,5,0.5f);
    boundary("donus",33.3333f,false,3,4,682.f/1024);
    boundary("donus",100,false,5,5,0);
    boundary("donus",150,false,5,5,0);
    boundary("donus",-10,false,3,4,0);
    boundary("donus",50,true,4,5,0.5f);
    boundary("donus",75,true,5,3,0.25f,4096);
    boundary("donus",100,true,3,4,0);
    boundary("donus",200,true,3,4,0);
    boundary("donus",-10,true,3,4,0);
    boundary("stand",100,false,2,2,0);
    boundary("stand",50,true,2,2,0); // Exact last coordinate is not a wrap.
    boundary("donus",0.0001f,false,3,4,0);
    boundary("donus 2",0,false,5,5,0);
    boundary("donus 2",50,true,5,5,0);
    boundary("donus 2",-10,true,5,5,0);
    const Vertex base0{shift,0,0,0,0,0,0,1}, base1{shift+1,0,0,0,0,0,0,1};
    const Vertex first{shift+100,0,0,0,0,1,0,0}, middle1{shift+1,200,0,0,0,0,1,0};
    const Vertex last{shift+300,0,0,0,0,0,0,1};
    sample("donus",0,false); pose(output[0],first); pose(output[1],base1);
    sample("donus",50,false); pose(output[0],base0); pose(output[1],middle1);
    sample("donus",100,false); pose(output[0],last); pose(output[1],base1);
    sample("donus",25,false); pose(output[0],mix(first,base0,0.5f)); pose(output[1],mix(base1,middle1,0.5f));
    sample("donus",50,true); pose(output[0],mix(base0,last,0.5f)); pose(output[1],mix(middle1,base1,0.5f));
    sample("donus",75,true); pose(output[0],mix(last,first,0.25f)); pose(output[1],base1);
    sample("donus",100,true); pose(output[0],first); pose(output[1],base1);
    sample("stand",100,false); pose(output[0],base0); pose(output[1],{shift+20,0,0,0,0,1,0,0});
    constexpr float transition_weight = 250.f/1024;
    auto transition = c4::assets::animation_transition(group,{3,4,0.5f},"stand",75,25);
    require(transition.first_frame==3 && transition.second_frame==1, "transition half tie or target floor mismatch");
    near(transition.blend,transition_weight,"transition Q10 literal blend mismatch");
    require(transition.frame_raw==4346 && transition.next_frame_raw==2048,
            "transition original FRAME/NEXT_FRAME mismatch");
    c4::assets::sample_pose(group,transition,output);
    pose(output[0],mix(first,{shift+10,0,0,0,0,0,1,0},transition_weight)); pose(output[1],base1);
    const auto subquantum_half = c4::assets::animation_transition(group,{3,4,0.5001f},"stand",75,25);
    require(subquantum_half.first_frame==3 && subquantum_half.second_frame==1 &&
            subquantum_half.frame_raw==4346 && subquantum_half.next_frame_raw==2048,
            "sub-Q10 half threshold must quantize to tie-down");
    near(subquantum_half.blend,transition_weight,"sub-Q10 half changed transition weight");
    require(!c4::assets::sample_pose(group,subquantum_half,output),
            "sub-Q10 half threshold changed retained pose");
    transition = c4::assets::animation_transition(group,{3,4,513.f/1024},"stand",75,25);
    require(transition.first_frame==4 && transition.second_frame==1, "transition nearest old frame mismatch");
    require(transition.frame_raw==5370 && transition.next_frame_raw==2048,
            "nearest transition original FRAME/NEXT_FRAME mismatch");
    c4::assets::sample_pose(group,transition,output);
    pose(output[0],mix(base0,{shift+10,0,0,0,0,0,1,0},transition_weight));
    pose(output[1],mix(middle1,base1,transition_weight));
    for (std::size_t i=0;i<output.size();++i) {
        near(output[i].u,group.mesh.vertices[i].u,"transition changed u");
        near(output[i].v,group.mesh.vertices[i].v,"transition changed v");
    }
    pose(output[0],output[3]);
    require(group.mesh.indices==indices,"transition changed indices");
    const auto full_transition = c4::assets::animation_transition(group,{3,4,0.5f},"stand",75,100);
    require(full_transition.first_frame==3 && full_transition.second_frame==1 &&
            full_transition.frame_raw==5096 && full_transition.next_frame_raw==2048,
            "100-percent source transition must retain Q10 .01 literal weight");
    near(full_transition.blend,1000.f/1024,"100-percent transition incorrectly normalized to one");
    rejects([&]{ c4::assets::animation_transition(group,{3,4,0.5f},"unknown clip",0,50); });
    rejects([&]{ c4::assets::animation_transition(group,{3,4,0.5f},"stand",0,-1); });
    rejects([&]{ c4::assets::animation_transition(group,{3,4,0.5f},"stand",0,101); });
    rejects([&]{ c4::assets::sample_pose(group,{6,0,0},output); });
    rejects([&]{ c4::assets::animation_sample(group,"unknown clip",0,false); });
    rejects([&]{ c4::assets::sample_animation(group,"unknown clip",0,false,output); });
    rejects([&]{ c4::assets::animation_sample(group,"donus",3000000,false); });
    rejects([&]{ c4::assets::animation_sample(group,"donus",-3000000,true); });
    rejects([&]{ c4::assets::sample_pose(group,{3,4,-0.1f},output); });
    rejects([&]{ c4::assets::sample_pose(group,{3,4,1.1f},output); });
    for (const float bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        rejects([&]{ c4::assets::animation_sample(group,"donus",bad,false); });
        rejects([&]{ c4::assets::sample_animation(group,"donus",bad,true,output); });
        rejects([&]{ c4::assets::animation_transition(group,{3,4,0.5f},"stand",0,bad); });
        rejects([&]{ c4::assets::sample_pose(group,{3,4,bad},output); });
    }
    rejects([&]{ c4::assets::sample_animation(group,"donus",0,false,std::span<Vertex>(output).first(1)); });
}
void shared_tangent_pose_cache(const c4::assets::ModelGroup& original) {
    using namespace c4::rendering;
    using c4::assets::TangentProvenance;
    auto group = original;
    group.mesh.tertiary_uv.assign(group.mesh.vertices.size(), {3.5f,-2.f,7.f,-0.25f});
    group.mesh.tangent_provenance = TangentProvenance::ImportedSource;
    const auto source_mesh = group.mesh;
    RenderScene scene;
    scene.animation = "stand";
    scene.posed_vertices.push_back(group.mesh.vertices);
    scene.material_indices = {{0,1,2}, {3,2,1}};
    for (std::uint32_t partition=0;partition<2;++partition) {
        MeshView view;
        view.vertices = scene.posed_vertices.front();
        view.indices = scene.material_indices[partition];
        view.tertiary_uv = group.mesh.tertiary_uv;
        view.tangent_provenance = group.mesh.tangent_provenance;
        view.tangent_binding = 0;
        scene.meshes.push_back(view);
        Instance instance;
        instance.mesh_index = partition;
        instance.source_group = 0;
        instance.source_skin = partition;
        instance.material_state.source_flags_known_mask = 0x10000;
        scene.instances.push_back(instance);
    }
    TangentBinding tangents;
    tangents.vertices = scene.posed_vertices.front();
    tangents.original_indices = group.mesh.indices;
    tangents.mesh_indices = {0,1};
    scene.tangent_bindings.push_back(std::move(tangents));
    PoseBinding binding;
    binding.group = &group;
    binding.mesh_indices = {0,1};
    binding.tangent_binding = 0;
    scene.pose_bindings.push_back(std::move(binding));
    const auto* vertices = scene.posed_vertices.front().data();
    auto& cache = scene.tangent_bindings.front();
    for (auto& instance : scene.instances) ensure_source_tangents(scene,instance);
    sample_selected_animation(scene,0);
    require(!cache.generated && cache.request_count==0 && cache.channel.empty(),
            "known-off material generated a tangent channel");
    for (const auto& view : scene.meshes)
        require(view.tertiary_uv.data()==group.mesh.tertiary_uv.data() &&
                view.tangent_provenance==TangentProvenance::ImportedSource &&
                std::equal(view.tertiary_uv.begin(),view.tertiary_uv.end(),source_mesh.tertiary_uv.begin()),
                "known-off posed material replaced imported raw tangents");
    const auto prior_revision = scene.meshes.front().pose_revision;
    scene.instances.front().material_state.source_flags = 0x10000;
    ensure_source_tangents(scene,scene.instances.front());
    std::vector<std::array<float,4>> expected;
    c4::assets::generate_source_tangents(scene.posed_vertices.front(),group.mesh.indices,expected);
    require(cache.generated && cache.request_count==1 && cache.channel==expected &&
            cache.original_indices.data()==group.mesh.indices.data(),
            "tangent request did not use the original full group");
    std::vector<std::array<float,4>> partition_channel;
    c4::assets::generate_source_tangents(scene.posed_vertices.front(),scene.material_indices.front(),partition_channel);
    require(partition_channel!=expected, "fixture cannot distinguish partition-local tangent generation");
    const auto* channel = cache.channel.data();
    auto consumers = [&](std::uint64_t revision) {
        for (const auto& view : scene.meshes)
            require(view.vertices.data()==vertices && view.tertiary_uv.data()==channel &&
                    view.tertiary_uv.size()==group.mesh.vertices.size() &&
                    view.tangent_provenance==TangentProvenance::SourceAlgorithmNativePort &&
                    view.pose_revision==revision &&
                    std::equal(view.tertiary_uv.begin(),view.tertiary_uv.end(),expected.begin()),
                    "material partitions do not share current posed tangent channel");
    };
    consumers(prior_revision+1);
    scene.instances.back().material_state.source_flags = 0x10000;
    ensure_source_tangents(scene,scene.instances.back());
    const auto initial_channel = cache.channel;
    const auto capacity = cache.channel.capacity();
    for (auto& instance : scene.instances) ensure_source_tangents(scene,instance);
    sample_selected_animation(scene,0);
    require(cache.request_count==2 && cache.channel.data()==channel &&
            cache.channel.capacity()==capacity && cache.channel==initial_channel &&
            scene.posed_vertices.front().data()==vertices,
            "unchanged request or sample replaced cached buffers");
    consumers(prior_revision+1);
    sample_selected_animation(scene,100);
    near(scene.meshes.front().vertices[1].x,20,"changed scene pose not visible to partition consumer");
    near(scene.meshes.back().vertices[1].nx,1,"changed scene normal not visible to partition consumer");
    c4::assets::generate_source_tangents(scene.posed_vertices.front(),group.mesh.indices,expected);
    require(expected!=initial_channel && cache.channel==expected && cache.channel.data()==channel,
            "changed pose did not regenerate retained full-group tangent channel");
    consumers(prior_revision+2);
    sample_selected_animation(scene,100);
    for (auto& instance : scene.instances) ensure_source_tangents(scene,instance);
    require(cache.request_count==2 && cache.channel.data()==channel && cache.channel.capacity()==capacity,
            "repeated changed pose replaced tangent cache");
    consumers(prior_revision+2);
    for (auto& instance : scene.instances) {
        instance.material_state.source_flags = 0;
        ensure_source_tangents(scene,instance);
    }
    sample_selected_animation(scene,0);
    require(cache.request_count==0 && !cache.generated,
            "unrequested changed pose left generated tangent cache valid");
    scene.instances.front().material_state.source_flags = 0x10000;
    ensure_source_tangents(scene,scene.instances.front());
    c4::assets::generate_source_tangents(scene.posed_vertices.front(),group.mesh.indices,expected);
    require(cache.request_count==1 && cache.generated && cache.channel==expected &&
            cache.channel.data()==channel && cache.channel.capacity()==capacity,
            "reactivated tangent request reused stale pose channel");
    consumers(prior_revision+4);
    ensure_source_tangents(scene,scene.instances.front());
    sample_selected_animation(scene,0);
    require(cache.request_count==1 && cache.channel.data()==channel,
            "unchanged reactivated request replaced tangent channel");
    consumers(prior_revision+4);
    require(group.mesh.indices==source_mesh.indices &&
            group.mesh.tertiary_uv==source_mesh.tertiary_uv &&
            group.mesh.tangent_provenance==source_mesh.tangent_provenance,
            "scene tangent generation mutated source mesh channels or topology");
    for (std::size_t i=0;i<group.mesh.vertices.size();++i)
        require(std::bit_cast<std::array<std::uint32_t,8>>(group.mesh.vertices[i])==
                std::bit_cast<std::array<std::uint32_t,8>>(source_mesh.vertices[i]),
                "scene pose or tangent generation mutated immutable source vertices");
}
void behavior() {
    require(!c4::assets::Model{}.source_no_linked_brightness_known,
            "manual model invented ordinary disk provenance");
    const auto input = mdl(); Temp temp; const auto path = temp.dir/"synthetic.mdl";
    save(path,input.bytes); const auto model = c4::assets::load_model(path);
    require(model.source_no_linked_brightness_known, "loaded MDL7 ordinary disk provenance lost");
    require(model.groups.size()==2, "multiple groups lost");
    shared_tangent_pose_cache(model.groups.front());
    require(model.entity_data.empty() && model.editor_data.empty(), "invented model metadata");
    constexpr std::array<std::string_view,5> stored_names{"stand 0","stand 1","donus 0","donus 1","donus 2"};
    for (std::size_t g=0;g<2;++g) {
        const auto& group = model.groups[g];
        require(group.name==(g ? "second group" : "first group"), "group label lost");
        require(group.skins.size()==2, "multiple skins lost");
        const auto& external = group.skins[0]; const auto& embedded = group.skins[1];
        require(external.format==7 && external.width==6 && external.height==1 &&
                external.name=="external skin" && external.external_texture=="a.dds", "external skin mismatch");
        require(embedded.format==4 && embedded.flags==0x14 && embedded.width==1 && embedded.height==1 &&
                embedded.name=="embedded skin" && embedded.payload==Bytes({17,34,51}), "embedded skin mismatch");
        require(embedded.material.size()==17, "material lost");
        for (std::size_t i=0;i<17;++i) near(embedded.material[i],float(i)/16,"material component mismatch");
        require(group.triangle_materials==std::vector<std::uint32_t>({0,1}), "triangle skin assignment mismatch");
        require(group.source_vertices.size()==3 && group.mesh.vertices.size()==4, "seam vertex count mismatch");
        require(group.frames.size()==6 && group.frames[0].name=="_base" && group.frames[0].edits.empty(),
                "first-edited MDL7 lost engine baseline");
        for (std::size_t frame=0;frame<stored_names.size();++frame)
            require(group.frames[frame+1].name==stored_names[frame],"original stored frame label changed");
        require(group.frames[3].edits.size()==1 && group.frames[3].edits[0].index==0,
                "sparse frame edits lost");
        require(group.clips.size()==3 && group.clips[0].name=="_base" &&
                group.clips[0].first_frame==0 && group.clips[0].frame_count==1 &&
                group.clips[1].name=="stand" && group.clips[1].first_frame==1 && group.clips[1].frame_count==2 &&
                group.clips[2].name=="donus" && group.clips[2].first_frame==3 && group.clips[2].frame_count==3,
                "runtime clip runs lost");
        near(group.mesh.vertices[3].u,0.25f,"seam u mismatch"); near(group.mesh.vertices[3].v,0.75f,"seam v mismatch");
        sampling(group,float(g)*1000);
        exact_morph_rounding(group);
    }
    auto base_prefix_bytes = input.bytes;
    for (const auto& at : input.offsets) {
        std::fill_n(base_prefix_bytes.begin()+at.frames[0],16,0);
        label(base_prefix_bytes,at.frames[0],"_base1");
    }
    const auto base_prefix_model=c4::assets::decode_model(base_prefix_bytes);
    require(base_prefix_model.source_no_linked_brightness_known, "decoded MDL7 ordinary disk provenance lost");
    for (const auto& group : base_prefix_model.groups) {
        require(group.frames.size()==6 && group.frames[0].name=="_base" && group.frames[1].name=="_base1",
                "baseline-prefixed stored frame label changed");
        require(group.clips[0].name=="_base" && group.clips[0].first_frame==0 &&
                group.clips[0].frame_count==2,"contiguous baseline-prefixed clip did not merge");
        const auto sample=c4::assets::animation_sample(group,"_base",100,false);
        require(sample.first_frame==1 && sample.second_frame==1 && sample.blend==0 &&
                sample.frame_raw==2048 && sample.next_frame_raw==0,
                "baseline-prefixed clip endpoint/properties mismatch");
    }
    auto base_alias_bytes = input.bytes;
    for (std::size_t g=input.offsets.size();g-- > 0;) {
        const auto& at=input.offsets[g];
        put32(base_alias_bytes,at.frames[0]+16,0);
        base_alias_bytes.erase(base_alias_bytes.begin()+at.edits[0],base_alias_bytes.begin()+at.edits[0]+26);
        const auto group_end=g+1<input.offsets.size() ? input.offsets[g+1].group : input.bytes.size();
        put32(base_alias_bytes,at.group+4,static_cast<std::uint32_t>(group_end-at.group-26));
    }
    put32(base_alias_bytes,16,static_cast<std::uint32_t>(base_alias_bytes.size()));
    const auto base_alias_model=c4::assets::decode_model(base_alias_bytes);
    for (const auto& group : base_alias_model.groups) {
        require(group.frames.size()==5 && group.frames[0].name=="stand 0" && group.frames[0].edits.empty(),
                "empty first disk frame did not alias engine baseline");
        require(group.clips.size()==2 && group.clips[0].first_frame==0 && group.clips[0].frame_count==2 &&
                group.clips[1].first_frame==2 && group.clips[1].frame_count==3,
                "base-alias MDL7 clip runs changed");
        for (std::uint32_t source=0;source<group.source_vertices.size();++source)
            pose(c4::assets::sample_source_vertex(group,{},source),group.source_vertices[source]);
    }
    // Exercise the public file path for every strict prefix, including both group boundaries.
    for (std::size_t n=0;n<input.bytes.size();++n) {
        save(path,Bytes(input.bytes.begin(),input.bytes.begin()+n));
        rejects([&]{ c4::assets::load_model(path); });
    }
    auto bad32 = [&](std::size_t offset, std::uint32_t value) {
        auto b = input.bytes; put32(b,offset,value); save(path,b); rejects([&]{ c4::assets::load_model(path); });
    };
    auto bad16 = [&](std::size_t offset, std::uint16_t value) {
        auto b = input.bytes; put16(b,offset,value); save(path,b); rejects([&]{ c4::assets::load_model(path); });
    };
    const auto& at = input.offsets[0];
    bad32(8,1); // Unsupported skeleton cannot be silently discarded.
    bad32(16,static_cast<std::uint32_t>(input.bytes.size()-1));
    bad32(at.group+4,43); bad32(at.group+4,0xffffffff);
    bad32(at.group+24,0xffffffff); bad32(at.group+40,0xffffffff);
    bad16(at.triangles,3); bad16(at.triangles+6,4); bad32(at.triangles+12,2);
    bad16(at.vertices+12,0); bad16(at.edits[2]+12,3);
    bad32(at.frames[2]+16,0xffffffff); bad32(at.frames[2]+20,1);
    bad32(at.external_skin+4,0xffffffff); bad32(at.external_skin+8,0xffffffff);
    bad32(at.embedded_skin+4,0xffffffff);
    const auto nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    bad32(at.uv,nan); bad32(at.vertices,nan); bad32(at.vertices+14,nan);
    bad32(at.edits[2],nan); bad32(at.edits[2]+14,nan); bad32(at.embedded_skin+31,nan);
    for (const auto offset : {at.group+1,at.group+2}) {
        auto b=input.bytes; b[offset]=1; save(path,b); rejects([&]{ c4::assets::load_model(path); });
    }
    auto duplicate=input.bytes;
    const Bytes replacement(input.bytes.begin()+at.edits[2],input.bytes.begin()+at.edits[2]+26);
    duplicate.insert(duplicate.begin()+at.edits[2]+26,replacement.begin(),replacement.end());
    put32(duplicate,at.frames[2]+16,2);
    put32(duplicate,at.group+4,static_cast<std::uint32_t>(input.offsets[1].group-at.group+26));
    put32(duplicate,16,static_cast<std::uint32_t>(duplicate.size()));
    save(path,duplicate); rejects([&]{ c4::assets::load_model(path); });
    auto unsupported_skin=input.bytes; unsupported_skin[at.external_skin] |= 0xc0;
    save(path,unsupported_skin); rejects([&]{ c4::assets::load_model(path); });
    auto empty_skin=input.bytes; empty_skin[at.external_skin+28]=0;
    save(path,empty_skin); rejects([&]{ c4::assets::load_model(path); });
    auto trailing=input.bytes; trailing.push_back(0); save(path,trailing);
    rejects([&]{ c4::assets::load_model(path); });
    rejects([&]{ c4::assets::load_model(temp.dir/"missing.mdl"); });
}
// Independent original-file oracle: these offsets/counts are measured from the
// original c4_robot_kaporta.mdl, not obtained from load_model or its frame edits.
std::uint16_t raw16(const Bytes& b, std::size_t offset) {
    return std::uint16_t(b.at(offset)) | std::uint16_t(b.at(offset+1))<<8;
}
std::uint32_t raw32(const Bytes& b, std::size_t offset) {
    std::uint32_t n=0; for (int i=0;i<4;++i) n |= std::uint32_t(b.at(offset+i))<<(8*i); return n;
}
float raw_float(const Bytes& b, std::size_t offset) { return std::bit_cast<float>(raw32(b,offset)); }
Vertex raw_vertex(const Bytes& b, std::size_t offset) {
    return {raw_float(b,offset),raw_float(b,offset+4),raw_float(b,offset+8),0,0,
            raw_float(b,offset+14),raw_float(b,offset+18),raw_float(b,offset+22)};
}
void original(const std::filesystem::path& path) {
    const auto bytes = c4::assets::read_resource(path);
    require(bytes.size()==14914144 && bytes.at(0)=='M' && bytes.at(1)=='D' && bytes.at(2)=='L' && bytes.at(3)=='7',
            "optional input is not original c4_robot_kaporta.mdl");
    require(raw32(bytes,12)==1 && raw32(bytes,48+36)==3489 && raw32(bytes,48+32)==3778 &&
            raw32(bytes,48+28)==7908 && raw32(bytes,48+40)==163, "original model counts mismatch");
    require(raw16(bytes,40)==26 && raw16(bytes,42)==26 && raw16(bytes,46)==24,
            "original vertex/frame strides mismatch");
    std::vector<Vertex> base; base.reserve(3489);
    for (std::size_t i=0;i<3489;++i) base.push_back(raw_vertex(bytes,123850+i*26));
    struct RawClip { std::string name; std::uint32_t first{}, count{}; };
    std::vector<RawClip> clips;
    std::array<std::vector<Vertex>,163> frames;
    std::size_t offset=214564;
    for (std::size_t frame=0;frame<frames.size();++frame) {
        std::string name;
        for (std::size_t c=0;c<16 && bytes.at(offset+c)!=0;++c)
            name.push_back(static_cast<char>(bytes.at(offset+c)));
        require(!name.empty(),"original frame label empty");
        const auto end=name.size();
        while (!name.empty() && name.back()>='0' && name.back()<='9') name.pop_back();
        if (name.size()!=end) while (!name.empty() && name.back()==' ') name.pop_back();
        require(!name.empty(),"original numeric clip prefix empty");
        if (clips.empty() || clips.back().name!=name)
            clips.push_back({name,static_cast<std::uint32_t>(frame),0});
        ++clips.back().count;
        const auto edits=raw32(bytes,offset+16), bones=raw32(bytes,offset+20);
        require(bones==0,"original animation has unsupported bones");
        if (frame==0)
            require(edits==0,"original first disk frame must alias runtime baseline, not prepend _base");
        frames[frame]=base; offset+=24;
        for (std::uint32_t edit=0;edit<edits;++edit,offset+=26) {
            const auto index=raw16(bytes,offset+12); require(index<base.size(),"original raw frame index mismatch");
            frames[frame][index]=raw_vertex(bytes,offset);
        }
    }
    require(offset==bytes.size(),"original frame records do not fill file");
    const auto model=c4::assets::load_model(path); require(model.groups.size()==1,"original group count lost");
    require(model.source_no_linked_brightness_known, "original MDL loader provenance lost");
    const auto& group=model.groups[0]; const auto indices=group.mesh.indices;
    require(indices.size()==3778*3 && group.mesh.vertices.size()==3954,"original seam topology mismatch");
    require(group.mesh.secondary_uv.size()==group.mesh.vertices.size(), "original split vertex UV1 missing");
    for (std::size_t i=0;i<group.mesh.vertices.size();++i)
        require(group.mesh.secondary_uv[i]==std::array<float,2>{group.mesh.vertices[i].u,group.mesh.vertices[i].v},
                "original UV1 differs from split vertex UV0");
    require(group.frames.size()==frames.size(),"base-alias original runtime frame count changed");
    require(group.clips.size()==clips.size(),"original discovered clip count mismatch");
    for (std::size_t i=0;i<clips.size();++i)
        require(group.clips[i].name==clips[i].name && group.clips[i].first_frame==clips[i].first &&
                group.clips[i].frame_count==clips[i].count,"original discovered clip range mismatch");
    std::vector<Vertex> output(group.mesh.vertices.size());
    auto check_pose = [&](std::size_t a, std::size_t b, float weight) {
        require(group.mesh.indices==indices,"original animation changed indices");
        for (std::size_t triangle=0;triangle<3778;++triangle) {
            for (std::size_t corner=0;corner<3;++corner) {
                const auto raw=63402+triangle*16;
                const auto source=raw16(bytes,raw+corner*2), uv=raw16(bytes,raw+6+corner*2);
                require(source<base.size() && uv<7908,"original raw topology invalid");
                const auto index=indices.at(triangle*3+corner); require(index<output.size(),"original output index invalid");
                const auto expected=weight==0 ? frames[a][source] : mix(frames[a][source],frames[b][source],weight);
                pose(output[index],expected);
                const std::array<float,3> actual_xyz{output[index].x,output[index].y,output[index].z};
                const std::array<float,3> expected_xyz{expected.x,expected.y,expected.z};
                for (std::size_t axis=0;axis<3;++axis)
                    require(std::bit_cast<std::uint32_t>(actual_xyz[axis])==
                            std::bit_cast<std::uint32_t>(expected_xyz[axis]),
                            "original raw-pose coordinate bits mismatch for nominal 53-bit recipe");
                near(output[index].u,raw_float(bytes,138+uv*8),"original u changed");
                near(output[index].v,raw_float(bytes,138+uv*8+4),"original v changed");
                require(group.mesh.secondary_uv[index]==std::array<float,2>{
                            raw_float(bytes,138+uv*8),raw_float(bytes,138+uv*8+4)},
                        "original posed UV1 differs from authored triangle corner UV0");
            }
        }
    };
    // At 0/50/75/100 percentages, the recovered 22.10 fixed-point
    // calculation is exact integer arithmetic: 1024 ticks per frame; cyclic
    // sampling spans count intervals, noncyclic sampling spans count-1.
    // This oracle uses raw label ranges, never the loader's clip/frame tables.
    auto raw_boundary = [](const RawClip& clip, unsigned percentage, bool cycle) {
        const auto last=clip.first+clip.count-1;
        if (clip.count==1) return c4::assets::AnimationSample{
            clip.first,clip.first,0,static_cast<std::int32_t>((clip.first+1)*1024),0};
        const auto fixed_percentage=cycle && percentage==100 ? 0u : percentage*1024;
        const auto distance=clip.count-(cycle ? 0u : 1u);
        const auto coordinate=clip.first*1024+fixed_percentage*distance/100;
        const auto first=coordinate/1024;
        const bool wrap=cycle && coordinate>last*1024;
        const auto second=wrap ? clip.first : std::min(first+1,last);
        return c4::assets::AnimationSample{first,second,float(coordinate%1024)/1024,
            static_cast<std::int32_t>(coordinate+1024),
            wrap ? static_cast<std::int32_t>((clip.first+1)*1024) : 0};
    };
    for (const auto& clip : clips) {
        for (const bool cycle : {false,true}) {
            for (const unsigned percentage : {0u,50u,75u,100u}) {
                const auto expected=raw_boundary(clip,percentage,cycle);
                const auto actual=c4::assets::animation_sample(group,clip.name,float(percentage),cycle);
                require(actual.first_frame==expected.first_frame && actual.second_frame==expected.second_frame,
                        "original all-clip frame boundary mismatch");
                near(actual.blend,expected.blend,"original all-clip fixed-point blend mismatch");
                require(actual.frame_raw==expected.frame_raw && actual.next_frame_raw==expected.next_frame_raw,
                        "original all-clip FRAME/NEXT_FRAME mismatch");
                c4::assets::sample_animation(group,clip.name,float(percentage),cycle,output);
                check_pose(expected.first_frame,expected.second_frame,expected.blend);
            }
        }
    }
    // Every adjacent discovered clip pair, including final->first, exercises
    // the old-frame Q10 half tie, subquantum tie-down, next tick, and target floor.
    // Source .01 is raw 10: independently truncate the FLOAT input to Q10,
    // multiply raw integers, then shift; this is not percentage / 100.
    for (std::size_t i=0;i<clips.size();++i) {
        const auto previous=raw_boundary(clips[i],50,false);
        const auto& target=clips[(i+1)%clips.size()];
        const auto target_frame=raw_boundary(target,50,false).first_frame;
        for (const float old_blend : {0.5f,0.5001f,513.f/1024}) {
            const auto old_fraction_raw=static_cast<std::int32_t>(static_cast<double>(old_blend)*1024);
            const auto source=old_fraction_raw>512 ? previous.second_frame : previous.first_frame;
            for (const float percentage : {0.f,25.f,33.3333f,50.f,100.f}) {
                const auto input_raw=static_cast<std::int64_t>(static_cast<double>(percentage)*1024);
                const auto blend_raw=static_cast<std::int32_t>((input_raw*10)>>10);
                const auto weight=float(blend_raw)/1024;
                const auto actual=c4::assets::animation_transition(group,
                    {previous.first_frame,previous.second_frame,old_blend},target.name,50,percentage);
                require(actual.first_frame==source && actual.second_frame==target_frame,
                        "original all-clip transition snapping mismatch");
                near(actual.blend,weight,"original all-clip Q10 transition weight mismatch");
                require(actual.frame_raw==static_cast<std::int32_t>((source+1)*1024)+blend_raw &&
                        actual.next_frame_raw==static_cast<std::int32_t>((target_frame+1)*1024),
                        "original all-clip transition FRAME/NEXT_FRAME mismatch");
                c4::assets::sample_pose(group,actual,output);
                check_pose(source,target_frame,weight);
            }
        }
    }
    std::cout << "original " << clips.size()
              << " clips: endpoints, midpoints, cycles, adjacent transitions and all triangle corners checked\n";
}
Bytes legacy_mdl() {
    Bytes b(84); b[0]='M'; b[1]='D'; b[2]='L'; b[3]='4';
    for (auto [offset,value] : {std::pair{8,2.f}, {12,3.f}, {16,0.5f},
                               {20,-10.f}, {24,20.f}, {28,-5.f}})
        put32(b,offset,std::bit_cast<std::uint32_t>(value));
    for (auto [offset,count] : {std::pair{48,1}, {52,8}, {56,4}, {60,3},
                               {64,2}, {68,2}, {72,4}}) put32(b,offset,count);
    u32(b,2); b.resize(b.size()+64,0x42); // RGB565 8x4 skin.
    for (auto n : {-2,1,7,0,0,3,12,-4}) u16(b,static_cast<std::uint16_t>(n));
    for (auto n : {0,1,2,0,1,2,0,2,1,3,2,1}) u16(b,static_cast<std::uint16_t>(n));
    for (unsigned frame=0;frame<2;++frame) {
        u32(b,2); b.resize(b.size()+16); // Packed bounds are not vertex normals.
        const auto at=b.size(); b.resize(b.size()+16); label(b,at,frame?"walk1":"walk0");
        for (unsigned source=0;source<3;++source) {
            u16(b,static_cast<std::uint16_t>(source+frame*10));
            u16(b,static_cast<std::uint16_t>(source+2)); u16(b,static_cast<std::uint16_t>(source+4));
            b.push_back(static_cast<std::uint8_t>(source==0?1:5)); b.push_back(0);
        }
    }
    return b;
}
void legacy_decoding() {
    const auto bytes=legacy_mdl(); const auto model=c4::assets::decode_model(bytes);
    require(model.source_no_linked_brightness_known, "decoded MDL4 ordinary disk provenance lost");
    require(model.groups.size()==1,"legacy group count"); const auto& g=model.groups[0];
    require(g.name.empty() && g.skins.size()==1 && g.skins[0].format==2 &&
            g.skins[0].width==8 && g.skins[0].height==4 && g.skins[0].payload.size()==64,
            "legacy skin metadata");
    require(g.frames.size()==2 && g.frames[0].name=="walk0" && g.frames[1].name=="walk1",
            "legacy frame labels");
    require(g.source_indices==std::vector<std::uint32_t>({0,1,2,0}) &&
            g.mesh.indices==std::vector<std::uint32_t>({0,1,2,3,2,1}),"legacy UV seam topology");
    near(g.mesh.vertices[0].x,-10,"legacy translated x");
    near(g.mesh.vertices[0].y,26,"legacy scaled y"); near(g.mesh.vertices[0].z,-3,"legacy scaled z");
    near(g.mesh.vertices[0].u,-0.25f,"legacy signed U"); near(g.mesh.vertices[0].v,0.25f,"legacy V");
    near(g.mesh.vertices[3].u,1.5f,"legacy wrapped U"); near(g.mesh.vertices[3].v,-1,"legacy signed V");
    require(g.mesh.secondary_uv.size()==g.mesh.vertices.size(), "MDL4 seam UV1 missing");
    for (std::size_t i=0;i<g.mesh.vertices.size();++i)
        require(g.mesh.secondary_uv[i]==std::array<float,2>{g.mesh.vertices[i].u,g.mesh.vertices[i].v},
                "MDL4 UV1 lost split vertex UV0");
    require(g.mesh.secondary_uv[0]!=g.mesh.secondary_uv[3], "MDL4 UV1 seam was welded");
    near(g.source_vertices[0].nx,-0.442863f,"legacy indexed normal x");
    near(g.source_vertices[0].ny,0.238856f,"legacy indexed normal y");
    near(g.source_vertices[0].nz,0.864188f,"legacy indexed normal z");
    std::vector<Vertex> output(g.mesh.vertices.size()); c4::assets::sample_pose(g,{1,1,0},output);
    near(output[0].x,10,"legacy frame coordinate"); pose(output[0],output[3]);
    near(output[3].u,1.5f,"legacy seam animation U");
    for (std::size_t i=0;i<output.size();++i)
        require(g.mesh.secondary_uv[i]==std::array<float,2>{output[i].u,output[i].v},
                "MDL4 posed UV1 differs from retained source UV0");
    for (std::size_t n=0;n<bytes.size();++n)
        rejects([&]{c4::assets::decode_model(std::span<const std::uint8_t>(bytes).first(n));});
    auto bad=bytes; put16(bad,168,3); rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; put16(bad,174,4); rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; bad[234]=162; rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; put32(bad,84,0); rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; put32(bad,192,0); rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; put32(bad,68,0); rejects([&]{c4::assets::decode_model(bad);});
    bad=bytes; bad.push_back(0); rejects([&]{c4::assets::decode_model(bad);});
}
void donus_playback() {
    using c4::assets::advance_donus_percentage;
    // Independent raw-integer oracle: SDK Q10 literals truncate float*1024.
    // 6.25=6400, .7=716, .925=947, 86.666=88745, 100=102400.
    // Positive tick multiplication shifts ten bits after integer division.
    auto expected = [](float percentage, float ticks) {
        const auto p=static_cast<std::int64_t>(static_cast<double>(percentage)*1024);
        const auto t=static_cast<std::int64_t>(static_cast<double>(ticks)*1024);
        if (p>102400) return float(p)/1024;
        const std::int64_t rate=(std::int64_t{6400}*1024)/(p<88745 ? 716 : 947);
        return float(p+((rate*t)>>10))/1024;
    };
    near(advance_donus_percentage(0,0),0,"donus initial pose must precede advance");
    near(advance_donus_percentage(0,1),expected(0,1),"donus initial Q10 tick rate");
    near(advance_donus_percentage(0,0.5f),expected(0,0.5f),"donus fractional Q10 tick rate");
    near(advance_donus_percentage(86.665f,1),expected(86.665f,1),"donus below-threshold Q10 rate");
    near(advance_donus_percentage(86.666f,1),expected(86.666f,1),"donus exact-threshold Q10 rate");
    near(advance_donus_percentage(90,1),expected(90,1),"donus late Q10 tick rate");
    const auto crossed=advance_donus_percentage(99,1);
    near(crossed,expected(99,1),"donus crossing must not clamp to 100");
    require(crossed>100,"donus completion requires crossing 100");
    near(advance_donus_percentage(crossed,1),crossed,"finished donus must stay unchanged");
    require(advance_donus_percentage(100,1)>100,"exactly 100 is not yet finished donus");
    near(advance_donus_percentage(90,0),90,"zero ticks changed donus percentage");
    near(advance_donus_percentage(0,0.333333f),expected(0,0.333333f),"donus Q10 tick quantization");
    near(advance_donus_percentage(102.333333f,1),expected(102.333333f,1),"finished donus Q10 input quantization");
    const auto sub_tick_above_100=std::nextafter(100.f,101.f);
    near(advance_donus_percentage(sub_tick_above_100,1),expected(sub_tick_above_100,1),
         "sub-Q10 crossing quantizes to active 100");
    near(advance_donus_percentage(100.f+1.f/1024,1),100.f+1.f/1024,
         "first Q10 tick above 100 must stay finished");
    rejects([&]{ advance_donus_percentage(0,-1); });
    for (const float bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        rejects([&]{ advance_donus_percentage(bad,1); });
        rejects([&]{ advance_donus_percentage(0,bad); });
    }
}
// Authored blob shadow (c4_arac_golge.mdl: 265x550 ARGB skin, alpha flag, no WDL
// TRANSPARENT). acknex 10061070 scales stored UVs into the padded upload domain, and
// 0x100552f0 draws such alpha-skin entities after the opaque list, far first.
void source_blob_shadow_draw() {
    using namespace c4::rendering;
    Bytes b{'M','D','L','7'};
    u32(b,0); u32(b,0); u32(b,1); u32(b,0); u32(b,0); u32(b,0);
    for (auto n : {36,28,16,68,8,16,26,26,68,24}) u16(b,static_cast<std::uint16_t>(n));
    const auto group = b.size(); b.resize(b.size()+44); b[group] = 1;
    for (auto [offset,count] : {std::pair{24,1}, {28,4}, {32,2}, {36,4}, {40,1}}) put32(b,group+offset,count);
    const auto skin = b.size(); b.resize(b.size()+28); b[skin] = 5; // 32-bit ARGB: source alpha flag.
    put32(b,skin+4,3); put32(b,skin+8,5);
    for (int n=0;n<3*5;++n) b.insert(b.end(), {0,0,0,128});
    for (auto n : {0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}) f32(b,n);
    for (auto n : {0,1,2,0,1,2}) u16(b,static_cast<std::uint16_t>(n)); u32(b,0);
    for (auto n : {3,2,1,3,2,1}) u16(b,static_cast<std::uint16_t>(n)); u32(b,0);
    for (auto [x,y] : {std::pair{0.f,0.f}, {1.f,0.f}, {0.f,1.f}, {1.f,1.f}}) vertex(b,{x,y,0,0,0,0,0,1},0xffff);
    b.resize(b.size()+24); label(b,b.size()-24,"Frame0");
    put32(b,group+4,static_cast<std::uint32_t>(b.size()-group));
    put32(b,16,static_cast<std::uint32_t>(b.size()));
    Temp temp; c4::Settings settings; settings.mesh = temp.dir/"blob.mdl"; save(settings.mesh,b);
    c4::assets::GpuImageRuntimeContext context; context.maximum_dimension = 4096;
    const auto scene = load_render_scene(settings, context);
    require(scene.instances.size()==1 && scene.instances[0].material_state.entity_skins[0], "blob draw lost its skin");
    const auto& gpu = scene.gpu_textures.at(*scene.instances[0].material_state.entity_skins[0]);
    require(gpu.width==8 && gpu.height==8 && gpu.source_alpha_flag, "blob skin is not the padded 8x8 alpha upload");
    const auto& mesh = scene.meshes.at(scene.instances[0].mesh_index);
    float u{}, v{};
    for (std::size_t i=0;i<mesh.vertices.size();++i) {
        u = std::max(u,mesh.vertices[i].u); v = std::max(v,mesh.vertices[i].v);
        require(mesh.secondary_uv[i]==std::array<float,2>{mesh.vertices[i].u,mesh.vertices[i].v}, "blob UV1 left UV0");
    }
    // 3/1002db10(3)=3/8 and 5/8: UV 1 must stop at the authored skin edge, not sample padding.
    require(u==3.f/8 && v==5.f/8, "blob UVs sample the padded upload instead of the authored skin");
    auto blob = scene.instances[0]; blob.source_entity = 0;
    auto ground = blob; ground.source_entity = 1; ground.material_state.entity_skins[0].reset();
    auto overlay = blob; overlay.source_entity = 2; overlay.flags = 0x1000; // OVERLAY keeps the opaque list.
    auto far_blob = blob; far_blob.source_entity = 3; far_blob.model_to_world[12] = 50;
    auto glass = ground; glass.source_entity = 4; glass.flags = 0x400; glass.model_to_world[12] = 20;
    // Sprite ctor 0x1005fe90 also marks alpha-bitmap sprites private0x80; only OVERLAY stays opaque.
    auto sprite = blob; sprite.source_entity = 5; sprite.source_sprite = true; sprite.model_to_world[12] = 10;
    auto overlay_sprite = sprite; overlay_sprite.source_entity = 6; overlay_sprite.flags = 0x1000;
    const std::array instances{blob, ground, overlay, far_blob, glass, sprite, overlay_sprite};
    FrameSnapshot frame{.scene=scene, .instances=instances, .camera={}};
    std::vector<std::uint32_t> order;
    require(source_entity_draw_order(frame,order)==3 && order==std::vector<std::uint32_t>({1,2,6,3,4,5,0}),
        "alpha-skin blob/sprite is not drawn after the opaque ground and OVERLAY sprite, far transparent first");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc<=2,"usage: test_animation [c4_robot_kaporta.mdl or archive::member]");
        behavior(); legacy_decoding(); donus_playback(); source_blob_shadow_draw(); if (argc==2) original(argv[1]);
        std::cout << "animation behavior checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
