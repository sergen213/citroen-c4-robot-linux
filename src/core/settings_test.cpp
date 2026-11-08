#include "settings.hpp"

#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <sys/stat.h>
#include <vector>

namespace {

namespace quality = c4::rendering::quality;
using c4::assets::SourceProfile;

struct SourceControl {
    const char* option;
    const char* key;
    unsigned maximum;
    std::uint8_t SourceProfile::* member;
};

constexpr SourceControl source_controls[] = {
    {"--source-atq", "source_atq", 2, &SourceProfile::atq},
    {"--source-mtq", "source_mtq", 2, &SourceProfile::mtq},
    {"--source-stq", "source_stq", 3, &SourceProfile::stq},
    {"--source-cgm", "source_cgm", 3, &SourceProfile::cgm},
    {"--source-shq", "source_shq", 2, &SourceProfile::shq},
    {"--source-ano", "source_ano", 1, &SourceProfile::ano},
    {"--source-bloom", "source_bloom", 1, &SourceProfile::bloom},
};

void assert_no_graphics_overrides(const quality::GraphicSettings& graphics) {
    assert(!graphics.samples && !graphics.post_aa && !graphics.ssao && !graphics.shadows);
    assert(!graphics.pbr && !graphics.internal_hdr && !graphics.bloom);
    assert(!graphics.exposure_ev && !graphics.tone_mapping);
    assert(!graphics.texture_filtering);
    assert(!graphics.denoising);
    assert(!graphics.backend_override);
    assert(graphics.material_overrides.empty());
}

void assert_same_graphics(const quality::GraphicSettings& actual,
                          const quality::GraphicSettings& expected) {
    assert(actual.preset == expected.preset && actual.samples == expected.samples);
    assert(actual.post_aa == expected.post_aa && actual.ssao == expected.ssao);
    assert(actual.shadows == expected.shadows && actual.pbr == expected.pbr);
    assert(actual.internal_hdr == expected.internal_hdr && actual.bloom == expected.bloom);
    assert(actual.exposure_ev == expected.exposure_ev && actual.tone_mapping == expected.tone_mapping);
    assert(actual.backend_override == expected.backend_override);
    assert(actual.texture_filtering == expected.texture_filtering);
    assert(actual.denoising == expected.denoising);
    assert(actual.material_overrides.size() == expected.material_overrides.size());
    for (std::size_t i = 0; i < actual.material_overrides.size(); ++i) {
        const auto& a = actual.material_overrides[i];
        const auto& e = expected.material_overrides[i];
        assert(a.material_name == e.material_name);
        assert(a.metallic == e.metallic && a.roughness == e.roughness && a.reflectivity == e.reflectivity);
    }
}

struct Environment {
    const char* key;
    std::string previous;
    bool existed;

    Environment(const char* name, const char* value) : key(name) {
        const char* old = std::getenv(key);
        existed = old != nullptr;
        if (old) previous = old;
        if (::setenv(key, value, 1) != 0) throw std::runtime_error("setenv failed");
    }
    ~Environment() {
        if (existed) ::setenv(key, previous.c_str(), 1);
        else ::unsetenv(key);
    }
};

struct Directory {
    std::filesystem::path path;
    Directory() {
        char pattern[] = "/tmp/c4-settings-test-XXXXXX";
        const char* created = ::mkdtemp(pattern);
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

c4::Arguments parse(std::initializer_list<const char*> options) {
    std::vector<char*> argv{const_cast<char*>("c4-runtime")};
    for (const char* option : options) argv.push_back(const_cast<char*>(option));
    return c4::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

void corrupt(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::trunc);
    file << text;
    file.close();
    assert(file.good());
    rejects([&] { c4::load_settings(path); });
}

void test_source_cgm_precedence(const std::filesystem::path& path) {
    c4::save_settings(path, {});
    for (const char* preset : {"original", "enhanced", "ultra", "custom"}) {
        const auto implicit = parse({"--quality", preset}).settings;
        const bool modern = implicit.graphics.preset == quality::Preset::Enhanced ||
                            implicit.graphics.preset == quality::Preset::Ultra;
        SourceProfile defaults = c4::assets::original_source_profile;
        defaults.cgm = modern ? 3 : 0;
        defaults.stq = implicit.graphics.preset == quality::Preset::Ultra ? 3 : modern ? 2 : 0;
        assert(!implicit.source_cgm_explicit && !implicit.source_stq_explicit);
        assert(implicit.source_profile == c4::assets::original_source_profile);
        assert(c4::effective_source_profile(implicit) == defaults);
        c4::save_settings(path, implicit);
        const auto restored = c4::load_settings(path);
        assert(!restored.source_cgm_explicit && !restored.source_stq_explicit);
        assert(restored.source_profile == implicit.source_profile);
        assert(c4::effective_source_profile(restored) == defaults);
        c4::save_settings(path, {});
        for (const auto& control : source_controls) {
            if (control.member != &SourceProfile::cgm && control.member != &SourceProfile::stq) continue;
            for (unsigned value = 0; value <= control.maximum; ++value) {
                const auto text = std::to_string(value);
                auto expected = defaults;
                expected.*control.member = static_cast<std::uint8_t>(value);
                for (const auto& selected :
                     {parse({"--quality", preset, control.option, text.c_str()}).settings,
                      parse({control.option, text.c_str(), "--quality", preset}).settings}) {
                    assert(selected.source_cgm_explicit == (control.member == &SourceProfile::cgm));
                    assert(selected.source_stq_explicit == (control.member == &SourceProfile::stq));
                    assert(c4::effective_source_profile(selected) == expected);
                    c4::save_settings(path, selected);
                    const auto restored_selected = c4::load_settings(path);
                    assert(restored_selected.source_profile == selected.source_profile);
                    assert(restored_selected.source_cgm_explicit == selected.source_cgm_explicit);
                    assert(restored_selected.source_stq_explicit == selected.source_stq_explicit);
                    assert(c4::effective_source_profile(restored_selected) == expected);
                    assert(c4::effective_source_profile(parse({"--quality", preset}).settings) == expected);
                }
                c4::save_settings(path, {});
            }
        }
        for (const char* next : {"original", "enhanced", "ultra", "custom"}) {
            auto request = implicit;
            request.graphics.preset = parse({"--quality", next}).settings.graphics.preset;
            assert(!c4::source_profile_requires_restart(implicit, request, defaults));
            assert(!c4::source_profile_requires_restart(request, request, defaults));
        }
        for (const bool selectors_first : {false, true}) {
            {
                std::ofstream config(path, std::ios::trunc);
                config << "version 1\n";
                if (!selectors_first) config << "quality " << preset << '\n';
                config << "source_stq 0\nsource_cgm 0\n";
                if (selectors_first) config << "quality " << preset << '\n';
            }
            const auto explicit_zero = c4::load_settings(path);
            assert(explicit_zero.source_stq_explicit && explicit_zero.source_cgm_explicit);
            assert(c4::effective_source_profile(explicit_zero) == c4::assets::original_source_profile);
            c4::save_settings(path, explicit_zero);
            const auto restored_zero = c4::load_settings(path);
            assert(restored_zero.source_stq_explicit && restored_zero.source_cgm_explicit);
            assert(c4::effective_source_profile(restored_zero) == c4::assets::original_source_profile);
        }
        c4::save_settings(path, {});
    }
    const auto implicit = parse({"--quality", "ultra", "--source-atq", "1", "--source-mtq", "1",
                                 "--source-stq", "1", "--source-shq", "1",
                                 "--source-ano", "1", "--source-bloom", "1"}).settings;
    auto expected = implicit.source_profile;
    expected.cgm = 3;
    assert(c4::effective_source_profile(implicit) == expected);
    c4::save_settings(path, implicit);
    const auto restored = c4::load_settings(path);
    assert(!restored.source_cgm_explicit && restored.source_stq_explicit);
    assert(restored.source_profile == implicit.source_profile);
    assert(c4::effective_source_profile(restored) == expected);
    expected.cgm = 0;
    assert(c4::effective_source_profile(parse({"--quality", "original"}).settings) == expected);
    assert(c4::effective_source_profile(parse({"--quality", "custom"}).settings) == expected);
    const auto minimum = parse({"--source-cgm", "0"}).settings;
    c4::save_settings(path, minimum);
    const auto restored_minimum = c4::load_settings(path);
    assert(restored_minimum.source_cgm_explicit && restored_minimum.source_stq_explicit);
    assert(restored_minimum.source_profile == expected);
    assert(c4::effective_source_profile(restored_minimum) == expected);
    for (const char* preset : {"enhanced", "ultra"})
        assert(c4::effective_source_profile(parse({"--quality", preset}).settings) == expected);
    const auto overridden = parse({"--source-cgm", "2"}).settings;
    expected.cgm = 2;
    assert(overridden.source_cgm_explicit && c4::effective_source_profile(overridden) == expected);
    auto omitted_stq = implicit;
    omitted_stq.source_stq_explicit = false;
    expected.cgm = 3;
    expected.stq = 3;
    assert(c4::effective_source_profile(omitted_stq) == expected);
    c4::save_settings(path, {});
    for (const char* renderer : {"opengl", "vulkan"}) {
        const auto all_zero = parse({"--quality", "custom", "--renderer", renderer,
                                     "--source-atq", "0", "--source-mtq", "0", "--source-stq", "0",
                                     "--source-cgm", "0", "--source-shq", "0", "--source-ano", "0",
                                     "--source-bloom", "0"}).settings;
        assert(all_zero.source_cgm_explicit && all_zero.source_stq_explicit);
        assert(c4::effective_source_profile(all_zero) == c4::assets::original_source_profile);
    }
    const auto ultra = parse({"--quality", "ultra"}).settings;
    const auto loaded = c4::effective_source_profile(ultra);
    auto request = ultra;
    request.graphics.preset = quality::Preset::Original;
    assert(!c4::source_profile_requires_restart(ultra, request, loaded));
    assert(c4::effective_source_profile(request).cgm == 0 && loaded.cgm == 3);
    assert(c4::effective_source_profile(request).stq == 0 && loaded.stq == 3);
    request.source_cgm_explicit = true;
    assert(c4::source_profile_requires_restart(ultra, request, loaded));
    request.source_profile.cgm = 3;
    request.source_profile.stq = 3;
    assert(!c4::source_profile_requires_restart(ultra, request, loaded));
    request.source_profile.mtq = 1;
    assert(c4::source_profile_requires_restart(ultra, request, loaded));
    request = ultra;
    request.source_stq_explicit = true;
    assert(c4::source_profile_requires_restart(ultra, request, loaded));
    request.source_profile.stq = 3;
    assert(!c4::source_profile_requires_restart(ultra, request, loaded));
    request.source_profile.stq = 2;
    assert(c4::source_profile_requires_restart(ultra, request, loaded));
    request.source_stq_explicit = false;
    assert(!c4::source_profile_requires_restart(ultra, request, loaded));
    c4::save_settings(path, {});
}

void test_material_overrides(const std::filesystem::path& path) {
    const auto requested = parse({"--material-override", " Body, \"paint\"\\layer =0,1",
                                  "--material-override", "body=1,0,0",
                                  "--material-override", "Body=0.125,0.75,1"}).settings;
    const auto& overrides = requested.graphics.material_overrides;
    assert(overrides.size() == 3);
    assert(overrides[0].material_name == " Body, \"paint\"\\layer ");
    assert(overrides[0].metallic == 0 && overrides[0].roughness == 1 && !overrides[0].reflectivity);
    assert(overrides[1].material_name == "body" && overrides[1].reflectivity == 0);
    assert(overrides[2].material_name == "Body" && overrides[2].reflectivity == 1);
    rejects([] { parse({"--material-override"}); });
    for (const char* value : {"", "=0,1", "Body", "Body=0", "Body=0,1,0,1",
                              "Body=,1", "Body=0,", "Body=0,1,", "Body=0,,1",
                              "Body=0,1x", "Body=0,1=0", "Body\n=0,1", "Body\t=0,1"})
        rejects([&] { parse({"--material-override", value}); });
    for (const char* scalar : {"", "-0.01", "1.000001", "nan", "inf", "-inf", "1e999",
                               "+0.5", " 0.5", "0.5 ", "0x1p0", "0.5x"}) {
        for (const auto& value : {std::string("Body=") + scalar + ",1",
                                  std::string("Body=0,") + scalar,
                                  std::string("Body=0,1,") + scalar})
            rejects([&] { parse({"--material-override", value.c_str()}); });
    }
    const auto repeated = parse({"--material-override", "Body=1,0,1",
                                 "--material-override", "Body=0,1"}).settings.graphics;
    assert(repeated.material_overrides.size() == 1);
    assert(repeated.material_overrides[0].metallic == 0 && repeated.material_overrides[0].roughness == 1);
    assert(!repeated.material_overrides[0].reflectivity);
    c4::save_settings(path, requested);
    assert_same_graphics(c4::load_settings(path).graphics, requested.graphics);
    assert_same_graphics(parse({}).settings.graphics, requested.graphics);
    {
        std::ifstream config(path);
        const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
        assert(text.find("material_override \"body=1,0,0\"\n") != std::string::npos);
        assert(text.find("material_override \"Body=0.125,0.75,1\"\n") != std::string::npos);
    }
    auto expected = requested.graphics;
    expected.material_overrides[2] = {"Body", 0.25f, 0.5f, {}};
    assert_same_graphics(parse({"--material-override", "Body=0.25,0.5"}).settings.graphics, expected);
    for (const char* preset : {"original", "enhanced", "ultra", "custom"}) {
        assert_no_graphics_overrides(parse({"--quality", preset}).settings.graphics);
        for (const auto& reset :
             {parse({"--material-override", "Body=0,1", "--quality", preset}).settings.graphics,
              parse({"--quality", preset, "--material-override", "Body=0,1"}).settings.graphics}) {
            assert(reset.material_overrides.size() == 1);
            assert(reset.material_overrides[0].material_name == "Body" && !reset.material_overrides[0].reflectivity);
        }
    }
    for (unsigned control = 0; control <= 127; ++control) {
        if (control >= 32 && control != 127) continue;
        auto invalid = requested;
        invalid.graphics.material_overrides[0].material_name = "Body" + std::string(1, static_cast<char>(control));
        rejects([&] { c4::save_settings(path, invalid); });
        assert_same_graphics(c4::load_settings(path).graphics, requested.graphics);
    }
    for (const char* name : {"", "Body=paint"}) {
        auto invalid = requested;
        invalid.graphics.material_overrides[0].material_name = name;
        rejects([&] { c4::save_settings(path, invalid); });
    }
    for (const float scalar : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()}) {
        for (unsigned field = 0; field < 3; ++field) {
            auto invalid = requested;
            auto& material = invalid.graphics.material_overrides[0];
            if (field == 0) material.metallic = scalar;
            else if (field == 1) material.roughness = scalar;
            else material.reflectivity = scalar;
            rejects([&] { c4::save_settings(path, invalid); });
        }
    }
    auto duplicate = requested;
    duplicate.graphics.material_overrides.push_back(overrides[0]);
    rejects([&] { c4::save_settings(path, duplicate); });
    assert_same_graphics(c4::load_settings(path).graphics, requested.graphics);
    for (const char* record : {"material_override", "material_override Body=0,1",
                               "material_override \"Body=0,1", "material_override \"=0,1\"",
                               "material_override \"Body=,1\"", "material_override \"Body=0,1,\"",
                               "material_override \"Body=nan,1\"", "material_override \"Body=0,2\"",
                               "material_override \"Body=0,1,inf\"",
                               "material_override \"Body=0,1,0,1\"",
                               "material_override \"Body=0,1\"tail",
                               "material_override \"Body=0,1\"\nmaterial_override \"Body=1,0\""})
        corrupt(path, std::string("version 1\n") + record + "\n");
    for (unsigned control = 0; control <= 127; ++control) {
        if (control >= 32 && control != 127) continue;
        corrupt(path, "version 1\nmaterial_override \"Body" + std::string(1, static_cast<char>(control)) + "=0,1\"\n");
    }
    assert(c4::help_text().find("--material-override NAME=metallic,roughness[,reflectivity]") != std::string_view::npos);
    c4::save_settings(path, {});
}

} // namespace

int main() {
    Directory directory;
    Environment xdg("XDG_CONFIG_HOME", directory.path.c_str());
    const auto path = c4::config_path();
    assert(path == directory.path / "citroen-c4-robot/runtime.conf");
    const auto defaults = parse({}).settings;
    assert(defaults.width == 1280 && defaults.height == 720);
    assert(defaults.renderer == "opengl" && defaults.frames == 0);
    assert(defaults.source_frames == 0);
    assert(defaults.vsync && !defaults.fullscreen && defaults.frame_limit == 60);
    assert(defaults.world.empty() && defaults.audio.empty() && defaults.diagnostics_json.empty());
    assert(defaults.animation.empty() && defaults.percentage == 0.0f && !defaults.audio_loop);
    assert(defaults.inspect_audio.empty() && defaults.audio_seconds == 1.0);
    assert(defaults.rt_effects == 0);
    assert(!defaults.percentage_supplied);
    assert(!defaults.inspection_requested && !defaults.graphics_capabilities && defaults.input_script.empty());
    assert(defaults.graphics.preset == quality::Preset::Original);
    assert_no_graphics_overrides(defaults.graphics);
    assert(defaults.source_profile == c4::assets::original_source_profile);
    test_material_overrides(path);
    test_source_cgm_precedence(path);
    const SourceProfile maximum_source{2, 2, 3, 3, 2, 1, 1};
    for (const auto& control : source_controls) {
        for (unsigned value = 0; value <= control.maximum; ++value) {
            const auto text = std::to_string(value);
            SourceProfile expected{};
            expected.*control.member = static_cast<std::uint8_t>(value);
            assert(parse({control.option, text.c_str()}).settings.source_profile == expected);
        }
        const auto upper = std::to_string(control.maximum);
        SourceProfile expected{};
        expected.*control.member = static_cast<std::uint8_t>(control.maximum);
        assert(parse({control.option, "0", control.option, upper.c_str()}).settings.source_profile == expected);
        assert(parse({control.option, upper.c_str(), control.option, "0"}).settings.source_profile ==
               c4::assets::original_source_profile);
        rejects([&] { parse({control.option}); });
        const auto invalid = std::to_string(control.maximum + 1);
        rejects([&] { parse({control.option, invalid.c_str()}); });
        for (const char* value : {"", "-1", "+1", "1.0", "1x", " 1", "1 ", "nan", "0x1",
                                  "9999999999999999999999999999999"})
            rejects([&] { parse({control.option, value}); });
    }
    for (const char* preset : {"original", "enhanced", "ultra", "custom"}) {
        const auto before = parse({"--source-atq", "2", "--source-mtq", "2", "--source-stq", "3",
                                   "--source-cgm", "3", "--source-shq", "2", "--source-ano", "1",
                                   "--source-bloom", "1", "--quality", preset}).settings;
        const auto after = parse({"--quality", preset, "--source-atq", "2", "--source-mtq", "2",
                                  "--source-stq", "3", "--source-cgm", "3", "--source-shq", "2",
                                  "--source-ano", "1", "--source-bloom", "1"}).settings;
        assert(before.source_profile == maximum_source && after.source_profile == maximum_source);
    }
    const auto source_bloom_only = parse({"--source-bloom", "1", "--bloom", "off"}).settings;
    assert(source_bloom_only.source_profile.bloom == 1 && source_bloom_only.graphics.bloom == false);
    const auto modern_bloom_only = parse({"--bloom", "on", "--source-bloom", "0"}).settings;
    assert(modern_bloom_only.source_profile.bloom == 0 && modern_bloom_only.graphics.bloom == true);
    assert(parse({"--renderer", "vulkan", "--rt-gi", "on", "--source-atq", "2"})
               .settings.source_profile.atq == 2);
    assert(parse({"--source-atq", "2", "--renderer", "vulkan", "--rt-gi", "on"})
               .settings.source_profile.atq == 2);
    const auto explicit_inspection = parse({"--inspect"}).settings;
    assert(explicit_inspection.inspection_requested && !explicit_inspection.graphics_capabilities);
    for (const char* selector : {"--world", "--mesh", "--texture", "--inspect-audio"}) {
        const auto selected = parse({selector, "original.asset"}).settings;
        assert(selected.inspection_requested && !selected.graphics_capabilities);
        assert(parse({"--inspect", selector, "original.asset"}).settings.inspection_requested);
        assert(parse({selector, "original.asset", "--inspect"}).settings.inspection_requested);
        rejects([&] { parse({"--graphics-capabilities", selector, "original.asset"}); });
        rejects([&] { parse({selector, "original.asset", "--graphics-capabilities"}); });
        rejects([&] { parse({"--input-script", "drive.json", selector, "original.asset"}); });
        rejects([&] { parse({selector, "original.asset", "--input-script", "drive.json"}); });
    }
    assert(parse({"--animation", "donus", "--mesh", "car.mdl"}).settings.inspection_requested);
    assert(parse({"--inspect", "--animation", "donus", "--mesh", "car.mdl"}).settings.inspection_requested);
    assert(parse({"--animation", "donus", "--mesh", "car.mdl", "--inspect"}).settings.inspection_requested);
    assert(parse({"--audio", "start.wav", "--frames", "1"}).settings.inspection_requested);
    assert(parse({"--inspect", "--audio", "start.wav", "--frames", "1"}).settings.inspection_requested);
    assert(parse({"--audio", "start.wav", "--frames", "1", "--inspect"}).settings.inspection_requested);
    const auto capabilities = parse({"--graphics-capabilities"}).settings;
    assert(capabilities.graphics_capabilities && capabilities.inspection_requested);
    assert(capabilities.game_dir.empty() && capabilities.frames == 0);
    for (const auto& queried :
         {parse({"--inspect", "--graphics-capabilities"}).settings,
          parse({"--graphics-capabilities", "--inspect"}).settings,
          parse({"--game-dir", "original game", "--graphics-capabilities"}).settings,
          parse({"--graphics-capabilities", "--game-dir", "original game"}).settings,
          parse({"--renderer", "vulkan", "--quality", "ultra", "--graphics-capabilities"}).settings})
        assert(queried.graphics_capabilities && queried.inspection_requested);
    for (const char* option : {"--frames", "--capture", "--diagnostics-json", "--input-script"}) {
        rejects([&] { parse({"--graphics-capabilities", option, "1"}); });
        rejects([&] { parse({option, "1", "--graphics-capabilities"}); });
    }
    rejects([] { parse({"--diagnostics", "--graphics-capabilities"}); });
    rejects([] { parse({"--graphics-capabilities", "--diagnostics"}); });
    rejects([] { parse({"--graphics-capabilities", "--animation", "donus", "--mesh", "car.mdl"}); });
    rejects([] { parse({"--animation", "donus", "--mesh", "car.mdl", "--graphics-capabilities"}); });
    rejects([] { parse({"--graphics-capabilities", "--audio", "start.wav", "--frames", "1"}); });
    rejects([] { parse({"--audio", "start.wav", "--frames", "1", "--graphics-capabilities"}); });
    const auto scripted = parse({"--input-script", "drive.json", "--frames", "12"}).settings;
    assert(scripted.input_script == "drive.json" && scripted.frames == 12);
    assert(!scripted.inspection_requested && !scripted.graphics_capabilities);
    assert(parse({"--frames", "12", "--input-script", "drive.json"}).settings.input_script == "drive.json");
    assert(parse({"--input-script", "drive.json"}).settings.input_script == "drive.json");
    assert(parse({"--input-script", "first.json", "--input-script", "last.json"}).settings.input_script == "last.json");
    rejects([] { parse({"--input-script"}); });
    rejects([] { parse({"--input-script", ""}); });
    rejects([] { parse({"--diagnostics", "--input-script", "drive.json"}); });
    rejects([] { parse({"--input-script", "drive.json", "--diagnostics"}); });
    rejects([] { parse({"--inspect", "--input-script", "drive.json"}); });
    rejects([] { parse({"--input-script", "drive.json", "--inspect"}); });
    rejects([] { parse({"--input-script", "drive.json", "--animation", "donus", "--mesh", "car.mdl"}); });
    rejects([] { parse({"--animation", "donus", "--mesh", "car.mdl", "--input-script", "drive.json"}); });
    rejects([] { parse({"--input-script", "drive.json", "--audio", "start.wav", "--frames", "1"}); });
    rejects([] { parse({"--audio", "start.wav", "--frames", "1", "--input-script", "drive.json"}); });
    assert(parse({"--source-frames", "1"}).settings.source_frames == 1);
    assert(parse({"--source-frames", "10000000"}).settings.source_frames == 10000000);
    assert(parse({"--source-frames", "00032"}).settings.source_frames == 32);
    assert(parse({"--source-frames", "1", "--source-frames", "32"}).settings.source_frames == 32);
    assert(parse({"--source-frames", "32", "--source-frames", "1"}).settings.source_frames == 1);
    rejects([] { parse({"--source-frames"}); });
    for (const char* value : {"", "0", "10000001", "-1", "+1", "1.0", "1x", " 1", "1 ",
                              "nan", "0x1", "18446744073709551616"})
        rejects([&] { parse({"--source-frames", value}); });
    rejects([] { parse({"--source-frames", "0", "--source-frames", "1"}); });
    rejects([] { parse({"--source-frames", "1", "--source-frames", "0"}); });
    for (const char* alias : {"--source-frame", "--sourceframes", "--source-frames=1", "--source-frames1"})
        rejects([&] { parse({alias, "1"}); });
    for (const auto& bounded :
         {parse({"--source-frames", "32", "--capture", "source.ppm"}).settings,
          parse({"--capture", "source.ppm", "--source-frames", "32"}).settings}) {
        assert(bounded.source_frames == 32 && bounded.frames == 0 && bounded.capture == "source.ppm");
        assert(!bounded.inspection_requested && !bounded.graphics_capabilities && !bounded.diagnostics);
    }
    for (const auto& bounded :
         {parse({"--source-frames", "32", "--frames", "500", "--capture", "source.ppm",
                 "--input-script", "drive.json", "--diagnostics-json", "source.json"}).settings,
          parse({"--diagnostics-json", "source.json", "--input-script", "drive.json",
                 "--capture", "source.ppm", "--frames", "500", "--source-frames", "32"}).settings}) {
        assert(bounded.source_frames == 32 && bounded.frames == 500 && bounded.capture == "source.ppm");
        assert(bounded.input_script == "drive.json" && bounded.diagnostics_json == "source.json");
        assert(!bounded.inspection_requested);
    }
    assert(parse({"--input-script", "drive.json", "--source-frames", "32"}).settings.frames == 0);
    assert(parse({"--source-frames", "32", "--input-script", "drive.json"}).settings.input_script == "drive.json");
    for (const char* mode : {"--inspect", "--diagnostics", "--graphics-capabilities"}) {
        rejects([&] { parse({"--source-frames", "1", mode}); });
        rejects([&] { parse({mode, "--source-frames", "1"}); });
    }
    for (const char* selector : {"--mesh", "--texture", "--world", "--animation", "--inspect-audio"}) {
        rejects([&] { parse({"--source-frames", "1", selector, "original.asset"}); });
        rejects([&] { parse({selector, "original.asset", "--source-frames", "1"}); });
    }
    rejects([] { parse({"--source-frames", "1", "--animation", "donus", "--mesh", "car.mdl"}); });
    rejects([] { parse({"--animation", "donus", "--mesh", "car.mdl", "--source-frames", "1"}); });
    rejects([] { parse({"--source-frames", "1", "--audio", "start.wav", "--frames", "10"}); });
    rejects([] { parse({"--audio", "start.wav", "--frames", "10", "--source-frames", "1"}); });
    assert(c4::help_text().find("--source-frames N") != std::string_view::npos);

    for (const auto& [name, preset] :
         {std::pair{"original", quality::Preset::Original},
          std::pair{"enhanced", quality::Preset::Enhanced},
          std::pair{"ultra", quality::Preset::Ultra},
          std::pair{"custom", quality::Preset::Custom}}) {
        const auto requested = parse({"--quality", name}).settings;
        assert(requested.graphics.preset == preset);
        assert_no_graphics_overrides(requested.graphics);
        assert(requested.renderer == (preset == quality::Preset::Enhanced || preset == quality::Preset::Ultra
                                      ? "vulkan" : "opengl"));
        assert(requested.rt_effects == (preset == quality::Preset::Ultra ? 7u : 0u));
    }
    for (const auto& overridden :
         {parse({"--renderer", "vulkan", "--quality", "original"}).settings,
          parse({"--quality", "original", "--renderer", "vulkan"}).settings}) {
        assert(overridden.graphics.preset == quality::Preset::Original);
        assert(overridden.renderer == "vulkan" && overridden.rt_effects == 0);
        assert(overridden.graphics.backend_override);
    }
    for (const auto& overridden :
         {parse({"--rt-shadows", "off", "--quality", "ultra"}).settings,
          parse({"--quality", "ultra", "--rt-shadows", "off"}).settings}) {
        assert(overridden.graphics.preset == quality::Preset::Ultra);
        assert(overridden.renderer == "vulkan" && overridden.rt_effects == 5);
        assert(overridden.graphics.backend_override);
    }
    for (const auto& overridden :
         {parse({"--renderer", "opengl", "--rt-reflections", "off", "--rt-shadows", "off",
                 "--rt-gi", "off", "--quality", "ultra"}).settings,
          parse({"--quality", "ultra", "--renderer", "opengl", "--rt-reflections", "off",
                 "--rt-shadows", "off", "--rt-gi", "off"}).settings}) {
        assert(overridden.graphics.preset == quality::Preset::Ultra);
        assert(overridden.renderer == "opengl" && overridden.rt_effects == 0);
        assert(overridden.graphics.backend_override);
    }
    for (const auto& overridden :
         {parse({"--renderer", "vulkan", "--rt-reflections", "on", "--quality", "original"}).settings,
          parse({"--quality", "original", "--renderer", "vulkan", "--rt-reflections", "on"}).settings}) {
        assert(overridden.graphics.preset == quality::Preset::Original);
        assert(overridden.renderer == "vulkan" && overridden.rt_effects == 1);
        assert(overridden.graphics.backend_override);
    }
    const auto last_original = parse({"--quality", "ultra", "--quality", "original"}).settings;
    assert(last_original.graphics.preset == quality::Preset::Original);
    assert(last_original.renderer == "opengl" && last_original.rt_effects == 0);
    const auto last_ultra = parse({"--quality", "original", "--quality", "ultra"}).settings;
    assert(last_ultra.graphics.preset == quality::Preset::Ultra);
    assert(last_ultra.renderer == "vulkan" && last_ultra.rt_effects == 7);
    const auto last_custom = parse({"--quality", "ultra", "--quality", "custom"}).settings;
    assert(last_custom.graphics.preset == quality::Preset::Custom);
    assert(last_custom.renderer == "opengl" && last_custom.rt_effects == 0);
    for (const auto& matching :
         {parse({"--renderer", "opengl", "--quality", "original"}).settings,
          parse({"--quality", "original", "--renderer", "opengl"}).settings,
          parse({"--renderer", "vulkan", "--quality", "ultra"}).settings,
          parse({"--quality", "ultra", "--renderer", "vulkan"}).settings}) {
        assert(matching.graphics.backend_override);
    }
    for (const char* option : {"--rt-reflections", "--rt-shadows", "--rt-gi"}) {
        assert(parse({option, "off", "--quality", "original"}).settings.graphics.backend_override);
        assert(parse({"--quality", "original", option, "off"}).settings.graphics.backend_override);
    }
    assert(!parse({"--msaa", "4"}).settings.graphics.backend_override);
    assert(!last_original.graphics.backend_override && !last_ultra.graphics.backend_override);
    assert(!last_custom.graphics.backend_override);
    rejects([] { parse({"--quality", "ultra", "--renderer", "opengl"}); });
    rejects([] { parse({"--renderer", "opengl", "--quality", "ultra"}); });
    const auto graphics = parse({"--quality", "ultra", "--msaa", "8", "--post-aa", "fxaa",
                                 "--ssao", "on", "--shadows", "high", "--pbr", "on",
                                 "--internal-hdr", "on", "--bloom", "on",
                                 "--exposure-ev", "-2.5", "--tone-map", "aces",
                                 "--texture-filter", "anisotropic", "--denoising", "high"}).settings.graphics;
    assert(graphics.preset == quality::Preset::Ultra && graphics.samples == 8u);
    assert(graphics.post_aa == quality::PostAA::Fxaa && graphics.ssao == true);
    assert(graphics.shadows == quality::ShadowQuality::High && graphics.pbr == true);
    assert(graphics.internal_hdr == true && graphics.bloom == true);
    assert(graphics.exposure_ev == -2.5f && graphics.tone_mapping == quality::ToneMapping::Aces);
    assert(graphics.texture_filtering == quality::TextureFiltering::Anisotropic);
    assert(graphics.denoising == quality::Denoising::High);
    const auto graphics_before_preset =
        parse({"--msaa", "8", "--post-aa", "fxaa", "--ssao", "on", "--shadows", "high",
               "--pbr", "on", "--internal-hdr", "on", "--bloom", "on",
               "--exposure-ev", "-2.5", "--tone-map", "aces", "--texture-filter", "anisotropic",
               "--denoising", "high", "--quality", "ultra"}).settings.graphics;
    assert_same_graphics(graphics_before_preset, graphics);
    assert(parse({"--msaa", "1"}).settings.graphics.samples == 1u);
    assert(parse({"--msaa", "2"}).settings.graphics.samples == 2u);
    assert(parse({"--msaa", "4"}).settings.graphics.samples == 4u);
    assert(parse({"--post-aa", "off"}).settings.graphics.post_aa == quality::PostAA::Off);
    assert(parse({"--shadows", "off"}).settings.graphics.shadows == quality::ShadowQuality::Off);
    assert(parse({"--shadows", "low"}).settings.graphics.shadows == quality::ShadowQuality::Low);
    assert(parse({"--tone-map", "reinhard"}).settings.graphics.tone_mapping == quality::ToneMapping::Reinhard);
    assert(parse({"--exposure-ev", "-10"}).settings.graphics.exposure_ev == -10.0f);
    assert(parse({"--exposure-ev", "10"}).settings.graphics.exposure_ev == 10.0f);
    assert(parse({"--exposure-ev", "0"}).settings.graphics.exposure_ev == 0.0f);
    assert(parse({"--texture-filter", "original"}).settings.graphics.texture_filtering ==
           quality::TextureFiltering::Original);
    assert(parse({"--texture-filter", "trilinear"}).settings.graphics.texture_filtering ==
           quality::TextureFiltering::Trilinear);
    assert(parse({"--texture-filter", "anisotropic"}).settings.graphics.texture_filtering ==
           quality::TextureFiltering::Anisotropic);
    rejects([] { parse({"--texture-filter"}); });
    for (const char* value : {"", "linear", "on", "Original", "TRILINEAR", " anisotropic", "original "})
        rejects([&] { parse({"--texture-filter", value}); });
    assert(parse({"--denoising", "off"}).settings.graphics.denoising == quality::Denoising::Off);
    assert(parse({"--denoising", "standard"}).settings.graphics.denoising == quality::Denoising::Standard);
    assert(parse({"--denoising", "high"}).settings.graphics.denoising == quality::Denoising::High);
    for (const char* value : {"on", "low", "medium", "HIGH", "Standard", " standard", "high "})
        rejects([&] { parse({"--denoising", value}); });
    const auto repeated_graphics =
        parse({"--quality", "original", "--msaa", "8", "--post-aa", "fxaa", "--ssao", "on",
               "--shadows", "high", "--pbr", "on", "--internal-hdr", "on", "--bloom", "on",
               "--exposure-ev", "10", "--tone-map", "aces", "--texture-filter", "anisotropic",
               "--denoising", "high", "--quality", "ultra",
               "--msaa", "2", "--post-aa", "off", "--ssao", "off", "--shadows", "low",
               "--pbr", "off", "--internal-hdr", "off", "--bloom", "off",
               "--exposure-ev", "0", "--tone-map", "reinhard", "--texture-filter", "original",
               "--denoising", "off", "--quality", "enhanced"}).settings.graphics;
    assert(repeated_graphics.preset == quality::Preset::Enhanced && repeated_graphics.samples == 2u);
    assert(repeated_graphics.post_aa == quality::PostAA::Off && repeated_graphics.ssao == false);
    assert(repeated_graphics.shadows == quality::ShadowQuality::Low && repeated_graphics.pbr == false);
    assert(repeated_graphics.internal_hdr == false && repeated_graphics.bloom == false);
    assert(repeated_graphics.exposure_ev == 0.0f);
    assert(repeated_graphics.texture_filtering == quality::TextureFiltering::Original);
    assert(repeated_graphics.denoising == quality::Denoising::Off);
    assert(repeated_graphics.tone_mapping == quality::ToneMapping::Reinhard);
    for (const char* option : {"--quality", "--msaa", "--post-aa", "--ssao", "--shadows",
                               "--pbr", "--internal-hdr", "--bloom", "--exposure-ev", "--tone-map",
                               "--texture-filter", "--denoising"}) {
        rejects([&] { parse({option}); });
        rejects([&] { parse({option, ""}); });
        rejects([&] { parse({option, "--quality", "original"}); });
    }
    for (const char* value : {"Original", "ULTRA", "high", "unknown", " enhanced"})
        rejects([&] { parse({"--quality", value}); });
    for (const char* value : {"0", "3", "16", "-1", "+2", "2.0", "2x", " 2",
                              "999999999999999999999999"})
        rejects([&] { parse({"--msaa", value}); });
    for (const char* value : {"on", "FXAA", "taa", " off"})
        rejects([&] { parse({"--post-aa", value}); });
    for (const char* value : {"on", "medium", "HIGH", " low"})
        rejects([&] { parse({"--shadows", value}); });
    for (const char* value : {"off", "ACES", "linear", " reinhard"})
        rejects([&] { parse({"--tone-map", value}); });
    for (const char* option : {"--ssao", "--pbr", "--internal-hdr", "--bloom"})
        for (const char* value : {"true", "false", "1", "0", "ON", "Off", " on"})
            rejects([&] { parse({option, value}); });
    for (const char* value : {"-10.000001", "10.000001", "nan", "NaN", "NAN", "inf", "INF",
                              "-inf", "1e999", "2.5x", " 2.5", "2.5 ", "+2.5", "0x1p2",
                              ".", "-", "--1", "1.2.3"})
        rejects([&] { parse({"--exposure-ev", value}); });

    const auto limits = parse({"--width", "320", "--height", "240", "--frame-limit", "0",
                               "--frames", "18446744073709551615", "--fullscreen",
                               "--vsync", "off", "--game-dir", "game dir",
                               "--renderer", "opengl", "--capture", "frame.ppm",
                               "--mesh", "models/car.mesh", "--texture", "images/car.png",
                               "--world", "maps/main.wmb", "--animation", "donus",
                               "--percentage", "37.5", "--audio", "sounds/start.wav",
                               "--audio-loop", "--diagnostics-json", "inspection.json"}).settings;
    assert(limits.width == 320 && limits.height == 240 && limits.frame_limit == 0);
    assert(limits.frames == UINT64_MAX && limits.fullscreen && !limits.vsync);
    assert(!limits.diagnostics && limits.capture == "frame.ppm");
    assert(parse({"--diagnostics"}).settings.diagnostics);
    assert(limits.mesh == "models/car.mesh" && limits.texture == "images/car.png");
    assert(limits.world == "maps/main.wmb" && limits.animation == "donus" && limits.percentage == 37.5f);
    assert(limits.percentage_supplied);
    assert(limits.audio == "sounds/start.wav" && limits.audio_loop);
    assert(limits.diagnostics_json == "inspection.json");
    const auto animation = parse({"--animation", "kosma", "--mesh", "archive.rc4::car.mdl"}).settings;
    assert(animation.animation == "kosma" && animation.percentage == 0.0f);
    assert(!animation.percentage_supplied);
    assert(parse({"--mesh", "car.mdl", "--animation", "donus", "--percentage", "0"})
               .settings.percentage_supplied);
    for (const char* value : {"0", "0.0", "100", "100.0", "12.25"})
        assert(parse({"--mesh", "car.mdl", "--animation", "donus", "--percentage", value})
                   .settings.percentage >= 0.0f);
    assert(parse({"--percentage", "100", "--animation", "donus", "--mesh", "car.mdl"})
               .settings.percentage == 100.0f);
    assert(parse({"--world", "archive.rc4::world.wmb"}).settings.world == "archive.rc4::world.wmb");
    assert(parse({"--diagnostics-json", "inspection.json"}).settings.diagnostics_json == "inspection.json");
    const auto audio = parse({"--audio-loop", "--audio", "archive.rc4::start.wav", "--frames", "1"}).settings;
    assert(audio.audio_loop && audio.audio == "archive.rc4::start.wav" && audio.frames == 1);
    assert(!parse({"--frames", "2", "--audio", "start.wav"}).settings.audio_loop);
    const auto inspected_audio = parse({"--audio-loop", "--audio-seconds", "0.5",
                                        "--inspect-audio", "/original/start.wav"}).settings;
    assert(inspected_audio.inspect_audio == "/original/start.wav");
    assert(inspected_audio.audio_loop && inspected_audio.audio_seconds == 0.5);
    assert(inspected_audio.game_dir.empty() && inspected_audio.frames == 0);
    assert(parse({"--inspect-audio", "archive.rc4::start.wav"}).settings.audio_seconds == 1.0);
    assert(parse({"--inspect-audio", "start.wav", "--audio-seconds", "0.1"}).settings.audio_seconds == 0.1);
    assert(parse({"--inspect-audio", "start.wav", "--audio-seconds", "60"}).settings.audio_seconds == 60.0);
    const auto upper = parse({"--width", "7680", "--height", "4320", "--frame-limit", "1000",
                              "--fullscreen", "--windowed", "--vsync", "on"}).settings;
    assert(upper.width == 7680 && upper.height == 4320 && upper.frame_limit == 1000);
    assert(!upper.fullscreen && upper.vsync);
    assert(parse({"--renderer", "vulkan"}).settings.rt_effects == 0);
    assert(parse({"--rt-reflections", "on", "--renderer", "vulkan"}).settings.rt_effects == 1);
    assert(parse({"--renderer", "vulkan", "--rt-shadows", "on"}).settings.rt_effects == 2);
    assert(parse({"--renderer", "vulkan", "--rt-gi", "on"}).settings.rt_effects == 4);
    assert(parse({"--renderer", "vulkan", "--rt-reflections", "on", "--rt-shadows", "on",
                  "--rt-gi", "on", "--rt-shadows", "off"}).settings.rt_effects == 5);
    assert(parse({"--rt-reflections", "on", "--rt-reflections", "off"}).settings.rt_effects == 0);
    for (const char* option : {"--rt-reflections", "--rt-shadows", "--rt-gi"}) {
        rejects([&] { parse({option, "on"}); });
        rejects([&] { parse({"--renderer", "vulkan", option, "true"}); });
        rejects([&] { parse({"--renderer", "vulkan", option}); });
    }
    rejects([] { parse({"--renderer", "vulkan", "--rt-gi", "on", "--renderer", "opengl"}); });
    rejects([] { parse({"--diagnostics", "--diagnostics-json", "inspection.json"}); });
    for (const char* value : {"319", "7681", "0", "-1", "+320", "320x", "", " 320",
                              "9999999999999999999999999999999"})
        rejects([&] { parse({"--width", value}); });
    for (const char* value : {"239", "4321"}) rejects([&] { parse({"--height", value}); });
    for (const char* value : {"-1", "1001"}) rejects([&] { parse({"--frame-limit", value}); });
    for (const char* value : {"0", "-1", "+1", "1x", "18446744073709551616"})
        rejects([&] { parse({"--frames", value}); });
    rejects([] { parse({"--renderer", "vulkan-rt"}); });
    rejects([] { parse({"--vsync", "true"}); });
    rejects([] { parse({"--unknown"}); });
    rejects([] { parse({"--width"}); });
    rejects([] { parse({"--width", "--height", "480"}); });
    rejects([] { parse({"--game-dir", ""}); });
    rejects([] { parse({"--capture", "frame.ppm"}); });
    rejects([] { parse({"--mesh", ""}); });
    rejects([] { parse({"--texture"}); });
    rejects([] { parse({"--animation", "donus"}); });
    rejects([] { parse({"--percentage", "0"}); });
    rejects([] { parse({"--mesh", "car.mdl", "--percentage", "0"}); });
    rejects([] { parse({"--audio-loop"}); });
    rejects([] { parse({"--audio", "start.wav"}); });
    rejects([] { parse({"--audio", "start.wav", "--frames", "0"}); });
    for (const char* option : {"--world", "--audio", "--animation", "--diagnostics-json", "--percentage",
                               "--inspect-audio", "--audio-seconds"}) {
        rejects([&] { parse({option}); });
        rejects([&] { parse({option, ""}); });
    }
    for (const char* value : {"-0.01", "100.01", "100.000001", "nan", "NaN", "NAN",
                              "inf", "INF", "-inf", "1e999", "12.5x", " 12.5", "12.5 ",
                              "+12.5", "0x1p2", "."})
        rejects([&] { parse({"--mesh", "car.mdl", "--animation", "donus", "--percentage", value}); });
    rejects([] { parse({"--audio-seconds", "1"}); });
    for (const char* value : {"0", "-1", "0.099999", "60.000001", "nan", "NAN", "inf",
                              "INF", "-inf", "1e999", "1x", " 1", "1 ", "+1", "0x1p0"})
        rejects([&] { parse({"--inspect-audio", "start.wav", "--audio-seconds", value}); });
    for (const char* option : {"--audio", "--world", "--mesh", "--texture", "--animation",
                               "--frames", "--capture", "--diagnostics-json"})
        rejects([&] { parse({"--inspect-audio", "start.wav", option, "1"}); });

    for (const auto& transient :
         {parse({"--inspect"}).settings, parse({"--graphics-capabilities"}).settings,
          parse({"--input-script", "drive.json", "--frames", "12"}).settings,
          parse({"--source-frames", "32", "--frames", "500", "--capture", "source.ppm"}).settings}) {
        c4::save_settings(path, transient);
        const auto restored = c4::load_settings(path);
        assert(!restored.inspection_requested && !restored.graphics_capabilities && restored.input_script.empty());
        assert(restored.source_frames == 0);
        const auto inherited = parse({}).settings;
        assert(!inherited.inspection_requested && !inherited.graphics_capabilities && inherited.input_script.empty());
        assert(inherited.source_frames == 0);
        std::ifstream config(path);
        const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
        for (const char* key : {"inspection_requested", "graphics_capabilities", "input_script", "source_frames"})
            assert(text.find(key) == std::string::npos);
    }

    auto saved = limits;
    saved.game_dir = "a directory/with \\\"quotes\\\"\nand newline";
    saved.inspect_audio = "another original.wav";
    saved.audio_seconds = 12.5;
    c4::save_settings(path, saved);
    const auto loaded = c4::load_settings(path);
    assert(loaded.game_dir == saved.game_dir && loaded.width == saved.width);
    assert(loaded.height == saved.height && loaded.fullscreen == saved.fullscreen);
    assert(loaded.vsync == saved.vsync && loaded.frame_limit == saved.frame_limit);
    assert(loaded.frames == 0 && loaded.capture.empty() && !loaded.diagnostics);
    assert(loaded.mesh.empty() && loaded.texture.empty());
    assert(loaded.world.empty() && loaded.audio.empty() && loaded.diagnostics_json.empty());
    assert(loaded.animation.empty() && loaded.percentage == 0.0f && !loaded.audio_loop);
    assert(!loaded.percentage_supplied);
    assert(loaded.inspect_audio.empty() && loaded.audio_seconds == 1.0);
    assert(!loaded.inspection_requested && !loaded.graphics_capabilities && loaded.input_script.empty());
    assert(parse({"--width", "640"}).settings.width == 640);
    assert(parse({"--width", "640"}).settings.height == saved.height);
    struct stat metadata{};
    assert(::stat(path.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600);
    saved.width = 640;
    c4::save_settings(path, saved);
    assert(c4::load_settings(path).width == 640);
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        assert(entry.path().filename() == "runtime.conf");
    const auto launcher = path.parent_path() / "launcher.conf";
    {
        std::ofstream unrelated(launcher);
        unrelated << "leave launcher settings unchanged\n";
    }
    c4::save_settings(path, saved);
    {
        std::ifstream unrelated(launcher);
        std::string line;
        std::getline(unrelated, line);
        assert(line == "leave launcher settings unchanged");
    }
    {
        auto graphics_saved = saved;
        graphics_saved.graphics = graphics;
        c4::save_settings(path, graphics_saved);
        assert_same_graphics(c4::load_settings(path).graphics, graphics);
        assert_same_graphics(parse({}).settings.graphics, graphics);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            for (const char* line : {"quality ultra\n", "msaa 8\n", "post_aa fxaa\n", "ssao on\n",
                                     "shadows high\n", "pbr on\n", "internal_hdr on\n", "bloom on\n",
                                     "exposure_ev -2.5\n", "tone_map aces\n", "texture_filter anisotropic\n",
                                     "denoising high\n"})
                assert(text.find(line) != std::string::npos);
        }
        graphics_saved.graphics = repeated_graphics;
        c4::save_settings(path, graphics_saved);
        assert_same_graphics(c4::load_settings(path).graphics, repeated_graphics);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            for (const char* line : {"quality enhanced\n", "msaa 2\n", "post_aa off\n", "ssao off\n",
                                     "shadows low\n", "pbr off\n", "internal_hdr off\n", "bloom off\n",
                                     "exposure_ev 0\n", "tone_map reinhard\n", "texture_filter original\n",
                                     "denoising off\n"})
                assert(text.find(line) != std::string::npos);
        }
        graphics_saved.graphics.texture_filtering = quality::TextureFiltering::Trilinear;
        graphics_saved.graphics.denoising = quality::Denoising::Standard;
        c4::save_settings(path, graphics_saved);
        assert_same_graphics(c4::load_settings(path).graphics, graphics_saved.graphics);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            assert(text.find("texture_filter trilinear\n") != std::string::npos);
            assert(text.find("denoising standard\n") != std::string::npos);
        }
        graphics_saved.graphics = graphics;
        graphics_saved.graphics.preset = quality::Preset::Custom;
        c4::save_settings(path, graphics_saved);
        assert_same_graphics(parse({}).settings.graphics, graphics_saved.graphics);
        const auto inherited_override = parse({"--msaa", "4"}).settings.graphics;
        auto expected_inherited = graphics_saved.graphics;
        expected_inherited.samples = 4;
        assert_same_graphics(inherited_override, expected_inherited);
        for (const char* preset : {"original", "enhanced", "ultra", "custom"})
            assert_no_graphics_overrides(parse({"--quality", preset}).settings.graphics);
        quality::GraphicSettings expected_reset;
        expected_reset.preset = quality::Preset::Enhanced;
        expected_reset.samples = 4;
        assert_same_graphics(parse({"--msaa", "4", "--quality", "enhanced"}).settings.graphics,
                             expected_reset);
        assert_same_graphics(parse({"--quality", "enhanced", "--msaa", "4"}).settings.graphics,
                             expected_reset);
        assert_same_graphics(parse({"--msaa", "4", "--quality", "original",
                                    "--quality", "enhanced"}).settings.graphics, expected_reset);
        const auto reset_before =
            parse({"--msaa", "2", "--post-aa", "off", "--ssao", "off", "--shadows", "low",
                   "--pbr", "off", "--internal-hdr", "off", "--bloom", "off",
                   "--exposure-ev", "0", "--tone-map", "reinhard", "--texture-filter", "original",
                   "--denoising", "off", "--quality", "enhanced"}).settings;
        const auto reset_after =
            parse({"--quality", "enhanced", "--msaa", "2", "--post-aa", "off", "--ssao", "off",
                   "--shadows", "low", "--pbr", "off", "--internal-hdr", "off", "--bloom", "off",
                   "--exposure-ev", "0", "--tone-map", "reinhard", "--texture-filter", "original",
                   "--denoising", "off"}).settings;
        assert_same_graphics(reset_before.graphics, repeated_graphics);
        assert_same_graphics(reset_after.graphics, repeated_graphics);
        assert(reset_before.width == saved.width && reset_before.height == saved.height);
        assert(reset_before.game_dir == saved.game_dir && reset_before.renderer == "vulkan");
        assert(reset_before.rt_effects == 0 && reset_after.renderer == "vulkan" && reset_after.rt_effects == 0);
        for (const auto& [name, preset] :
             {std::pair{"original", quality::Preset::Original},
              std::pair{"enhanced", quality::Preset::Enhanced},
              std::pair{"ultra", quality::Preset::Ultra},
              std::pair{"custom", quality::Preset::Custom}}) {
            graphics_saved.graphics = {};
            graphics_saved.graphics.preset = preset;
            c4::save_settings(path, graphics_saved);
            const auto restored = c4::load_settings(path).graphics;
            assert(restored.preset == preset);
            assert_no_graphics_overrides(restored);
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            assert(text.find(std::string("quality ") + name + "\n") != std::string::npos);
            for (const char* key : {"msaa ", "post_aa ", "ssao ", "shadows ", "pbr ", "internal_hdr ",
                                    "bloom ", "exposure_ev ", "tone_map ", "texture_filter ", "denoising "})
                assert(text.find(key) == std::string::npos);
            assert(text.find("backend_override ") == std::string::npos);
        }
        graphics_saved.renderer = "vulkan";
        graphics_saved.rt_effects = 7;
        graphics_saved.graphics = graphics;
        graphics_saved.graphics.preset = quality::Preset::Custom;
        graphics_saved.graphics.backend_override = true;
        c4::save_settings(path, graphics_saved);
        assert_same_graphics(c4::load_settings(path).graphics, graphics_saved.graphics);
        assert(parse({}).settings.graphics.backend_override);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            assert(text.find("backend_override on\n") != std::string::npos);
        }
        const auto inherited_original = parse({"--quality", "original"}).settings;
        assert(inherited_original.renderer == "opengl" && inherited_original.rt_effects == 0);
        assert(inherited_original.graphics.preset == quality::Preset::Original);
        assert_no_graphics_overrides(inherited_original.graphics);
        const auto inherited_enhanced = parse({"--quality", "enhanced"}).settings;
        assert(inherited_enhanced.renderer == "vulkan" && inherited_enhanced.rt_effects == 0);
        assert(inherited_enhanced.graphics.preset == quality::Preset::Enhanced);
        assert_no_graphics_overrides(inherited_enhanced.graphics);
        const auto inherited_ultra = parse({"--quality", "ultra"}).settings;
        assert(inherited_ultra.renderer == "vulkan" && inherited_ultra.rt_effects == 7);
        assert(inherited_ultra.graphics.preset == quality::Preset::Ultra);
        assert_no_graphics_overrides(inherited_ultra.graphics);
        const auto inherited_custom = parse({"--quality", "custom"}).settings;
        assert(inherited_custom.renderer == "vulkan" && inherited_custom.rt_effects == 7);
        assert(inherited_custom.graphics.preset == quality::Preset::Custom);
        assert_no_graphics_overrides(inherited_custom.graphics);
        const auto custom_after_bundle = parse({"--quality", "original", "--quality", "custom"}).settings;
        assert(custom_after_bundle.renderer == "vulkan" && custom_after_bundle.rt_effects == 7);
        for (const auto& explicit_backend :
             {parse({"--renderer", "vulkan", "--quality", "enhanced"}).settings,
              parse({"--quality", "enhanced", "--renderer", "vulkan"}).settings,
              parse({"--rt-shadows", "off", "--quality", "original"}).settings,
              parse({"--quality", "original", "--rt-shadows", "off"}).settings}) {
            assert(explicit_backend.graphics.backend_override);
        }
        {
            std::ofstream explicit_false(path, std::ios::trunc);
            explicit_false << "version 1\nbackend_override off\n";
        }
        const auto false_marker = c4::load_settings(path);
        assert(!false_marker.graphics.backend_override);
        c4::save_settings(path, false_marker);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            assert(text.find("backend_override ") == std::string::npos);
        }
        c4::save_settings(path, saved);
    }
    {
        auto source_saved = saved;
        source_saved.source_profile = maximum_source;
        c4::save_settings(path, source_saved);
        assert(c4::load_settings(path).source_profile == maximum_source);
        assert(parse({}).settings.source_profile == maximum_source);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            for (const auto& control : source_controls)
                assert(text.find(std::string(control.key) + " " + std::to_string(control.maximum) + "\n") !=
                       std::string::npos);
        }
        for (const auto& control : source_controls) {
            auto expected = maximum_source;
            expected.*control.member = 0;
            const auto explicit_zero = parse({control.option, "0"}).settings;
            assert(explicit_zero.source_profile == expected);
            c4::save_settings(path, explicit_zero);
            assert(c4::load_settings(path).source_profile == expected);
            {
                std::ifstream config(path);
                const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
                const auto field = text.find(std::string(control.key) + " ");
                if (control.member == &SourceProfile::cgm || control.member == &SourceProfile::stq) {
                    assert(field != std::string::npos);
                    const auto restored = c4::load_settings(path);
                    assert(control.member == &SourceProfile::cgm ? restored.source_cgm_explicit :
                                                                 restored.source_stq_explicit);
                } else {
                    assert(field == std::string::npos);
                }
            }
            c4::save_settings(path, source_saved);
            auto invalid = source_saved;
            invalid.source_profile.*control.member = static_cast<std::uint8_t>(control.maximum + 1);
            rejects([&] { c4::save_settings(path, invalid); });
            assert(c4::load_settings(path).source_profile == maximum_source);
        }
        for (const char* preset : {"original", "enhanced", "ultra", "custom"})
            assert(parse({"--quality", preset}).settings.source_profile == maximum_source);
        assert(parse({"--renderer", "vulkan", "--rt-reflections", "on"}).settings.source_profile == maximum_source);
        assert(parse({"--bloom", "off"}).settings.source_profile == maximum_source);
        source_saved.source_profile = c4::assets::original_source_profile;
        c4::save_settings(path, source_saved);
        assert(c4::load_settings(path).source_profile == c4::assets::original_source_profile);
        {
            std::ifstream config(path);
            const std::string text{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
            for (const auto& control : source_controls)
                assert(text.find(std::string(control.key) + " ") == std::string::npos);
        }
        {
            std::ofstream explicit_zero(path, std::ios::trunc);
            explicit_zero << "version 1\n";
            for (const auto& control : source_controls) explicit_zero << control.key << " 0\n";
        }
        assert(c4::load_settings(path).source_profile == c4::assets::original_source_profile);
        c4::save_settings(path, saved);
    }
    saved.renderer = "vulkan";
    saved.rt_effects = 7;
    c4::save_settings(path, saved);
    assert(c4::load_settings(path).renderer == "vulkan" && c4::load_settings(path).rt_effects == 7);
    assert(parse({"--rt-shadows", "off"}).settings.rt_effects == 5);
    rejects([] { parse({"--renderer", "opengl"}); });
    assert(parse({"--renderer", "opengl", "--rt-reflections", "off", "--rt-shadows", "off", "--rt-gi", "off"})
               .settings.rt_effects == 0);
    saved.renderer = "opengl";
    rejects([&] { c4::save_settings(path, saved); });
    saved.renderer = "vulkan-rt";
    rejects([&] { c4::save_settings(path, saved); });
    assert(c4::load_settings(path).renderer == "vulkan");
    {
        std::ofstream old_config(path, std::ios::trunc);
        old_config << "version 1\nrenderer \"vulkan\"\n";
    }
    assert(parse({}).settings.rt_effects == 0);
    const auto old_graphics = c4::load_settings(path).graphics;
    assert(old_graphics.preset == quality::Preset::Original);
    assert_no_graphics_overrides(old_graphics);
    assert_no_graphics_overrides(parse({}).settings.graphics);
    assert(c4::load_settings(path).source_profile == c4::assets::original_source_profile);
    assert(parse({}).settings.source_profile == c4::assets::original_source_profile);
    {
        std::ofstream inconsistent_config(path, std::ios::trunc);
        inconsistent_config << "version 1\nrenderer \"opengl\"\nrt_effects 1\n";
    }
    rejects([] { parse({}); });
    assert(parse({"--renderer", "vulkan"}).settings.rt_effects == 1);

    for (const char* text : {"", "version 2\n", "version 1\nunknown 3\n",
                             "version 1\nwidth 320\nwidth 640\n", "width 640\n",
                             "version 1\nwidth 319\n", "version 1\nheight 4321\n",
                             "version 1\nframe_limit 1001\n", "version 1\nwidth +640\n",
                             "version 1\nwidth 640x\n", "version 1\nfullscreen true\n",
                             "version 1\nvsync maybe\n", "version 1\nrenderer \"vulkan-rt\"\n",
                             "version 1\ngame_dir unquoted\n", "version 1\ngame_dir \"unfinished",
                             "version 1\nframes 20\n", "version 1\nversion 1\n",
                             "version 1\nworld \"main.wmb\"\n", "version 1\naudio \"start.wav\"\n",
                             "version 1\ndiagnostics_json \"inspection.json\"\n",
                             "version 1\nanimation \"donus\"\n", "version 1\npercentage 25\n",
                             "version 1\naudio_loop on\n", "version 1\ninspect_audio \"start.wav\"\n",
                             "version 1\naudio_seconds 2\n", "version 1\nrt_effects 8\n",
                             "version 1\nrt_effects -1\n", "version 1\nrt_effects +1\n",
                             "version 1\nrt_effects 1x\n", "version 1\nrt_effects 0\nrt_effects 1\n"})
        corrupt(path, text);
    for (const char* field : {"inspection_requested on", "graphics_capabilities on",
                              "input_script \"drive.json\"", "inspect on", "source_frames 32"})
        corrupt(path, std::string("version 1\n") + field + "\n");
    for (const char* field : {"quality original", "msaa 2", "post_aa off", "ssao off",
                              "shadows low", "pbr off", "internal_hdr off", "bloom off",
                              "exposure_ev 0", "tone_map reinhard"})
        corrupt(path, std::string("version 1\n") + field + "\n" + field + "\n");
    for (const char* field : {"quality unknown", "quality Original", "quality \"ultra\"",
                              "msaa 0", "msaa 3", "msaa 16", "msaa +2", "msaa 2x",
                              "msaa 999999999999999999999999", "post_aa on", "post_aa FXAA",
                              "post_aa \"off\"", "ssao true", "ssao 1", "shadows medium",
                              "shadows HIGH", "shadows \"low\"", "pbr false", "internal_hdr ON",
                              "bloom 0", "exposure_ev -10.000001", "exposure_ev 10.000001",
                              "exposure_ev nan", "exposure_ev NaN", "exposure_ev inf",
                              "exposure_ev -inf", "exposure_ev 1e999", "exposure_ev 2.5x",
                              "exposure_ev +2.5", "exposure_ev 0x1p2", "exposure_ev .",
                              "exposure_ev \"0\"", "tone_map linear", "tone_map ACES",
                              "tone_map \"aces\""})
        corrupt(path, std::string("version 1\n") + field + "\n");
    corrupt(path, "version 1\nbackend_override on\nbackend_override off\n");
    for (const char* value : {"true", "false", "1", "0", "ON", "Off", "maybe", "\"on\""})
        corrupt(path, std::string("version 1\nbackend_override ") + value + "\n");
    corrupt(path, "version 1\nbackend_override\n");
    corrupt(path, "version 1\ntexture_filter original\ntexture_filter anisotropic\n");
    for (const char* value : {"linear", "on", "Original", "TRILINEAR", "\"original\""})
        corrupt(path, std::string("version 1\ntexture_filter ") + value + "\n");
    corrupt(path, "version 1\ntexture_filter\n");
    corrupt(path, "version 1\ndenoising off\ndenoising high\n");
    for (const char* value : {"on", "low", "medium", "HIGH", "Standard", "\"off\""})
        corrupt(path, std::string("version 1\ndenoising ") + value + "\n");
    corrupt(path, "version 1\ndenoising\n");
    for (const char* key : {"quality", "msaa", "post_aa", "ssao", "shadows", "pbr",
                            "internal_hdr", "bloom", "exposure_ev", "tone_map"})
        corrupt(path, std::string("version 1\n") + key + "\n");
    for (const auto& control : source_controls) {
        corrupt(path, std::string("version 1\n") + control.key + " 0\n" + control.key + " 1\n");
        corrupt(path, std::string("version 1\n") + control.key + " " +
                      std::to_string(control.maximum + 1) + "\n");
        corrupt(path, std::string("version 1\n") + control.key + "\n");
        for (const char* value : {"-1", "+1", "1.0", "1x", "\"1\"", "nan", "0x1",
                                  "9999999999999999999999999999999"})
            corrupt(path, std::string("version 1\n") + control.key + " " + value + "\n");
    }
    assert(parse({"--help"}).help);
    assert(parse({"--percentage", "nan", "--audio-loop", "--help"}).help);
    assert(parse({"--quality", "unknown", "--msaa", "3", "--exposure-ev", "nan", "--help"}).help);
    assert(parse({"--graphics-capabilities", "--input-script", "", "--inspect", "--help"}).help);
    assert(parse({"--diagnostics", "--graphics-capabilities", "--help"}).help);
    assert(parse({"--graphics-capabilities", "--diagnostics", "--help"}).help);
    assert(parse({"--diagnostics", "--input-script", "drive.json", "--help"}).help);
    assert(parse({"--input-script", "drive.json", "--diagnostics", "--help"}).help);
    assert(parse({"--source-atq", "nan", "--source-ano", "2", "--help"}).help);
    assert(parse({"--source-frames", "0", "--inspect", "--help"}).help);
    {
        Environment invalid("XDG_CONFIG_HOME", "relative");
        rejects([] { c4::config_path(); });
        assert(parse({"--help"}).help);
    }
    {
        Environment home("HOME", directory.path.c_str());
        ::unsetenv("XDG_CONFIG_HOME");
        assert(c4::config_path() == directory.path / ".config/citroen-c4-robot/runtime.conf");
        Environment empty("XDG_CONFIG_HOME", "");
        assert(c4::config_path() == directory.path / ".config/citroen-c4-robot/runtime.conf");
    }
}
