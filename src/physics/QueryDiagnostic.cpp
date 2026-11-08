#include "PhysicsWorld.hpp"
#include "NativeNumeric.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using c4::physics::Vec3;
using c4::physics::Mode;

constexpr Mode trace_mode = Mode::ignore_me | Mode::ignore_passable |
    Mode::ignore_content | Mode::ignore_push | Mode::ignore_sprites | Mode::ignore_flag2;

double parse_number(std::string_view text) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '-' || text.front() == '+')
            throw std::runtime_error("expected a complete finite numeric coordinate");
    }
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value))
        throw std::runtime_error("expected a complete finite numeric coordinate");
    return value;
}

std::uint32_t parse_index(std::string_view text) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("expected a complete unsigned integer index");
    return value;
}

std::uint32_t entity_index(std::string_view text, const c4::physics::PhysicsWorld& world) {
    const auto index = parse_index(text);
    if (index >= world.size()) throw std::runtime_error("entity index out of bounds");
    return index;
}

bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void print_vector(Vec3 value) {
    std::cout << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}

void print_hit(const c4::physics::Hit& hit, std::size_t entities) {
    if (!std::isfinite(hit.distance) || !std::isfinite(hit.fraction) ||
        !finite(hit.target) || !finite(hit.normal))
        throw std::runtime_error("query result exceeds finite numeric range");
    std::cout << "{\"scene_entities\":" << entities
              << ",\"trace_hit\":" << (hit.trace_hit ? "true" : "false")
              << ",\"you\":";
    if (hit.you == c4::physics::no_entity) std::cout << "null";
    else std::cout << hit.you;
    std::cout << ",\"distance\":" << hit.distance << ",\"fraction\":" << hit.fraction
              << ",\"target\":";
    print_vector(hit.target);
    std::cout << ",\"normal\":";
    print_vector(hit.normal);
    std::cout << ",\"initialoverlap\":" << (hit.initially_overlapping ? "true" : "false");
}

bool apply_car_hull(c4::physics::PhysicsWorld& world, const c4::assets::WorldScene& scene,
                    std::uint32_t entity) {
    if (scene.entities[entity].action != "aracgovde") return false;
    world.set_minmax(entity);
    world.body(entity).flags.polygon = false;
    auto& max_z = world.body(entity).hull.max.z;
    max_z = c4::physics::number(c4::physics::property(max_z) * c4::physics::v(0.5));
    return true;
}

void print_pose(const c4::physics::Body& body) {
    std::cout << "{\"position\":";
    print_vector(body.position);
    std::cout << ",\"angles\":";
    print_vector(body.angles);
    std::cout << '}';
}

void move_diagnostic(c4::physics::PhysicsWorld& world, const c4::assets::WorldScene& scene,
                     char** argv) {
    const auto entity = entity_index(argv[3], world);
    const auto steps = parse_index(argv[4]);
    if (steps < 1 || steps > 1024) throw std::runtime_error("steps must be in [1,1024]");
    const Vec3 delta{parse_number(argv[5]), parse_number(argv[6]), parse_number(argv[7])};
    if (!std::isfinite(c4::physics::length(delta)))
        throw std::runtime_error("movement exceeds finite numeric range");
    const bool source_car_hull = apply_car_hull(world, scene, entity);
    world.move_friction = 0;
    world.move_min_z = 0.4;
    constexpr auto mode = Mode::ignore_passable | Mode::ignore_content | Mode::ignore_push |
                          Mode::ignore_sprites | Mode::glide;
    std::vector<c4::physics::EntityEvent> events;
    world.on_entity = [&events](const c4::physics::EntityEvent& event) { events.push_back(event); };
    std::cout << "{\"scene_entities\":" << world.size() << ",\"entity\":" << entity
              << ",\"pose\":\"disk-placement\",\"source_car_hull_applied\":"
              << (source_car_hull ? "true" : "false")
              << ",\"move_friction\":0,\"move_min_z\":0.4,\"world_delta\":";
    print_vector(delta);
    std::cout << ",\"initial_pose\":";
    print_pose(world.body(entity));
    std::cout << ",\"steps\":[";
    for (std::uint32_t step = 0; step < steps; ++step) {
        if (!finite(world.body(entity).position + delta))
            throw std::runtime_error("movement destination exceeds finite numeric range");
        events.clear();
        const auto result = world.move(entity, {}, delta, mode);
        if (!std::isfinite(result.distance) || !finite(world.body(entity).position))
            throw std::runtime_error("movement result exceeds finite numeric range");
        if (step != 0) std::cout << ',';
        std::cout << "{\"step\":" << step + 1 << ",\"distance\":" << result.distance
                  << ",\"hit\":";
        print_hit(result.hit, world.size());
        std::cout << "},\"events\":[";
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            if (!finite(event.target) || !finite(event.normal))
                throw std::runtime_error("entity event exceeds finite numeric range");
            if (i != 0) std::cout << ',';
            std::cout << "{\"me\":" << event.me << ",\"you\":";
            if (event.you == c4::physics::no_entity) std::cout << "null";
            else std::cout << event.you;
            std::cout << ",\"target\":";
            print_vector(event.target);
            std::cout << ",\"normal\":";
            print_vector(event.normal);
            std::cout << '}';
        }
        std::cout << "],\"pose\":";
        print_pose(world.body(entity));
        std::cout << '}';
    }
    world.on_entity = {};
    std::cout << "],\"final_pose\":";
    print_pose(world.body(entity));
    std::cout << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view operation = argc > 2 ? argv[2] : "";
        const bool vertex = operation == "--vertex";
        const bool ground = operation == "--ground";
        const bool move = operation == "--move";
        if ((vertex && argc != 5) || (ground && argc != 4) || (move && argc != 8) ||
            (!vertex && !ground && !move && argc != 8 && argc != 10) ||
            (!vertex && !ground && !move && argc == 10 && std::string_view(argv[8]) != "me")) {
            std::cerr << "Usage: c4-physics-query WORLD FROMX FROMY FROMZ TOX TOY TOZ [me INDEX]\n"
                         "       c4-physics-query WORLD --vertex ENTITY ONE_BASED_VERTEX\n"
                         "       c4-physics-query WORLD --ground ENTITY\n"
                         "       c4-physics-query WORLD --move ENTITY STEPS DX DY DZ\n"
                         "WORLD accepts an original file or archive::member. Entity indices are zero-based.\n"
                         "Ground/move diagnostics retain disk pose; only aracgovde applies source car hull initialization.\n";
            return 2;
        }
        const auto scene = c4::assets::load_world(argv[1]);
        c4::physics::PhysicsWorld world(scene);
        std::cout << std::setprecision(17);
        if (move) {
            move_diagnostic(world, scene, argv);
            return 0;
        }
        if (vertex) {
            const auto entity = entity_index(argv[3], world);
            const auto index = parse_index(argv[4]);
            if (index == 0) throw std::runtime_error("vertex index must be one-based");
            const auto position = world.vec_for_vertex(entity, index);
            if (!finite(position)) throw std::runtime_error("vertex exceeds finite numeric range");
            std::cout << "{\"scene_entities\":" << world.size() << ",\"entity\":" << entity
                      << ",\"one_based_vertex\":" << index << ",\"vertex\":";
            print_vector(position);
            std::cout << "}\n";
            return 0;
        }

        Vec3 from{}, to{};
        auto mode = trace_mode;
        std::uint32_t me = c4::physics::no_entity;
        bool source_car_hull = false;
        if (ground) {
            me = entity_index(argv[3], world);
            source_car_hull = apply_car_hull(world, scene, me);
            from = world.body(me).position;
            to = from + Vec3{0, 0, -1000};
            mode = mode | Mode::use_box;
        } else {
            from = {parse_number(argv[2]), parse_number(argv[3]), parse_number(argv[4])};
            to = {parse_number(argv[5]), parse_number(argv[6]), parse_number(argv[7])};
            if (argc == 10) {
                me = entity_index(argv[9], world);
                mode = mode | Mode::use_box;
            }
        }
        if (!finite(from) || !finite(to) || !finite(to - from))
            throw std::runtime_error("query displacement exceeds finite numeric range");
        const auto hit = world.trace(from, to, mode, me);
        print_hit(hit, world.size());
        if (ground) {
            std::cout << ",\"entity\":" << me << ",\"pose\":\"disk-placement\""
                      << ",\"source_car_hull_applied\":" << (source_car_hull ? "true" : "false")
                      << ",\"from\":";
            print_vector(from);
            std::cout << ",\"to\":";
            print_vector(to);
        }
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "c4-physics-query: " << error.what() << '\n';
        return 1;
    }
}
