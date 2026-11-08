#include "Geometry.hpp"
#include "PhysicsWorld.hpp"
#include "FixedPoint.hpp"
#include "NativeNumeric.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace c4::physics;

namespace {
constexpr double tolerance = 1e-9;
void near(double actual, double expected) {
    assert(std::isfinite(actual));
    assert(std::abs(actual - expected) < tolerance);
}
void near(Vec3 actual, Vec3 expected) {
    near(actual.x, expected.x);
    near(actual.y, expected.y);
    near(actual.z, expected.z);
}
void contact(const SweepContact& result, double fraction, Vec3 normal) {
    assert(result.hit);
    assert(!result.initially_overlapping);
    near(result.fraction, fraction);
    near(result.normal, normal);
}
template<class F> void invalid(F operation) {
    bool threw = false;
    try { operation(); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}

void rays() {
    const Triangle face{{Vec3{0,0,0}, Vec3{4,0,0}, Vec3{0,4,0}}};
    auto start = ray_triangle({1,1,0}, {0,0,-2}, face);
    contact(start, 0, {0,0,1});
    near(start.point, {1,1,0});
    auto end = ray_triangle({1,1,2}, {0,0,-2}, face);
    contact(end, 1, {0,0,1});
    near(end.point, {1,1,0});
    assert(!ray_triangle({3,3,2}, {0,0,-4}, face).hit);
    assert(!ray_triangle({1,1,2}, {0,0,-1}, face).hit);

    // Explicit rotation and translation of the same finite triangle.
    const Triangle transformed{{Vec3{1,2,1}, Vec3{5,2,1}, Vec3{1,2,5}}};
    const Triangle reversed{{transformed.vertices[2], transformed.vertices[1],
                             transformed.vertices[0]}};
    for (const Triangle& triangle : {transformed, reversed}) {
        const auto result = ray_triangle({2,0,2}, {0,4,0}, triangle);
        contact(result, .5, {0,-1,0});
        near(result.point, {2,2,2});
        assert(dot(result.normal, Vec3{0,4,0}) < 0);
    }
}

void spheres() {
    const Triangle face{{Vec3{0,0,0}, Vec3{4,0,0}, Vec3{0,4,0}}};
    const auto plane = sweep_unit_sphere({1,1,3}, {0,0,-4}, face);
    contact(plane, .5, {0,0,1});
    near(plane.point, {1,1,0});
    contact(sweep_unit_sphere({1,1,-3}, {0,0,4}, face), .5, {0,0,-1});
    assert(!sweep_unit_sphere({1,1,3}, {0,0,-1}, face).hit);

    // The finite edge is contacted before the center reaches the triangle plane.
    const auto edge = sweep_unit_sphere({2,-.6,2}, {0,0,-4}, face);
    contact(edge, .3, {0,-.6,.8});
    near(edge.point, {2,0,0});
    assert(!sweep_unit_sphere({2,-1.1,2}, {0,0,-4}, face).hit);

    const double vertex_height = std::sqrt(.55);
    const auto vertex = sweep_unit_sphere({-.6,-.3,2}, {0,0,-4}, face);
    contact(vertex, (2-vertex_height)/4, {-.6,-.3,vertex_height});
    near(vertex.point, {0,0,0});
    const auto tangent = sweep_unit_sphere({-1,0,2}, {0,0,-4}, face);
    contact(tangent, .5, {-1,0,0});
    near(tangent.point, {0,0,0});
    assert(!sweep_unit_sphere({-.8,-.8,2}, {0,0,-4}, face).hit);
    assert(!sweep_unit_sphere({1,1,1}, {0,0,2}, face).hit);
    assert(!sweep_unit_sphere({1,1,1}, {.25,0,0}, face).hit);
    contact(sweep_unit_sphere({1,1,1}, {0,0,-2}, face), 0, {0,0,1});
    contact(sweep_unit_sphere({1,1,3}, {0,0,-2}, face), 1, {0,0,1});
    const auto overlap = sweep_unit_sphere({1,1,.5}, {0,0,2}, face);
    assert(overlap.hit && overlap.initially_overlapping);
    near(overlap.fraction, 0);
}

void invalid_and_degenerate() {
    const Triangle flat{{Vec3{0,0,0}, Vec3{1,0,0}, Vec3{2,0,0}}};
    const Triangle face{{Vec3{0,0,0}, Vec3{4,0,0}, Vec3{0,4,0}}};
    assert(!ray_triangle({1,0,2}, {0,0,-4}, flat).hit);
    assert(!sweep_unit_sphere({1,0,2}, {0,0,-4}, flat).hit);

    for (double bad : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        const Vec3 invalid_vector{bad,0,0};
        const Triangle invalid_triangle{{Vec3{bad,0,0}, Vec3{4,0,0}, Vec3{0,4,0}}};
        const Bounds invalid_bounds{{-.5,-.5,-.5}, {bad,.5,.5}};
        invalid([&] { ray_triangle(invalid_vector, {0,0,-4}, face); });
        invalid([&] { ray_triangle({1,1,2}, invalid_vector, face); });
        invalid([&] { ray_triangle({1,1,2}, {0,0,-4}, invalid_triangle); });
        invalid([&] { sweep_unit_sphere(invalid_vector, {0,0,-4}, face); });
        invalid([&] { sweep_unit_sphere({1,1,2}, invalid_vector, face); });
        invalid([&] { sweep_unit_sphere({1,1,2}, {0,0,-4}, invalid_triangle); });
    }
}

// Small in-memory geometry, not an archive or a claim of original-game precision parity.
struct WorldFixture {
    c4::assets::WorldScene scene;

    std::uint32_t triangle(Triangle face, Bounds source_bounds, Vec3 position = {},
                           std::uint32_t flags = 0x4000000, Vec3 angles = {},
                           Vec3 scale = {1,1,1}, bool uv_split = false,
                           bool indexed_faces = true) {
        const auto vertex = [](Vec3 p) {
            return c4::assets::Vertex{float(p.x),float(p.y),float(p.z),0,0};
        };
        c4::assets::WorldModel model;
        c4::assets::ModelGroup group;
        for (Vec3 p : face.vertices) group.source_vertices.push_back(vertex(p));
        group.mesh.vertices = group.source_vertices;
        group.mesh.indices = {0,1,2};
        group.source_indices = {0,1,2};
        if (uv_split) {
            auto duplicate = group.mesh.vertices[0];
            duplicate.u = 1;
            group.mesh.vertices.push_back(duplicate);
            group.source_indices.push_back(0);
            group.mesh.indices = {0,1,2,3,2,1};
        }
        if (!indexed_faces) {
            group.mesh.indices.clear();
            group.source_indices.clear();
        }
        // Unrendered original vertices must still contribute to set_minmax.
        group.source_vertices.push_back(vertex(source_bounds.min));
        group.source_vertices.push_back(vertex(source_bounds.max));
        group.frames.push_back({"base",{}});
        auto geometry = std::make_shared<c4::assets::Model>();
        geometry->groups.push_back(std::move(group));
        model.model = std::move(geometry);
        const auto model_index = static_cast<std::uint32_t>(scene.models.size());
        scene.models.push_back(std::move(model));
        c4::assets::WorldEntity entity;
        entity.model_index = model_index;
        entity.position = {float(position.x),float(position.y),float(position.z)};
        entity.angles = {float(angles.x),float(angles.y),float(angles.z)};
        entity.scale = {float(scale.x),float(scale.y),float(scale.z)};
        entity.flags = flags;
        const auto index = static_cast<std::uint32_t>(scene.entities.size());
        scene.entities.push_back(std::move(entity));
        return index;
    }

    std::uint32_t box(Bounds hull, Vec3 position = {}, Vec3 angles = {}) {
        return triangle({{Vec3{hull.min.x,hull.min.y,hull.min.z},
                          Vec3{hull.min.x,hull.max.y,hull.min.z},
                          Vec3{hull.min.x,hull.min.y,hull.max.z}}},
                        hull, position, 0, angles);
    }
};

constexpr Bounds unit_hull{{-.5,-.5,-.5},{.5,.5,.5}};
constexpr Triangle finite_wall{{Vec3{0,-4,-4},Vec3{0,4,-4},Vec3{0,0,4}}};
constexpr Bounds wall_bounds{{0,-4,-4},{0,4,4}};
double contact_advance(double clearance) {
    // Recovered mover floors the float-ABI clearance to Q10, then backs off 3 LSBs.
    return std::max((std::trunc(double(float(clearance))*1024)-3)/1024, 0.0);
}
double sdk_value(double value) {
    return number(Var::from_float(static_cast<float>(value)));
}
Vec3 sdk_value(Vec3 value) {
    return {sdk_value(value.x), sdk_value(value.y), sdk_value(value.z)};
}

void world_filters_move_and_event_snapshot() {
    WorldFixture fixture; // Scene remains alive through every query and callback.
    const auto flag2 = fixture.triangle(finite_wall, wall_bounds, {}, 0x4000000 | 2);
    const auto passable = fixture.triangle(finite_wall, wall_bounds, {2,0,0},
                                          0x4000000 | 0x200);
    const auto actor = fixture.box(unit_hull, {4,0,0});
    PhysicsWorld world(fixture.scene);
    world.set_minmax(actor);
    assert(world.body(flag2).flags.flag2 && !world.body(flag2).flags.passable);
    assert(world.body(passable).flags.passable && !world.body(passable).flags.flag2);
    const auto ray = [&](Mode filter) {
        return world.trace({4,0,0}, {-2,0,0}, filter | Mode::ignore_me, actor);
    };
    assert(ray(Mode::none).you == passable);
    assert(ray(Mode::ignore_flag2).you == passable);
    const auto solid_ray = ray(Mode::ignore_passable);
    assert(solid_ray.trace_hit && solid_ray.you == flag2);
    near(solid_ray.distance, 4);
    near(solid_ray.target, {0,0,0});
    near(solid_ray.normal, {1,0,0});
    assert(!ray(Mode::ignore_flag2 | Mode::ignore_passable).trace_hit);

    const auto stopped_by_passable = world.move(actor, {}, {-6,0,0}, Mode::none);
    assert(stopped_by_passable.hit.you == passable);
    near(stopped_by_passable.distance, contact_advance(1.5));
    near(world.body(actor).position, {4-contact_advance(1.5),0,0});
    world.body(actor).position = {4,0,0};

    unsigned callbacks = 0;
    EntityEvent saved;
    world.on_entity = [&](const EntityEvent& event) {
        ++callbacks;
        assert(event.me == actor && event.you == flag2);
        near(event.target, {0,0,0});
        near(event.normal, {1,0,0});
        saved = event;
        const auto nested = world.trace({-2,0,0}, {3,0,0},
                                        Mode::ignore_flag2 | Mode::ignore_me, flag2);
        assert(nested.trace_hit && nested.you == passable);
        near(nested.target, {2,0,0});
        near(nested.normal, {-1,0,0});
        assert(event.me == saved.me && event.you == saved.you);
        near(event.target, saved.target);
        near(event.normal, saved.normal);
    };
    // The recovered movement caller omits ignore_flag2; its ground ray includes it.
    const Mode move_mode = Mode::ignore_passable | Mode::ignore_content |
                           Mode::ignore_push | Mode::ignore_sprites | Mode::glide;
    const auto moved = world.move(actor, {}, {-6,0,0}, move_mode);
    assert(callbacks == 1 && moved.hit.trace_hit && moved.hit.you == flag2);
    assert(!moved.hit.initially_overlapping);
    // Head-on GLIDE retraces one unit backward after reaching contact.
    near(moved.distance, contact_advance(3.5)-1);
    near(moved.hit.distance, 3.5);
    near(moved.hit.fraction, 3.5/6);
    near(world.body(actor).position, {5-contact_advance(3.5),0,0});
    assert(saved.me == actor && saved.you == moved.hit.you);
    near(saved.target, moved.hit.target);
    near(saved.normal, moved.hit.normal);
}

void world_glide_finite_polygon() {
    WorldFixture fixture;
    const auto wall = fixture.triangle(finite_wall, wall_bounds);
    const auto actor = fixture.box(unit_hull, {3,0,0});
    PhysicsWorld world(fixture.scene);
    world.set_minmax(actor);
    world.move_friction = 0; // Isolate overshot normal projection from friction.
    const auto slide = world.move(actor, {}, {-5,1,0}, Mode::glide);
    assert(slide.hit.trace_hit && slide.hit.you == wall);
    near(slide.hit.fraction, .5);
    near(slide.hit.normal, {1,0,0});
    near(slide.hit.target, {0,.5,0});
    const Vec3 reached = sdk_value(Vec3{3,0,0} + Vec3{-5,1,0}/std::sqrt(26.0)*
                                  contact_advance(std::sqrt(26.0)/2));
    const double slide_x = reached.x + (reached.x+2)*(double(1.01f)-1);
    const Vec3 final_position = sdk_value(Vec3{slide_x,1,0});
    near(world.body(actor).position, final_position);
    near(slide.distance, sdk_value(length(final_position-Vec3{3,0,0})));

    // Inside the wall AABB, but .75/sqrt(1.25) away from the finite sloped edge.
    world.body(actor).position = {3,1.75,2};
    const auto outside = world.move(actor, {}, {-5,0,0}, Mode::glide);
    assert(!outside.hit.trace_hit);
    near(outside.distance, 5);
    near(world.body(actor).position, {-2,1.75,2});
}

void world_source_vertices_and_minmax() {
    WorldFixture fixture;
    const auto actor = fixture.triangle(
        {{Vec3{0,0,0},Vec3{1,0,0},Vec3{0,1,1}}},
        {{-2,-1,-4},{3,2,6}}, {10,20,30}, 0x4000000, {90,0,0}, {2,3,4}, true);
    PhysicsWorld world(fixture.scene);
    world.set_minmax(actor);
    near(world.body(actor).hull.min, {-4,-3,-16});
    near(world.body(actor).hull.max, {6,6,24});
    assert(world.body(actor).flags.fat && world.body(actor).flags.narrow);
    // Original caller halves max.z after set_minmax; the API retains full extents.
    world.body(actor).hull.max.z *= .5;
    near(world.body(actor).hull.max.z, 12);
    near(world.body(actor).hull.min.z, -16);
    near(world.vec_for_vertex(actor, 1), {10,20,30});
    near(world.vec_for_vertex(actor, 0), {10,20,30}); // Original index saturation.
    near(world.vec_for_vertex(actor, 2), {10,22,30});
    near(world.vec_for_vertex(actor, 3), {7,20,34});
    // Source ID four is an unrendered extremum, not render vertex four's UV duplicate.
    near(world.vec_for_vertex(actor, 4), {13,16,14});
    // The stored 90-degree cosine is slightly negative: the affine FLOAT x
    // falls below four, so source publication truncates to raw Q10 4095.
    const Vec3 published_max{4095.0/1024,26,54};
    near(world.vec_for_vertex(actor, 5), published_max);
    near(world.vec_for_vertex(actor, std::numeric_limits<std::uint32_t>::max()),
         published_max);

    // Original instructions 1004e240(mode2) -> 1004bd80, not a native parity
    // oracle: work/reports/physics/model-vertex-affine.json records FLOAT bits.
    const Vec3 local{16384.001953125,.25,-.5}, position{-24576,20,30}, scale{1.5,3,4};
    near(model_vertex_position(local, position, {}, scale), {3.0/1024,20.75,28});
    near(model_vertex_position({1,2,3}, {10,20,30}, {30,20,10}, {1.5,3,4}),
         {5.499178409576416,21.81825828552246,42.597084045410156});
    WorldFixture affine_fixture;
    const auto affine_actor = affine_fixture.triangle(
        {{local,Vec3{0,0,0},Vec3{0,1,0}}},
        {{0,0,-1},{16385,1,1}}, position, 0x4000000, {}, scale);
    PhysicsWorld affine_world(affine_fixture.scene);
    near(affine_world.vec_for_vertex(affine_actor, 1), {3.0/1024,20.75,28});
}

void world_transformed_polygon_normal() {
    WorldFixture fixture;
    const auto wall = fixture.triangle(
        {{Vec3{-4,4,-4},Vec3{4,-4,-4},Vec3{0,0,4}}},
        {{-4,-4,-4},{4,4,4}}, {3,5,7}, 0x4000000, {90,0,0}, {2,1,3});
    const auto actor = fixture.box(unit_hull);
    fixture.scene.entities[wall].native_prepared_inactive = true;
    const auto model_index = fixture.scene.entities[wall].model_index;
    const auto source_flags = fixture.scene.entities[wall].flags;
    const auto geometry = fixture.scene.models[model_index].model.get();
    PhysicsWorld world(fixture.scene);
    assert(!world.trace({5,5,7}, {1,5,7}, Mode::none).trace_hit);
    fixture.scene.entities[actor].native_prepared_inactive = true;
    const Body prepared = world.body(actor);
    const auto actor_source_flags = fixture.scene.entities[actor].flags;
    unsigned callbacks = 0;
    world.on_entity = [&](const EntityEvent&) { ++callbacks; };
    const auto unchanged = [&] {
        near(world.body(actor).position, prepared.position);
        near(world.body(actor).angles, prepared.angles);
        near(world.body(actor).hull.min, prepared.hull.min);
        near(world.body(actor).hull.max, prepared.hull.max);
        assert(world.body(actor).flags.fat == prepared.flags.fat);
        assert(world.body(actor).flags.narrow == prepared.flags.narrow);
        assert(fixture.scene.entities[actor].flags == actor_source_flags);
        assert(callbacks == 0);
    };
    for (bool passable : {false, true}) {
        world.body(actor).flags.passable = passable;
        invalid([&] { world.move(actor, {}, {1,0,0}, Mode::none); });
        invalid([&] { world.rotate(actor, {30,0,0}, Mode::none); });
        unchanged();
        assert(world.body(actor).flags.passable == passable);
    }
    invalid([&] { world.trace({}, {1,0,0}, Mode::use_box, actor); });
    invalid([&] { world.set_minmax(actor); });
    invalid([&] { world.vec_for_vertex(actor, 1); });
    unchanged();
    assert(!world.trace({5,5,7}, {1,5,7}, Mode::ignore_me, actor).trace_hit);
    fixture.scene.entities[actor].native_prepared_inactive = false;
    world.body(actor).flags.passable = false;
    world.set_minmax(actor);
    near(world.vec_for_vertex(actor, 1), {-.5,-.5,-.5});
    assert(!world.trace({}, {1,0,0}, Mode::use_box | Mode::ignore_me, actor).trace_hit);
    near(world.move(actor, {}, {1,0,0}, Mode::none).distance, 1);
    assert(world.rotate(actor, {30,0,0}, Mode::none));
    near(world.body(actor).position, {1,0,0});
    near(world.body(actor).angles, {30,0,0});
    assert(callbacks == 0 && fixture.scene.entities[actor].flags == actor_source_flags);
    fixture.scene.entities[wall].native_prepared_inactive = false;
    assert(fixture.scene.entities[wall].model_index == model_index);
    assert(fixture.scene.entities[wall].flags == source_flags);
    assert(fixture.scene.models[model_index].model.get() == geometry);
    // Prime bounds/axes, then move the same target far outside its cached AABB.
    const auto old_hit = world.trace({5,5,7}, {1,5,7}, Mode::none);
    assert(old_hit.trace_hit && old_hit.you == wall);
    near(old_hit.target, {3,5,7});
    world.body(wall).position = {103,105,107};
    world.body(wall).angles = {45,0,0};
    // Actual binary32 SDK axes; inverse-transpose normal under nonuniform scale.
    const auto axes = rotation_axes({45,0,0});
    const Vec3 normal = normalized(axes[0]*(-.5)-axes[1]);
    const Vec3 center{103,105,107};
    const Vec3 from = sdk_value(center+normal*2), to = sdk_value(center-normal*2);
    const Triangle original{{Vec3{-4,4,-4},Vec3{4,-4,-4},Vec3{0,0,4}}};
    Triangle transformed;
    for (unsigned i=0; i<3; ++i) {
        const auto p = original.vertices[i];
        transformed.vertices[i] = center+axes[0]*(p.x*2)+axes[1]*p.y+axes[2]*(p.z*3);
    }
    const auto expected_ray = ray_triangle(from, to-from, transformed);
    assert(expected_ray.hit);
    const auto hit = world.trace(from, to, Mode::none);
    assert(hit.trace_hit && hit.you == wall);
    near(hit.fraction, expected_ray.fraction);
    near(hit.distance, sdk_value(length(to-from)*expected_ray.fraction));
    near(hit.target, sdk_value(expected_ray.point));
    near(hit.normal, sdk_value(expected_ray.normal));
    world.set_minmax(actor);
    const Vec3 start = center+normal*2;
    world.body(actor).position = start;
    const auto moved = world.move(actor, {}, normal*-4, Mode::none);
    assert(moved.hit.trace_hit && moved.hit.you == wall);
    // Check published Q10 clearance and response, not an ideal continuous
    // fraction: the actor retains rounded 30-degree SDK axes from above.
    const double clearance = dot(start-center,normal)-.5;
    const Vec3 final_position = sdk_value(start-normal*contact_advance(clearance));
    near(moved.distance, sdk_value(length(final_position-start)));
    near(moved.hit.distance, number(v(clearance)));
    near(moved.hit.target, native(source(center)));
    near(moved.hit.normal, native(source(normal)));
    assert(length(moved.hit.normal-normal) > tolerance);
    near(world.body(actor).position, final_position);
}

void world_nonpolygon_obb() {
    WorldFixture fixture;
    const auto obstacle = fixture.box({{-2,-.25,-.5},{2,.25,.5}}, {}, {45,0,0});
    const auto actor = fixture.box({{-.125,-.125,-.125},{.125,.125,.125}},
                                   {3,-1.5,0});
    PhysicsWorld world(fixture.scene);
    world.set_minmax(obstacle);
    world.set_minmax(actor);
    assert(!world.body(obstacle).flags.polygon);
    // This path enters the expanded world AABB but stays outside the rotated thin box.
    const auto miss = world.move(actor, {}, {-1.75,0,0}, Mode::none);
    assert(!miss.hit.trace_hit);
    near(miss.distance, 1.75);
    near(world.body(actor).position, {1.25,-1.5,0});

    const Vec3 normal = Vec3{-1,1,0}/std::sqrt(2.0);
    world.body(actor).position = normal*2;
    const auto hit = world.move(actor, {}, normal*-3, Mode::none);
    const auto target_axes = rotation_axes({45,0,0});
    const double target_radius = .25*std::abs(dot(target_axes[1],normal));
    const double clearance = target_radius + .125*std::sqrt(2.0);
    assert(hit.hit.trace_hit && hit.hit.you == obstacle);
    assert(!hit.hit.initially_overlapping);
    const Vec3 final_position = sdk_value(normal*(2-contact_advance(2-clearance)));
    near(hit.distance, sdk_value(length(final_position-normal*2)));
    near(hit.hit.fraction, (2-clearance)/3);
    near(hit.hit.distance, number(v(2-clearance)));
    near(hit.hit.normal, native(source(normal)));
    near(hit.hit.target, native(source(normal*target_radius)));
    near(world.body(actor).position, final_position);
}

void world_vertex_only_hull() {
    WorldFixture fixture;
    const auto obstacle = fixture.triangle(
        {{Vec3{0,-2,-1},Vec3{0,2,-1},Vec3{0,0,1}}},
        {{0,-2,-1},{0,2,1}}, {}, 0, {}, {-2,3,.5}, false, false);
    const auto actor = fixture.box(unit_hull, {3,5,0});
    const auto& model = fixture.scene.models[fixture.scene.entities[obstacle].model_index];
    assert(!model.model->groups[0].source_vertices.empty());
    assert(model.model->groups[0].mesh.indices.empty());
    PhysicsWorld world(fixture.scene);
    world.set_minmax(obstacle);
    world.set_minmax(actor);
    near(world.body(obstacle).hull.min, {0,-6,-.5});
    near(world.body(obstacle).hull.max, {0,6,.5});
    assert(world.body(obstacle).flags.fat && world.body(obstacle).flags.narrow);
    assert(!world.body(obstacle).flags.polygon);
    // No indexed faces: rays/polygon sweeps miss, but the real vertex OBB blocks.
    assert(!world.trace({3,5,0}, {-1,5,0}, Mode::ignore_me, actor).trace_hit);
    const auto hit = world.move(actor, {}, {-4,0,0}, Mode::none);
    assert(hit.hit.trace_hit && hit.hit.you == obstacle && !hit.hit.initially_overlapping);
    // Zero x width becomes one BEFORE centering: its effective interval is [0,1].
    near(hit.hit.fraction, .375);
    near(hit.hit.distance, number(v(1.5)));
    near(hit.hit.target, native(source(Vec3{1,5,0})));
    near(hit.hit.normal, native(source(Vec3{1,0,0})));
    near(hit.distance, contact_advance(1.5));
    near(world.body(actor).position, {3-contact_advance(1.5),5,0});
    world.body(actor).position = {-3,5,0};
    const auto opposite = world.move(actor, {}, {4,0,0}, Mode::none);
    assert(opposite.hit.trace_hit && opposite.hit.you == obstacle);
    near(opposite.hit.fraction, .625);
    near(opposite.hit.distance, number(v(2.5)));
    near(opposite.hit.target, native(source(Vec3{0,5,0})));
    near(opposite.hit.normal, native(source(Vec3{-1,0,0})));
    near(opposite.distance, contact_advance(2.5));
    near(world.body(actor).position, {-3+contact_advance(2.5),5,0});
    world.body(actor).position = {3,5,0};
    assert(!world.move(actor, {}, {-4,0,0}, Mode::use_polygon).hit.trace_hit);
    near(world.body(actor).position, {-1,5,0});
}

void world_missing_probe_hull() {
    WorldFixture fixture;
    const auto actor = fixture.box(unit_hull, {3,0,0});
    const auto sprite = fixture.box(unit_hull, {100,0,0});
    fixture.scene.models[fixture.scene.entities[sprite].model_index].sprite.width = 1;
    c4::assets::WorldEntity missing;
    missing.model_index = c4::assets::no_world_model;
    const auto no_model = static_cast<std::uint32_t>(fixture.scene.entities.size());
    fixture.scene.entities.push_back(std::move(missing));
    PhysicsWorld world(fixture.scene);
    assert(world.body(sprite).flags.sprite);
    for (const auto probe : {no_model, sprite}) {
        invalid([&] {
            world.trace({3,0,0}, {-1,0,0},
                        Mode::use_box | Mode::ignore_me | Mode::ignore_sprites, probe);
        });
        invalid([&] { world.move(probe, {}, {-4,0,0}, Mode::ignore_sprites); });
    }
    // A missing-model target must not acquire a synthetic unit OBB either.
    world.set_minmax(actor);
    const auto moved = world.move(actor, {}, {-4,0,0}, Mode::ignore_sprites);
    assert(!moved.hit.trace_hit);
    near(world.body(actor).position, {-1,0,0});
}

void world_rotate_occupancy_rollback() {
    WorldFixture fixture;
    const auto actor = fixture.box({{-2,-.25,-.5},{2,.25,.5}});
    const auto obstacle = fixture.box({{-.25,-.25,-.25},{.25,.25,.25}}, {0,1.5,0});
    // This wall blocks both outward and secondary old-angle escape attempts.
    const auto blocker = fixture.box({{-.125,-4,-4},{.125,4,4}}, {-2.25,0,0});
    PhysicsWorld world(fixture.scene);
    world.set_minmax(actor);
    world.set_minmax(obstacle);
    world.set_minmax(blocker);
    const auto hull = world.body(actor).hull;
    unsigned callbacks = 0;
    world.on_entity = [&](const EntityEvent& event) {
        ++callbacks;
        assert(event.me == actor && (event.you == obstacle || event.you == blocker));
        assert(length(event.normal) > 1-2.0/1024 && length(event.normal) < 1+2.0/1024);
        near(event.normal, sdk_value(event.normal));
    };
    assert(!world.rotate(actor, {90,0,0}, Mode::none));
    assert(callbacks >= 1);
    near(world.body(actor).angles, {0,0,0});
    near(world.body(actor).position, {0,0,0});
    near(world.body(actor).hull.min, hull.min);
    near(world.body(actor).hull.max, hull.max);
    const auto blocked_callbacks = callbacks;
    assert(world.rotate(actor, {10,0,0}, Mode::none));
    near(world.body(actor).angles, {10,0,0});
    assert(callbacks == blocked_callbacks);
}

void world_rotate_escape_displacement() {
    WorldFixture fixture;
    const auto actor = fixture.box({{-2,-.25,-.5},{2,.25,.5}});
    const auto obstacle = fixture.box({{-.25,-.25,-.25},{.25,.25,.25}}, {0,1.5,0});
    PhysicsWorld world(fixture.scene);
    world.set_minmax(actor);
    world.set_minmax(obstacle);
    // Escape clears occupancy; recovery approaches the obstacle with the rotated hull.
    assert(world.rotate(actor, {90,0,0}, Mode::none));
    near(world.body(actor).angles, {90,0,0});
    const double step = (std::sqrt(17.25)*.5-.25)*.5;
    const double escaped_x = sdk_value(-step);
    near(world.body(actor).position, sdk_value(Vec3{escaped_x+contact_advance(-escaped_x-.5),0,0}));
}

void fixed_point_conversion() {
    assert(Var::from_float(.1f).raw == 102);
    assert(Var::from_float(-.1f).raw == -102);
    assert(Var::from_float(.5f/1024).raw == 0);
    assert(Var::from_float(-.5f/1024).raw == 0);
    const auto exact = Var::from_float(-12.75f);
    assert(exact.raw == -13056);
    near(exact.to_float(), -12.75);
    assert(Var::from_float(exact.to_float()).raw == exact.raw);
    const auto truncated = Var::from_float(.1f);
    near(truncated.to_float(), 102.0/1024);
    assert(Var::from_float(truncated.to_float()).raw == truncated.raw);
    // vec_scale product rounding has different half-tie behavior from _var(float).
    assert(Var{1}.scaled(Var{512}).raw == 1);
    assert(Var{-1}.scaled(Var{512}).raw == 0);
    assert(Var{3}.scaled(Var{512}).raw == 2);
    assert(Var{-3}.scaled(Var{512}).raw == -1);
}

void source_numeric_contract() {
    using namespace c4::physics::literals;
    static_assert(!std::is_convertible_v<double, Var>);
    static_assert(!std::is_convertible_v<Var, double>);
    assert(v(.689).raw == 705);
    assert((0.689_v).raw == 705);
    assert((1_v).raw == 1024);
    const Var large{16777217};
    assert(number(large) == 16777217.0/1024);
    assert(number(Var{-16777217}) == -16777217.0/1024);
    assert(Var::from_float(static_cast<float>(number(large))).raw == 16777216);

    const Var body_raw{28349727};
    const Vec3 body_publication{number(body_raw), number(-body_raw), 1};
    const auto body_imported = body_source(body_publication);
    assert(body_imported.x.raw == body_raw.raw &&
           body_imported.y.raw == -body_raw.raw && body_imported.z.raw == 1024);
    const auto sdk_imported = source(body_publication);
    assert(sdk_imported.x.raw != body_imported.x.raw &&
           sdk_imported.y.raw != body_imported.y.raw);
    assert(std::abs(sdk_imported.x.raw - body_imported.x.raw) == 1 &&
           std::abs(sdk_imported.y.raw - body_imported.y.raw) == 1);
    assert(property(number(body_raw)).raw == body_raw.raw);
    assert(property(-1.5/1024).raw == -1);

    WorldFixture fixture;
    const auto exact_pose = fixture.box(unit_hull);
    const std::array<std::int32_t,3> pose_words{28349727,-33124955,1537};
    const Vec3 pose{number(Var{pose_words[0]}), number(Var{pose_words[1]}),
                    number(Var{pose_words[2]})};
    const auto float_pose = fixture.box(unit_hull, pose);
    fixture.scene.entities[exact_pose].position = fixture.scene.entities[float_pose].position;
    fixture.scene.entities[exact_pose].source_position_words = pose_words;
    PhysicsWorld pose_world(fixture.scene);
    near(pose_world.body(exact_pose).position, pose);
    near(pose_world.body(float_pose).position, native(source(pose)));
    assert(pose_world.body(exact_pose).position.x != pose_world.body(float_pose).position.x);
    assert(pose_world.body(exact_pose).position.y != pose_world.body(float_pose).position.y);
    const auto restored = body_source(pose_world.body(exact_pose).position);
    assert(restored.x.raw == pose_words[0] && restored.y.raw == pose_words[1] &&
           restored.z.raw == pose_words[2]);

    const Var maximum{std::numeric_limits<std::int32_t>::max()};
    const Var minimum{std::numeric_limits<std::int32_t>::min()};
    assert((maximum + Var{1}).raw == minimum.raw);
    assert((minimum - Var{1}).raw == maximum.raw);
    assert((-minimum).raw == minimum.raw);
    assert((+large).raw == large.raw);
    assert((Var{-1} * Var{1}).raw == 0);
    assert((Var{-65537} * Var{65537}).raw == -4194433);
    assert((std::int64_t{-65537} * 65537)/1024 == -4194432);
    assert((maximum * 2_v).raw == -2);
    assert((Var{-1} / Var{3}).raw == -341);
    assert((Var{1} / Var{-3}).raw == -341);

    Var counter = 1_v;
    assert((counter++).raw == 1024 && counter.raw == 2048);
    assert((--counter).raw == 1024);
    assert((++counter).raw == 2048);
    assert((counter--).raw == 2048 && counter.raw == 1024);
    counter += 2_v;
    counter -= 1_v;
    counter *= 3_v;
    counter /= 2_v;
    assert(counter == 3_v && counter > 2_v && 2_v < counter);
    assert(counter >= 3_v && counter <= 3_v && counter != 0_v);
    assert(static_cast<bool>(counter) && !Var{});
    assert(var_abs(-2_v) == 2_v);
    assert(var_sign(-2_v) == -1_v && var_sign(Var{}) == 0_v);
    assert(var_sign(2_v) == 1_v);
    assert(var_min(2_v, 3_v) == 2_v && var_max(2_v, 3_v) == 3_v);
    assert(var_clamp(4_v, 1_v, 3_v) == 3_v);
    assert(var_clamp(0_v, 1_v, 3_v) == 1_v);
    assert(var_clamp(2_v, 3_v, 1_v) == 1_v);
    assert(var_integer(v(-1.75)) == -1_v);
    assert(var_integer(v(1.75)) == 1_v);
    assert(var_frc(Var{-1025}).raw == -1);
    assert(var_ang(180_v) == -180_v);

    const ScriptVec odd{Var{1}, Var{-1}, Var{3}};
    const auto scaled = var_vec_scale(odd, v(.5));
    assert(scaled.x.raw == 1 && scaled.y.raw == 0 && scaled.z.raw == 2);
    const auto combined = (odd + odd) - odd;
    assert(combined.x.raw == odd.x.raw && combined.y.raw == odd.y.raw &&
           combined.z.raw == odd.z.raw);
    const auto published = native(ScriptVec{large, -large, 1_v});
    assert(published.x == number(large) && published.y == number(-large) &&
           published.z == 1);
    const auto imported = source(published);
    assert(imported.x.raw == 16777216 && imported.y.raw == -16777216 &&
           imported.z.raw == 1024);

    bool zero_fault = false;
    try { (void)(1_v / 0_v); }
    catch (const std::domain_error&) { zero_fault = true; }
    assert(zero_fault);
    bool overflow_fault = false;
    try { (void)(maximum / Var{1}); }
    catch (const std::overflow_error&) { overflow_fault = true; }
    assert(overflow_fault);
    for (float bad : {std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN(), 2097152.f}) {
        bool import_fault = false;
        try { (void)Var::from_float(bad); }
        catch (const std::out_of_range&) { import_fault = true; }
        assert(import_fault);
    }
}

void native_math_contract() {
    using namespace c4::physics::literals;
    assert(var_sin(0_v) == 0_v && var_sin(90_v) == 1_v);
    assert(var_cos(0_v) == 1_v && var_cos(180_v) == -1_v);
    assert(var_sin(30_v).raw == 511); // Recovered signed16 angle quantization.
    assert(var_ang(Var{1}).raw == 0 && var_ang(1_v).raw == 1023);
    // Original public SDK oracle, nominal masked CW0x027f/ftolflag1.
    const std::int32_t angle_inputs[]{256, 512, 1023, 1024, 184319,
                                     std::numeric_limits<std::int32_t>::max(),
                                     std::numeric_limits<std::int32_t>::min()};
    const std::int32_t angle_outputs[]{253, 511, 1018, 1023, 184314, 155643, -155643};
    for (std::size_t i = 0; i < 7; ++i)
        assert(var_ang(Var{angle_inputs[i]}).raw == angle_outputs[i]);
    assert(var_sqrt(0_v) == 0_v);
    assert(var_sqrt(4_v) == 2_v);
    assert(var_asin(2_v) == 90_v && var_asin(-2_v) == -90_v);
    assert(var_vec_length({3_v, 4_v, 0_v}) == 5_v);
    assert(var_modulo(Var{-1025}, Var{1024}).raw == 1023);
    assert(var_modulo(Var{-1025}, Var{-1024}).raw == 1023);
    assert(var_modulo(Var{1025}, Var{1}).raw == 0);
    bool modulo_overflow = false, modulo_zero = false;
    try { (void)var_modulo(Var{-1025}, Var{1}); }
    catch (const std::overflow_error&) { modulo_overflow = true; }
    try { (void)var_modulo(1_v, 0_v); }
    catch (const std::domain_error&) { modulo_zero = true; }
    assert(modulo_overflow && modulo_zero);
    assert(var_sqrt(-1_v).raw == std::numeric_limits<std::int32_t>::min());
    assert(var_sqrt(Var{std::numeric_limits<std::int32_t>::min()}).raw ==
           std::numeric_limits<std::int32_t>::min());

    const ScriptVec previous{12_v, 34_v, 56_v};
    const auto degenerate = var_vec_to_angle({Var{1}, {}, {}}, previous);
    assert(degenerate.x == previous.x && degenerate.y == previous.y && degenerate.z == previous.z);
    const auto negative_y = var_vec_to_angle({0_v, -1_v, 0_v}, previous);
    assert(negative_y.x == 270_v && negative_y.y == 0_v && negative_y.z == previous.z);
    const auto positive_z = var_vec_to_angle({0_v, 0_v, 1_v}, previous);
    assert(positive_z.x == 270_v && positive_z.y == 90_v && positive_z.z == previous.z);
    const auto same_raw = [](ScriptVec actual, ScriptVec expected) {
        assert(actual.x == expected.x && actual.y == expected.y && actual.z == expected.z);
    };
    // Observed initialized original engine basis results, not idealized trig.
    const ScriptVec angles{30_v, 20_v, 10_v};
    same_raw(var_vec_rotate({1_v, 0_v, 0_v}, angles), {Var{833}, Var{481}, Var{350}});
    same_raw(var_vec_rotate({0_v, 1_v, 0_v}, angles), {Var{-556}, Var{842}, Var{167}});
    same_raw(var_vec_rotate({0_v, 0_v, 1_v}, angles), {Var{-209}, Var{-326}, Var{947}});
    same_raw(var_vec_rotate({1_v, 0_v, 0_v}, {45_v, 0_v, 0_v}), {Var{724}, Var{724}, {}});
    const auto axes = rotation_axes({30, 20, 10});
    const auto float_rotation = rotate_vector({1, 0, 0}, {30, 20, 10});
    assert(float_rotation.x == axes[0].x && float_rotation.y == axes[0].y && float_rotation.z == axes[0].z);
    assert(float_rotation.x*1024 != std::trunc(float_rotation.x*1024));
    same_raw(source(float_rotation), var_vec_rotate({1_v, 0_v, 0_v}, angles));
}

void source_matrix_inverse_contract() {
    // Algebraic regressions, NOT original-runtime or instruction-emulated goldens.
    const auto inverse_product = [](const std::array<float, 16>& matrix,
                                    const std::array<float, 16>& inverse) {
        for (bool reverse : {false, true}) {
            const auto& a = reverse ? inverse : matrix;
            const auto& b = reverse ? matrix : inverse;
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    double sum = 0;
                    for (std::size_t k = 0; k < 4; ++k)
                        sum += static_cast<double>(a[row*4+k])*b[k*4+column];
                    assert(std::isfinite(sum));
                    assert(std::abs(sum - (row == column ? 1.0 : 0.0)) < 0.0002);
                }
            }
        }
    };
    const std::array<float, 16> affine{
        2,1,-1,0, 0,4,2,0, 0,0,-.5f,0, 3,-7,11,1};
    const std::array<float, 16> affine_inverse{
        .5f,-.125f,-1.5f,0, 0,.25f,1,0, 0,0,-2,0, -1.5f,2.125f,33.5f,1};
    std::array<float, 16> result;
    assert(source_matrix_inverse(affine, result));
    // Mathematical reference, not an RCPSS hardware-estimate golden.
    for (std::size_t i = 0; i < result.size(); ++i)
        assert(std::abs(static_cast<double>(result[i]) - affine_inverse[i]) <
               0.00001*std::max(1.0, std::abs(static_cast<double>(affine_inverse[i]))));
    inverse_product(affine, result);
    auto alias = affine;
    assert(source_matrix_inverse(alias, alias));
    assert(alias == result);
    const std::array<float, 16> projective{
        2,1,0,.25f, -1,3,2,-.5f, .5f,0,4,.125f, 3,-7,11,2};
    assert(source_matrix_inverse(projective, result));
    inverse_product(projective, result);
    // Canonical CameraProjection.cpp source_view_matrix assembly followed by
    // SourceLighting's XZY input-column swap, using existing rounded axes.
    // This is a native producer fixture, not an observed original-game matrix.
    const Vec3 position{123.25,-47.5,81.75};
    const auto axes = rotation_axes({37.125,-12.375,4.25});
    const auto forward = axes[0], right = axes[1]*-1, up = axes[2];
    const std::array<float, 16> rounded_view{
        float(right.x),float(up.x),float(forward.x),0,
        float(right.z),float(up.z),float(forward.z),0,
        float(right.y),float(up.y),float(forward.y),0,
        float(-dot(right,position)),float(-dot(up,position)),float(-dot(forward,position)),1};
    assert(dot(forward,forward) != 1 || dot(right,right) != 1 || dot(up,up) != 1);
    assert(source_matrix_inverse(rounded_view, result));
    inverse_product(rounded_view, result);
    bool differs_from_rotation_transpose = false;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            differs_from_rotation_transpose |= result[row*4+column] != rounded_view[column*4+row];
    assert(differs_from_rotation_transpose);
    auto singular = affine;
    singular[10] = 0;
    result.fill(-77);
    const auto unchanged = result;
    assert(!source_matrix_inverse(singular, result));
    assert(result == unchanged);
    const auto singular_before = singular;
    assert(!source_matrix_inverse(singular, singular));
    assert(singular == singular_before);
}

void world_default_hull_contract() {
    struct Selection {
        Bounds bounds;
        std::uint32_t flags;
        bool fat, narrow;
        double radius;
    };
    // Auto-selection uses the first-frame x+y span, with strict 16/96 cutoffs.
    for (const Selection selected : {
             Selection{{{-4,-4,-1},{4,4,1}}, 0, false, false, 4},
             Selection{{{-5,-4,-1},{5,4,1}}, 0, false, true, 16},
             Selection{{{-24,-24,-1},{24,24,1}}, 0, false, true, 16},
             Selection{{{-25,-24,-1},{25,24,1}}, 0, true, false, 32},
             Selection{{{-25,-24,-1},{25,24,1}}, 0x4000000, true, true, 25},
             Selection{{{-25,-24,-1},{25,24,1}}, 0x20000000, true, true, 25}}) {
        WorldFixture fixture;
        const auto actor = fixture.triangle(
            {{Vec3{0,0,0},Vec3{1,0,0},Vec3{0,1,1}}}, selected.bounds,
            {100,0,0}, selected.flags);
        const auto wall = fixture.triangle(
            {{Vec3{0,-200,-200},Vec3{0,200,-200},Vec3{0,0,200}}},
            {{0,-200,-200},{0,200,200}});
        PhysicsWorld world(fixture.scene);
        near(world.narrow_hull.min, {-16,-16,-16});
        near(world.narrow_hull.max, {16,16,16});
        near(world.fat_hull.min, {-32,-32,-16});
        near(world.fat_hull.max, {32,32,32});
        assert(world.body(actor).flags.fat == selected.fat);
        assert(world.body(actor).flags.narrow == selected.narrow);
        near(world.body(actor).hull.min, selected.bounds.min);
        near(world.body(actor).hull.max, selected.bounds.max);
        const auto trace = [&](double radius) {
            const auto hit = world.trace({100,0,0}, {-100,0,0},
                                         Mode::use_box | Mode::ignore_me, actor);
            assert(hit.trace_hit && !hit.initially_overlapping && hit.you == wall);
            near(hit.fraction, (100-radius)/200);
        };
        trace(selected.radius);
        // Selection reads the public globals at query time, not the stored model hull.
        world.narrow_hull = {{-7,-8,-9},{7,8,9}};
        world.fat_hull = {{-11,-12,-13},{11,12,13}};
        trace(selected.fat != selected.narrow ? (selected.fat ? 11 : 7)
                                              : selected.radius);
        world.set_minmax(actor);
        assert(world.body(actor).flags.fat && world.body(actor).flags.narrow);
        near(world.body(actor).hull.min, selected.bounds.min);
        near(world.body(actor).hull.max, selected.bounds.max);
        trace(selected.bounds.max.x);
    }

    WorldFixture fixture;
    const auto actor = fixture.triangle(
        {{Vec3{0,0,0},Vec3{1,0,0},Vec3{0,1,1}}},
        {{-200,-200,-200},{300,300,300}}, {}, 0);
    auto& asset = fixture.scene.models[fixture.scene.entities[actor].model_index];
    auto geometry = std::make_shared<c4::assets::Model>(*asset.model);
    auto& group = geometry->groups[0];
    group.frames[0].edits = {{3, {-3,-2,-4,0,0}}, {4, {5,4,6,0,0}}};
    group.frames.push_back({"later", {{3, {-1000,-1000,-1000,0,0}},
                                     {4, {2000,2000,2000,0,0}}}});
    asset.model = geometry;
    PhysicsWorld world(fixture.scene);
    // Raw source extrema and later frame extrema must not inflate the frame-zero hull.
    near(world.body(actor).hull.min, {-3,-2,-4});
    near(world.body(actor).hull.max, {5,4,6});
    assert(!world.body(actor).flags.fat && !world.body(actor).flags.narrow);
    near(world.vec_for_vertex(actor, 4), {-3,-2,-4});
    near(world.vec_for_vertex(actor, 5), {5,4,6});
    near(world.vec_for_vertex(actor, 4, {1,1,0}), {-1000,-1000,-1000});
    near(world.vec_for_vertex(actor, 5, {1,1,0}), {2000,2000,2000});
    // Live sampling sees changed geometry; set_minmax must reuse constructor-cached bounds.
    group.frames[0].edits = {{3, {-4000,-4000,-4000,0,0}},
                             {4, {5000,5000,5000,0,0}}};
    near(world.vec_for_vertex(actor, 4), {-4000,-4000,-4000});
    world.body(actor).scale = {-2,3,.5};
    world.set_minmax(actor);
    assert(world.body(actor).flags.fat && world.body(actor).flags.narrow);
    near(world.body(actor).hull.min, {-10,-6,-2});
    near(world.body(actor).hull.max, {6,12,3});
}
} // namespace

int main() {
    rays();
    spheres();
    invalid_and_degenerate();
    world_filters_move_and_event_snapshot();
    world_glide_finite_polygon();
    world_source_vertices_and_minmax();
    world_transformed_polygon_normal();
    world_nonpolygon_obb();
    world_vertex_only_hull();
    world_missing_probe_hull();
    world_rotate_occupancy_rollback();
    world_rotate_escape_displacement();
    world_default_hull_contract();
    fixed_point_conversion();
    source_numeric_contract();
    native_math_contract();
    source_matrix_inverse_contract();
}
