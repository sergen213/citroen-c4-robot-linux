#include "UiResources.hpp"
#include "assets/World.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace c4::ui {
namespace {
char32_t next_character(std::string_view text, std::size_t& offset) noexcept {
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 128) return first;
    const unsigned count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc2 ? 1 : 0;
    if (!count || count > text.size() - offset) return U'?';
    char32_t result = first & ((1u << (6 - count)) - 1u);
    for (unsigned i = 0; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset]);
        if ((byte & 0xc0) != 0x80) return U'?';
        ++offset;
        result = (result << 6) | (byte & 0x3f);
    }
    if ((count == 1 && result < 0x80) || (count == 2 && result < 0x800) ||
        (count == 3 && result < 0x10000) || result > 0x10ffff ||
        (result >= 0xd800 && result <= 0xdfff)) return U'?';
    return result;
}
unsigned windows1254(char32_t character) noexcept {
    switch (character) {
    case U'Ğ': return 0xd0; case U'ğ': return 0xf0;
    case U'İ': return 0xdd; case U'ı': return 0xfd;
    case U'Ş': return 0xde; case U'ş': return 0xfe;
    case U'€': return 0x80; case U'’': return 0x92;
    default: return character <= 255 ? static_cast<unsigned>(character) : '?';
    }
}
std::size_t character_count(std::string_view text) noexcept {
    std::size_t count = 0, offset = 0;
    while (offset < text.size()) { next_character(text, offset); ++count; }
    return count;
}
}

Resources::Resources(std::filesystem::path asset_root,
                     std::optional<assets::GpuImageRuntimeContext> file_context,
                     int launch_width, int launch_height)
    : root(std::move(asset_root)), tier_(
          launch_width >= 1280 && launch_height >= 1024 ? 3 :
          launch_width >= 1024 && launch_height >= 768 ? 2 :
          launch_width >= 800 && launch_height >= 600 ? 1 : 0),
      file_context_(std::move(file_context)) {
    images.reserve(512);
    revisions.reserve(512);
    gpu_records.reserve(512);
    gpu_images.reserve(512);
    names_.reserve(512);
    black_keys_.reserve(512);
    overlay_uploads_.reserve(512);
    bitmap_flags_.reserve(512);
    models.reserve(2);
    model_names_.reserve(2);
}
std::uint32_t Resources::add(std::string_view name, bool black_key, bool overlay_upload,
                             std::uint32_t bitmap_flags) {
    if (const auto found = indices_.find(name); found != indices_.end()) {
        black_keys_[found->second] = black_keys_[found->second] || black_key;
        return found->second;
    }
    const auto id = static_cast<std::uint32_t>(images.size());
    indices_.emplace(std::string(name), id);
    names_.emplace_back(name);
    black_keys_.push_back(black_key);
    overlay_uploads_.push_back(overlay_upload);
    bitmap_flags_.push_back(bitmap_flags);
    images.emplace_back();
    revisions.push_back(0);
    gpu_records.emplace_back();
    gpu_images.push_back(nullptr);
    return id;
}
std::uint32_t Resources::add_image(assets::Image image) {
    const auto id = static_cast<std::uint32_t>(images.size());
    images.push_back(std::move(image));
    revisions.push_back(1);
    names_.emplace_back();
    black_keys_.push_back(false);
    overlay_uploads_.push_back(false);
    bitmap_flags_.push_back(0);
    gpu_records.emplace_back();
    gpu_images.push_back(nullptr);
    return id;
}
std::uint32_t Resources::add_model(std::string_view name) {
    for (std::size_t i = 0; i < model_names_.size(); ++i)
        if (model_names_[i] == name) return static_cast<std::uint32_t>(i);
    const auto id = static_cast<std::uint32_t>(models.size());
    models.emplace_back();
    model_names_.emplace_back(name);
    return id;
}
void Resources::load() {
    std::vector<std::string_view> names;
    std::vector<std::size_t> indices;
    std::vector<std::size_t> model_indices;
    names.reserve(names_.size() + model_names_.size()); indices.reserve(names_.size());
    model_indices.reserve(model_names_.size());
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (!names_[i].empty() && images[i].rgba.empty()) {
            names.push_back(names_[i]); indices.push_back(i);
        }
    }
    const auto image_count = names.size();
    for (std::size_t i = 0; i < model_names_.size(); ++i) {
        if (models[i].groups.empty()) {
            names.push_back(model_names_[i]);
            model_indices.push_back(i);
        }
    }
    const auto paths = assets::resolve_resources(root / "C4.wrs", names);
    for (std::size_t n = 0; n < image_count; ++n) {
        const auto i = indices[n];
        // First registration owns the original upload role, independently of
        // CPU font black-keying. Authored alpha still bypasses GPU keying.
        set_gpu_image(static_cast<std::uint32_t>(i),
                      assets::load_gpu_image(paths[n], images[i],
                          gpu_upload_options(overlay_uploads_[i], bitmap_flags_[i])));
        if (black_keys_[i]) {
            auto& rgba = images[i].rgba;
            for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4)
                if (rgba[pixel] == 0 && rgba[pixel + 1] == 0 && rgba[pixel + 2] == 0)
                    rgba[pixel + 3] = 0;
        }
        ++revisions[i];
    }
    for (std::size_t n = 0; n < model_indices.size(); ++n)
        models[model_indices[n]] = assets::load_model(paths[image_count + n]);
}
void Resources::set_gpu_image(std::uint32_t index, assets::GpuImage&& image) {
    auto& record = gpu_records.at(index);
    record = std::move(image);
    gpu_images.at(index) = &record;
}
void Resources::mark_dynamic(std::uint32_t index) {
    gpu_images.at(index) = nullptr;
    gpu_records.at(index) = {};
}
assets::GpuImageUploadOptions Resources::gpu_upload_options(
    bool overlay_upload, std::uint32_t bitmap_flags) const {
    assets::GpuImageUploadOptions options{overlay_upload, 16, 32, file_context_};
    if (options.runtime) options.runtime->bitmap_flags |= bitmap_flags;
    return options;
}
std::uint32_t Resources::find(std::string_view name) const {
    const auto found = indices_.find(name);
    if (found == indices_.end()) throw std::logic_error("UI image was not registered: " + std::string(name));
    return found->second;
}

void Painter::image(std::uint32_t id, float x, float y, float sx, float sy, float alpha, bool filtered) {
    const auto& image = resources.image(id);
    const float width = static_cast<float>(image.width) * sx;
    const float height = static_cast<float>(image.height) * sy;
    Quad quad{id, {x + std::min(width, 0.0f), y + std::min(height, 0.0f),
                   std::abs(width), std::abs(height)},
              {sx < 0 ? 1.0f : 0.0f, sy < 0 ? 1.0f : 0.0f,
               sx < 0 ? 0.0f : 1.0f, sy < 0 ? 0.0f : 1.0f}, {1,1,1,alpha}};
    quad.layer = layer;
    quad.source_order = source_order;
    quad.filtered = filtered;
    quads.push_back(quad);
}
void Painter::bitmap_text(std::uint32_t id, int cell_width, int cell_height,
                          std::string_view text, float x, float y, bool centered,
                          bool condensed, float alpha) {
    const auto& sheet = resources.image(id);
    if (cell_width <= 0 || cell_height <= 0 || sheet.width % static_cast<unsigned>(cell_width) ||
        sheet.height % static_cast<unsigned>(cell_height))
        throw std::logic_error("Original bitmap font dimensions do not match source cells");
    const unsigned columns = sheet.width / static_cast<unsigned>(cell_width);
    const unsigned rows = sheet.height / static_cast<unsigned>(cell_height);
    // Original Digital8 and Time sheets both index cells by the character byte.
    // Authentic Digital8 cells 48/78 contain 0/N; cells 16..25 are blank.
    const float advance = static_cast<float>(cell_width - (condensed ? 1 : 0));
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        const auto line = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
        float cursor = x;
        if (centered) cursor -= static_cast<float>(character_count(line)) * advance * .5f;
        std::size_t offset = 0;
        while (offset < line.size()) {
            unsigned code = windows1254(next_character(line, offset));
            if (code >= columns * rows) code = '?';
            const auto cell = code;
            const float left = static_cast<float>((cell % columns) * static_cast<unsigned>(cell_width));
            const float top = static_cast<float>((cell / columns) * static_cast<unsigned>(cell_height));
            Quad quad{id, {cursor, y, static_cast<float>(cell_width), static_cast<float>(cell_height)},
                      {left / static_cast<float>(sheet.width), top / static_cast<float>(sheet.height),
                       (left + static_cast<float>(cell_width)) / static_cast<float>(sheet.width),
                       (top + static_cast<float>(cell_height)) / static_cast<float>(sheet.height)},
                      {1,1,1,alpha}};
            quad.filtered = false;
            quad.layer = layer;
            quad.source_order = source_order;
            quads.push_back(quad);
            cursor += advance;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
        y += static_cast<float>(cell_height);
    }
}
void Painter::solid(std::uint32_t white, float x, float y, float width, float height,
                    std::array<float,4> color) {
    if (width > 0 && height > 0) {
        Quad quad{white,{x,y,width,height},{0,0,1,1},color};
        quad.layer = layer;
        quad.source_order = source_order;
        quads.push_back(quad);
    }
}
} // namespace c4::ui
