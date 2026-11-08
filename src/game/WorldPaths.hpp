#pragma once
#include "GameState.hpp"
#include <span>

namespace c4::game {
// c4_effects.wdl:725-832,1046-2274. Preload flags/gates only; queued first entries own scales/RNG.
void prepare_world_paths(assets::WorldScene&,SharedState&,std::vector<VisualState>&);
std::string_view world_paths_source_owner(std::string_view action);
class WorldPaths {
public:
    struct Coverage {
        std::size_t helicopters{},active_helicopters{},trios{},active_trios{},schools{},active_schools{},gated_entities{};
    };
    explicit WorldPaths(GameContext&);
    void initialize(GameContext&,FrameTiming);
    void update(GameContext&,FrameTiming);
    void teardown();
    Coverage coverage() const;
    std::span<const EntityId> bound_entities() const;
    bool dispatch_entity(GameContext&,FrameTiming,EntityId);
private:
    struct Trio {
        EntityId path{no_entity};
        std::array<EntityId,3> fish{no_entity,no_entity,no_entity};
        std::array<bool,3> registered{};
        std::array<Var,3> phase{};
        Var travel{},swim{},travel_speed{},swim_speed{};
        ScriptVec angles{};
        std::uint64_t last_frame{};
        bool enabled{},started{},path_registered{};
    };
    struct SchoolFish {
        Var phase{},travel{},swim{},timer{},speed{Var::literal(100)},target{Var::literal(100)},initial{Var::literal(100)};
    };
    struct School {
        EntityId path{no_entity};
        std::array<EntityId,6> fish{no_entity,no_entity,no_entity,no_entity,no_entity,no_entity};
        std::array<bool,6> registered{};
        std::array<SchoolFish,6> locals{};
        ScriptVec angles{};
        std::uint64_t last_frame{};
        bool enabled{},started{};
    };
    struct Helicopter {
        EntityId body{no_entity},rotor{no_entity},path{no_entity};
        Var cycle{},flicker{},travel{};
        ScriptVec angles{};
        std::uint64_t last_frame{},rotor_wait_frame{};
        bool started{},flip{},rotor_registered{},body_entered{},rotor_entered{},path_registered{};
    } heli_;
    std::array<Trio,4> trios_{};
    std::array<School,2> schools_{};
    bool live_{};
    std::uint64_t origin_frame_{};
    enum class Kind : std::uint8_t { finite_gate,heli_body,heli_rotor,heli_path,trio_path,trio_fish,school_path,school_fish };
    struct Binding { Kind kind{Kind::finite_gate};std::uint8_t group{},fish{}; };
    std::array<EntityId,33> bound_entities_{};
    std::array<Binding,33> bindings_{};
    std::array<bool,33> standalone_queued_{};
    std::size_t bound_entity_count_{};
    std::vector<std::int16_t> entity_to_record_;
    Coverage configured_{};
    void start_trio(GameContext&,Trio&);
    void start_school(GameContext&,School&);
    void update_trio(GameContext&,Trio&,Var);
    void update_school(GameContext&,School&,Var);
    void update_helicopter(GameContext&,Var);
};
} // namespace c4::game
