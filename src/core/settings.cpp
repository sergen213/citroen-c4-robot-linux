#include "settings.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace c4 {
namespace {

using namespace rendering::quality;
using assets::SourceProfile;

constexpr std::array<std::pair<std::string_view, std::string_view>, 11> graphics_options{{
    {"--msaa", "msaa"}, {"--post-aa", "post_aa"}, {"--ssao", "ssao"},
    {"--shadows", "shadows"}, {"--pbr", "pbr"}, {"--internal-hdr", "internal_hdr"},
    {"--bloom", "bloom"}, {"--exposure-ev", "exposure_ev"}, {"--tone-map", "tone_map"},
    {"--texture-filter", "texture_filter"}, {"--denoising", "denoising"}
}};

struct SourceOption {
    std::string_view flag, key;
    std::uint8_t SourceProfile::* member;
    unsigned maximum;
};
constexpr std::array<SourceOption, 7> source_options{{
    {"--source-atq", "source_atq", &assets::SourceProfile::atq, 2},
    {"--source-mtq", "source_mtq", &assets::SourceProfile::mtq, 2},
    {"--source-stq", "source_stq", &assets::SourceProfile::stq, 3},
    {"--source-cgm", "source_cgm", &assets::SourceProfile::cgm, 3},
    {"--source-shq", "source_shq", &assets::SourceProfile::shq, 2},
    {"--source-ano", "source_ano", &assets::SourceProfile::ano, 1},
    {"--source-bloom", "source_bloom", &assets::SourceProfile::bloom, 1}
}};

[[noreturn]] void invalid(std::string_view field, std::string_view reason) {
    throw std::runtime_error(std::string(field) + ": " + std::string(reason));
}

template<class T>
T checked_range(T value, T minimum, T maximum, std::string_view field) {
    if (value < minimum || value > maximum)
        invalid(field, "expected an integer in " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return value;
}

template<class T>
T number(std::string_view value, T minimum, T maximum, std::string_view field) {
    if (value.empty()) invalid(field, "expected a nonempty integer");
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        invalid(field, "expected an integer in " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return checked_range(result, minimum, maximum, field);
}

void source_option(Settings& settings, std::string_view key, std::string_view value) {
    for (const auto& option : source_options) {
        if (key != option.key) continue;
        settings.source_profile.*option.member =
            static_cast<std::uint8_t>(number<unsigned>(value, 0, option.maximum, key));
        if (option.member == &SourceProfile::cgm) settings.source_cgm_explicit = true;
        if (option.member == &SourceProfile::stq) settings.source_stq_explicit = true;
        return;
    }
    invalid(key, "unknown original source selector");
}

double decimal(std::string_view value, double minimum, double maximum, std::string_view field) {
    if (value.empty()) invalid(field, "expected a nonempty decimal");
    double result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result,
                                        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || !std::isfinite(result) || result < minimum || result > maximum)
        invalid(field, "expected a finite decimal in " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return result;
}

void material_name(std::string_view name) {
    if (name.empty()) invalid("material_override", "expected a nonempty exact material name");
    for (const unsigned char byte : name)
        if (byte < 32 || byte == 127 || byte == '=')
            invalid("material_override", "material name contains a control byte or '=' delimiter");
}

void material_override(GraphicSettings& graphics, std::string_view value, bool replace) {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos)
        invalid("material_override", "expected NAME=metallic,roughness[,reflectivity]");
    const auto name = value.substr(0, separator);
    material_name(name);
    value.remove_prefix(separator + 1);
    const auto comma = value.find(',');
    if (comma == std::string_view::npos)
        invalid("material_override", "expected metallic,roughness[,reflectivity]");
    ModernMaterialOverride material;
    material.material_name = name;
    material.metallic = static_cast<float>(decimal(value.substr(0, comma), 0, 1, "material_override"));
    value.remove_prefix(comma + 1);
    const auto reflectivity = value.find(',');
    material.roughness = static_cast<float>(decimal(value.substr(0, reflectivity), 0, 1, "material_override"));
    if (reflectivity != std::string_view::npos)
        material.reflectivity = static_cast<float>(decimal(value.substr(reflectivity + 1), 0, 1, "material_override"));
    for (auto& existing : graphics.material_overrides) {
        if (existing.material_name != material.material_name) continue;
        if (!replace) invalid("material_override", "duplicate exact material name");
        existing = std::move(material);
        return;
    }
    graphics.material_overrides.push_back(std::move(material));
}

void material_scalar(float value) {
    if (!std::isfinite(value) || value < 0 || value > 1)
        invalid("material_override", "expected a finite decimal in 0..1");
}

bool toggle(std::string_view value, std::string_view field) {
    if (value == "on") return true;
    if (value == "off") return false;
    invalid(field, "expected on or off");
}

void renderer(std::string_view value) {
    if (value != "opengl" && value != "vulkan") invalid("renderer", "expected opengl or vulkan");
}

Preset preset(std::string_view value, std::string_view field) {
    if (value == "original") return Preset::Original;
    if (value == "enhanced") return Preset::Enhanced;
    if (value == "ultra") return Preset::Ultra;
    if (value == "custom") return Preset::Custom;
    invalid(field, "expected original, enhanced, ultra or custom");
}

void graphic_option(GraphicSettings& graphics, std::string_view key, std::string_view value) {
    if (key == "quality") graphics.preset = preset(value, key);
    else if (key == "msaa") {
        const auto samples = number<unsigned>(value, 1, 8, key);
        if (samples != 1 && samples != 2 && samples != 4 && samples != 8)
            invalid(key, "expected 1, 2, 4 or 8");
        graphics.samples = samples;
    } else if (key == "post_aa") {
        if (value == "off") graphics.post_aa = PostAA::Off;
        else if (value == "fxaa") graphics.post_aa = PostAA::Fxaa;
        else invalid(key, "expected off or fxaa");
    } else if (key == "ssao") graphics.ssao = toggle(value, key);
    else if (key == "shadows") {
        if (value == "off") graphics.shadows = ShadowQuality::Off;
        else if (value == "low") graphics.shadows = ShadowQuality::Low;
        else if (value == "high") graphics.shadows = ShadowQuality::High;
        else invalid(key, "expected off, low or high");
    } else if (key == "pbr") graphics.pbr = toggle(value, key);
    else if (key == "internal_hdr") graphics.internal_hdr = toggle(value, key);
    else if (key == "bloom") graphics.bloom = toggle(value, key);
    else if (key == "exposure_ev") graphics.exposure_ev = static_cast<float>(decimal(value, -10.0, 10.0, key));
    else if (key == "tone_map") {
        if (value == "reinhard") graphics.tone_mapping = ToneMapping::Reinhard;
        else if (value == "aces") graphics.tone_mapping = ToneMapping::Aces;
        else invalid(key, "expected reinhard or aces");
    } else if (key == "texture_filter") {
        if (value == "original") graphics.texture_filtering = TextureFiltering::Original;
        else if (value == "trilinear") graphics.texture_filtering = TextureFiltering::Trilinear;
        else if (value == "anisotropic") graphics.texture_filtering = TextureFiltering::Anisotropic;
        else invalid(key, "expected original, trilinear or anisotropic");
    } else if (key == "denoising") {
        if (value == "off") graphics.denoising = Denoising::Off;
        else if (value == "standard") graphics.denoising = Denoising::Standard;
        else if (value == "high") graphics.denoising = Denoising::High;
        else invalid(key, "expected off, standard or high");
    } else invalid(key, "unknown graphics field");
}

void validate_graphics(const GraphicSettings& graphics) {
    if (static_cast<unsigned>(graphics.preset) > static_cast<unsigned>(Preset::Custom))
        invalid("quality", "invalid preset");
    if (graphics.samples && *graphics.samples != 1 && *graphics.samples != 2
        && *graphics.samples != 4 && *graphics.samples != 8)
        invalid("msaa", "expected 1, 2, 4 or 8");
    if (graphics.post_aa && static_cast<unsigned>(*graphics.post_aa) > static_cast<unsigned>(PostAA::Fxaa))
        invalid("post_aa", "invalid post antialiasing");
    if (graphics.shadows && static_cast<unsigned>(*graphics.shadows) > static_cast<unsigned>(ShadowQuality::High))
        invalid("shadows", "invalid shadow quality");
    if (graphics.tone_mapping && static_cast<unsigned>(*graphics.tone_mapping) > static_cast<unsigned>(ToneMapping::Aces))
        invalid("tone_map", "invalid tone mapping");
    if (graphics.texture_filtering
        && static_cast<unsigned>(*graphics.texture_filtering) > static_cast<unsigned>(TextureFiltering::Anisotropic))
        invalid("texture_filter", "invalid texture filtering");
    if (graphics.denoising && static_cast<unsigned>(*graphics.denoising) > static_cast<unsigned>(Denoising::High))
        invalid("denoising", "invalid denoising quality");
    if (graphics.exposure_ev && (!std::isfinite(*graphics.exposure_ev)
        || *graphics.exposure_ev < -10.0f || *graphics.exposure_ev > 10.0f))
        invalid("exposure_ev", "expected a finite decimal in -10..10");
    for (std::size_t i = 0; i < graphics.material_overrides.size(); ++i) {
        const auto& material = graphics.material_overrides[i];
        material_name(material.material_name);
        material_scalar(material.metallic);
        material_scalar(material.roughness);
        if (material.reflectivity) material_scalar(*material.reflectivity);
        for (std::size_t previous = 0; previous < i; ++previous)
            if (graphics.material_overrides[previous].material_name == material.material_name)
                invalid("material_override", "duplicate exact material name");
    }
}

void path_string(std::string_view value, std::string_view field) {
    if (value.find('\0') != std::string_view::npos) invalid(field, "path contains a NUL byte");
}

std::string quoted_value(std::istream& input, std::string_view field) {
    input >> std::ws;
    if (input.peek() != '"') invalid(field, "expected a quoted string");
    std::string value;
    if (!(input >> std::quoted(value))) invalid(field, "unterminated quoted string");
    const auto next = input.peek();
    if (next != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(next)))
        invalid(field, "expected whitespace after quoted string");
    return value;
}

void validate_persistent(const Settings& settings) {
    renderer(settings.renderer);
    checked_range(settings.rt_effects, std::uint32_t{0}, std::uint32_t{7}, "rt_effects");
    if (settings.rt_effects && settings.renderer != "vulkan")
        invalid("rt_effects", "ray-tracing effects require renderer vulkan");
    checked_range(settings.width, 320, 7680, "width");
    checked_range(settings.height, 240, 4320, "height");
    checked_range(settings.frame_limit, 0, 1000, "frame_limit");
    path_string(settings.game_dir.native(), "game_dir");
    validate_graphics(settings.graphics);
    if (!assets::valid_source_profile(settings.source_profile))
        invalid("source_profile", "original source selectors exceed recovered bounds");
}

[[noreturn]] void file_error(std::string_view operation, const std::filesystem::path& path, int error) {
    throw std::system_error(error, std::generic_category(), std::string(operation) + " " + path.string());
}

struct TemporaryFile {
    std::string name;
    int descriptor = -1;

    explicit TemporaryFile(const std::filesystem::path& destination)
        : name(destination.string() + ".tmp.XXXXXX") {
        descriptor = ::mkstemp(name.data());
        if (descriptor < 0) file_error("create temporary config for", destination, errno);
    }
    ~TemporaryFile() {
        if (descriptor >= 0) ::close(descriptor);
        if (!name.empty()) ::unlink(name.c_str());
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
};

} // namespace

assets::SourceProfile effective_source_profile(const Settings& settings) {
    auto profile = settings.source_profile;
    if (settings.graphics.preset == rendering::quality::Preset::Enhanced ||
        settings.graphics.preset == rendering::quality::Preset::Ultra) {
        if (!settings.source_cgm_explicit) profile.cgm = 3;
        if (!settings.source_stq_explicit)
            profile.stq = settings.graphics.preset == rendering::quality::Preset::Ultra ? 3 : 2;
    }
    return profile;
}

bool source_profile_requires_restart(const Settings& active, const Settings& requested,
                                     const assets::SourceProfile& loaded) {
    // Modern preset changes apply to rendering now, and source defaults on the next world load.
    const bool changed = requested.source_profile != active.source_profile ||
                         requested.source_cgm_explicit != active.source_cgm_explicit ||
                         requested.source_stq_explicit != active.source_stq_explicit;
    return changed && effective_source_profile(requested) != loaded;
}

std::filesystem::path config_path() {
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        base = xdg;
        if (!base.is_absolute()) invalid("XDG_CONFIG_HOME", "must be an absolute path");
    } else {
        const char* home = std::getenv("HOME");
        if (!home || !std::filesystem::path(home).is_absolute())
            invalid("HOME", "must be an absolute path when XDG_CONFIG_HOME is unset or empty");
        base = std::filesystem::path(home) / ".config";
    }
    return base / "citroen-c4-robot" / "runtime.conf";
}

Settings load_settings(const std::filesystem::path& path) {
    path_string(path.native(), "config path");
    errno = 0;
    std::ifstream input(path);
    if (!input.is_open()) {
        const int error = errno;
        if (error == ENOENT) return {};
        file_error("open config", path, error ? error : EIO);
    }
    Settings settings;
    constexpr std::array<std::string_view, 29> keys{
        "version", "game_dir", "renderer", "width", "height", "fullscreen", "vsync", "frame_limit", "rt_effects",
        "quality", "msaa", "post_aa", "ssao", "shadows", "pbr", "internal_hdr", "bloom", "exposure_ev", "tone_map",
        "backend_override", "texture_filter", "denoising",
        "source_atq", "source_mtq", "source_stq", "source_cgm", "source_shq", "source_ano", "source_bloom"
    };
    unsigned seen = 0;
    std::string key;
    try {
        while (input >> key) {
            if (key == "material_override") {
                material_override(settings.graphics, quoted_value(input, key), false);
                continue;
            }
            std::size_t index = 0;
            while (index < keys.size() && keys[index] != key) ++index;
            if (index == keys.size()) invalid(key, "unknown config field");
            const unsigned bit = 1u << index;
            if (seen & bit) invalid(key, "duplicate config field");
            seen |= bit;
            if (key == "game_dir") {
                const auto value = quoted_value(input, key);
                path_string(value, key);
                settings.game_dir = value;
            } else if (key == "renderer") {
                const auto value = quoted_value(input, key);
                renderer(value);
                settings.renderer = value;
            } else {
                std::string value;
                if (!(input >> value)) invalid(key, "missing config value");
                if (key == "version") number<int>(value, 1, 1, key);
                else if (key == "width") settings.width = number<int>(value, 320, 7680, key);
                else if (key == "height") settings.height = number<int>(value, 240, 4320, key);
                else if (key == "fullscreen") settings.fullscreen = toggle(value, key);
                else if (key == "vsync") settings.vsync = toggle(value, key);
                else if (key == "frame_limit") settings.frame_limit = number<int>(value, 0, 1000, key);
                else if (key == "rt_effects") settings.rt_effects = number<std::uint32_t>(value, 0, 7, key);
                else if (key == "backend_override") settings.graphics.backend_override = toggle(value, key);
                else if (key.starts_with("source_")) source_option(settings, key, value);
                else graphic_option(settings.graphics, key, value);
            }
        }
        if (input.bad() || !input.eof()) invalid("config", "read failure");
        if (!(seen & 1u)) invalid("version", "missing config version");
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
    return settings;
}

void save_settings(const std::filesystem::path& path, const Settings& settings) {
    validate_persistent(settings);
    path_string(path.native(), "config path");
    std::ostringstream output;
    output << "version 1\n"
           << "game_dir " << std::quoted(settings.game_dir.string()) << '\n'
           << "renderer " << std::quoted(settings.renderer) << '\n'
           << "width " << settings.width << '\n'
           << "height " << settings.height << '\n'
           << "fullscreen " << (settings.fullscreen ? "on" : "off") << '\n'
           << "vsync " << (settings.vsync ? "on" : "off") << '\n'
           << "frame_limit " << settings.frame_limit << '\n'
           << "rt_effects " << settings.rt_effects << '\n';
    const auto& graphics = settings.graphics;
    output << "quality " << preset_name(graphics.preset) << '\n';
    if (graphics.backend_override) output << "backend_override on\n";
    if (graphics.samples) output << "msaa " << *graphics.samples << '\n';
    if (graphics.post_aa) output << "post_aa " << post_aa_name(*graphics.post_aa) << '\n';
    if (graphics.ssao) output << "ssao " << (*graphics.ssao ? "on" : "off") << '\n';
    if (graphics.shadows) output << "shadows " << shadow_quality_name(*graphics.shadows) << '\n';
    if (graphics.pbr) output << "pbr " << (*graphics.pbr ? "on" : "off") << '\n';
    if (graphics.internal_hdr) output << "internal_hdr " << (*graphics.internal_hdr ? "on" : "off") << '\n';
    if (graphics.bloom) output << "bloom " << (*graphics.bloom ? "on" : "off") << '\n';
    if (graphics.exposure_ev)
        output << "exposure_ev " << std::setprecision(std::numeric_limits<float>::max_digits10) << *graphics.exposure_ev << '\n';
    if (graphics.tone_mapping) output << "tone_map " << tone_mapping_name(*graphics.tone_mapping) << '\n';
    if (graphics.texture_filtering) output << "texture_filter " << texture_filtering_name(*graphics.texture_filtering) << '\n';
    if (graphics.denoising) output << "denoising " << denoising_name(*graphics.denoising) << '\n';
    for (const auto& material : graphics.material_overrides) {
        std::ostringstream record;
        record << material.material_name << '=' << std::setprecision(std::numeric_limits<float>::max_digits10)
               << material.metallic << ',' << material.roughness;
        if (material.reflectivity) record << ',' << *material.reflectivity;
        output << "material_override " << std::quoted(std::move(record).str()) << '\n';
    }
    for (const auto& option : source_options) {
        const auto value = settings.source_profile.*option.member;
        if (value != assets::original_source_profile.*option.member ||
            (option.member == &SourceProfile::cgm && settings.source_cgm_explicit) ||
            (option.member == &SourceProfile::stq && settings.source_stq_explicit))
            output << option.key << ' ' << static_cast<unsigned>(value) << '\n';
    }
    const std::string contents = std::move(output).str();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    TemporaryFile temporary(path);
    if (::fchmod(temporary.descriptor, 0600) != 0) file_error("set config permissions for", path, errno);
    std::size_t written = 0;
    while (written < contents.size()) {
        const auto count = ::write(temporary.descriptor, contents.data() + written, contents.size() - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            file_error("write config", path, errno);
        }
        if (count == 0) file_error("write config", path, EIO);
        written += static_cast<std::size_t>(count);
    }
    if (::fsync(temporary.descriptor) != 0) file_error("sync config", path, errno);
    const int descriptor = temporary.descriptor;
    temporary.descriptor = -1;
    if (::close(descriptor) != 0) file_error("close config", path, errno);
    if (::rename(temporary.name.c_str(), path.c_str()) != 0) file_error("replace config", path, errno);
    temporary.name.clear();
}

Arguments parse_arguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--help") return {{}, true};
    }
    Arguments arguments{load_settings(config_path()), false};
    Settings& settings = arguments.settings;
    int quality_argument = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--quality") continue;
        if (i + 1 >= argc || argv[i + 1][0] == '\0' || std::string_view(argv[i + 1]).starts_with("--"))
            invalid("--quality", "missing nonempty value");
        quality_argument = ++i;
    }
    if (quality_argument) {
        settings.graphics = {};
        settings.graphics.preset = preset(argv[quality_argument], "--quality");
        if (const auto defaults = profile_defaults(settings.graphics.preset)) {
            settings.renderer = defaults->backend == RendererBackend::OpenGL ? "opengl" : "vulkan";
            settings.rt_effects = defaults->rt_effects;
        }
    }
    bool frames_supplied = false;
    bool audio_seconds_supplied = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' || std::string_view(argv[i + 1]).starts_with("--"))
                invalid(option, "missing nonempty value");
            return argv[++i];
        };
        if (option == "--diagnostics") settings.diagnostics = true;
        else if (option == "--inspect") settings.inspection_requested = true;
        else if (option == "--graphics-capabilities") {
            settings.graphics_capabilities = true;
            settings.inspection_requested = true;
        } else if (option == "--input-script") settings.input_script = value();
        else if (option == "--fullscreen") settings.fullscreen = true;
        else if (option == "--windowed") settings.fullscreen = false;
        else if (option == "--game-dir") settings.game_dir = value();
        else if (option == "--renderer") {
            const auto name = value();
            renderer(name);
            settings.renderer = name;
            settings.graphics.backend_override = true;
        } else if (option == "--width") settings.width = number<int>(value(), 320, 7680, option);
        else if (option == "--height") settings.height = number<int>(value(), 240, 4320, option);
        else if (option == "--vsync") settings.vsync = toggle(value(), option);
        else if (option == "--quality") settings.graphics.preset = preset(value(), option);
        else if (option == "--material-override") material_override(settings.graphics, value(), true);
        else if (option == "--rt-reflections" || option == "--rt-shadows" || option == "--rt-gi") {
            const std::uint32_t bit = option == "--rt-reflections" ? 1u : option == "--rt-shadows" ? 2u : 4u;
            if (toggle(value(), option)) settings.rt_effects |= bit;
            else settings.rt_effects &= ~bit;
            settings.graphics.backend_override = true;
        } else if (option == "--frame-limit") settings.frame_limit = number<int>(value(), 0, 1000, option);
        else if (option == "--frames") {
            settings.frames = number<std::uint64_t>(value(), 1, std::numeric_limits<std::uint64_t>::max(), option);
            frames_supplied = true;
        } else if (option == "--source-frames") {
            settings.source_frames = number<std::uint64_t>(value(), 1, 10000000, option);
        } else if (option == "--capture") settings.capture = value();
        else if (option == "--mesh") settings.mesh = value();
        else if (option == "--texture") settings.texture = value();
        else if (option == "--world") settings.world = value();
        else if (option == "--animation") settings.animation = value();
        else if (option == "--percentage") {
            settings.percentage = static_cast<float>(decimal(value(), 0.0, 100.0, option));
            settings.percentage_supplied = true;
        } else if (option == "--audio") settings.audio = value();
        else if (option == "--inspect-audio") settings.inspect_audio = value();
        else if (option == "--audio-seconds") {
            settings.audio_seconds = decimal(value(), 0.1, 60.0, option);
            audio_seconds_supplied = true;
        } else if (option == "--audio-loop") settings.audio_loop = true;
        else if (option == "--diagnostics-json") settings.diagnostics_json = value();
        else {
            bool known = false;
            for (const auto& [flag, key] : graphics_options) {
                if (option != flag) continue;
                graphic_option(settings.graphics, key, value());
                known = true;
                break;
            }
            if (!known) {
                for (const auto& source : source_options) {
                    if (option != source.flag) continue;
                    source_option(settings, source.key, value());
                    known = true;
                    break;
                }
            }
            if (!known) invalid(option, "unknown option (see --help)");
        }
    }
    const bool selected_inspection_asset = !settings.mesh.empty() || !settings.texture.empty()
        || !settings.world.empty() || !settings.animation.empty() || !settings.inspect_audio.empty()
        || !settings.audio.empty();
    settings.inspection_requested = settings.inspection_requested || selected_inspection_asset;
    if (settings.source_frames && (settings.inspection_requested || settings.diagnostics))
        invalid("--source-frames", "requires native gameplay, not inspection, diagnostics or graphics-capabilities");
    if (settings.graphics_capabilities
        && (selected_inspection_asset || frames_supplied || !settings.capture.empty()
            || !settings.diagnostics_json.empty() || !settings.input_script.empty()))
        invalid("--graphics-capabilities", "cannot combine with asset/audio selection, frames, capture, diagnostics-json or input-script");
    if (!settings.input_script.empty() && settings.inspection_requested)
        invalid("--input-script", "requires native gameplay, not inspection or graphics-capabilities");
    if (!settings.capture.empty() && !frames_supplied && !settings.source_frames)
        invalid("--capture", "requires --frames or --source-frames");
    if (!settings.animation.empty() && settings.mesh.empty()) invalid("--animation", "requires --mesh");
    if (settings.percentage_supplied && settings.animation.empty()) invalid("--percentage", "requires --animation");
    if (settings.audio_loop && settings.audio.empty() && settings.inspect_audio.empty())
        invalid("--audio-loop", "requires --audio or --inspect-audio");
    if (!settings.audio.empty() && !frames_supplied) invalid("--audio", "requires --frames for finite inspection");
    if (audio_seconds_supplied && settings.inspect_audio.empty()) invalid("--audio-seconds", "requires --inspect-audio");
    if (!settings.inspect_audio.empty()
        && (!settings.audio.empty() || !settings.world.empty() || !settings.mesh.empty()
            || !settings.texture.empty() || !settings.animation.empty() || frames_supplied
            || !settings.capture.empty() || !settings.diagnostics_json.empty()))
        invalid("--inspect-audio", "cannot combine with --audio or rendering/asset selection options");
    if (settings.diagnostics && !settings.diagnostics_json.empty())
        invalid("--diagnostics-json", "cannot combine with headless --diagnostics");
    if (settings.diagnostics && settings.graphics_capabilities)
        invalid("--graphics-capabilities", "cannot combine with headless --diagnostics");
    if (settings.diagnostics && !settings.input_script.empty())
        invalid("--input-script", "cannot combine with headless --diagnostics");
    validate_persistent(settings);
    return arguments;
}

std::string_view help_text() {
    return R"(Citroen C4 Robot native runtime
Usage: citroen-c4-robot [options]
  --help                 Show this help without loading configuration
  --diagnostics          Inspect installed files and exit without opening a window
  --inspect              Explicit windowed asset inspection, not gameplay
  --graphics-capabilities Query actual backend capabilities without loading game assets
  --input-script PATH    Read a native gameplay input scenario; not valid in inspection
  --game-dir PATH        Installed game directory (native gameplay requires original assets)
  --renderer opengl|vulkan Select raster renderer (default opengl)
  --rt-reflections on|off Request Vulkan RT reflections (default off)
  --rt-shadows on|off     Request Vulkan RT shadows (default off)
  --rt-gi on|off          Request Vulkan RT global illumination (default off)
  --quality original|enhanced|ultra|custom Select requested graphics preset (default original)
  --msaa 1|2|4|8         Override multisample antialiasing
  --post-aa off|fxaa     Override postprocess antialiasing
  --ssao on|off          Override screen-space ambient occlusion
  --shadows off|low|high Override raster shadow quality
  --pbr on|off           Override enhanced material shading
  --material-override NAME=metallic,roughness[,reflectivity] Native override by exact material name
                          Scalars must be finite 0..1; omitted reflectivity preserves its source-derived value
  --internal-hdr on|off  Override internal HDR rendering (not HDR display output)
  --bloom on|off         Override bloom
  --exposure-ev EV       Override exposure, finite decimal -10..10
  --tone-map reinhard|aces Override tone mapping
  --texture-filter original|trilinear|anisotropic Override texture sampling quality
  --denoising off|standard|high Override ray-tracing denoising quality
  --source-atq N         Original texture selector, 0..2 (default 0)
  --source-mtq N         Original texture selector, 0..2 (default 0)
  --source-stq N         Original shadow textures, 0..3; omitted Enhanced=2, Ultra=3, Original/Custom=0
                         Explicit 0 preserves the supplemental native-shadow path
  --source-cgm N         Original visibility selector, 0..3; explicit 0 preserves Minimum
  --source-shq N         Original shadow selector, 0..2 (default 0)
  --source-ano N         Original source selector, 0..1 (default 0)
  --source-bloom N       Original bloom selector, 0..1 (default 0)
  --width N              Window width, 320..7680 (default 1280)
  --height N             Window height, 240..4320 (default 720)
  --fullscreen           Start fullscreen
  --windowed             Start windowed
  --vsync on|off         Enable or disable vertical synchronization
  --frame-limit N        Frame limit, 0..1000; 0 is uncapped (default 60)
  --frames N             Exit after N presents; positive optional safety bound for --source-frames
  --source-frames N      Stop native gameplay after N completed 32 Hz source ticks, 1..10000000
  --capture PATH         Write a frame capture; requires --frames or --source-frames
  --mesh PATH            Inspect a recovered mesh (file or archive::member)
  --texture PATH         Inspect a recovered image (file or archive::member)
  --world PATH           Inspect a recovered original world (file or archive::member)
  --animation CLIP       Inspect a real original clip; requires --mesh
  --percentage P         Freeze original pose at decimal 0..100; requires --animation
  --audio PATH           Play original audio during inspection; requires finite --frames
  --inspect-audio PATH   Inspect/play original audio without a window; no rendering options
  --audio-seconds S      Finite audio inspection duration, decimal 0.1..60 (default 1)
  --audio-loop           Loop selected audio until exit; requires --audio or --inspect-audio
  --diagnostics-json FILE Write observed render diagnostics to a new file outside game-dir
Nonzero RT effects require Vulkan; unsupported RT uses an explicitly reported raster fallback.
Graphics overrides take precedence over presets regardless of CLI argument order.
Original selects OpenGL/RT off; Enhanced Vulkan/RT off; Ultra Vulkan/all RT requests.
Custom preserves the selected backend/RT. Explicit backend or RT flags make the result Custom.
--quality clears saved graphics overrides; explicit options are saved, not preset defaults.
Material override names are nonempty, case-sensitive and untrimmed; no control bytes or '='.
Repeat --material-override for distinct names; the last exact-name override wins.
Material overrides are native approximations, not recovered original physical metadata.
At world load, unset --source-cgm uses 3 for Enhanced/Ultra, 0 for Original/Custom.
Explicit source selectors override presets, including CGM0; other selectors default to zero.
Graphics Apply preserves the loaded source world; inferred CGM preset changes take effect next launch.
--source-bloom controls the original source selector; --bloom controls modern bloom.
Asset file/archive paths may be relative to game-dir or absolute.
World, frozen animation poses and audio are asset inspection, not recovered simulation.
Without --percentage, donus plays its recovered original one-shot clock.
Other clips require --percentage until their original timing is recovered.
Inspection does not substitute recovered native gameplay or camera behavior.
Window controls: Escape quit, F5 restart, F11 fullscreen, V vsync.
Only game-directory, source selectors, renderer, graphics/RT requests and display options are saved to runtime.conf.
)";
}

} // namespace c4
