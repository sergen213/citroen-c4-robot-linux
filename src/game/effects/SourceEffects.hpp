#pragma once

#include "EffectFrame.hpp"
#include "physics/Geometry.hpp"
#include "physics/FixedPoint.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace c4::game {
struct GameContext;
struct EffectRequest;
struct FrameTiming;
struct SourceParticle {
    std::array<physics::Var,3> position{}, velocity{};
    physics::Var size{}, alpha{}, lifespan{}, gravity{};
    physics::Var initial_alpha{}, rise{}, acceleration{};
    std::array<physics::Var,3> previous_anchor{};
    std::string_view source;
    std::uint32_t texture{}, ordinal{};
    bool move{}, additive{}, bright{}, transparent{}, pending_initialization{};
};
class SourceEffects {
public:
    explicit SourceEffects(const std::filesystem::path& asset_root);
    ~SourceEffects();
    SourceEffects(const SourceEffects&)=delete;
    SourceEffects& operator=(const SourceEffects&)=delete;
    void initialize(GameContext& context);
    void emit(GameContext& context,const EffectRequest& request);
    void run_processes(GameContext& context,FrameTiming timing);
    void update(GameContext& context,FrameTiming timing);
    void publish_frame(GameContext& context);
    void clear();
    const EffectFrame& frame() const;
    std::string diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace c4::game
