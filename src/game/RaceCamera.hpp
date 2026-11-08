#pragma once
#include "GameState.hpp"
#include "RecoveredLogic.hpp"
#include <memory>
#include <string>

namespace c4::game {
struct GateObservation { bool eligible{}; int side_sum{}; };
// Actual ordered XY gate endpoints and four source camera-target model vertices.
GateObservation observe_race_gate(GameContext& context, int gate);
class RaceCamera {
public:
    RaceCamera();
    ~RaceCamera();
    RaceCamera(const RaceCamera&)=delete;
    RaceCamera& operator=(const RaceCamera&)=delete;
    void initialize(GameContext& context);
    void reset(GameContext& context);
    void update_early(GameContext& context, const InputSnapshot& input, const FrameTiming& timing);
    void update_late(GameContext& context, const InputSnapshot& input, const FrameTiming& timing);
    std::string diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace c4::game
