#pragma once

#include "NativeUi.hpp"
#include "assets/Animation.hpp"
#include "assets/GpuImage.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <map>
#include <vector>

namespace c4::ui {
class Resources {
public:
    // Native runtime supplies explicit caps; absent context keeps authored views.
    explicit Resources(std::filesystem::path root,
                       std::optional<assets::GpuImageRuntimeContext> file_context = std::nullopt,
                       int launch_width = 640, int launch_height = 480);
    // Native mapping to an authored original RES tier, fixed at launch.
    int tier() const noexcept { return tier_; }
    int canvas_width() const noexcept { return std::array{640, 800, 1024, 1280}[tier_]; }
    int canvas_height() const noexcept { return std::array{480, 600, 768, 1024}[tier_]; }
    float scale_x() const noexcept { return std::array{.625f, .781f, 1.f, 1.25f}[tier_]; }
    float scale_y() const noexcept { return std::array{.625f, .781f, 1.f, 1.334f}[tier_]; }
    // The first registration fixes the original GPU role; black_key is CPU-only.
    std::uint32_t add(std::string_view name, bool black_key = false,
                      bool overlay_upload = false, std::uint32_t bitmap_flags = 0);
    std::uint32_t add_image(assets::Image image);
    std::uint32_t add_model(std::string_view name);
    void load(); // Resolve all registered original names in one shared catalog batch.
    void set_gpu_image(std::uint32_t index, assets::GpuImage&& image);
    void mark_dynamic(std::uint32_t index);
    // Derived original skins reuse the same immutable first-upload context.
    assets::GpuImageUploadOptions gpu_upload_options(bool overlay_upload,
                                                    std::uint32_t bitmap_flags = 0) const;
    const assets::Image& image(std::uint32_t index) const { return images.at(index); }
    std::uint32_t find(std::string_view name) const;
    std::filesystem::path root;
    std::vector<assets::Image> images;
    std::vector<std::uint64_t> revisions;
    // Registered before load; source records and their published pointers remain
    // stable for the lifetime of the loaded UI. Null selects live/generated RGBA.
    std::vector<assets::GpuImage> gpu_records;
    std::vector<const assets::GpuImage*> gpu_images;
    std::vector<assets::Model> models;
private:
    const int tier_;
    std::optional<assets::GpuImageRuntimeContext> file_context_;
    std::map<std::string, std::uint32_t, std::less<>> indices_;
    std::vector<std::string> names_;
    std::vector<bool> black_keys_;
    std::vector<bool> overlay_uploads_;
    std::vector<std::uint32_t> bitmap_flags_;
    std::vector<std::string> model_names_;
};

class Painter {
public:
    Painter(Resources& resources, std::vector<Quad>& quads) : resources(resources), quads(quads) {}
    void set_layer(int value) noexcept { layer = value; }
    void set_source_order(std::uint32_t value) noexcept { source_order = value; }
    void image(std::uint32_t id, float x, float y, float sx = .625f, float sy = .625f,
               float alpha = 1, bool filtered = true);
    void image(std::string_view name, float x, float y, float sx = .625f, float sy = .625f,
               float alpha = 1, bool filtered = true) { image(resources.find(name), x, y, sx, sy, alpha, filtered); }
    void bitmap_text(std::uint32_t image, int cell_width, int cell_height,
                     std::string_view text, float x, float y, bool centered = false,
                     bool condensed = false, float alpha = 1);
    void solid(std::uint32_t white, float x, float y, float width, float height,
               std::array<float,4> color);
    Resources& resources;
    std::vector<Quad>& quads;
    int layer{10};
    std::uint32_t source_order{};
};
} // namespace c4::ui
