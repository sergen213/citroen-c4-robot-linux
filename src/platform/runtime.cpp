#include "platform/runtime.hpp"
#include "assets/GpuImage.hpp"
#include "core/settings.hpp"
#include "core/game_session.hpp"
#include "game/GameState.hpp"
#include "game/ui/NativeUi.hpp"
#include "rendering/opengl/inspection.hpp"
#include "rendering/opengl/runtime_diagnostics.hpp"
#include "rendering/opengl/ui.hpp"
#include "rendering/opengl/composition.hpp"
#include "rendering/quality/OpenGLQuality.hpp"
#include "rendering/SourceLighting.hpp"
#include "Audio.hpp"
#include "rendering/vulkan/VulkanScene.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace c4 {
namespace {

[[noreturn]] void sdl_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

void require_sdl(bool success, const char* operation) {
    if (!success) sdl_error(operation);
}

bool request_vsync(bool enabled) {
    if (SDL_GL_SetSwapInterval(enabled ? 1 : 0)) return true;
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Cannot request vsync %s: %s; retaining backend swap behavior",
                enabled ? "on" : "off", SDL_GetError());
    return false;
}

struct SDLSession {
    SDLSession() {
        for (const auto& hint : {std::pair{SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0"},
                                 std::pair{SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "1"}}) {
            if (!SDL_SetHintWithPriority(hint.first, hint.second, SDL_HINT_OVERRIDE))
                throw std::runtime_error(std::string("Cannot require source mouse hint ") +
                                         hint.first + "=" + hint.second + ": " + SDL_GetError());
            const char* applied = SDL_GetHint(hint.first);
            if (!applied || std::strcmp(applied, hint.second) != 0)
                throw std::runtime_error(std::string("Required source mouse hint was not honored: ") +
                                         hint.first + "=" + hint.second);
        }
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            const std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error("SDL_Init: " + error);
        }
    }
    ~SDLSession() { SDL_Quit(); }
    SDLSession(const SDLSession&) = delete;
    SDLSession& operator=(const SDLSession&) = delete;
};

struct ContextDeleter {
    void operator()(SDL_GLContext context) const { SDL_GL_DestroyContext(context); }
};
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using Context = std::unique_ptr<SDL_GLContextState, ContextDeleter>;
using Gamepad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

const char* printable(const char* text) { return text ? text : "(unavailable)"; }

// Recovered 100282d4..1002839a: binary32 K/H, extended product/sum, ftoi then clamp.
// CVTTSD2SI's invalid/overflow result is INT_MIN, not a saturating conversion.
constexpr std::int32_t source_mouse_ftoi(long double value) {
    if (!(value > -2147483649.0L && value < 2147483648.0L))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

struct MouseForceSample {
    std::int32_t raw;
    float history;
};

constexpr MouseForceSample source_mouse_force(std::int32_t dx, float history,
                                              std::int32_t previous_time_raw) {
    constexpr std::int32_t sense_raw = 1024; // Original MOUSE_SENSE default; no resource override.
    const float denominator = std::max(static_cast<float>(previous_time_raw), 50.0f);
    const float factor = static_cast<float>((sense_raw * 10.0L) / denominator);
    const long double impulse = static_cast<long double>(dx) * factor;
    return {std::clamp(source_mouse_ftoi(impulse + history), -2 * sense_raw, 2 * sense_raw),
            static_cast<float>(impulse)};
}

static_assert(std::numeric_limits<long double>::digits >= 64,
              "Source mouse arithmetic requires x87-equivalent extended precision");
static_assert(source_mouse_ftoi(-1.75L) == -1 &&
              source_mouse_ftoi(2147483648.0L) == std::numeric_limits<std::int32_t>::min());
static_assert(source_mouse_force(1, 0, 0).raw == 204);
static_assert(source_mouse_force(1, 0, 512).raw == 20);
static_assert(source_mouse_force(0, source_mouse_force(1, 0, 512).history, 512).raw == 20);
static_assert(source_mouse_force(0, source_mouse_force(0, 20, 512).history, 512).raw == 0);
static_assert(source_mouse_force(200, 0, 512).raw == 2048 &&
              source_mouse_force(200, 0, 512).history == 4000);
static_assert(source_mouse_force(0, 4000, 512).raw == 2048);
static_assert(source_mouse_force(-1, 0, 512).raw == -20);
static_assert(source_mouse_force(std::numeric_limits<std::int32_t>::max(), 0, 512).raw == -2048);

struct SourceMouse {
    double delta_x{};
    float history_x{};
    std::int32_t previous_time_raw{}, force_raw{}, mode_raw{};
    std::uint64_t sample_frames{};
    SDL_WindowID configured_window{};
    bool focused{}, relative_requested{};

    void configure(SDL_Window* window, game::Var mode) {
        const SDL_WindowID id = SDL_GetWindowID(window);
        const bool focus = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        const bool relative = !(mode.raw & 0x800) && focus;
        if (id == configured_window && mode.raw == mode_raw && focus == focused) return;
        if (SDL_GetWindowRelativeMouseMode(window) != relative) {
            require_sdl(SDL_SetWindowRelativeMouseMode(window, relative),
                        "SDL_SetWindowRelativeMouseMode for source MOUSE_MODE");
            if (SDL_GetWindowRelativeMouseMode(window) != relative)
                throw std::runtime_error("SDL did not honor source MOUSE_MODE relative capture");
        }
        configured_window = id;
        mode_raw = mode.raw;
        focused = focus;
        relative_requested = relative;
    }

    game::Var sample() {
        // SDL supplies FLOAT deltas; accumulate device counts before the SDK LONG boundary.
        const double counts = std::trunc(std::exchange(delta_x, 0.0));
        if (!std::isfinite(counts) ||
            counts < std::numeric_limits<std::int32_t>::min() ||
            counts > std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("Source relative mouse count sum exceeds DirectInput signed LONG");
        force_raw = 0;
        if (!(mode_raw & 0x800) && focused) {
            const auto force = source_mouse_force(static_cast<std::int32_t>(counts),
                                                   history_x, previous_time_raw);
            force_raw = force.raw;
            history_x = force.history;
        }
        ++sample_frames;
        return game::Var{force_raw};
    }
};

// Source keyboard publication is physical held OR pending press (10028274..100282a1).
// Pending keyboard presses survive render frames; pointer edges use source-tick samples.
struct SourceInput {
    game::InputSnapshot held;
    struct KeyboardPresses {
        bool key_cuu{}, key_cud{}, key_cul{}, key_cur{};
        bool key_d{}, key_s{}, key_space{}, key_c{};
        bool key_esc{}, key_f1{};
    } pending;
    ui::MenuInput menu;
    bool previous_hardware_pointer_down{};

    static void set_key(bool& physical, bool& pressed, bool down) {
        if (down && !physical) pressed = true;
        physical = down;
    }

    void key(SDL_Scancode code, bool down) {
        switch (code) {
        case SDL_SCANCODE_UP: set_key(held.key_cuu, pending.key_cuu, down); break;
        case SDL_SCANCODE_DOWN: set_key(held.key_cud, pending.key_cud, down); break;
        case SDL_SCANCODE_LEFT:
            set_key(held.key_cul, pending.key_cul, down);
            if (down) menu.left = true;
            break;
        case SDL_SCANCODE_RIGHT:
            set_key(held.key_cur, pending.key_cur, down);
            if (down) menu.right = true;
            break;
        case SDL_SCANCODE_D: set_key(held.key_d, pending.key_d, down); break;
        case SDL_SCANCODE_S: set_key(held.key_s, pending.key_s, down); break;
        case SDL_SCANCODE_SPACE: set_key(held.key_space, pending.key_space, down); break;
        case SDL_SCANCODE_C: set_key(held.key_c, pending.key_c, down); break;
        case SDL_SCANCODE_ESCAPE:
            if (down && !held.key_esc) held.key_esc_pressed = true;
            set_key(held.key_esc, pending.key_esc, down);
            if (down) menu.escape_pressed = true;
            else menu.escape_released = true;
            break;
        case SDL_SCANCODE_F1:
            if (down && !held.key_f1) held.key_f1_pressed = true;
            set_key(held.key_f1, pending.key_f1, down);
            break;
        case SDL_SCANCODE_TAB: if (down) menu.tab = true; break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
            if (down) menu.enter = true;
            break;
        case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
            menu.shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            break;
        default: break;
        }
    }
    game::InputSnapshot published() const {
        auto input = held;
        input.key_cuu = held.key_cuu || pending.key_cuu;
        input.key_cud = held.key_cud || pending.key_cud;
        input.key_cul = held.key_cul || pending.key_cul;
        input.key_cur = held.key_cur || pending.key_cur;
        input.key_d = held.key_d || pending.key_d;
        input.key_s = held.key_s || pending.key_s;
        input.key_space = held.key_space || pending.key_space;
        input.key_c = held.key_c || pending.key_c;
        input.key_esc = held.key_esc || pending.key_esc;
        input.key_f1 = held.key_f1 || pending.key_f1;
        return input;
    }
    void synchronize_keyboard() {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        set_key(held.key_cuu, pending.key_cuu, keys[SDL_SCANCODE_UP]);
        set_key(held.key_cud, pending.key_cud, keys[SDL_SCANCODE_DOWN]);
        set_key(held.key_cul, pending.key_cul, keys[SDL_SCANCODE_LEFT]);
        set_key(held.key_cur, pending.key_cur, keys[SDL_SCANCODE_RIGHT]);
        set_key(held.key_d, pending.key_d, keys[SDL_SCANCODE_D]);
        set_key(held.key_s, pending.key_s, keys[SDL_SCANCODE_S]);
        set_key(held.key_space, pending.key_space, keys[SDL_SCANCODE_SPACE]);
        set_key(held.key_c, pending.key_c, keys[SDL_SCANCODE_C]);
        held.key_esc_pressed |= keys[SDL_SCANCODE_ESCAPE] && !held.key_esc;
        held.key_f1_pressed |= keys[SDL_SCANCODE_F1] && !held.key_f1;
        set_key(held.key_esc, pending.key_esc, keys[SDL_SCANCODE_ESCAPE]);
        set_key(held.key_f1, pending.key_f1, keys[SDL_SCANCODE_F1]);
        menu.shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    }
    void sample_pointer(SDL_MouseButtonFlags buttons, bool focused) {
        menu.pointer_down = focused && (buttons & SDL_BUTTON_LMASK) != 0;
        held.mouse_right = focused && (buttons & SDL_BUTTON_RMASK) != 0;
        menu.pointer_pressed = menu.pointer_down && !previous_hardware_pointer_down;
        menu.pointer_released = !menu.pointer_down && previous_hardware_pointer_down;
        previous_hardware_pointer_down = menu.pointer_down;
    }
    void clear_edges() {
        pending = {};
        menu.pointer_pressed = menu.pointer_released = false;
        menu.tab = menu.enter = menu.left = menu.right = false;
        menu.escape_pressed = menu.escape_released = false;
        held.key_esc_pressed = held.key_f1_pressed = false;
        held.mouse_force_x = {};
    }
};

void check_gl(const char* operation) {
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(std::string(operation) + ": OpenGL error " +
                                 std::to_string(error));
    }
}

bool observe_opengl_mip_storage() {
    // Prior failures are not evidence that mip storage is unsupported.
    check_gl("Unexpected prior OpenGL error before mip-storage observation");
    GLint previous_binding{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_binding);
    check_gl("Read current texture-unit binding for mip-storage observation");
    bool supported{};
    {
        struct TemporaryTexture {
            GLint previous_binding;
            GLuint name{};
            ~TemporaryTexture() {
                // No active-unit changes: restore the binding on the original current unit.
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_binding));
                if (name) glDeleteTextures(1, &name);
            }
        } texture{previous_binding};
        glGenTextures(1, &texture.name);
        check_gl("Create temporary OpenGL mip-storage witness texture");
        if (!texture.name)
            throw std::runtime_error("OpenGL mip-storage observation produced no texture");
        glBindTexture(GL_TEXTURE_2D, texture.name);
        check_gl("Bind temporary OpenGL mip-storage witness texture");
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        check_gl("Allocate OpenGL RGBA8 mip-storage witness level 0");
        glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        check_gl("Allocate OpenGL RGBA8 mip-storage witness level 1");
        supported = true;
        for (GLint level = 0; level != 2; ++level) {
            GLint width{}, height{}, format{};
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &height);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_INTERNAL_FORMAT, &format);
            check_gl("Query allocated OpenGL mip-storage witness level");
            const GLint expected = level == 0 ? 2 : 1;
            supported = supported && width == expected && height == expected && format == GL_RGBA8;
        }
    }
    check_gl("Restore binding and delete OpenGL mip-storage witness texture");
    return supported;
}

void capture_ppm(const std::filesystem::path& path, int width, int height) {
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3;
    constexpr std::size_t maximum_capture_bytes = 512ULL * 1024 * 1024;
    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(height) > maximum_capture_bytes / row_bytes) {
        throw std::runtime_error("Invalid framebuffer dimensions or capture exceeds 512 MiB");
    }
    // Exclusive creation is atomic: an existing file (including a symlink) is never overwritten.
    File file(std::fopen(path.string().c_str(), "wbx"), &std::fclose);
    if (!file) {
        throw std::runtime_error("Cannot create capture " + path.string() + ": " +
                                 std::strerror(errno));
    }
    std::vector<unsigned char> pixels(row_bytes * static_cast<std::size_t>(height));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    check_gl("glReadPixels");
    if (std::fprintf(file.get(), "P6\n%d %d\n255\n", width, height) < 0) {
        throw std::runtime_error("Cannot write capture header: " + std::string(std::strerror(errno)));
    }
    for (int y = height; y-- > 0;) {
        if (std::fwrite(pixels.data() + static_cast<std::size_t>(y) * row_bytes,
                        1, row_bytes, file.get()) != row_bytes) {
            throw std::runtime_error("Cannot write capture pixels: " + std::string(std::strerror(errno)));
        }
    }
    if (std::fclose(file.release()) != 0) {
        throw std::runtime_error("Cannot finish capture: " + std::string(std::strerror(errno)));
    }
    SDL_Log("Captured actual OpenGL framebuffer: %s (%d x %d)", path.string().c_str(), width, height);
}

void validate_source_gpu_images(const rendering::RenderScene& scene, const ui::UiFrame& ui,
                                std::uint32_t maximum_dimension, bool mip_storage_witness) {
    const auto extent = [&](std::uint32_t width, std::uint32_t height,
                            std::string_view source, std::size_t index, std::size_t level) {
        if (level > 0 && !mip_storage_witness)
            throw std::runtime_error("Created graphics device mipmap support required by prepared source image " +
                std::string(source) + " [" + std::to_string(index) + "] physical mip " +
                std::to_string(level) + " is not proven");
        if (width > maximum_dimension || height > maximum_dimension)
            throw std::runtime_error("Created graphics device texture limit " +
                std::to_string(maximum_dimension) + " cannot fit immutable " + std::string(source) +
                " [" + std::to_string(index) + "] physical mip " + std::to_string(level) +
                " (" + std::to_string(width) + " x " + std::to_string(height) + ")");
    };
    const auto gpu_image = [&](const assets::GpuImage& image, std::string_view source, std::size_t index) {
        assets::validate_gpu_image(image);
        extent(image.width, image.height, source, index, 0);
        for (std::size_t level = 0; level < image.levels.size(); ++level)
            extent(image.levels[level].width, image.levels[level].height, source, index, level);
    };
    for (std::size_t index = 0; index < scene.gpu_textures.size(); ++index)
        gpu_image(scene.gpu_textures[index],
            index < scene.texture_sources.size() ? std::string_view(scene.texture_sources[index]) : "scene image", index);
    if (!ui.gpu_images.empty() && ui.gpu_images.size() != ui.images.size())
        throw std::runtime_error("Native UI GPU image slots are not aligned with source images");
    for (std::size_t index = 0; index < ui.images.size(); ++index) {
        if (!ui.gpu_images.empty() && ui.gpu_images[index]) {
            gpu_image(*ui.gpu_images[index], "UI image", index);
        } else {
            const auto& image = ui.images[index];
            extent(image.width, image.height, "live/generated UI image", index, 0);
            for (std::size_t level = 0; level < image.mip_levels.size(); ++level)
                extent(image.mip_levels[level].width, image.mip_levels[level].height,
                       "live/generated UI image", index, level + 1);
        }
    }
}

// Slots stay stable while Apply swaps their owning handles. No source borrowers move.
struct Backend {
    Window window{nullptr, &SDL_DestroyWindow};
    Context context;
    std::array<GLint, 2> maximum_viewport{};
    std::uint32_t maximum_texture_dimension{};
    bool mip_storage_witness{};
    std::unique_ptr<rendering::quality::OpenGLQuality> gl_quality;
    std::unique_ptr<InspectionGL> scene_gl;
    std::unique_ptr<InspectionScene> scene;
#if C4_ENABLE_VULKAN
    std::unique_ptr<rendering::VulkanScene> vulkan;
#endif
    std::unique_ptr<UiRenderer> overlay;
    std::unique_ptr<OriginalComposition> composition;

    bool use_vulkan{};
    std::string backend_fallback_reason;
    Backend(const Settings& settings, bool native_game, bool hidden = false,
            bool validate_requested_display = true) {
        const bool requested_vulkan = settings.renderer == "vulkan";
        if (settings.renderer != "opengl" && !requested_vulkan)
            throw std::runtime_error("Unknown graphics renderer: " + settings.renderer);
        if (settings.width < 320 || settings.width > 7680 ||
            settings.height < 240 || settings.height > 4320 || settings.frame_limit < 0 ||
            settings.frame_limit > 1000 || (!requested_vulkan && settings.rt_effects != 0) ||
            (settings.rt_effects & ~7u) != 0)
            throw std::runtime_error("Invalid native runtime display settings");
        try {
            initialize(settings, native_game, hidden, requested_vulkan);
        } catch (const rendering::VulkanUnavailableError& failure) {
            if (!requested_vulkan || settings.graphics.backend_override || settings.graphics_capabilities)
                throw;
            backend_fallback_reason = "Vulkan initialization unavailable; using OpenGL: ";
            backend_fallback_reason += failure.what();
            retire_gpu_resources();
            context.reset();
            window.reset();
            initialize(settings, native_game, hidden, false);
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", backend_fallback_reason.c_str());
        }
        if (hidden && settings.fullscreen) {
            SDL_DisplayMode mode{};
            const auto display = SDL_GetDisplayForWindow(window.get());
            if (!display) sdl_error("SDL_GetDisplayForWindow for graphics Apply");
            require_sdl(SDL_GetClosestFullscreenDisplayMode(display, settings.width, settings.height,
                        0, false, &mode), "Find fullscreen display mode for graphics Apply");
            if (mode.w != settings.width || mode.h != settings.height)
                throw std::runtime_error("Requested fullscreen resolution is unavailable on the selected display");
            require_sdl(SDL_SetWindowFullscreenMode(window.get(), &mode), "SDL_SetWindowFullscreenMode for graphics Apply");
            require_sdl(SDL_SetWindowFullscreen(window.get(), true), "SDL_SetWindowFullscreen for graphics Apply");
            require_sdl(SDL_SyncWindow(window.get()), "SDL_SyncWindow for graphics Apply");
        }
        if (!use_vulkan) {
            if (!request_vsync(settings.vsync) && hidden && validate_requested_display)
                sdl_error("SDL_GL_SetSwapInterval for graphics Apply");
        }
        if (!use_vulkan && hidden && validate_requested_display) {
            int interval{};
            require_sdl(SDL_GL_GetSwapInterval(&interval), "Read applied OpenGL swap interval");
            if ((interval != 0) != settings.vsync)
                throw std::runtime_error("Requested OpenGL vsync setting was not applied");
        }
    }

    const char* name() const noexcept { return use_vulkan ? "vulkan" : "opengl"; }

    void initialize(const Settings& settings, bool native_game, bool hidden, bool preferred_vulkan) {
        use_vulkan = preferred_vulkan;
#if !C4_ENABLE_VULKAN
        if (use_vulkan) throw rendering::VulkanUnavailableError("Vulkan renderer is unavailable in this build");
#endif
        if (!use_vulkan) {
            SDL_GL_ResetAttributes();
            require_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2), "Set OpenGL major version");
            require_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1), "Set OpenGL minor version");
            require_sdl(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "Set OpenGL double buffering");
            require_sdl(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24), "Set OpenGL depth buffer");
        }
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (use_vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL);
        if (hidden) flags |= SDL_WINDOW_HIDDEN;
        else if (settings.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
        window.reset(SDL_CreateWindow(native_game ? "Citroen C4 Robot - source-native game"
                                      : "Citroen C4 Robot - native inspection runtime",
                                      settings.width, settings.height, flags));
        if (!window) {
            if (use_vulkan)
                throw rendering::VulkanUnavailableError(std::string("SDL_CreateWindow: ") + SDL_GetError());
            sdl_error("SDL_CreateWindow");
        }
        if (!use_vulkan) {
            context.reset(SDL_GL_CreateContext(window.get()));
            if (!context) sdl_error("SDL_GL_CreateContext");
            make_current();
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maximum_viewport.data());
            check_gl("Query OpenGL viewport limits");
            GLint texture_limit{};
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
            check_gl("Query created OpenGL context texture limit");
            if (texture_limit <= 0)
                throw std::runtime_error("Created OpenGL context reported a nonpositive GL_MAX_TEXTURE_SIZE");
            maximum_texture_dimension = static_cast<std::uint32_t>(texture_limit);
            mip_storage_witness = observe_opengl_mip_storage();
            int width{}, height{};
            require_sdl(SDL_GetWindowSizeInPixels(window.get(), &width, &height), "SDL_GetWindowSizeInPixels for quality");
            if (width > maximum_viewport[0] || height > maximum_viewport[1])
                throw std::runtime_error("Window pixel dimensions exceed the OpenGL viewport limit");
            gl_quality = std::make_unique<rendering::quality::OpenGLQuality>();
            gl_quality->initialize(static_cast<unsigned>(std::max(1, width)), static_cast<unsigned>(std::max(1, height)));
            gl_quality->set_settings(settings.graphics);
        }
#if C4_ENABLE_VULKAN
        else {
            vulkan = std::make_unique<rendering::VulkanScene>(
                window.get(), settings.vsync, settings.graphics_capabilities ? 0 : settings.rt_effects,
                settings.graphics_capabilities || native_game);
            maximum_texture_dimension = vulkan->max_texture_dimension();
            if (maximum_texture_dimension == 0)
                throw std::runtime_error("Created Vulkan device reported a zero maxImageDimension2D");
            mip_storage_witness = vulkan->source_mipmaps_supported();
        }
#endif
    }

    void make_current() const {
        if (context) require_sdl(SDL_GL_MakeCurrent(window.get(), context.get()), "SDL_GL_MakeCurrent");
    }

    void initialize_scene(const Settings& settings, const rendering::RenderScene& render_scene,
                          bool native_ui = false) {
        make_current();
#if C4_ENABLE_VULKAN
        if (vulkan) {
            vulkan->prepare_scene(render_scene);
            vulkan->configure_graphics(settings.graphics);
        } else
#endif
        if (native_ui || settings.graphics_capabilities || !render_scene.instances.empty()) {
            scene_gl = std::make_unique<InspectionGL>();
            scene = std::make_unique<InspectionScene>(render_scene, *scene_gl, gl_quality.get());
            composition = std::make_unique<OriginalComposition>(*scene_gl);
            if (native_ui) overlay = std::make_unique<UiRenderer>(*scene_gl);
        }
    }

    void swap(Backend& other) noexcept {
        window.swap(other.window);
        context.swap(other.context);
        maximum_viewport.swap(other.maximum_viewport);
        std::swap(maximum_texture_dimension, other.maximum_texture_dimension);
        std::swap(mip_storage_witness, other.mip_storage_witness);
        gl_quality.swap(other.gl_quality);
        scene_gl.swap(other.scene_gl);
        scene.swap(other.scene);
#if C4_ENABLE_VULKAN
        vulkan.swap(other.vulkan);
#endif
        overlay.swap(other.overlay);
        composition.swap(other.composition);
        std::swap(use_vulkan, other.use_vulkan);
        backend_fallback_reason.swap(other.backend_fallback_reason);
    }

    void retire_gpu_resources() noexcept {
        // A candidate can be current when the previous backend is retired.
        if (context && SDL_GL_GetCurrentContext() != context.get() &&
            !SDL_GL_MakeCurrent(window.get(), context.get())) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Cannot restore owning OpenGL context for GPU teardown: %s", SDL_GetError());
            std::terminate();
        }
        composition.reset();
        overlay.reset();
#if C4_ENABLE_VULKAN
        vulkan.reset();
#endif
        scene.reset();
        scene_gl.reset();
        gl_quality.reset();
    }
    ~Backend() { retire_gpu_resources(); }
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
};

} // namespace

int run_runtime(Settings& settings) {
    const bool requested_vulkan = settings.renderer == "vulkan";
    const bool native_game = !settings.inspection_requested && !settings.graphics_capabilities;
    const std::uint64_t requested_source_frames = settings.source_frames;
    if (requested_source_frames && (!native_game || requested_source_frames > 10'000'000)) {
        throw std::runtime_error("--source-frames requires native gameplay and a count in 1..10000000");
    }
    if (native_game && settings.game_dir.empty()) {
        throw std::runtime_error("Native gameplay requires validated original --game-dir assets; use --inspect for an empty window");
    }
    if ((settings.renderer != "opengl" && !requested_vulkan) || settings.width < 320 || settings.width > 7680 ||
        settings.height < 240 || settings.height > 4320 || settings.frame_limit < 0 ||
        settings.frame_limit > 1000 || (!requested_vulkan && settings.rt_effects != 0)) {
        throw std::runtime_error("Invalid native runtime display settings");
    }
    if (!settings.capture.empty() && settings.frames == 0 && requested_source_frames == 0) {
        throw std::runtime_error("Capture requires a finite --frames or --source-frames limit");
    }
    if (!settings.audio.empty() && settings.frames == 0) {
        throw std::runtime_error("Original audio inspection requires a finite --frames limit");
    }
    const bool animation_playback = !settings.animation.empty() && !settings.percentage_supplied;
    if (animation_playback && settings.animation != "donus") {
        throw std::runtime_error("No recovered clock for selected animation; supply --percentage to freeze its original pose");
    }

    bool requested_vsync = settings.vsync;
    SDLSession session;
    if (settings.graphics_capabilities) {
        rendering::RenderScene probe_scene;
        Backend query(settings, false);
#if C4_ENABLE_VULKAN
        if (query.vulkan) {
            query.vulkan->configure_graphics(settings.graphics);
            write_graphics_capabilities(query.name(), query.vulkan->diagnostics().device_name,
                query.vulkan->graphics_capabilities(), query.vulkan->resolved_quality(),
                &query.vulkan->diagnostics(), settings.rt_effects);
        } else
#endif
        {
            // Prepare actual OpenGL pipelines and probes without loading source gameplay.
            query.initialize_scene(settings, probe_scene);
            write_graphics_capabilities(query.name(),
                printable(reinterpret_cast<const char*>(glGetString(GL_RENDERER))),
                query.gl_quality->capabilities(), query.gl_quality->resolved());
        }
        return 0;
    }

    // The finalized source scene and all its borrowers outlive every GPU backend.
    std::unique_ptr<GameSession> game;
    std::optional<rendering::RenderScene> inspection_scene;
    std::optional<Backend> owned_backend;
    owned_backend.emplace(settings, native_game);
    auto& backend = *owned_backend;
    // Native-equivalent cold bridge [INFERENCE], not an original D3D CAPS observation.
    // Keep both the prepared context and its witness immutable across Apply/F5.
    const bool source_gpu_preparation_mip_witness = backend.mip_storage_witness;
    const auto source_gpu_context = assets::cold_gpu_image_context(
        backend.maximum_texture_dimension, backend.maximum_texture_dimension,
        source_gpu_preparation_mip_witness);
    if (native_game) {
        Settings launch = settings;
        require_sdl(SDL_GetWindowSizeInPixels(backend.window.get(), &launch.width, &launch.height),
                    "SDL_GetWindowSizeInPixels for authored UI launch tier");
        game = std::make_unique<GameSession>(launch, source_gpu_context);
    } else inspection_scene.emplace(rendering::load_render_scene(settings, source_gpu_context));
    auto& render_scene = game ? game->scene() : *inspection_scene;
    backend.initialize_scene(settings, render_scene, native_game);
    auto& window = backend.window;
    auto& use_vulkan = backend.use_vulkan;
    auto& maximum_viewport = backend.maximum_viewport;
    auto& gl_quality = backend.gl_quality;
    auto& scene_gl = backend.scene_gl;
    auto& scene = backend.scene;
#if C4_ENABLE_VULKAN
    auto& vulkan = backend.vulkan;
#endif
    auto& overlay = backend.overlay;
    auto& composition = backend.composition;
    if (game) require_sdl(SDL_HideCursor(), "SDL_HideCursor for original bitmap cursor");

    const int version = SDL_GetVersion();
    SDL_Log("%s", native_game ? "Source-native game: 32 Hz source frames, 0.5 original ticks; independent presentation"
                             : "Native inspection runtime; no gameplay simulation");
    SDL_Log("SDL runtime %d.%d.%d; video driver: %s", SDL_VERSIONNUM_MAJOR(version),
            SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
            printable(SDL_GetCurrentVideoDriver()));
    if (!use_vulkan) {
        SDL_Log("OpenGL vendor: %s; renderer: %s; version: %s",
                printable(reinterpret_cast<const char*>(glGetString(GL_VENDOR))),
                printable(reinterpret_cast<const char*>(glGetString(GL_RENDERER))),
                printable(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    }
    SDL_Log("%s", native_game ? "F11 fullscreen; V vsync; Escape source pause/menu; F1 source help; F5 rebuild display/backend (preserves game session, including finite --frames)"
                             : "F11 fullscreen; V vsync; Escape quit; F5 relaunch inspection (ignored for finite --frames)");

    std::unordered_map<SDL_JoystickID, Gamepad> gamepads;
    const auto configure_graphics = [&](std::string_view apply_error = {}) {
        if (!game) return;
        Settings active = settings;
        require_sdl(SDL_GetWindowSize(window.get(), &active.width, &active.height), "Read active graphics resolution");
        active.fullscreen = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
#if C4_ENABLE_VULKAN
        if (vulkan) {
            const auto& diagnostics = vulkan->diagnostics();
            auto effective = vulkan->resolved_quality();
            effective.effective_preset = rendering::quality::effective_profile(
                effective, rendering::quality::RendererBackend::Vulkan, diagnostics.rt_enabled);
            std::string reason = diagnostics.quality_fallback_reason;
            if (!diagnostics.rt_fallback_reason.empty()) {
                if (!reason.empty()) reason += "; ";
                reason += diagnostics.rt_fallback_reason;
            }
            game->ui().configure_graphics(active, vulkan->graphics_capabilities(), effective,
                vulkan->ray_tracing_capabilities(), diagnostics.rt_enabled, backend.name(), reason, apply_error,
                diagnostics.present_mode == "fifo");
        } else
#endif
        {
            int interval{};
            std::optional<bool> observed_vsync;
            if (SDL_GL_GetSwapInterval(&interval)) observed_vsync = interval != 0;
            auto effective = gl_quality->resolved();
            effective.effective_preset = rendering::quality::effective_profile(
                effective, rendering::quality::RendererBackend::OpenGL, 0);
            std::string reason = backend.backend_fallback_reason;
            if (!gl_quality->diagnostics().fallback_reason.empty()) {
                if (!reason.empty()) reason += "; ";
                reason += gl_quality->diagnostics().fallback_reason;
            }
            game->ui().configure_graphics(active, gl_quality->capabilities(), effective,
                0, 0, backend.name(), reason, apply_error, observed_vsync);
        }
    };
    configure_graphics();
    rendering::NativeGameVisualState inspection_globals;
    rendering::SourceLightingState inspection_lighting;
    rendering::FrameSnapshot snapshot{render_scene, render_scene.instances, {}};
    if (!game) SDL_Log("Camera is explicit orthographic asset-inspection framing");
    SDL_Log("Recovered snapshot: %llu vertices; %llu triangles; %llu world instances; %llu world models",
            static_cast<unsigned long long>(render_scene.counts.mesh_vertices),
            static_cast<unsigned long long>(render_scene.counts.mesh_triangles),
            static_cast<unsigned long long>(render_scene.counts.world_instances),
            static_cast<unsigned long long>(render_scene.counts.world_models));
    if (render_scene.counts.omitted_sprite_entities) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%llu original sprite entities retained as snapshots, not drawn: original orientation not recovered",
                    static_cast<unsigned long long>(render_scene.counts.omitted_sprite_entities));
    }
    if (render_scene.counts.unresolved_actions) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%llu original dynamic/conditional world actions not executed",
                    static_cast<unsigned long long>(render_scene.counts.unresolved_actions));
    }
    if (game && game->audio().mode() == audio::Mode::Device) {
        const auto driver = game->audio().driver();
        if (driver == "dummy" || driver == "disk")
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Explicit non-audible native-game audio backend; not listener proof");
    }
    std::optional<audio::Audio> sound;
    std::shared_ptr<const audio::Clip> audio_clip;
    audio::Handle audio_handle = 0;
    std::string audio_driver, audio_device;
    if (!settings.audio.empty()) {
        audio_clip = audio::load_clip(settings.audio);
        sound.emplace();
        audio_handle = sound->play(audio_clip, 100, 0, settings.audio_loop);
        if (!audio_handle) throw std::runtime_error("Native audio could not allocate selected clip voice");
        audio_driver = sound->driver();
        audio_device = sound->device();
        SDL_Log("Playing original audio: %s; driver: %s; device: %s; loop: %s",
                settings.audio.string().c_str(), audio_driver.c_str(), audio_device.c_str(),
                settings.audio_loop ? "yes" : "no");
        if (audio_driver == "dummy" || audio_driver == "disk") {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Explicit non-audible audio backend; not listener proof");
        }
    }
    SDL_WindowID window_id = SDL_GetWindowID(window.get());
    SDL_WindowID retired_window_id{};
    bool quit = false;
    int result = 0;
    bool source_target_reached = false;
    bool source_target_presented = false;
    bool source_frame_frozen = false;
    bool source_quit = false;
    bool viewport_dirty = true;
    bool display_rebuild_pending = false;
    int pixel_width = 0, pixel_height = 0;
    std::uint64_t rendered_frames = 0;
    RuntimeTimingSamples frame_cpu_elapsed_until_draw_return, present_call, present_return_interval;
    std::optional<Uint64> previous_present_return;
    Uint64 frame_cpu_start{};
    SourceInput source_input;
    SourceMouse source_mouse;
    const auto configure_source_mouse = [&] {
        if (game) source_mouse.configure(window.get(), game->source_mouse_mode());
    };
    configure_source_mouse();
    constexpr Uint64 source_interval = 1'000'000'000ULL / 32;
    const Uint64 source_epoch = SDL_GetTicksNS();
    Uint64 next_source_tick = source_epoch;
    Uint64 frame_interval = settings.frame_limit > 0 ?
        1'000'000'000ULL / static_cast<Uint64>(settings.frame_limit) : 0;
#if C4_ENABLE_VULKAN
    const std::filesystem::path no_capture;
#endif
    float animation_percentage = settings.percentage;
    float sampled_percentage = settings.percentage;
    Uint64 previous_animation_sample = 0;
    if (animation_playback) {
        SDL_Log("donus uses recovered source recurrence and 1/16-second ticks with measured native elapsed time; original VM scheduler not reconstructed");
    }
    const auto apply_graphics = [&] {
        if (!game) return;
        const auto request = game->ui().take_graphics_request();
        const bool rebuild = display_rebuild_pending;
        display_rebuild_pending = false;
        if (!request && !rebuild) return;
        if (request && render_scene.world &&
            source_profile_requires_restart(settings, *request, render_scene.world->source_profile)) {
            configure_graphics("Changing original source compile selectors requires a new GameSession; graphics Apply preserves the loaded world");
            return;
        }
        Settings next = settings;
        if (request) {
            next.renderer = request->renderer;
            next.rt_effects = request->rt_effects;
            next.graphics = request->graphics;
            next.source_profile = request->source_profile;
            next.source_cgm_explicit = request->source_cgm_explicit;
            next.source_stq_explicit = request->source_stq_explicit;
            next.width = request->width;
            next.height = request->height;
            next.fullscreen = request->fullscreen;
            next.vsync = request->vsync;
            next.frame_limit = request->frame_limit;
        }
        std::optional<Backend> candidate;
        bool swapped = false;
        try {
            candidate.emplace(next, true, true, request && next.vsync != settings.vsync);
            validate_source_gpu_images(render_scene, game->ui().frame(),
                candidate->maximum_texture_dimension, candidate->mip_storage_witness);
            require_sdl(SDL_ShowWindow(candidate->window.get()), "SDL_ShowWindow for graphics Apply");
            require_sdl(SDL_SyncWindow(candidate->window.get()), "SDL_SyncWindow for graphics Apply");
            require_sdl(SDL_RaiseWindow(candidate->window.get()), "SDL_RaiseWindow for graphics Apply");
            candidate->initialize_scene(next, render_scene, true);
            if (candidate->overlay) candidate->overlay->prepare(game->ui().frame());
#if C4_ENABLE_VULKAN
            if (request && next.graphics.backend_override && candidate->vulkan &&
                (candidate->vulkan->diagnostics().rt_supported_effects & next.rt_effects) != next.rt_effects) {
                const auto& reason = candidate->vulkan->diagnostics().rt_fallback_reason;
                throw std::runtime_error(reason.empty() ? "Requested ray-effect pipelines are unavailable" : reason);
            }
            if (request && next.vsync != settings.vsync && candidate->vulkan && !next.vsync &&
                candidate->vulkan->diagnostics().present_mode == "fifo")
                throw std::runtime_error("Vulkan surface supports only synchronized FIFO presentation; vsync-off was not applied");
#endif
            int width{}, height{}, pixel_width{}, pixel_height{};
            require_sdl(SDL_GetWindowSize(candidate->window.get(), &width, &height), "Read applied window resolution");
            require_sdl(SDL_GetWindowSizeInPixels(candidate->window.get(), &pixel_width, &pixel_height),
                        "Read applied drawable resolution");
            if (width != next.width || height != next.height)
                throw std::runtime_error("Requested resolution was not applied; actual window is " +
                                         std::to_string(width) + " x " + std::to_string(height));
            if (pixel_width <= 0 || pixel_height <= 0)
                throw std::runtime_error("Applied graphics window has no drawable framebuffer");
            if (candidate->gl_quality &&
                (candidate->gl_quality->diagnostics().width != static_cast<unsigned>(pixel_width) ||
                 candidate->gl_quality->diagnostics().height != static_cast<unsigned>(pixel_height)))
                candidate->gl_quality->resize(static_cast<unsigned>(pixel_width), static_cast<unsigned>(pixel_height));
            require_sdl(SDL_HideWindow(window.get()), "SDL_HideWindow previous graphics backend");
            backend.make_current();
            candidate->make_current();
            backend.swap(*candidate);
            swapped = true;
            save_settings(config_path(), next);
        } catch (const std::exception& failure) {
            const std::string error = failure.what();
            if (swapped) backend.swap(*candidate);
            candidate.reset(); // Failed GL resources are deleted with their own context current.
            backend.make_current();
            require_sdl(SDL_SetWindowFullscreen(window.get(), settings.fullscreen), "Restore fullscreen after graphics Apply failure");
            require_sdl(SDL_ShowWindow(window.get()), "Restore window after graphics Apply failure");
            require_sdl(SDL_SyncWindow(window.get()), "Synchronize restored graphics window");
            require_sdl(SDL_RaiseWindow(window.get()), "Restore focus after graphics Apply failure");
            viewport_dirty = true;
            configure_graphics(error);
            return;
        }
        settings = std::move(next);
        candidate.reset(); // Retire the old GPU resources before destroying their old GL context.
        backend.make_current();
        retired_window_id = window_id;
        window_id = SDL_GetWindowID(window.get());
        SDL_PumpEvents();
        source_input.synchronize_keyboard();
        configure_source_mouse();
        requested_vsync = settings.vsync;
        frame_interval = settings.frame_limit > 0
            ? 1'000'000'000ULL / static_cast<Uint64>(settings.frame_limit) : 0;
        viewport_dirty = true;
        int width{}, height{};
        require_sdl(SDL_GetWindowSizeInPixels(window.get(), &width, &height), "Read applied source viewport");
        if (width > 0 && height > 0) game->set_viewport(width, height, settings.fullscreen);
        configure_graphics();
        // A new window/backend is a new measurement context, not a mixed-backend sample.
        frame_cpu_elapsed_until_draw_return = {};
        present_call = {};
        present_return_interval = {};
        previous_present_return.reset();
        frame_cpu_start = SDL_GetTicksNS();
    };


    const auto handle_event = [&](const SDL_Event& event) {
        // After the final tick, only window lifecycle events may affect presentation.
        // Source/UI state and the captured packet must remain unchanged on retries.
        if (source_target_reached && event.type != SDL_EVENT_QUIT &&
            event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.type != SDL_EVENT_WINDOW_RESIZED &&
            event.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
            event.type != SDL_EVENT_WINDOW_RESTORED &&
            event.type != SDL_EVENT_WINDOW_SHOWN &&
            event.type != SDL_EVENT_WINDOW_ENTER_FULLSCREEN &&
            event.type != SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            return;
        switch (event.type) {
        case SDL_EVENT_QUIT:
            quit = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == window_id) quit = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (event.window.windowID == window_id) {
                const bool escape = source_input.held.key_esc_pressed;
                const bool help = source_input.held.key_f1_pressed;
                source_input.held = {};
                source_input.menu = {};
                source_mouse.delta_x = 0;
                source_input.held.key_esc_pressed = escape;
                source_input.held.key_f1_pressed = help;
                configure_source_mouse();
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (event.window.windowID == window_id) configure_source_mouse();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (game && event.motion.windowID == window_id) {
                if (source_mouse.relative_requested && source_mouse.focused &&
                    event.motion.which != SDL_TOUCH_MOUSEID && event.motion.which != SDL_PEN_MOUSEID &&
                    SDL_GetWindowRelativeMouseMode(window.get())) {
                    source_mouse.delta_x += event.motion.xrel;
                } else if (!SDL_GetWindowRelativeMouseMode(window.get())) {
                    int width{}, height{};
                    require_sdl(SDL_GetWindowSize(window.get(), &width, &height), "SDL_GetWindowSize for source pointer");
                    const auto& canvas = game->ui().frame();
                    source_input.menu.pointer_inside = ui::Viewport::fit(width, height,
                        canvas.canvas_width, canvas.canvas_height).map(
                        event.motion.x, event.motion.y, source_input.menu.x, source_input.menu.y);
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (game && event.button.windowID == window_id) {
                if (!SDL_GetWindowRelativeMouseMode(window.get())) {
                    int width{}, height{};
                    require_sdl(SDL_GetWindowSize(window.get(), &width, &height), "SDL_GetWindowSize for source pointer click");
                    const auto& canvas = game->ui().frame();
                    source_input.menu.pointer_inside = ui::Viewport::fit(width, height,
                        canvas.canvas_width, canvas.canvas_height).map(
                        event.button.x, event.button.y, source_input.menu.x, source_input.menu.y);
                }
                if (event.button.button == SDL_BUTTON_RIGHT) source_input.held.mouse_right = event.button.down;
                if (event.button.button == SDL_BUTTON_LEFT)
                    source_input.menu.pointer_down = event.button.down;
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (event.window.windowID == window_id) {
                if (!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN)) {
                    settings.width = std::clamp(event.window.data1, 320, 7680);
                    settings.height = std::clamp(event.window.data2, 240, 4320);
                }
                viewport_dirty = true;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            if (event.window.windowID == window_id) {
                viewport_dirty = true;
                settings.fullscreen = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if ((event.key.windowID != window_id &&
                 (!retired_window_id || event.key.windowID != retired_window_id)) ||
                event.key.repeat) break;
            SDL_Log("Keyboard %s: %s (scancode %d)", event.key.down ? "down" : "up",
                    SDL_GetKeyName(event.key.key), static_cast<int>(event.key.scancode));
            if (game) source_input.key(event.key.scancode, event.key.down);
            if (!event.key.down) break;
            switch (event.key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                if (!game) quit = true;
                break;
            case SDL_SCANCODE_F5:
                if (game) display_rebuild_pending = true;
                else if (settings.frames == 0) { result = 75; quit = true; }
                else SDL_Log("Inspection restart ignored during finite presented-frame run");
                break;
            case SDL_SCANCODE_F11:
                require_sdl(SDL_SetWindowFullscreen(window.get(), !settings.fullscreen),
                            "SDL_SetWindowFullscreen");
                settings.fullscreen = !settings.fullscreen;
                viewport_dirty = true;
                configure_graphics();
                break;
            case SDL_SCANCODE_V:
                if (
#if C4_ENABLE_VULKAN
                    use_vulkan ? vulkan->set_vsync(!settings.vsync) :
#endif
                    request_vsync(!settings.vsync)) {
                    settings.vsync = !settings.vsync;
                    SDL_Log("Vsync %s", settings.vsync ? "on" : "off");
                    requested_vsync = settings.vsync;
                    configure_graphics();
                }
                break;
            default:
                break;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED: {
            const auto id = event.gdevice.which;
            if (gamepads.contains(id)) break;
            Gamepad gamepad(SDL_OpenGamepad(id), &SDL_CloseGamepad);
            if (!gamepad) {
                // Hot-plugged pads can vanish or lack a mapping before open; never end the game for it.
                SDL_Log("Gamepad %u ignored: SDL_OpenGamepad: %s", static_cast<unsigned>(id), SDL_GetError());
                break;
            }
            SDL_Log("Gamepad added: %u %s", static_cast<unsigned>(id),
                    printable(SDL_GetGamepadName(gamepad.get())));
            gamepads.emplace(id, std::move(gamepad));
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED:
            SDL_Log("Gamepad removed: %u", static_cast<unsigned>(event.gdevice.which));
            gamepads.erase(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            SDL_Log("Gamepad %u button %s: %s", static_cast<unsigned>(event.gbutton.which),
                    event.gbutton.down ? "down" : "up",
                    printable(SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(event.gbutton.button))));
            break;
        default:
            break;
        }
    };

    const auto advance_game = [&](Uint64 now) {
        if (!game || source_target_reached) return;
        int width{}, height{};
        require_sdl(SDL_GetWindowSizeInPixels(window.get(), &width, &height), "SDL_GetWindowSizeInPixels for source camera");
        if (width > 0 && height > 0)
            game->set_viewport(width, height, (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0);
        while (!quit && next_source_tick <= now) {
            SDL_Event source_event;
            while (SDL_PollEvent(&source_event)) handle_event(source_event);
            if (quit) break;
            configure_source_mouse();
            source_input.held.mouse_force_x = source_mouse.sample();
            source_input.sample_pointer(SDL_GetMouseState(nullptr, nullptr), source_mouse.focused);
            game->update(source_input.published(), source_input.menu,
                         static_cast<double>(next_source_tick - source_epoch) / 1'000'000'000.0);
            source_target_reached = requested_source_frames != 0 &&
                                    game->source_frames() == requested_source_frames;
            // Original input poll precedes timing publication; render FPS never enters K.
            source_mouse.previous_time_raw = game::FrameTiming{}.originaltime_step_ticks.raw;
            source_input.clear_edges();
            if (source_target_reached) break;
            next_source_tick += source_interval;
            apply_graphics();
            configure_source_mouse();
            if (game->finished()) { source_quit = true; quit = true; }
        }
        const auto error = game->audio().error();
        if (!error.empty()) throw std::runtime_error("Native game audio stream: " + error);
    };

    while (!quit && (settings.frames == 0 || rendered_frames < settings.frames)) {
        // Keep the existing presentation deadline/source scheduler unchanged.
        // This wall interval includes input/source/driver work, not limiter waiting.
        frame_cpu_start = SDL_GetTicksNS();
        configure_source_mouse();
        SDL_Event event;
        while (SDL_PollEvent(&event)) handle_event(event);
        if (retired_window_id) {
            source_input.synchronize_keyboard();
            retired_window_id = 0;
        }
        advance_game(SDL_GetTicksNS());
        if (quit) break;
        const auto state = SDL_GetWindowFlags(window.get());
        if (state & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) {
            const auto now = SDL_GetTicksNS();
            const auto timeout = game && next_source_tick > now
                ? static_cast<Sint32>((next_source_tick - now + 999'999) / 1'000'000) : 100;
            if (SDL_WaitEventTimeout(&event, std::max<Sint32>(1, timeout))) handle_event(event);
            continue;
        }
        if (viewport_dirty) {
            require_sdl(SDL_GetWindowSizeInPixels(window.get(), &pixel_width, &pixel_height),
                        "SDL_GetWindowSizeInPixels");
            if (!use_vulkan) {
                if (pixel_width > maximum_viewport[0] || pixel_height > maximum_viewport[1]) {
                    throw std::runtime_error("Window pixel dimensions exceed the OpenGL viewport limit");
                }
                glViewport(0, 0, pixel_width, pixel_height);
            }
            if (gl_quality && pixel_width > 0 && pixel_height > 0 &&
                (gl_quality->diagnostics().width != static_cast<unsigned>(pixel_width) ||
                 gl_quality->diagnostics().height != static_cast<unsigned>(pixel_height)))
                gl_quality->resize(static_cast<unsigned>(pixel_width), static_cast<unsigned>(pixel_height));
            if (pixel_width > 0 && pixel_height > 0) {
                if (!game) {
                    snapshot.camera = rendering::inspection_camera(render_scene, pixel_width, pixel_height);
                    if (render_scene.world) {
                        inspection_globals = rendering::source_inspection_visual_state(*render_scene.world, snapshot.camera);
                        inspection_lighting = rendering::source_lighting_state(*render_scene.world, inspection_globals);
                        snapshot.visuals = &inspection_globals;
                        snapshot.source_lighting = &inspection_lighting;
                    }
                }
#if C4_ENABLE_VULKAN
                if (vulkan) vulkan->resize();
#endif
            }
            viewport_dirty = false;
        }
        if (pixel_width <= 0 || pixel_height <= 0) {
            if (SDL_WaitEventTimeout(&event, 100)) handle_event(event);
            continue;
        }
        const Uint64 start = SDL_GetTicksNS();
        if (game && !source_frame_frozen) {
            const auto source_frame = game->frame(pixel_width, pixel_height);
            snapshot.instances = source_frame.instances;
            snapshot.camera = source_frame.camera;
            snapshot.overlay = source_frame.overlay;
            snapshot.visuals = source_frame.visuals;
            snapshot.bloom = source_frame.bloom;
            snapshot.effects = source_frame.effects;
            snapshot.gamma = source_frame.gamma;
            snapshot.source_lighting = source_frame.source_lighting;
            snapshot.source_effects_enabled = source_frame.source_effects_enabled;
            snapshot.sky = source_frame.sky;
            snapshot.history_epoch = source_frame.history_epoch;
            source_frame_frozen = source_target_reached;
        }
        if (animation_playback && animation_percentage <= 100.0f) {
            if (previous_animation_sample != 0) {
                const float ticks = static_cast<float>(static_cast<double>(start - previous_animation_sample) * (16.0 / 1'000'000'000.0));
                animation_percentage = assets::advance_donus_percentage(animation_percentage, ticks);
            }
            previous_animation_sample = start;
            // Original donus exits on >100 without forcing a final 100-percent pose.
            if (animation_percentage <= 100.0f && animation_percentage != sampled_percentage) {
                rendering::sample_selected_animation(render_scene, animation_percentage);
                sampled_percentage = animation_percentage;
            }
        }
        bool presented = false;
        Uint64 present_return{};
        if (sound) {
            const auto error = sound->error();
            if (!error.empty()) throw std::runtime_error("Native audio stream: " + error);
        }
        const bool capture = !settings.capture.empty() &&
            (requested_source_frames ? source_target_reached
                                     : rendered_frames == settings.frames - 1);
#if C4_ENABLE_VULKAN
        if (use_vulkan) {
            presented = vulkan->draw(snapshot, capture ? settings.capture : no_capture);
            present_return = SDL_GetTicksNS();
            // draw contains Vulkan acquire/render/present: no separate present duration
            // or pure CPU-work duration is observable at this API boundary.
            frame_cpu_elapsed_until_draw_return.observe(present_return - frame_cpu_start);
        } else
#endif
        {
            if (composition) composition->begin(pixel_width, pixel_height, snapshot.bloom, snapshot.gamma);
            if (snapshot.overlay && !snapshot.overlay->draw_world) glClearColor(0, 0, 0, 1);
            else glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (scene) scene->draw(snapshot);
            if (composition) composition->compose_world();
            if (snapshot.overlay) {
                if (!scene_gl) scene_gl = std::make_unique<InspectionGL>();
                if (!overlay) overlay = std::make_unique<UiRenderer>(*scene_gl);
                overlay->draw(*snapshot.overlay, pixel_width, pixel_height);
            }
            if (composition) composition->finish();
            check_gl("Render inspection framebuffer");
            if (capture) {
                capture_ppm(settings.capture, pixel_width, pixel_height);
            }
            const Uint64 swap_start = SDL_GetTicksNS();
            const bool swap_succeeded = SDL_GL_SwapWindow(window.get());
            present_return = SDL_GetTicksNS();
            frame_cpu_elapsed_until_draw_return.observe(swap_start - frame_cpu_start);
            present_call.observe(present_return - swap_start);
            require_sdl(swap_succeeded, "SDL_GL_SwapWindow");
            presented = true;
        }
        if (presented) {
            if (previous_present_return)
                present_return_interval.observe(present_return - *previous_present_return);
            previous_present_return = present_return;
        }
        if (presented) ++rendered_frames;
        else viewport_dirty = true;
        if (presented && source_target_reached) {
            source_target_presented = true;
            break;
        }
        if (settings.frames != 0 && rendered_frames == settings.frames) break;
        // This deadline limits presentation only; it is not a recovered game timestep.
        while (!quit && frame_interval > 0) {
            const Uint64 elapsed = SDL_GetTicksNS() - start;
            if (elapsed >= frame_interval) break;
            const Uint64 remaining = frame_interval - elapsed;
            Uint64 wait_ns = std::min<Uint64>(100'000'000, remaining);
            if (game) {
                const auto now = SDL_GetTicksNS();
                advance_game(now);
                if (quit || source_target_reached) break;
                wait_ns = std::min(wait_ns, next_source_tick > now ? next_source_tick - now : 0);
            }
            const Sint32 timeout_ms = static_cast<Sint32>((wait_ns + 999'999) / 1'000'000);
            if (SDL_WaitEventTimeout(&event, std::max<Sint32>(1, timeout_ms))) {
                handle_event(event);
                while (SDL_PollEvent(&event)) handle_event(event);
            }
        }
    }
    const std::uint64_t completed_source_frames = game ? game->source_frames() : 0;
    std::string_view finite_stop_failure_reason;
    if (requested_source_frames && !source_target_presented) {
        result = 1;
        if (source_target_reached)
            finite_stop_failure_reason = "quit_before_final_source_presentation";
        else if (source_quit)
            finite_stop_failure_reason = "source_quit_before_source_target";
        else if (quit)
            finite_stop_failure_reason = "window_quit_before_source_target";
        else
            finite_stop_failure_reason = "present_budget_before_source_target";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Finite source stop failed: %s; requested %llu completed %llu; target capture omitted",
            finite_stop_failure_reason.data(),
            static_cast<unsigned long long>(requested_source_frames),
            static_cast<unsigned long long>(completed_source_frames));
    }
    SDL_Log("Rendered frames: %llu", static_cast<unsigned long long>(rendered_frames));
    if (!settings.diagnostics_json.empty()) {
        RuntimeDiagnostics diagnostics;
        diagnostics.requested_source_frames = requested_source_frames;
        diagnostics.target_reached = source_target_presented;
        diagnostics.finite_stop_failure_reason = finite_stop_failure_reason;
        diagnostics.source_gpu_backend_limit = backend.maximum_texture_dimension;
        diagnostics.source_gpu_preparation_limit = source_gpu_context.maximum_dimension;
        diagnostics.source_gpu_backend_mip_witness = backend.mip_storage_witness;
        diagnostics.source_gpu_preparation_mip_witness = source_gpu_preparation_mip_witness;
        diagnostics.source_gpu_cold_limit_policy = "nonzero_feature_word_cold_limit";
        diagnostics.source_gpu_cold_limit_proof = "native_equivalent_inference";
        require_sdl(SDL_GetWindowSize(window.get(), &diagnostics.logical_width, &diagnostics.logical_height),
                    "SDL_GetWindowSize for diagnostics");
        require_sdl(SDL_GetWindowSizeInPixels(window.get(), &diagnostics.drawable_width, &diagnostics.drawable_height),
                    "SDL_GetWindowSizeInPixels for diagnostics");
        diagnostics.fullscreen = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
        const auto display = SDL_GetDisplayForWindow(window.get());
        if (display) {
            diagnostics.observed_display_id = display;
            if (const auto* mode = SDL_GetCurrentDisplayMode(display);
                mode && std::isfinite(mode->refresh_rate) && mode->refresh_rate > 0)
                diagnostics.observed_refresh_hz = mode->refresh_rate;
        }
        diagnostics.frame_cpu_elapsed_until_draw_return = frame_cpu_elapsed_until_draw_return;
        diagnostics.present_call = present_call;
        diagnostics.present_return_interval = present_return_interval;
        diagnostics.history_epoch = snapshot.history_epoch;
        diagnostics.backend = backend.name();
        diagnostics.requested_backend = settings.renderer;
        diagnostics.backend_fallback_reason = backend.backend_fallback_reason;
        diagnostics.runtime_kind = game ? "native-game" : "native-inspection";
        diagnostics.camera_provenance = std::string(snapshot.camera.provenance);
        if (game) {
            diagnostics.game_diagnostics = game->diagnostics();
            diagnostics.source_frames = completed_source_frames;
            diagnostics.input_scenario_finished = game->input_scenario_finished();
            diagnostics.source_mouse_force_raw = source_mouse.force_raw;
            diagnostics.source_mouse_history_x = source_mouse.history_x;
            diagnostics.source_mouse_previous_time_frame_raw = source_mouse.previous_time_raw;
            diagnostics.source_mouse_relative_requested = source_mouse.relative_requested;
            diagnostics.source_mouse_focused = source_mouse.focused;
            diagnostics.source_mouse_mode_raw = source_mouse.mode_raw;
            diagnostics.source_mouse_sample_frames = source_mouse.sample_frames;
            diagnostics.source_mouse_backend_parity = "not_proven";
        }
#if C4_ENABLE_VULKAN
        if (vulkan) {
            diagnostics.quality_capabilities = rendering::quality::capabilities_json(vulkan->graphics_capabilities());
            auto resolved = vulkan->resolved_quality();
            resolved.effective_preset = rendering::quality::effective_profile(
                resolved, rendering::quality::RendererBackend::Vulkan, vulkan->diagnostics().rt_enabled);
            diagnostics.resolved_quality = rendering::quality::resolved_graphics_json(resolved);
        } else
#endif
        if (gl_quality) {
            diagnostics.quality_capabilities = rendering::quality::capabilities_json(gl_quality->capabilities());
            auto resolved = gl_quality->resolved();
            resolved.effective_preset = rendering::quality::effective_profile(
                resolved, rendering::quality::RendererBackend::OpenGL, 0);
            diagnostics.resolved_quality = rendering::quality::resolved_graphics_json(resolved);
        }
        diagnostics.requested_vsync = requested_vsync;
        if (!use_vulkan) {
            const auto gl_string = [](GLenum value) {
                const auto text = glGetString(value);
                return text ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
            };
            diagnostics.vendor = gl_string(GL_VENDOR);
            diagnostics.renderer = gl_string(GL_RENDERER);
            diagnostics.version = gl_string(GL_VERSION);
            check_gl("Read diagnostic OpenGL strings");
            int swap_interval = 0;
            if (SDL_GL_GetSwapInterval(&swap_interval)) diagnostics.applied_swap_interval = swap_interval;
            else SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Applied swap interval unavailable: %s", SDL_GetError());
        }
        diagnostics.frame_limit = settings.frame_limit;
        diagnostics.rendered_frames = rendered_frames;
        diagnostics.animation_percentage = sampled_percentage;
        diagnostics.animation_playback = animation_playback;
        const auto submitted = rendering::submitted_scene_counts(snapshot);
        diagnostics.mesh_vertices = submitted.mesh_vertices;
        diagnostics.mesh_triangles = submitted.mesh_triangles;
        diagnostics.mesh_groups = submitted.groups;
        diagnostics.world_models = submitted.world_models;
        diagnostics.world_instances = submitted.world_instances;
        diagnostics.world_entities = submitted.world_entities;
        diagnostics.world_materials = submitted.materials;
        diagnostics.world_lightmaps = submitted.lightmaps;
        diagnostics.world_sprite_entities = submitted.sprite_entities;
        diagnostics.world_omitted_sprite_entities = submitted.omitted_sprite_entities;
        diagnostics.sprite_private_flags_unknown = submitted.sprite_private_flags_unknown;
        diagnostics.sprite_cached_euler_unknown = submitted.sprite_cached_euler_unknown;
        if (submitted.omitted_sprite_entities) {
            for (const auto& instance : snapshot.instances) {
                if (!instance.source_sprite || instance.source_sprite_state_known
                        || instance.native_prepared_inactive || (instance.flags & (1u << 8))
                        || instance.source_sprite_unavailable == rendering::SourceSpriteUnavailable::None)
                    continue;
                if (diagnostics.source_sprite_omissions_size == diagnostics.source_sprite_omissions.size())
                    break;
                diagnostics.source_sprite_omissions[diagnostics.source_sprite_omissions_size++] = {
                    instance.source_entity, instance.source,
                    rendering::source_sprite_unavailable_reason(instance)};
            }
        }
        diagnostics.world_hidden_entities = submitted.hidden_entities;
        diagnostics.world_unresolved_actions = submitted.unresolved_actions;
        diagnostics.world_vertices = submitted.world_vertices;
        diagnostics.world_triangles = submitted.world_triangles;
        if (scene) {
            scene->diagnostics(diagnostics);
            diagnostics.source_ano_raw = scene->source_anisotropy_raw();
            diagnostics.source_sampler_anisotropy_level = scene->sampler_anisotropy_level();
            diagnostics.source_sampler_anisotropy_maximum = scene->sampler_anisotropy_maximum();
            diagnostics.source_sampler_anisotropy_supported = scene->sampler_anisotropy_supported();
            diagnostics.source_sampler_anisotropy_fallback_reason = scene->sampler_anisotropy_fallback_reason();
            diagnostics.source_texture_images = scene->source_gpu_texture_count();
            diagnostics.source_texture_precision_fallbacks = scene->source_gpu_precision_fallback_count();
            const auto reasons = scene->source_gpu_fallback_reasons();
            diagnostics.source_texture_fallback_reasons.assign(reasons.begin(), reasons.end());
        }
        diagnostics.scene_draw_calls = diagnostics.draw_calls;
        if (composition) {
            diagnostics.source_bloom_passes = composition->source_bloom_passes();
            diagnostics.gamma_passes = composition->gamma_passes();
        }
        if (overlay) {
            diagnostics.ui_draw_calls = overlay->draw_calls();
            diagnostics.draw_calls += diagnostics.ui_draw_calls;
            diagnostics.ui_texture_images = overlay->source_gpu_texture_count();
            diagnostics.ui_texture_precision_fallbacks = overlay->source_gpu_precision_fallback_count();
            const auto reasons = overlay->source_gpu_fallback_reasons();
            diagnostics.ui_texture_fallback_reasons.assign(reasons.begin(), reasons.end());
        }
#if C4_ENABLE_VULKAN
        if (vulkan) {
            diagnostics.vulkan = &vulkan->diagnostics();
            diagnostics.vulkan_json = vulkan->diagnostics_json();
            diagnostics.draw_calls = vulkan->draw_calls();
            diagnostics.world_draw_calls = diagnostics.vulkan->world_draw_calls;
            diagnostics.mesh_draw_calls = diagnostics.vulkan->mesh_draw_calls;
            diagnostics.image_draw_calls = diagnostics.vulkan->image_draw_calls;
            diagnostics.ui_draw_calls = diagnostics.vulkan->ui_draw_calls;
            if (diagnostics.ui_draw_calls > diagnostics.draw_calls)
                throw std::logic_error("Vulkan UI draw count exceeds total observed draw count");
            diagnostics.scene_draw_calls = diagnostics.draw_calls - diagnostics.ui_draw_calls;
            diagnostics.material_draw_calls = diagnostics.vulkan->material_draw_calls;
            diagnostics.incomplete_material_draw_calls = diagnostics.vulkan->incomplete_material_draw_calls;
            diagnostics.particle_draw_calls = diagnostics.vulkan->particle_draw_calls;
            diagnostics.shadow_draw_calls = diagnostics.vulkan->shadow_draw_calls;
            diagnostics.pbr_draw_calls = diagnostics.vulkan->pbr_draw_calls;
            diagnostics.source_bloom_passes = diagnostics.vulkan->source_bloom_passes;
            diagnostics.gamma_passes = diagnostics.vulkan->gamma_passes;
        }
#endif
        diagnostics.audio_requested = game != nullptr || sound.has_value();
        diagnostics.audio_loop = settings.audio_loop;
        diagnostics.audio_mode = "not_requested";
        diagnostics.audio_device_open = false;
        if (sound) {
            diagnostics.audio_device_open = sound->mode() == audio::Mode::Device;
            diagnostics.audio_mode = diagnostics.audio_device_open ? "device" : "offline";
            diagnostics.audio_playing = sound->playing(audio_handle);
            diagnostics.audio_active_voices = sound->active_voices();
            diagnostics.audio_master_volume = sound->master_volume();
            diagnostics.audio_driver = audio_driver;
            diagnostics.audio_device = audio_device;
            diagnostics.audio_error = sound->error();
            diagnostics.audio_frames = sound->rendered_frames();
            diagnostics.audio_clip_frames = audio_clip->frames();
            diagnostics.audio_sample_rate = audio_clip->sample_rate;
            diagnostics.audio_channels = audio_clip->channels;
            if (!diagnostics.audio_error.empty()) {
                throw std::runtime_error("Native audio stream: " + diagnostics.audio_error);
            }
        }
        if (game) {
            auto& device = game->audio();
            diagnostics.audio_device_open = device.mode() == audio::Mode::Device;
            diagnostics.audio_mode = diagnostics.audio_device_open ? "device" : "offline";
            diagnostics.audio_active_voices = device.active_voices();
            diagnostics.audio_master_volume = device.master_volume();
            diagnostics.audio_playing = diagnostics.audio_active_voices != 0;
            diagnostics.audio_driver = device.driver();
            diagnostics.audio_device = device.device();
            diagnostics.audio_error = device.error();
            diagnostics.audio_frames = device.rendered_frames();
            if (!diagnostics.audio_error.empty())
                throw std::runtime_error("Native game audio stream: " + diagnostics.audio_error);
        }
        write_runtime_diagnostics(settings.diagnostics_json, settings, diagnostics);
    }
    if (sound) {
        const auto error = sound->error();
        sound->stop(audio_handle);
        if (!error.empty()) throw std::runtime_error("Native audio stream: " + error);
    }
    return result;
}

} // namespace c4
