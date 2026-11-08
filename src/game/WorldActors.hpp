#pragma once

#include "GameState.hpp"
#include "WorldPaths.hpp"
#include "WorldAmbience.hpp"
#include "WorldLeaves.hpp"

#include <array>
#include <limits>

namespace c4::game {
struct WorldActorSourceOwner {
    std::string_view action, native_owner;
    unsigned source_begin, source_end;
};
// c4_effects.wdl ranges; water_yeni belongs to effect_define.wdl.
inline constexpr auto world_actor_source_owners=std::to_array<WorldActorSourceOwner>({
    {"animate_hiz","WorldActors",14,41},
    {"GMA_C4_BALON","WorldActors",109,114}, {"GMA_LOGO_BAL","WorldActors",102,107},
    {"GMA_bayrak","WorldActors",165,170}, {"GMA_bayrak2_c4","WorldActors",73,78},
    {"GMA_bayrak_drek","WorldActors",81,93}, {"GMA_bez_afis","WorldActors",158,163},
    {"GMA_dolap","WorldActors",123,128}, {"GMA_marti","WorldActors",130,135},
    {"GMA_park_ani","WorldActors",137,142}, {"GMA_pervane","WorldActors",116,121},
    {"GMA_tent24","WorldActors",151,156}, {"GMA_tunel_atmos","WorldActors",144,149},
    {"sadece_don","WorldActors",201,217},
    {"GMA_ag_plane1","WorldActors",328,336}, {"GMA_ag_plane2","WorldActors",338,346},
    {"GMA_ag_plane3","WorldActors",348,356}, {"GMA_over_cali_tek","WorldActors",368,376},
    {"GMA_over_cali_tek2","WorldActors",358,366}, {"GMA_calim","WorldActors",513,521},
    {"GMA_arka_a1","WorldActors",423,431}, {"GMA_arka_a2","WorldActors",433,441},
    {"GMA_arka_a3","WorldActors",443,451}, {"GMA_arka_a4","WorldActors",453,461},
    {"GMA_arka_a5","WorldActors",463,471}, {"GMA_arka_a6","WorldActors",473,481},
    {"GMA_arka_a7","WorldActors",483,491}, {"GMA_arka_a8","WorldActors",493,501},
    {"GMA_arka_a9","WorldActors",503,511},
    {"GMA_start_lamba","WorldActors",598,604}, {"GMA_start_lamba_gl","WorldActors",606,609},
    {"GMA_uc_ok","WorldActors",611,616}, {"GMA_ucok_av","WorldActors",618,623},
    {"GMA_ucok_tun","WorldActors",625,630}, {"GMA_loop_reklam_bez","WorldActors",999,1033},
    {"water_yeni","WorldActors/MaterialEvents",243,252}
});

// scheduler-contract.json: positive wait1 resumes one later eligible frame;
// negative seconds use a signed tick countdown, not simulation TIME_STEP.
struct WorldActorWait {
    Var remaining{};
    std::uint64_t yielded_frame{};
    void frame(FrameTiming timing);
    void seconds(Var duration, FrameTiming timing);
    bool resume(FrameTiming timing);
};

void prepare_world_actors(assets::WorldScene& world, SharedState& state,
                          std::vector<VisualState>& visuals);

class WorldActors {
public:
    explicit WorldActors(GameContext& context);
    // Proven original mode1 level_load queues initial records with remaining0.
    // No action body or wait runs here; first entry belongs to a later source pass.
    void initialize(GameContext& context, FrameTiming timing);
    void teardown(GameContext& context);
    void update(GameContext& context, FrameTiming timing);
    struct Coverage {
        bool queued{}, started{};
        std::size_t pending_entries{};
        std::size_t cycles{}, active_cycles{}, facing{}, active_facing{};
        std::size_t skins{}, active_skins{}, banners{}, active_banners{};
        std::size_t source_queue_size{};
        EntityId first_queued_id{no_entity};
        std::size_t water{}, materials{};
        WorldPaths::Coverage paths;
        WorldLeaves::Coverage leaves;
        EntityId cycle_id{no_entity}, skin_id{no_entity}, banner_id{no_entity};
        Var cycle_frame{}, skin_frame{}, banner_frame{}, banner_wait{};
        int skin_number{1};
        bool banner_reverse{};
    };
    Coverage coverage() const;
    EntityId queued_entity(std::size_t ordinal) const;
private:
    struct Cycle {
        EntityId id{};
        Var frame{}, speed{};
        WorldActorWait wait;
        bool wrapper{}, started{}, entered{}, random_on{};
    };
    struct Facing {
        EntityId id{};
        WorldActorWait wait;
        bool immediate{}, started{}, entered{};
    };
    struct Skin {
        EntityId id{}, partner{no_entity};
        Var frame{}, speed{};
        WorldActorWait wait;
        int skin{1};
        bool lamp{}, started{}, entered{};
    };
    enum class BannerStage { entry, startup, delay, moving };
    struct Banner {
        EntityId id{};
        Var frame{};
        WorldActorWait wait;
        BannerStage stage{BannerStage::entry};
        bool reverse{};
    };
    struct Water {
        EntityId id{};
        WorldActorWait wait;
        bool entered{};
    };
    struct Material {
        EntityId id{};
        WorldActorWait wait;
        bool entered{};
    };
    enum class ProcessKind { cycle, facing, skin, banner, water, material, path, leaf };
    struct Process {
        ProcessKind kind{};
        EntityId id{};
        std::size_t local{};
        bool staged_leaf{};
    };
    std::vector<Process> processes_;
    std::vector<std::size_t> source_queue_, destination_queue_;
    std::vector<std::size_t> entity_to_process_;
    std::vector<Cycle> cycles_;
    std::vector<Facing> facing_;
    std::vector<Skin> skins_;
    std::vector<Banner> banners_;
    std::vector<Water> water_;
    std::vector<Material> materials_;
    WorldLeaves leaves_;
    WorldPaths paths_;
    WorldAmbience ambience_;
    bool active_{};
    std::uint64_t last_frame_{std::numeric_limits<std::uint64_t>::max()};
    void cycle(GameContext&, Cycle&, FrameTiming);
    void face(GameContext&, Facing&, FrameTiming);
    void skin(GameContext&, Skin&, FrameTiming);
    void banner(GameContext&, Banner&, FrameTiming);
    bool dispatch(GameContext&, const Process&, FrameTiming);
};
} // namespace c4::game
