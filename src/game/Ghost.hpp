#pragma once

#include "physics/NativeNumeric.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace c4::game {
class GhostSaveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
inline constexpr std::size_t ghost_capacity = 4999;
// The original loader rejects slot 4999 even when it contains the sentinel.
inline constexpr std::size_t ghost_import_capacity = 4998;
inline constexpr physics::Var ghost_sentinel{-666*1024};
struct GhostRecord {
    physics::Var time{};
    physics::ScriptVec position{}, angles{};
    int action{};
    physics::Var frame{};
    int mode{};
};
struct GhostTrack {
    std::string name;
    physics::Var record_ticks{};
    std::array<physics::Var,9> checkpoint_ticks{};
    // Real records only; the original ten-field sentinel is added by serialization.
    std::vector<GhostRecord> records;
};
struct GhostSample {
    GhostRecord record;
    int next_action{};
    physics::Var next_frame{}, blend{};
    bool action_transition{};
};
// Original bhlt.rc4 codec. Invalid, truncated and over-capacity input throws.
GhostTrack parse_ghost(std::string_view encoded);
std::string serialize_ghost(const GhostTrack& track);
// Only an absent file returns nullopt; invalid data and IO failures throw.
std::optional<GhostTrack> load_ghost(const std::filesystem::path& path);
std::filesystem::path local_ghost_path();
// Writes only to the XDG user-data path, via fsync + atomic rename.
// Rejects >ghost_import_capacity real records rather than committing an unloadable best.
void save_local_ghost(const GhostTrack& track);
// Non-selectable recovery archive; retains all 4999 native records in source codec bytes.
std::filesystem::path local_ghost_archive_path();
void save_ghost_archive(const GhostTrack& track);
std::optional<GhostTrack> load_ghost_archive();
// Sentinel is never interpolated; times outside the real-record interval return nullopt.
std::optional<GhostSample> sample_ghost(const GhostTrack& track, physics::Var ticks);
// Source alpha in percent; zero means invisible. Distance is to the camera target.
physics::Var ghost_alpha(physics::Var distance, physics::Var minimum_distance);
} // namespace c4::game
