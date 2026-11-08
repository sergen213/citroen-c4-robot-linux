#include "PhysicsWorld.hpp"
#include "NativeNumeric.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace c4::physics {
namespace {
void require_created(const assets::WorldScene& scene,std::uint32_t entity) {
    if(scene.entities.at(entity).native_prepared_inactive)
        throw std::invalid_argument("Original entity has not been created");
}
Vec3 vector(const std::array<float,3>& a) { return {a[0],a[1],a[2]}; }
Vec3 vertex(const assets::Vertex& a) { return {a.x,a.y,a.z}; }
Vec3 product(Vec3 a,Vec3 b) { return {a.x*b.x,a.y*b.y,a.z*b.z}; }
Vec3 quotient(Vec3 a,Vec3 b) { return {a.x/b.x,a.y/b.y,a.z/b.z}; }
double fixed_float(double value) {
    return double(Var::from_float(static_cast<float>(value)).raw)/1024;
}
Vec3 fixed_vector(Vec3 value) {
    return {fixed_float(value.x),fixed_float(value.y),fixed_float(value.z)};
}
void finite(Vec3 a) {
    if (!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z))
        throw std::invalid_argument("Nonfinite original-world vector");
}
struct Basis {
    std::array<Vec3,3> axes;
    explicit Basis(Vec3 angles):axes(rotation_axes(angles)) {}
    explicit Basis(const std::array<Vec3,3>& value):axes(value) {}
    Vec3 forward(Vec3 p) const { return axes[0]*p.x+axes[1]*p.y+axes[2]*p.z; }
    Vec3 inverse(Vec3 p) const { return {dot(p,axes[0]),dot(p,axes[1]),dot(p,axes[2])}; }
};
struct Box {
    Vec3 center,radius;
    Basis basis;
    explicit Box(const Body& b):center(b.position),radius((b.hull.max-b.hull.min)*0.5),basis(b.angles) {
        finite(radius);
        //1007b070 coerces each nonpositive width to1, then centers at min+width/2.
        if(radius.x<=0)radius.x=0.5;
        if(radius.y<=0)radius.y=0.5;
        if(radius.z<=0)radius.z=0.5;
        center=center+basis.forward(b.hull.min+radius);
        finite(center);
    }
    double support(Vec3 axis) const {
        return std::abs(dot(axis,basis.axes[0]))*radius.x+
               std::abs(dot(axis,basis.axes[1]))*radius.y+
               std::abs(dot(axis,basis.axes[2]))*radius.z;
    }
    double ellipsoid_support(Vec3 axis) const {
        return length(product(basis.inverse(axis),radius));
    }
};
Bounds transformed_bounds(Bounds bounds,const Body& body,const Basis& basis) {
    const Vec3 c=body.position+basis.forward(product((bounds.min+bounds.max)*0.5,body.scale));
    const Vec3 r=product((bounds.max-bounds.min)*0.5,body.scale);
    const Vec3 ex=basis.axes[0]*r.x,ey=basis.axes[1]*r.y,ez=basis.axes[2]*r.z;
    const Vec3 extent{std::abs(ex.x)+std::abs(ey.x)+std::abs(ez.x),std::abs(ex.y)+std::abs(ey.y)+std::abs(ez.y),std::abs(ex.z)+std::abs(ey.z)+std::abs(ez.z)};
    return {c-extent,c+extent};
}
bool broadphase(Vec3 origin,Vec3 delta,Bounds target,Vec3 extent) {
    target.min=target.min-extent;target.max=target.max+extent;
    const std::array<double,3> o{origin.x,origin.y,origin.z},d{delta.x,delta.y,delta.z},mn{target.min.x,target.min.y,target.min.z},mx{target.max.x,target.max.y,target.max.z};
    double enter=0,leave=1;
    for (unsigned i=0;i<3;++i) {
        if (d[i]==0) { if (o[i]<mn[i]||o[i]>mx[i]) return false; }
        else { double a=(mn[i]-o[i])/d[i],b=(mx[i]-o[i])/d[i];if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);if(enter>leave)return false; }
    }
    return true;
}
SweepContact sweep_boxes(const Box& a,Vec3 delta,const Box& b) {
    std::array<Vec3,15> axes{};
    for(unsigned i=0;i<3;++i){axes[i]=a.basis.axes[i];axes[i+3]=b.basis.axes[i];}
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)axes[6+i*3+j]=cross(a.basis.axes[i],b.basis.axes[j]);
    double enter=-std::numeric_limits<double>::infinity(),leave=std::numeric_limits<double>::infinity(),depth=std::numeric_limits<double>::infinity();
    Vec3 contact{},penetration{};bool overlap=true;
    for(Vec3 axis:axes) {
        const double norm=length(axis);if(norm<1e-12)continue;axis=axis/norm;
        const double radius=a.support(axis)+b.support(axis),offset=dot(a.center-b.center,axis),velocity=dot(delta,axis);
        const double gap=radius-std::abs(offset);
        if(gap<=1e-10)overlap=false;
        if(gap<depth){depth=gap;penetration=axis*(offset>=0?1.0:-1.0);}
        if(velocity==0){if(std::abs(offset)>radius)return {};continue;}
        double t0=(-radius-offset)/velocity,t1=(radius-offset)/velocity;
        Vec3 n=axis*(velocity>0?-1.0:1.0);
        if(t0>t1)std::swap(t0,t1);
        if(t0>enter){enter=t0;contact=n;}
        leave=std::min(leave,t1);if(enter>leave)return {};
    }
    if(overlap)return {true,true,0,penetration,a.center-penetration*a.support(penetration)};
    if(enter< -1e-10||enter>1||leave<=0)return {};
    enter=std::max(0.0,enter);
    return {true,false,enter,contact,a.center+delta*enter-contact*a.support(contact)};
}
void extend(Bounds& bounds,Vec3 p) {
    bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
    bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};
}
Bounds empty_bounds() {
    const double inf=std::numeric_limits<double>::infinity();return {{inf,inf,inf},{-inf,-inf,-inf}};
}
} // namespace


PhysicsWorld::PhysicsWorld(const assets::WorldScene& scene):scene_(&scene),bodies_(scene.entities.size()),model_colliders_(scene.models.size()),colliders_(scene.entities.size()) {
    for(auto& collider:model_colliders_) {
        collider.bounds=empty_bounds();collider.polygon_bounds=empty_bounds();
    }
    for(std::size_t i=0;i<scene.entities.size();++i) {
        const auto& source=scene.entities[i];auto& b=bodies_[i];
        b.position=source.source_position_words?
            Vec3{number(Var{(*source.source_position_words)[0]}),number(Var{(*source.source_position_words)[1]}),number(Var{(*source.source_position_words)[2]})}:
            fixed_vector(vector(source.position));
        b.angles=fixed_vector(vector(source.angles));
        b.scale=fixed_vector(vector(source.scale));
        finite(b.position);finite(b.angles);finite(b.scale);
        const auto flags=(source.flags|source.source_flags_set)&~source.source_flags_clear;
        b.flags.passable=(flags&0x200)!=0;b.flags.polygon=(flags&0x4000000)!=0;
        b.flags.flag2=(flags&2)!=0;b.flags.flag4=(flags&8)!=0;
        if(source.model_index==assets::no_world_model)continue;
        const auto& model=scene.models.at(source.model_index);b.flags.sprite=model.sprite.width!=0;
        colliders_[i].model_index=source.model_index;
        auto& collider=model_colliders_.at(source.model_index);
        const auto& geometry=model.morphed_model?model.morphed_model:model.model;
        if(!geometry)continue;
        if(!collider.initialized) {
            std::size_t group_index=0;
            for(const auto& group:geometry->groups) {
                for(std::uint32_t j=0;j<group.source_vertices.size();++j)
                    extend(collider.bounds,vertex(assets::sample_source_vertex(group,{},j)));
                const bool imported=group_index++==0&&!model.imported_mesh.indices.empty();
                const auto& mesh=imported?model.imported_mesh:group.mesh;
                if(mesh.indices.size()%3!=0)throw std::invalid_argument("Invalid original triangle topology");
                for(std::size_t j=0;j<mesh.indices.size();j+=3) {
                    Triangle triangle;
                    for(unsigned k=0;k<3;++k) {
                        const auto render_index=mesh.indices[j+k];
                        triangle.vertices[k]=imported?vertex(mesh.vertices.at(render_index)):
                            vertex(assets::sample_source_vertex(group,{},group.source_indices.at(render_index)));
                        extend(collider.polygon_bounds,triangle.vertices[k]);
                    }
                    collider.triangles.push_back(triangle);
                }
            }
            collider.initialized=true;
        }
        if(!geometry->groups.empty()) {
            const Vec3 a=fixed_vector(product(collider.bounds.min,b.scale)),z=fixed_vector(product(collider.bounds.max,b.scale));
            b.hull={{std::min(a.x,z.x),std::min(a.y,z.y),std::min(a.z,z.z)},
                    {std::max(a.x,z.x),std::max(a.y,z.y),std::max(a.z,z.z)}};
            //1003acb0 disk POLYGON/BBox forces REAL before1003cb50 auto selection.
            if((source.flags&0x24000000)!=0)b.flags.fat=b.flags.narrow=true;
            else {
                const double span=b.hull.max.x-b.hull.min.x+b.hull.max.y-b.hull.min.y;
                b.flags.fat=span>6*narrow_hull.max.y;
                b.flags.narrow=!b.flags.fat&&span>narrow_hull.max.y;
            }
        }
    }
}
const Body& PhysicsWorld::body(std::uint32_t entity) const { return bodies_.at(entity); }
Body& PhysicsWorld::body(std::uint32_t entity) { return bodies_.at(entity); }
Body PhysicsWorld::collision_body(std::uint32_t entity) const {
    Body selected=body(entity);
    if(selected.flags.narrow&&!selected.flags.fat)selected.hull=narrow_hull;
    if(selected.flags.fat&&!selected.flags.narrow)selected.hull=fat_hull;
    return selected;
}
void PhysicsWorld::cache_transform(std::uint32_t entity) const {
    const auto& b=body(entity);const auto& c=colliders_.at(entity);
    const auto same=[](Vec3 a,Vec3 z){return a.x==z.x&&a.y==z.y&&a.z==z.z;};
    if(c.cached&&same(c.cached_position,b.position)&&same(c.cached_angles,b.angles)&&same(c.cached_scale,b.scale))return;
    finite(b.position);finite(b.angles);finite(b.scale);
    c.axes=rotation_axes(b.angles);
    const auto& model=model_colliders_.at(c.model_index);
    const Bounds& bounds=model.triangles.empty()?model.bounds:model.polygon_bounds;
    c.world_polygon_bounds=transformed_bounds(bounds,b,Basis(c.axes));
    c.cached_position=b.position;c.cached_angles=b.angles;c.cached_scale=b.scale;c.cached=true;
}
void PhysicsWorld::set_minmax(std::uint32_t entity) {
    require_created(*scene_,entity);
    auto& b=body(entity);const auto model_index=colliders_.at(entity).model_index;
    if(model_index==assets::no_world_model)throw std::invalid_argument("Nonfinite original-world vector");
    const auto& bounds=model_colliders_.at(model_index).bounds;
    finite(bounds.min);finite(bounds.max);
    const Vec3 a=fixed_vector(product(bounds.min,b.scale)),z=fixed_vector(product(bounds.max,b.scale));
    b.hull={{std::min(a.x,z.x),std::min(a.y,z.y),std::min(a.z,z.z)},{std::max(a.x,z.x),std::max(a.y,z.y),std::max(a.z,z.z)}};
    b.flags.fat=true;b.flags.narrow=true;
}
Vec3 PhysicsWorld::vec_for_vertex(std::uint32_t entity,std::uint32_t index,assets::AnimationSample sample,std::uint32_t group_index) const {
    require_created(*scene_,entity);
    const auto& source=scene_->entities.at(entity);
    const auto& model=scene_->models.at(source.model_index);
    const auto& geometry=model.morphed_model?model.morphed_model:model.model;
    if(!geometry)throw std::invalid_argument("Entity has no original source model");
    const auto& group=geometry->groups.at(group_index);
    if(group.source_vertices.empty())throw std::invalid_argument("Original model group has no source vertices");
    const auto source_index=std::min<std::size_t>(index==0?0:index-1,group.source_vertices.size()-1);
    const auto p=vertex(assets::sample_source_vertex(group,sample,static_cast<std::uint32_t>(source_index)));
    const auto& b=body(entity);return fixed_vector(model_vertex_position(p,b.position,b.angles,b.scale));
}
bool PhysicsWorld::filtered(std::uint32_t candidate,Mode mode,std::uint32_t me,std::uint32_t you) const {
    if(scene_->entities[candidate].native_prepared_inactive)return true;
    const auto& b=body(candidate);
    return (has(mode,Mode::ignore_me)&&candidate==me)||(has(mode,Mode::ignore_you)&&candidate==you)||
        (has(mode,Mode::ignore_passable)&&b.flags.passable)||(has(mode,Mode::ignore_sprites)&&b.flags.sprite)||
        (has(mode,Mode::ignore_flag2)&&b.flags.flag2)||
        (has(mode,Mode::ignore_push)&&me!=no_entity&&(b.push<body(me).push||(b.group!=0&&b.group==body(me).group)));
}
Hit PhysicsWorld::query(Vec3 from,Vec3 delta,Mode mode,std::uint32_t me,std::uint32_t you,bool moving) const {
    finite(from);finite(delta);if(me!=no_entity)(void)body(me);if(you!=no_entity)(void)body(you);
    const bool volume=moving||has(mode,Mode::use_box);
    if(volume&&me==no_entity)throw std::invalid_argument("Original USE_BOX query requires a me entity");
    if(volume)require_created(*scene_,me);
    if(volume&&(body(me).flags.sprite||colliders_.at(me).model_index==assets::no_world_model||
                !std::isfinite(model_colliders_.at(colliders_.at(me).model_index).bounds.min.x)))
        throw std::invalid_argument("Original probe entity has no recovered model hull");
    Hit result;result.target=from+delta;
    const double distance=length(delta);
    Body probe;if(volume){probe=collision_body(me);probe.position=from;}
    const Box shape(probe);
    const Vec3 extent=volume?Vec3{shape.support({1,0,0}),shape.support({0,1,0}),shape.support({0,0,1})}:Vec3{};
    const Vec3 sphere_delta=volume?quotient(shape.basis.inverse(delta),shape.radius):Vec3{};
    for(std::uint32_t i=0;i<size();++i) {
        if((moving&&i==me)||filtered(i,mode,me,you))continue;
        if(body(i).flags.sprite)
            throw std::runtime_error("Unfiltered original sprite collision orientation is not recovered");
        const auto& collider=colliders_[i];
        if(collider.model_index==assets::no_world_model)continue;
        const auto& model=model_colliders_[collider.model_index];
        const auto& target=body(i);
        const bool box_target=moving&&!target.flags.polygon&&!has(mode,Mode::use_polygon);
        if((model.triangles.empty()&&!box_target)||!std::isfinite(model.bounds.min.x))continue;
        cache_transform(i);
        Bounds target_bounds=collider.world_polygon_bounds;
        if(box_target) {
            const Box box(collision_body(i));
            const Vec3 radius{box.support({1,0,0}),box.support({0,1,0}),box.support({0,0,1})};
            target_bounds={box.center-radius,box.center+radius};
        }
        if(!broadphase(volume?shape.center:from,delta,target_bounds,extent))continue;
        SweepContact best;
        if(box_target) {
            best=sweep_boxes(shape,delta,Box(collision_body(i)));
        } else {
            const Basis target_basis(collider.axes);
            for(const auto& local:model.triangles) {
                Triangle triangle;
                for(unsigned j=0;j<3;++j) {
                    const Vec3 world=target.position+target_basis.forward(product(local.vertices[j],target.scale));
                    triangle.vertices[j]=volume?quotient(shape.basis.inverse(world-shape.center),shape.radius):world;
                }
                auto contact=volume?sweep_unit_sphere({},sphere_delta,triangle):ray_triangle(from,delta,triangle);
                if(!contact.hit|| (best.hit&&contact.fraction>=best.fraction))continue;
                if(volume) {
                    contact.point=shape.center+shape.basis.forward(product(contact.point,shape.radius));
                    contact.normal=normalized(shape.basis.forward(quotient(contact.normal,shape.radius)));
                }
                best=contact;
            }
        }
        if(!best.hit||(result.trace_hit&&best.fraction>=result.fraction))continue;
        result={true,best.initially_overlapping,i,distance*best.fraction,best.fraction,best.point,best.normal};
        if(best.initially_overlapping) {
            const bool polygon=!moving||target.flags.polygon||has(mode,Mode::use_polygon);
            const double support=polygon?shape.ellipsoid_support(best.normal):shape.support(best.normal);
            const double depth=support-dot(shape.center-best.point,best.normal);
            result.distance=-std::max(0.0,depth);
        }
    }
    if(has(mode,Mode::ignore_you))result.you=you;
    return result;
}
Hit PhysicsWorld::trace(Vec3 from,Vec3 to,Mode mode,std::uint32_t me,std::uint32_t you) const {
    from=fixed_vector(from);to=fixed_vector(to);
    auto hit=query(from,to-from,mode,me,you,false);
    hit.distance=fixed_float(hit.distance);
    hit.target=fixed_vector(hit.target);hit.normal=fixed_vector(hit.normal);
    return hit;
}
void PhysicsWorld::event(std::uint32_t entity,const Hit& hit) const {
    if(on_entity&&hit.trace_hit)on_entity({entity,hit.you,fixed_vector(hit.target),fixed_vector(hit.normal)});
}
MoveResult PhysicsWorld::move(std::uint32_t entity,Vec3 relative,Vec3 absolute,Mode mode,std::uint32_t you) {
    require_created(*scene_,entity);
    finite(relative);finite(absolute);
    if(!std::isfinite(move_friction)||move_friction<0||move_friction>1||!std::isfinite(move_min_z))throw std::invalid_argument("Invalid native gliding parameters");
    auto& b=body(entity);const Vec3 start=b.position;Vec3 delta=rotate_vector(relative,b.angles)+absolute;
    if(b.flags.passable) { b.position=fixed_vector(b.position+delta);return {fixed_float(length(b.position-start)),{}}; }
    Hit last;
    // Actual 7.07.6 OBB mover1007a020 retraces with loop index0..3.
    for(unsigned attempt=0;attempt<4&&length(delta)>=double(0.015f);++attempt) {
        const Vec3 wanted=b.position+delta;
        const double remaining=length(delta);
        const Vec3 direction=delta/remaining;
        auto hit=query(b.position,delta,mode,entity,you,true);
        if(!hit.trace_hit){b.position=fixed_vector(wanted);break;}
        last=hit;event(entity,hit);
        if(!hit.initially_overlapping) {
            //10079db0: positive clearance is floored to Q10 then set back3LSBs.
            const double advance=std::max((std::trunc(double(float(hit.distance))*1024)-3)/1024,0.0);
            b.position=fixed_vector(b.position+direction*std::min(remaining,advance));
        }
        if(!has(mode,Mode::glide))break;
        if(hit.distance<0) {
            delta=hit.normal*std::min(remaining,std::max(-hit.distance,double(0.017f)));
        } else {
            //10079a10 initializes reverse direction, then replaces it with the
            // overshot tangent residual unless the impact is effectively head-on.
            delta=direction*(-1);
            const double incidence=-dot(direction,hit.normal);
            if(incidence>0&&incidence<1-double(0.05f)*move_friction) {
                const double projection=dot(wanted-b.position,hit.normal);
                delta=(wanted-b.position-hit.normal*(projection*double(1.01f)))*(1-move_friction);
            }
        }
        if(disable_z_glide||hit.normal.z<move_min_z)delta.z=0;
    }
    last.distance=fixed_float(last.distance);
    last.target=fixed_vector(last.target);last.normal=fixed_vector(last.normal);
    return {fixed_float(length(b.position-start)),last};
}
bool PhysicsWorld::occupied(std::uint32_t entity,Mode mode,std::uint32_t you,Hit& hit) const {
    hit=query(body(entity).position,{},mode,entity,you,true);return hit.trace_hit&&hit.initially_overlapping;
}
bool PhysicsWorld::rotate(std::uint32_t entity,Vec3 angle,Mode mode,std::uint32_t you) {
    require_created(*scene_,entity);
    finite(angle);auto& b=body(entity);const Vec3 old_angles=b.angles,old_position=b.position;
    const Vec3 desired=fixed_vector(old_angles+angle);b.angles=desired;
    if(b.flags.passable)return true;
    Hit hit;if(!occupied(entity,mode,you,hit))return true;
    event(entity,hit);
    const Body shape=collision_body(entity);
    const Vec3 dimensions=shape.hull.max-shape.hull.min;
    const double half_min=std::min({dimensions.x,dimensions.y,dimensions.z})*0.5;
    const double step=(length(dimensions)*0.5-half_min)*0.5;
    const Mode single=Mode(std::uint32_t(mode)&~std::uint32_t(Mode::glide));
    auto escape_normal=[&](Vec3 normal) {
        if(disable_z_glide||normal.z<move_min_z)normal.z=0;
        return normal;
    };
    auto recover=[&]() {
        // Recovered rotation tries to regain its original position after success.
        (void)move(entity,{},old_position-b.position,single,you);
        return true;
    };
    //1007a580 performs two outward attempts and two secondary contact-normal
    // attempts, even without GLIDE. Double contact arithmetic is not bit parity.
    const Vec3 first_normal=escape_normal(hit.normal);
    for(unsigned outer=1;outer<=2;++outer) {
        b.position=old_position;b.angles=old_angles;
        (void)move(entity,{},first_normal*(step*outer),single,you);
        b.angles=desired;
        if(!occupied(entity,mode,you,hit))return recover();
        const Vec3 secondary_start=b.position;
        const Vec3 secondary_normal=escape_normal(hit.normal);
        for(unsigned inner=1;inner<=2;++inner) {
            b.position=secondary_start;b.angles=old_angles;
            (void)move(entity,{},secondary_normal*(step*inner),single,you);
            b.angles=desired;
            if(!occupied(entity,mode,you,hit))return recover();
        }
    }
    b.angles=old_angles;b.position=old_position;
    return false;
}
} // namespace c4::physics
