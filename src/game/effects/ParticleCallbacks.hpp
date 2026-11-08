#pragma once

#include "SourceEffects.hpp"

namespace c4::game {
struct NitroParticleContext {
    std::array<std::array<physics::Var,3>,2> start{}, end{};
    bool active{};
    int freeze{};
};

void initialize_particle(GameContext& context, SourceParticle& particle,
                         std::string_view callback,
                         const NitroParticleContext& nitro = {});
// Original engine tail: called only after the particle's source property callback.
// Positive life is decremented before MOVE, but crossing zero removes next frame.
bool step_particle_engine(SourceParticle& particle, physics::Var ticks);
bool step_particle(GameContext& context, SourceParticle& particle, physics::Var ticks,
                   const NitroParticleContext& nitro = {});
} // namespace c4::game
