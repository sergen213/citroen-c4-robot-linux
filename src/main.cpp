#include "core/settings.hpp"
#include "platform/runtime.hpp"
#include "AudioDiagnostics.hpp"

#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef C4_VERSION
#define C4_VERSION "development"
#endif

namespace {
void inspect_directory(c4::Settings& settings) {
    if (settings.game_dir.empty() && !settings.inspection_requested && !settings.diagnostics)
        throw std::runtime_error("Native gameplay requires an original game directory. Import your legally owned installer using the launcher or tools/import_assets.py, then pass --game-dir PATH; use --inspect for asset inspection.");
    if (!settings.game_dir.empty()) {
        settings.game_dir = std::filesystem::canonical(settings.game_dir);
        if (!std::filesystem::is_directory(settings.game_dir))
            throw std::runtime_error("--game-dir must identify a readable directory");
        std::size_t files = 0;
        std::size_t directories = 0;
        for (const auto& entry : std::filesystem::directory_iterator(settings.game_dir)) {
            if (entry.is_regular_file()) ++files;
            else if (entry.is_directory()) ++directories;
        }
        std::clog << "Read-only game directory: " << settings.game_dir
                  << " (" << files << " immediate files, " << directories << " directories)\n";
        if (!settings.inspection_requested && !settings.diagnostics) {
            auto archive = settings.game_dir / "kaynak_kap.rc4";
            if (!std::filesystem::is_regular_file(archive))
                archive = settings.game_dir / "app" / "kaynak_kap.rc4";
            if (std::filesystem::is_regular_file(archive))
                settings.world = archive.string() + "::bogazici.wmb";
        }
    }
    if (!settings.inspection_requested && !settings.diagnostics && settings.world.empty())
        throw std::runtime_error("Native gameplay requires kaynak_kap.rc4::bogazici.wmb in --game-dir or its app directory; use --inspect for an empty inspection window.");
    for (auto* selected : {&settings.mesh, &settings.texture, &settings.world, &settings.audio,
                           &settings.inspect_audio}) {
        if (selected->empty()) continue;
        const std::string selection = selected->string();
        const auto member_separator = selection.find("::");
        std::filesystem::path file = selection.substr(0, member_separator);
        if (file.is_relative() && !settings.game_dir.empty()) file = settings.game_dir / file;
        file = std::filesystem::canonical(file);
        if (!std::filesystem::is_regular_file(file))
            throw std::runtime_error("Selected asset container is not a regular file: " + file.string());
        std::ifstream stream(file, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot read selected asset container: " + file.string());
        *selected = member_separator == std::string::npos
            ? file.string() : file.string() + selection.substr(member_separator);
    }
    if (!settings.input_script.empty()) {
        if (settings.input_script.string().find("::") != std::string::npos)
            throw std::runtime_error("--input-script must identify a regular file, not an archive member");
        settings.input_script = std::filesystem::canonical(settings.input_script);
        if (!std::filesystem::is_regular_file(settings.input_script))
            throw std::runtime_error("--input-script must identify a readable regular file");
        std::ifstream stream(settings.input_script, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot read --input-script: " + settings.input_script.string());
    }
    if (!settings.game_dir.empty()) {
        for (const auto* output : {&settings.capture, &settings.diagnostics_json}) {
            if (output->empty()) continue;
            const auto destination = std::filesystem::weakly_canonical(*output);
            const auto within_assets = destination.lexically_relative(settings.game_dir);
            if (!within_assets.empty() && *within_assets.begin() != "..")
                throw std::runtime_error("Diagnostic output must be outside the read-only game directory");
        }
    }
}

void diagnostics() {
    const int runtime = SDL_GetVersion();
    std::cout << "Citroen C4 Robot native runtime " << C4_VERSION << '\n'
              << "Source compatibility remains under validation; these diagnostics report metadata only.\n"
              << "SDL compiled: " << SDL_MAJOR_VERSION << '.' << SDL_MINOR_VERSION << '.' << SDL_MICRO_VERSION << '\n'
              << "SDL runtime: " << SDL_VERSIONNUM_MAJOR(runtime) << '.' << SDL_VERSIONNUM_MINOR(runtime) << '.' << SDL_VERSIONNUM_MICRO(runtime) << '\n';
#if defined(C4_ENABLE_VULKAN) && C4_ENABLE_VULKAN
    std::cout << "Compiled renderer selections: OpenGL and independent Vulkan; active device/effects require window diagnostics.\n";
#else
    std::cout << "Compiled renderer selection: OpenGL; active device/effects require window diagnostics.\n";
#endif
    std::cout << "Available SDL video drivers:";
    for (int index = 0; index < SDL_GetNumVideoDrivers(); ++index)
        std::cout << ' ' << SDL_GetVideoDriver(index);
    std::cout << "\nNo window/context is created by --diagnostics; driver availability is not rendering verification.\n";
}
}

int main(int argc, char** argv) {
    try {
        auto arguments = c4::parse_arguments(argc, argv);
        if (arguments.help) {
            std::cout << c4::help_text();
            return 0;
        }
        auto& settings = arguments.settings;
        if (settings.graphics_capabilities) return c4::run_runtime(settings);
        if (settings.diagnostics) {
            diagnostics();
            return 0;
        }
        std::clog << (settings.inspection_requested ? "Native asset inspection" : "Source-native game")
                  << " runtime; source compatibility remains under validation. Original files are read-only.\n";
        inspect_directory(settings);
        if (!settings.inspect_audio.empty()) {
            c4::audio::run_audio_diagnostic(settings.inspect_audio, settings.audio_seconds,
                                          settings.audio_loop, std::cout);
            return 0;
        }
        int status = 0;
        do {
            status = c4::run_runtime(settings);
            c4::save_settings(c4::config_path(), settings);
            if (status == 75) std::clog << "Restarting native inspection window.\n";
        } while (status == 75);
        return status;
    } catch (const std::exception& error) {
        std::cerr << "citroen-c4-robot: " << error.what() << '\n';
        return 1;
    }
}
