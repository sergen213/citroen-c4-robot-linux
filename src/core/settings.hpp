#pragma once

#include "rendering/quality/GraphicSettings.hpp"
#include "assets/SourceProfile.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace c4 {

struct Settings {
    std::filesystem::path game_dir;
    std::string renderer = "opengl";
    std::uint32_t rt_effects = 0;
    rendering::quality::GraphicSettings graphics;
    assets::SourceProfile source_profile = assets::original_source_profile;
    bool source_cgm_explicit = false;
    bool source_stq_explicit = false;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    int frame_limit = 60;
    std::uint64_t frames = 0;
    std::uint64_t source_frames = 0;
    std::filesystem::path capture;
    std::filesystem::path mesh;
    std::filesystem::path texture;
    std::filesystem::path world;
    std::filesystem::path audio;
    std::filesystem::path inspect_audio;
    double audio_seconds = 1.0;
    std::filesystem::path diagnostics_json;
    std::string animation;
    float percentage = 0.0f;
    bool percentage_supplied = false;
    bool audio_loop = false;
    bool inspection_requested = false;
    bool graphics_capabilities = false;
    std::filesystem::path input_script;
    bool diagnostics = false;
};

struct Arguments {
    Settings settings;
    bool help = false;
};

assets::SourceProfile effective_source_profile(const Settings& settings);
bool source_profile_requires_restart(const Settings& active, const Settings& requested,
                                     const assets::SourceProfile& loaded);
Arguments parse_arguments(int argc, char** argv);
std::filesystem::path config_path();
Settings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const Settings& settings);
std::string_view help_text();

} // namespace c4
