#pragma once

#include "core/settings.hpp"
#if defined(C4_ENABLE_VULKAN) && C4_ENABLE_VULKAN
#include "rendering/vulkan/VulkanScene.hpp"
#else
namespace c4::rendering { struct VulkanBackendDiagnostics; }
#endif
#include "rendering/quality/QualityDiagnostics.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace c4 {

struct SourceSpriteOmissionDiagnostics {
    std::optional<std::uint32_t> source_entity;
    std::string_view source;
    std::string_view reason;
};

// Wall-clock nanoseconds, retained in a fixed ring with no per-frame allocation.
// Statistics are sorted only when the final diagnostic document is written.
struct RuntimeTimingSamples {
    static constexpr std::size_t capacity = 512;
    std::array<std::uint64_t, capacity> nanoseconds{};
    std::uint64_t total_samples{};

    constexpr void observe(std::uint64_t elapsed_ns) {
        nanoseconds[total_samples % capacity] = elapsed_ns;
        ++total_samples;
    }

    constexpr std::size_t retained_samples() const {
        return static_cast<std::size_t>(std::min<std::uint64_t>(total_samples, capacity));
    }

    // Nearest rank: ceil(0.95 * count) - 1; count is in 1..capacity.
    static constexpr std::size_t p95_index(std::size_t count) {
        return (95 * count + 99) / 100 - 1;
    }
};

static_assert(RuntimeTimingSamples::p95_index(1) == 0);
static_assert(RuntimeTimingSamples::p95_index(20) == 18);
static_assert(RuntimeTimingSamples::p95_index(512) == 486);
static_assert([] {
    RuntimeTimingSamples samples;
    for (std::uint64_t i = 0; i < 513; ++i) samples.observe(i);
    return samples.total_samples == 513 && samples.retained_samples() == 512
        && samples.nanoseconds[0] == 512 && samples.nanoseconds[1] == 1;
}());

struct RuntimeDiagnostics {
    std::string runtime_kind = "native-inspection";
    std::string game_diagnostics;
    std::uint64_t source_frames{};
    std::uint64_t requested_source_frames{};
    std::uint64_t history_epoch{};
    bool target_reached{};
    std::string_view finite_stop_failure_reason;
    bool input_scenario_finished{};
    std::int32_t source_mouse_force_raw{}, source_mouse_previous_time_frame_raw{}, source_mouse_mode_raw{};
    float source_mouse_history_x{};
    bool source_mouse_relative_requested{}, source_mouse_focused{};
    std::uint64_t source_mouse_sample_frames{};
    std::string source_mouse_backend_parity = "not_proven";
    // The original cold branch is reconstructed using native-device evidence,
    // not an observation of the original executable's D3D capability word.
    std::uint32_t source_gpu_backend_limit{}, source_gpu_preparation_limit{};
    bool source_gpu_backend_mip_witness{}, source_gpu_preparation_mip_witness{};
    std::string source_gpu_cold_limit_policy = "not_prepared";
    std::string source_gpu_cold_limit_proof = "not_prepared";
    std::string camera_provenance = "new_orthographic";
    std::string quality_capabilities, resolved_quality;
    std::string backend = "opengl";
    std::string requested_backend;
    std::string backend_fallback_reason;
    const rendering::VulkanBackendDiagnostics* vulkan = nullptr;
    std::string vulkan_json;
    int logical_width = 0;
    int logical_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    bool fullscreen = false;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::int32_t source_ano_raw{};
    float source_sampler_anisotropy_level{}, source_sampler_anisotropy_maximum{};
    bool source_sampler_anisotropy_supported{};
    std::string source_sampler_anisotropy_fallback_reason;
    std::uint64_t source_texture_images{}, source_texture_precision_fallbacks{};
    std::vector<std::string> source_texture_fallback_reasons;
    std::uint64_t ui_texture_images{}, ui_texture_precision_fallbacks{};
    std::vector<std::string> ui_texture_fallback_reasons;
    bool requested_vsync = false;
    std::optional<int> applied_swap_interval;
    int frame_limit = 0;
    std::uint64_t rendered_frames = 0;
    std::optional<std::uint32_t> observed_display_id;
    std::optional<double> observed_refresh_hz;
    RuntimeTimingSamples frame_cpu_elapsed_until_draw_return;
    RuntimeTimingSamples present_call;
    RuntimeTimingSamples present_return_interval;
    float animation_percentage = 0.0f;
    bool animation_playback = false;
    std::uint64_t mesh_vertices = 0;
    std::uint64_t mesh_triangles = 0;
    std::uint64_t mesh_groups = 0;
    std::uint64_t world_instances = 0;
    std::uint64_t world_models = 0;
    std::uint64_t world_vertices = 0;
    std::uint64_t world_triangles = 0;
    std::uint64_t world_lightmaps = 0;
    std::uint64_t world_materials = 0;
    std::uint64_t world_entities = 0;
    std::uint64_t world_sprite_entities = 0;
    std::uint64_t world_omitted_sprite_entities = 0;
    std::uint64_t sprite_private_flags_unknown{}, sprite_cached_euler_unknown{};
    std::array<SourceSpriteOmissionDiagnostics, 64> source_sprite_omissions;
    std::size_t source_sprite_omissions_size{};
    std::uint64_t world_hidden_entities = 0;
    std::uint64_t world_unresolved_actions = 0;
    std::uint64_t draw_calls = 0;
    std::uint64_t scene_draw_calls = 0;
    std::uint64_t mesh_draw_calls = 0;
    std::uint64_t world_draw_calls = 0;
    std::uint64_t image_draw_calls = 0;
    std::uint64_t ui_draw_calls = 0;
    std::uint64_t material_draw_calls{}, incomplete_material_draw_calls{}, particle_draw_calls{};
    std::uint64_t shadow_draw_calls{}, pbr_draw_calls{}, source_bloom_passes{}, gamma_passes{};
    bool authored_source_shadows{};
    unsigned active_shadow_cascades{}, active_shadow_size{};
    bool audio_requested = false;
    bool audio_loop = false;
    bool audio_playing = false;
    std::uint32_t audio_active_voices{};
    float audio_master_volume{};
    std::string audio_mode = "not_requested";
    bool audio_device_open{};
    std::string audio_driver;
    std::string audio_device;
    std::string audio_error;
    std::uint64_t audio_frames = 0;
    std::uint64_t audio_clip_frames = 0;
    std::uint32_t audio_sample_rate = 0;
    std::uint32_t audio_channels = 0;
};

namespace {

class RuntimeDiagnosticsJSON {
    std::FILE* file_;

public:
    explicit RuntimeDiagnosticsJSON(std::FILE* file) : file_(file) {}

    void write(std::string_view text) {
        if (std::fwrite(text.data(), 1, text.size(), file_) != text.size()) {
            throw std::system_error(errno ? errno : EIO, std::generic_category(),
                                    "cannot write runtime diagnostics");
        }
    }

    void value(std::string_view text) {
        static constexpr char hex[] = "0123456789abcdef";
        write("\"");
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte >= 0x20 && byte != '"' && byte != '\\') continue;
            write(text.substr(start, i - start));
            if (byte == '"') write("\\\"");
            else if (byte == '\\') write("\\\\");
            else {
                const char escaped[] = {'\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 15]};
                write(std::string_view(escaped, sizeof escaped));
            }
            start = i + 1;
        }
        write(text.substr(start));
        write("\"");
    }

    void value(const std::string& text) { value(std::string_view(text)); }
    void value(const char* text) { value(std::string_view(text)); }
    void value(bool flag) { write(flag ? "true" : "false"); }

    template<class Number>
        requires (std::is_integral_v<Number> || std::is_floating_point_v<Number>)
    void value(Number number) {
        if constexpr (std::is_floating_point_v<Number>) {
            if (!std::isfinite(number)) {
                throw std::runtime_error("non-finite number in runtime diagnostics");
            }
        }
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, number);
        if (result.ec != std::errc{}) {
            throw std::system_error(std::make_error_code(result.ec),
                                    "cannot format runtime diagnostics number");
        }
        write(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
    }

    template<class Number>
    void value(const std::optional<Number>& number) {
        if (number) value(*number);
        else write("null");
    }

    void value(const std::filesystem::path& path) {
        value(std::string_view(path.native()));
    }

    template<class Value>
    void field(std::string_view name, const Value& data, bool last = false) {
        value(name);
        write(":");
        value(data);
        write(last ? "\n" : ",\n");
    }

    void nullable_field(std::string_view name, std::string_view text,
                        bool available = true, bool last = false) {
        value(name);
        write(":");
        if (available && !text.empty()) value(text);
        else write("null");
        write(last ? "\n" : ",\n");
    }
};

inline void write_runtime_timing_distribution(RuntimeDiagnosticsJSON& json,
                                              std::string_view name,
                                              const RuntimeTimingSamples& samples,
                                              std::string_view scope,
                                              bool last = false) {
    json.value(name);
    json.write(":{\n");
    json.field("scope", scope);
    json.field("total_samples", samples.total_samples);
    const auto count = samples.retained_samples();
    json.field("retained_samples", count);
    std::optional<double> minimum, median, p95, maximum;
    if (count) {
        auto sorted = samples.nanoseconds;
        std::sort(sorted.begin(), sorted.begin() + count);
        constexpr double ns_per_ms = 1'000'000.0;
        minimum = sorted[0] / ns_per_ms;
        median = (sorted[(count - 1) / 2] / ns_per_ms + sorted[count / 2] / ns_per_ms) / 2.0;
        p95 = sorted[RuntimeTimingSamples::p95_index(count)] / ns_per_ms;
        maximum = sorted[count - 1] / ns_per_ms;
    }
    json.field("min_ms", minimum);
    json.field("median_ms", median);
    json.field("p95_ms", p95);
    json.field("max_ms", maximum, true);
    json.write(last ? "}\n" : "},\n");
}

} // namespace

inline void write_graphics_capabilities(std::string_view backend, std::string_view device,
                                       const rendering::quality::GraphicsCapabilities& capabilities,
                                       const rendering::quality::ResolvedGraphics& resolved,
                                       const rendering::VulkanBackendDiagnostics* vulkan = nullptr,
                                       std::uint32_t requested_mask = 0) {
    RuntimeDiagnosticsJSON json(stdout);
    json.write("{\n");
    json.field("schema_version", 1);
    json.field("renderer", backend);
    json.field("device", device);
    json.write("\"capabilities\":");
    json.write(rendering::quality::capabilities_json(capabilities));
    json.write(",\n\"resolved\":");
    auto display_resolved = resolved;
    display_resolved.effective_preset = rendering::quality::effective_profile(
        display_resolved, backend == "vulkan" ? rendering::quality::RendererBackend::Vulkan
                                            : rendering::quality::RendererBackend::OpenGL,
        0);
    json.write(rendering::quality::resolved_graphics_json(display_resolved));
    json.write(",\n\"ray_tracing\":{\n");
    json.field("requested_mask", requested_mask);
    std::uint32_t supported_mask{};
    std::string_view fallback_reason = "OpenGL backend has no ray tracing implementation";
#if defined(C4_ENABLE_VULKAN) && C4_ENABLE_VULKAN
    if (vulkan) {
        supported_mask = vulkan->rt_supported_effects;
        fallback_reason = vulkan->rt_fallback_reason;
    }
#else
    (void)vulkan;
    if (backend == "vulkan") fallback_reason = "Vulkan support is not compiled in this build";
#endif
    json.field("supported_mask", supported_mask);
    json.field("active_mask", 0u); // Capability queries never dispatch effects.
    json.field("pipelines_ready", supported_mask != 0);
    json.nullable_field("fallback_reason", fallback_reason, true, true);
    json.write("}\n}\n");
    if (std::fflush(stdout) != 0)
        throw std::system_error(errno ? errno : EIO, std::generic_category(), "cannot flush graphics capabilities");
}

inline void write_runtime_diagnostics(const std::filesystem::path& path,
                                      const Settings& settings,
                                      const RuntimeDiagnostics& diagnostics) {
    const auto& filename = path.native();
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(
        std::fopen(filename.c_str(), "wbx"), &std::fclose);
    if (!file) {
        throw std::system_error(errno ? errno : EIO, std::generic_category(),
                                "cannot exclusively create runtime diagnostics: " + filename);
    }
    RuntimeDiagnosticsJSON json(file.get());
    json.write("{\n");
    json.field("schema_version", 1);
    json.field("runtime_kind", diagnostics.runtime_kind);
    json.field("backend", diagnostics.backend);
    json.field("requested_backend", diagnostics.requested_backend);
    json.field("backend_fallback_reason", diagnostics.backend_fallback_reason);
    json.field("history_epoch", diagnostics.history_epoch);
    json.field("source_frames", diagnostics.source_frames);
    json.field("requested_source_frames", diagnostics.requested_source_frames);
    json.field("completed_source_frames", diagnostics.source_frames);
    json.field("last_source_frame_index", diagnostics.source_frames
        ? std::optional<std::uint64_t>(diagnostics.source_frames - 1) : std::nullopt);
    json.field("target_reached", diagnostics.target_reached);
    json.nullable_field("finite_stop_failure_reason", diagnostics.finite_stop_failure_reason);
    json.field("input_scenario_finished", diagnostics.input_scenario_finished);
    json.write("\"source_texture_preparation\":{\n");
    json.field("original_cold_limit_policy", diagnostics.source_gpu_cold_limit_policy);
    json.field("original_cold_limit_proof", diagnostics.source_gpu_cold_limit_proof);
    json.field("original_caps_observed", false);
    json.field("current_backend_mip_witness", diagnostics.source_gpu_backend_mip_witness);
    json.field("prepared_context_mip_witness", diagnostics.source_gpu_preparation_mip_witness);
    json.field("current_backend_limit", diagnostics.source_gpu_backend_limit);
    json.field("prepared_context_limit", diagnostics.source_gpu_preparation_limit, true);
    json.write("},\n");
    json.write("\"source_input\":");
    if (diagnostics.runtime_kind == "native-game") {
        json.write("{\"mouse\":{\n");
        json.field("publication", "physical_source_producer_before_optional_script_override");
        json.field("force_raw", diagnostics.source_mouse_force_raw);
        json.field("history_x", diagnostics.source_mouse_history_x);
        json.field("previous_time_frame_raw", diagnostics.source_mouse_previous_time_frame_raw);
        json.field("mode_raw", diagnostics.source_mouse_mode_raw);
        json.field("relative_requested", diagnostics.source_mouse_relative_requested);
        json.field("focused", diagnostics.source_mouse_focused);
        json.field("sample_frames", diagnostics.source_mouse_sample_frames);
        json.field("backend_parity", diagnostics.source_mouse_backend_parity, true);
        json.write("}}");
    } else json.write("null");
    json.write(",\n");
    json.write("\"game\":");
    json.write(diagnostics.game_diagnostics.empty() ? "null" : diagnostics.game_diagnostics);
    json.write(",\n\"graphics\":");
    if (diagnostics.quality_capabilities.empty()) json.write("null");
    else {
        json.write("{\"capabilities\":");
        json.write(diagnostics.quality_capabilities);
        json.write(",\"resolved\":");
        json.write(diagnostics.resolved_quality);
        json.write("}");
    }
    json.write(",\n");
    json.write("\"window\":{\n");
    json.field("logical_width", diagnostics.logical_width);
    json.field("logical_height", diagnostics.logical_height);
    json.field("drawable_width", diagnostics.drawable_width);
    json.field("drawable_height", diagnostics.drawable_height);
    json.field("fullscreen", diagnostics.fullscreen, true);
    json.write("},\n\"opengl\":");
    if (diagnostics.backend == "opengl") {
        json.write("{\n");
        json.nullable_field("vendor", diagnostics.vendor);
        json.nullable_field("renderer", diagnostics.renderer);
        json.nullable_field("version", diagnostics.version);
        json.field("source_ano_raw", diagnostics.source_ano_raw);
        json.field("source_sampler_anisotropy_level", diagnostics.source_sampler_anisotropy_level);
        json.field("source_sampler_anisotropy_maximum", diagnostics.source_sampler_anisotropy_maximum);
        json.field("source_sampler_anisotropy_supported", diagnostics.source_sampler_anisotropy_supported);
        json.field("source_sampler_anisotropy_fallback_reason", diagnostics.source_sampler_anisotropy_fallback_reason);
        json.write("\"active_shadows\":{\n");
        json.field("mode", diagnostics.authored_source_shadows ? "authored" :
                   diagnostics.active_shadow_cascades ? "supplemental" : "off");
        json.field("cascades", diagnostics.active_shadow_cascades);
        json.field("size", diagnostics.active_shadow_size, true);
        json.write("},\n");
        json.write("\"source_textures\":{\n");
        json.field("images", diagnostics.source_texture_images);
        json.field("precision_fallbacks", diagnostics.source_texture_precision_fallbacks);
        json.write("\"fallback_reasons\":[");
        for (std::size_t i = 0; i < diagnostics.source_texture_fallback_reasons.size(); ++i) {
            if (i) json.write(",");
            json.value(diagnostics.source_texture_fallback_reasons[i]);
        }
        json.write("]},\n\"ui_textures\":{\n");
        json.field("images", diagnostics.ui_texture_images);
        json.field("precision_fallbacks", diagnostics.ui_texture_precision_fallbacks);
        json.write("\"fallback_reasons\":[");
        for (std::size_t i = 0; i < diagnostics.ui_texture_fallback_reasons.size(); ++i) {
            if (i) json.write(",");
            json.value(diagnostics.ui_texture_fallback_reasons[i]);
        }
        json.write("]}\n");
        json.write("}");
    } else {
        json.write("null");
    }
    json.write(",\n\"vulkan\":");
    if (diagnostics.backend == "vulkan" && !diagnostics.vulkan_json.empty()) {
        json.write(diagnostics.vulkan_json);
    } else {
        json.write("null");
    }
    json.write(",\n\"display\":{\n");
    json.field("requested_vsync", diagnostics.requested_vsync);
    json.field("applied_swap_interval", diagnostics.backend == "opengl"
               ? diagnostics.applied_swap_interval : std::optional<int>{});
    json.field("frame_limit", diagnostics.frame_limit);
    json.field("frame_limit_scope", "requested_presentation_cap_not_attained_fps");
    json.field("observed_display_id", diagnostics.observed_display_id);
    json.field("observed_refresh_hz", diagnostics.observed_refresh_hz);
    json.field("refresh_scope", "SDL_current_display_mode_at_final_window");
    json.field("vrr_status", "unknown");
    json.field("input_to_photon_status", "unmeasured", true);
    json.write("},\n\"timings\":{\n");
    json.field("clock", "SDL_GetTicksNS");
    json.field("context_scope", "final_window_context_since_last_successful_graphics_apply");
    json.field("sample_scope", "elapsed_per_draw_attempt_return_intervals_per_successful_presentation");
    json.field("retention", "latest_samples_ring");
    json.field("capacity", RuntimeTimingSamples::capacity);
    json.field("percentiles", "median_average_central_pair_p95_nearest_rank");
    json.field("measurement_scope", "wall_clock_not_pure_cpu_time_not_input_latency_not_physical_scanout");
    if (diagnostics.backend == "opengl") {
        write_runtime_timing_distribution(json, "cpu_work_ms",
            diagnostics.frame_cpu_elapsed_until_draw_return,
            "frame_loop_entry_through_render_and_capture_before_SDL_GL_SwapWindow_excludes_limiter_wait_includes_driver_waits");
        write_runtime_timing_distribution(json, "present_call_ms", diagnostics.present_call,
            "SDL_GL_SwapWindow_entry_to_return_not_physical_scanout");
    } else {
        json.write("\"cpu_work_ms\":null,\n\"present_call_ms\":null,\n");
    }
    write_runtime_timing_distribution(json, "frame_cpu_elapsed_until_draw_return_ms",
        diagnostics.frame_cpu_elapsed_until_draw_return,
        diagnostics.backend == "opengl"
            ? "frame_loop_entry_through_render_and_capture_before_swap_includes_driver_waits_excludes_limiter_wait"
            : "frame_loop_entry_through_Vulkan_draw_return_includes_driver_acquire_present_waits_excludes_limiter_wait");
    write_runtime_timing_distribution(json, "present_return_interval_ms",
        diagnostics.present_return_interval,
        diagnostics.backend == "opengl"
            ? "successive_successful_SDL_GL_SwapWindow_returns_includes_intervening_work_and_waits_not_scanout"
            : "successive_successful_Vulkan_draw_returns_CPU_present_submission_return_not_scanout",
        true);
    json.write("},\n\"sources\":{\n");
    json.field("game_dir", settings.game_dir);
    json.field("mesh", settings.mesh);
    json.field("texture", settings.texture);
    json.field("world", settings.world);
    json.field("animation", settings.animation);
    json.field("percentage", diagnostics.animation_percentage);
    json.field("requested_percentage", settings.percentage);
    json.field("percentage_supplied", settings.percentage_supplied);
    json.field("input_script", settings.input_script);
    json.field("audio", settings.audio, true);
    json.write("},\n");
    json.field("rendered_frames", diagnostics.rendered_frames);
    json.write("\"scene\":{\n");
    json.field("camera", diagnostics.camera_provenance);
    json.field("inspection_camera", diagnostics.runtime_kind == "native-inspection" ? "new_orthographic" : "not_active");
    json.field("gameplay", diagnostics.runtime_kind == "native-game" ? "source_native_port" : "not_executed");
    json.field("timing", diagnostics.runtime_kind == "native-game" ? "source_32hz_ticks_0.5_scheduler_parity_not_proven" : "inspection_presentation_only");
    json.field("animation_percentage", diagnostics.animation_percentage);
    json.field("animation_playback", diagnostics.animation_playback);
    json.field("animation_clock", diagnostics.animation_playback
               ? "recovered_donus_time16" : "explicit_frozen_percentage_or_unselected");
    json.field("material_rendering", "source_material_translation; see backend completeness observations");
    json.field("mesh_vertices", diagnostics.mesh_vertices);
    json.field("mesh_triangles", diagnostics.mesh_triangles);
    json.field("mesh_groups", diagnostics.mesh_groups);
    json.field("world_instances", diagnostics.world_instances);
    json.field("world_models", diagnostics.world_models);
    json.field("world_vertices", diagnostics.world_vertices);
    json.field("world_triangles", diagnostics.world_triangles);
    json.field("world_lightmaps", diagnostics.world_lightmaps);
    json.field("world_materials", diagnostics.world_materials);
    json.field("world_entities", diagnostics.world_entities);
    json.field("world_sprite_entities", diagnostics.world_sprite_entities);
    json.field("world_omitted_sprite_entities", diagnostics.world_omitted_sprite_entities);
    json.field("sprite_private_flags_unknown", diagnostics.sprite_private_flags_unknown);
    json.field("sprite_cached_euler_unknown", diagnostics.sprite_cached_euler_unknown);
    json.write("\"source_sprite_omissions\":{\n");
    json.field("total_omitted", diagnostics.world_omitted_sprite_entities);
    json.field("list_truncated",
               diagnostics.world_omitted_sprite_entities > diagnostics.source_sprite_omissions_size);
    json.write("\"entries\":[");
    for (std::size_t i = 0; i < diagnostics.source_sprite_omissions_size; ++i) {
        if (i) json.write(",");
        const auto& omission = diagnostics.source_sprite_omissions[i];
        json.write("{\n\"source_entity\":");
        if (omission.source_entity) json.value(*omission.source_entity);
        else json.write("null");
        json.write(",\n");
        json.field("source", omission.source);
        json.field("reason", omission.reason, true);
        json.write("}");
    }
    json.write("]\n},\n");
    json.field("world_hidden_entities", diagnostics.world_hidden_entities);
    json.field("world_unresolved_actions", diagnostics.world_unresolved_actions);
    json.field("draw_calls", diagnostics.scene_draw_calls, true);
    json.write("},\n\"audio\":{\n");
    json.field("requested", diagnostics.audio_requested);
    json.field("mode", diagnostics.audio_mode);
    json.field("device_open", diagnostics.audio_device_open);
    json.field("loop", diagnostics.audio_loop);
    json.field("playing", diagnostics.audio_playing);
    json.field("active_voices", diagnostics.audio_active_voices);
    json.field("master_volume", diagnostics.audio_master_volume);
    json.field("playing_observation", diagnostics.runtime_kind == "native-game"
        ? "active_logical_voices_not_listener_proof" : "selected_clip_voice");
    json.field("listener_proof", "not_observed");
    json.nullable_field("driver", diagnostics.audio_driver, diagnostics.audio_requested);
    json.nullable_field("device", diagnostics.audio_device, diagnostics.audio_requested);
    json.nullable_field("error", diagnostics.audio_error);
    json.field("frames", diagnostics.audio_frames);
    json.field("clip_frames", diagnostics.audio_clip_frames);
    json.field("sample_rate", diagnostics.audio_sample_rate);
    json.field("channels", diagnostics.audio_channels, true);
    json.write("},\n\"draw_calls\":{\n");
    json.field("mesh", diagnostics.mesh_draw_calls);
    json.field("world", diagnostics.world_draw_calls);
    json.field("image", diagnostics.image_draw_calls);
    json.field("material", diagnostics.material_draw_calls);
    json.field("incomplete_material", diagnostics.incomplete_material_draw_calls);
    json.field("particle", diagnostics.particle_draw_calls);
    json.field("shadow", diagnostics.shadow_draw_calls);
    json.field("pbr", diagnostics.pbr_draw_calls);
    json.field("source_bloom_passes", diagnostics.source_bloom_passes);
    json.field("gamma_passes", diagnostics.gamma_passes);
    json.field("ui", diagnostics.ui_draw_calls, true);
    json.write("}\n}\n");
    if (std::fclose(file.release()) != 0) {
        throw std::system_error(errno ? errno : EIO, std::generic_category(),
                                "cannot close runtime diagnostics: " + filename);
    }
}

} // namespace c4
