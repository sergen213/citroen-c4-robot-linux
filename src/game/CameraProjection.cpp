#include "CameraProjection.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace c4::game {
namespace {
void validate(double arc,double near_plane,double far_plane,double pixel_aspect,int width,int height) {
    if(width<=0||height<=0||!std::isfinite(arc)||arc<=0||arc>=180||
       !std::isfinite(near_plane)||near_plane<=0||!std::isfinite(far_plane)||far_plane<=near_plane||
       !std::isfinite(pixel_aspect)||pixel_aspect<=0)
        throw std::invalid_argument("Source perspective needs recovered positive near plane/pixel aspect and a valid viewport");
}
struct ProjectionScales { float x,y,depth,offset; };
ProjectionScales projection_scales(double arc,double near_plane,double far_plane,
                                    double pixel_aspect,int width,int height) {
    // Original 10051a60/10051bd0/100530b3: retain each SDK binary32 store.
    // Aperture correction 1.005 is the original PE double at 10190d90,
    // confirmed by the selected camera's actual D3D projection matrix.
    const float tangent=float(2*std::tan(std::clamp(arc,1.0,175.0)*std::numbers::pi/360));
    const float ratio=float(double(float(pixel_aspect))*width/height);
    const float aperture=float(double(float(tangent/ratio))*1.005);
    const float fovy=float(2*std::atan(double(aperture)*.5));
    const float y=1.0f/std::tan(fovy*.5f),x=y/ratio;
    const float n=float(near_plane),f=std::max(float(far_plane),n+1.0f);
    const float d=f/(f-n),z=-n*d;
    // Vulkan converts this common OpenGL depth row back to original [0,w].
    return {x,y,2*d-1,2*z};
}
}
std::array<float,16> source_view_matrix(Vec3 position,Vec3 angles) {
    position={float(position.x),float(position.y),float(position.z)};
    const auto axes=physics::rotation_axes(angles);
    const auto forward=axes[0],right=axes[1]*-1,up=axes[2];
    return {float(right.x),float(up.x),float(forward.x),0,
        float(right.y),float(up.y),float(forward.y),0,
        float(right.z),float(up.z),float(forward.z),0,
        float(-physics::dot(right,position)),float(-physics::dot(up,position)),
        float(-physics::dot(forward,position)),1};
}
static rendering::CameraState camera_from_scales(Vec3 position,Vec3 angles,ProjectionScales scales,
                                                double near_plane,double far_plane) {
    position={float(position.x),float(position.y),float(position.z)};
    const auto axes=physics::rotation_axes(angles);
    const auto forward=axes[0],right=axes[1]*-1,up=axes[2];
    const auto [sx,sy,depth,offset]=scales;
    rendering::CameraState result{{},"original +X perspective; recovered 1.005 aperture/binary32 projection and effective clipping"};
    result.view_position={float(position.x),float(position.y),float(position.z)};
    result.view={float(right.x),float(up.x),float(-forward.x),0,
        float(right.y),float(up.y),float(-forward.y),0,
        float(right.z),float(up.z),float(-forward.z),0,
        float(-physics::dot(right,position)),float(-physics::dot(up,position)),
        float(physics::dot(forward,position)),1};
    result.projection={sx,0,0,0, 0,sy,0,0, 0,0,-depth,-1, 0,0,offset,0};
    result.inverse_projection={1/sx,0,0,0, 0,1/sy,0,0, 0,0,0,1/offset, 0,0,-1,-depth/offset};
    result.clip_near=float(near_plane);
    result.clip_far=std::max(float(far_plane),result.clip_near+1.0f);
    result.projection_known=true;
    auto& m=result.view_projection;
    m={float(sx*right.x),float(sy*up.x),float(depth*forward.x),float(forward.x),
       float(sx*right.y),float(sy*up.y),float(depth*forward.y),float(forward.y),
       float(sx*right.z),float(sy*up.z),float(depth*forward.z),float(forward.z),
       float(-sx*physics::dot(right,position)),float(-sy*physics::dot(up,position)),
       float(offset-depth*physics::dot(forward,position)),float(-physics::dot(forward,position))};
    return result;
}
rendering::CameraState perspective_camera(Vec3 position,Vec3 angles,double horizontal_arc,
    double near_plane,double far_plane,double pixel_aspect,int width,int height) {
    validate(horizontal_arc,near_plane,far_plane,pixel_aspect,width,height);
    return camera_from_scales(position,angles,projection_scales(horizontal_arc,near_plane,far_plane,pixel_aspect,width,height),near_plane,far_plane);
}
SourceViewSnapshot capture_source_view(const SharedState& state) {
    const bool bloom=bool(state.bloom_aktif)&&bool(state.bloom_fx_ok);
    const bool no_shader=bloom?state.bloom_noshader:state.noshader;
    const bool no_particles=bloom?state.bloom_noparticle:state.noparticle;
    // Original SAR10 conversion; unknown capability clamp branches agree only in [0,127].
    const std::int32_t ambient=(bloom?state.bloom_camera_ambient:state.camera_ambient).raw>>10;
    // Constructor/masked source writes prove CULLCW/alpha modes/WIREFRAME10000 clear.
    const std::uint32_t flags=(no_shader?0x1000u:0u)|(no_particles?0x800000u:0u);
    return {bloom?state.bloom_camera_position:state.camera_position,
        bloom?state.bloom_camera_angles:state.camera_angles,
        bloom?state.bloom_camera_arc:state.camera_arc,
        bloom?Var{10463}:state.camera_clip_near,state.camera_clip_far,
        bloom?observed_empty_view_near:state.camera_effective_near,state.camera_pixel_aspect,
        no_shader,no_particles,flags,0x00831410u,ambient,ambient>=0&&ambient<=127};
}
rendering::CameraState source_camera(const SharedState& state,int width,int height) {
    return source_camera(capture_source_view(state),width,height);
}
rendering::CameraState source_camera(const SourceViewSnapshot& view,int width,int height) {
    const double arc=physics::number(view.arc),far=physics::number(view.clip_far),near=view.effective_near;
    validate(arc,near,far,view.pixel_aspect,width,height);
    auto scales=projection_scales(arc,near,far,view.pixel_aspect,640,480);
    const float ratio=float(double(float(view.pixel_aspect))*width/height);
    // Modern render-only Hor+/Vert+: keep the complete original 4:3 view,
    // adding horizontal (wide) or vertical (narrow) coverage without cropping.
    if(std::int64_t(width)*3>std::int64_t(height)*4) scales.x=scales.y/ratio;
    if(std::int64_t(width)*3<std::int64_t(height)*4) scales.y=scales.x*ratio;
    auto result=camera_from_scales(physics::native(view.position),physics::native(view.angles),scales,near,far);
    result.source_view_flags=view.flags_value;
    result.source_view_flags_known_mask=view.flags_known_mask;
    result.provenance="captured source VIEW pose/aperture and effective clipping; original 640x480 projection";
    if(std::int64_t(width)*3!=std::int64_t(height)*4)
        result.provenance="captured source VIEW pose/aperture and effective clipping; original 640x480 projection with modern Hor+/Vert+ expansion";
    return result;
}
std::optional<rendering::SourceSkyFrame> source_sky_frame(const rendering::SourceSkyBinding& sky,
    const SourceViewSnapshot& view, const rendering::CameraState& camera, bool draw_world) {
    constexpr std::uint32_t wireframe = 0x10000; // Original VIEW sky-blocking bit, not a guessed NO_SKY.
    if (!draw_world || !sky.stage_enabled_known || !sky.stage_enabled
        || (view.flags_known_mask & wireframe) == 0 || (view.flags_value & wireframe)
        || !camera.projection_known) return std::nullopt;
    if (!sky.first_face_width) throw std::invalid_argument("Source sky needs its original first-face width");
    const float far = camera.clip_far;
    const float extended_far = far * 2;
    const float ratio = static_cast<float>(static_cast<double>(far) * 2 / sky.first_face_width);
    const float scale = Var::from_float(ratio).to_float(); // 100543fb: FLOAT spill, Q10 truncation.
    rendering::SourceSkyFrame result{camera, {}};
    auto& sky_camera = result.camera;
    const float depth = extended_far / (extended_far - camera.clip_near);
    sky_camera.provenance = "captured selected world VIEW; original declared CUBE stage with doubled far clipping";
    const float offset = -camera.clip_near * depth;
    sky_camera.clip_far = extended_far;
    sky_camera.projection[10] = -(2 * depth - 1);
    sky_camera.projection[14] = 2 * offset;
    sky_camera.inverse_projection[11] = 1 / (2 * offset);
    sky_camera.inverse_projection[15] = -(2 * depth - 1) / (2 * offset);
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) {
            float entry = 0;
            for (std::size_t k = 0; k < 4; ++k)
                entry += sky_camera.projection[k * 4 + row] * camera.view[column * 4 + k];
            sky_camera.view_projection[column * 4 + row] = entry;
        }
    // Immutable PE binary32:10190dd8=-90,10190ddc=90,10190de0=270,1018a438=180.
    static const auto axes = [] {
        std::array<std::array<Vec3, 3>, 6> value;
        const std::array<Vec3, 6> angles{{{0,0,0}, {270,0,0}, {180,0,0},
            {90,0,0}, {0,90,-90}, {0,-90,90}}};
        for (std::size_t face = 0; face < value.size(); ++face)
            value[face] = physics::rotation_axes(angles[face]);
        return value;
    }();
    constexpr std::array<std::array<float, 3>, 6> offsets{{
        {-1,0,0}, {0,1,0}, {1,0,0}, {0,-1,0}, {0,0,-1}, {0,0,1}}};
    for (std::size_t face = 0; face < result.model_to_world.size(); ++face) {
        const auto& basis = axes[face];
        const auto& eye = camera.view_position;
        // Source scale_x/y applies to the bitmap's local Y/Z corners, not its +X normal.
        result.model_to_world[face] = {
            float(basis[0].x), float(basis[0].y), float(basis[0].z), 0,
            float(basis[1].x * scale), float(basis[1].y * scale), float(basis[1].z * scale), 0,
            float(basis[2].x * scale), float(basis[2].y * scale), float(basis[2].z * scale), 0,
            eye[0] + offsets[face][0] * far, eye[1] + offsets[face][1] * far,
            eye[2] + offsets[face][2] * far, 1};
    }
    return result; // Normal captured camera/projection was never mutated.
}
rendering::CameraState default_hud_camera(int width, int height) {
    auto result=perspective_camera({},{},61663.0/1024,observed_empty_view_near,51200223.0/1024,1,width,height);
    result.provenance="original cam_ibre captured zero pose; VIEW arc/aspect/clips proven initializer 0x100267d0; recovered aperture";
    // The private cam_ibre VIEW does not inherit selected world VIEW flags.
    result.source_view_flags=0;
    result.source_view_flags_known_mask=0x00821410u;
    return result;
}
std::array<double,3> project_view_entity(Vec3 p,double horizontal_arc,double near_plane,
    double far_plane,double pixel_aspect,int width,int height) {
    validate(horizontal_arc,near_plane,far_plane,pixel_aspect,width,height);
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.x<=0)
        throw std::invalid_argument("Source view entity must have finite positive camera depth");
    const auto [sx,sy,depth,offset]=projection_scales(horizontal_arc,near_plane,far_plane,pixel_aspect,width,height);
    return {(1-sx*p.y/p.x)*width*.5,(1-sy*p.z/p.x)*height*.5,depth+offset/p.x};
}
} // namespace c4::game
