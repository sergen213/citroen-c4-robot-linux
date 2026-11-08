#pragma once

#include "Geometry.hpp"
#include "assets/World.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace c4::physics {
inline constexpr std::uint32_t no_entity = std::numeric_limits<std::uint32_t>::max();
// Semantic native modes; these are not the engine's var-encoded ABI bit values.
enum class Mode : std::uint32_t {
    none=0, ignore_me=1, ignore_you=2, ignore_passable=4, ignore_content=8,
    ignore_push=16, ignore_sprites=32, ignore_flag2=64, use_box=128,
    glide=256, use_polygon=512
};
constexpr Mode operator|(Mode a, Mode b) { return Mode(std::uint32_t(a)|std::uint32_t(b)); }
constexpr bool has(Mode mode, Mode bit) { return (std::uint32_t(mode)&std::uint32_t(bit))!=0; }
struct EntityFlags {
    bool passable{}, polygon{}, flag2{}, flag4{}, sprite{}, fat{}, narrow{};
};
struct Body {
    Vec3 position{}, angles{}, scale{};
    Bounds hull{};
    EntityFlags flags{};
    double push{};
    std::uint32_t group{};
};
struct Hit {
    bool trace_hit{}, initially_overlapping{};
    std::uint32_t you{no_entity};
    double distance{}, fraction{1};
    Vec3 target{}, normal{};
};
struct MoveResult { double distance{}; Hit hit{}; };
struct EntityEvent {
    std::uint32_t me{no_entity}, you{no_entity};
    Vec3 target{}, normal{};
};
// Concrete original-asset query consumer. Engine float/fixed-point precision and
// response parity are documented separately in work/reports/physics; never implied.
class PhysicsWorld {
public:
    explicit PhysicsWorld(const assets::WorldScene& scene);
    const Body& body(std::uint32_t entity) const;
    Body& body(std::uint32_t entity);
    std::size_t size() const noexcept { return bodies_.size(); }
    void set_minmax(std::uint32_t entity);
    Vec3 vec_for_vertex(std::uint32_t entity, std::uint32_t one_based_vertex,
                        assets::AnimationSample sample = {}, std::uint32_t group = 0) const;
    Hit trace(Vec3 from, Vec3 to, Mode mode, std::uint32_t me=no_entity,
              std::uint32_t you=no_entity) const;
    MoveResult move(std::uint32_t entity, Vec3 relative_distance, Vec3 absolute_distance,
                    Mode mode, std::uint32_t you=no_entity);
    bool rotate(std::uint32_t entity, Vec3 angle, Mode mode,
                std::uint32_t you=no_entity);
    // Original caller enables EVENT_ENTITY explicitly. Synchronous callback receives
    // its own me/you/target/normal snapshot rather than mutable global query state.
    std::function<void(const EntityEvent&)> on_entity;
    double move_friction{0.25};
    double move_min_z{-1};
    bool disable_z_glide{};
    // Original registered defaults. Script bootstrap changes narrow_hull after load.
    Bounds narrow_hull{{-16,-16,-16},{16,16,16}};
    Bounds fat_hull{{-32,-32,-16},{32,32,32}};
private:
    struct ModelCollider {
        std::vector<Triangle> triangles;
        Bounds bounds{}, polygon_bounds{};
        bool initialized{};
    };
    struct EntityCollider {
        std::uint32_t model_index{assets::no_world_model};
        mutable bool cached{};
        mutable Vec3 cached_position{}, cached_angles{}, cached_scale{};
        mutable std::array<Vec3,3> axes{};
        mutable Bounds world_polygon_bounds{};
    };
    // Borrowed scene identity/geometry must outlive this world and remain unchanged.
    const assets::WorldScene* scene_;
    std::vector<Body> bodies_;
    // Model geometry is immutable after construction; pose caches remain single-threaded.
    std::vector<ModelCollider> model_colliders_;
    std::vector<EntityCollider> colliders_;
    bool filtered(std::uint32_t candidate, Mode mode, std::uint32_t me,
                  std::uint32_t you) const;
    Hit query(Vec3 from, Vec3 delta, Mode mode, std::uint32_t me,
              std::uint32_t you, bool moving) const;
    bool occupied(std::uint32_t entity, Mode mode, std::uint32_t you, Hit& hit) const;
    void event(std::uint32_t entity, const Hit& hit) const;
    Body collision_body(std::uint32_t entity) const;
    void cache_transform(std::uint32_t entity) const;
};
// Native Euler transform in original Z-up units; evidence fixes handedness/order.
Vec3 rotate_vector(Vec3 vector, Vec3 pan_tilt_roll);
} // namespace c4::physics
