#pragma once

#include "game/ui/NativeUi.hpp"
#include "rendering/opengl/gl.hpp"
#include "rendering/opengl/gpu_texture.hpp"
#include "rendering/opengl/ui_mesh.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace c4 {
namespace {

class UiRenderer {
    struct Vertex { float x, y, u, v; };
    struct ImageUpload {
        std::uint32_t width{}, height{};
        std::uint64_t revision{};
        const assets::GpuImage* gpu{};
        std::size_t texture_owner{};
        std::array<float, 2> uv_scale{1, 1};
        bool filtered{true};
    };
    InspectionGL& gl_;
    InspectionResources resources_{gl_};
    OriginalUiMeshRenderer mesh_renderer_{gl_};
    GpuTextureCapabilities texture_capabilities_;
    const assets::Image* image_origin_ = nullptr;
    std::vector<ImageUpload> image_uploads_;
    std::vector<GLuint> image_textures_;
    std::vector<std::string> source_gpu_fallback_reasons_;
    std::uint64_t source_gpu_texture_count_{};
    std::uint64_t source_gpu_precision_fallback_count_{};
    GLint maximum_texture_ = 0;
    GLint color_location_ = -1;
    bool initialized_ = false;
    bool revisioned_ = false;
    std::uint64_t draw_calls_ = 0;

    void validate_image(const assets::Image& image, const assets::GpuImage* gpu) const {
        if (image.width == 0 || image.height == 0 || image.face_count != 1) {
            throw std::runtime_error("Invalid original UI logical image dimensions");
        }
        if (gpu) {
            if (gpu->face_count != 1 || gpu->width == 0 || gpu->height == 0 ||
                gpu->width > static_cast<std::uint32_t>(maximum_texture_) ||
                gpu->height > static_cast<std::uint32_t>(maximum_texture_)) {
                throw std::runtime_error("Invalid original UI canonical GPU storage dimensions");
            }
        } else {
            if (image.width > static_cast<std::uint32_t>(maximum_texture_) ||
                image.height > static_cast<std::uint32_t>(maximum_texture_))
                throw std::runtime_error("Live/generated original UI image exceeds GPU texture limit");
            const auto texels = static_cast<std::uint64_t>(image.width) * image.height;
            if (texels > std::numeric_limits<std::size_t>::max() / 4 || texels * 4 != image.rgba.size())
                throw std::runtime_error("Invalid live/generated original UI RGBA image size");
        }
    }

    void update_images(const ui::UiFrame& frame) {
        const bool revisioned = !frame.image_revisions.empty();
        if (revisioned && frame.image_revisions.size() != frame.images.size()) {
            throw std::runtime_error("Original UI image revision count does not match images");
        }
        if (!frame.gpu_images.empty() && frame.gpu_images.size() != frame.images.size()) {
            throw std::runtime_error("Original UI canonical GPU image count does not match images");
        }
        if (initialized_) {
            if (frame.images.data() != image_origin_ || frame.images.size() != image_uploads_.size() ||
                revisioned != revisioned_) {
                throw std::runtime_error("Original UI image lifetime does not match uploaded assets");
            }
            for (std::size_t i = 0; i < frame.images.size(); ++i) {
                auto& upload = image_uploads_[i];
                const auto* gpu = frame.gpu_images.empty() ? nullptr : frame.gpu_images[i];
                if (gpu != upload.gpu)
                    throw std::runtime_error("Original UI canonical GPU image identity changed");
                const auto& image = frame.images[i];
                if (gpu) {
                    if (image.width != upload.width || image.height != upload.height)
                        throw std::runtime_error("Original UI canonical logical image dimensions changed");
                    continue;
                }
                if (!revisioned_ || upload.revision == frame.image_revisions[i]) continue;
                validate_image(image, nullptr);
                glBindTexture(GL_TEXTURE_2D, image_textures_[i]);
                GpuTextureUnpackState unpack{gl_};
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                if (upload.width == image.width && upload.height == image.height) {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image.width),
                                    static_cast<GLsizei>(image.height), GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
                } else {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width),
                                 static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
                }
                upload.width = image.width;
                upload.height = image.height;
                upload.revision = frame.image_revisions[i];
            }
            return;
        }
        // Empty pre-source frames have no asset lifetime to retain yet.
        if (frame.images.empty()) return;
        if (frame.images.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
            throw std::runtime_error("Too many original UI images for OpenGL");
        }
        for (std::size_t i = 0; i < frame.images.size(); ++i)
            validate_image(frame.images[i], frame.gpu_images.empty() ? nullptr : frame.gpu_images[i]);
        image_uploads_.resize(frame.images.size());
        image_textures_.resize(frame.images.size());
        resources_.textures.resize(frame.images.size());
        for (std::size_t i = 0; i < frame.images.size(); ++i) {
            const auto& image = frame.images[i];
            const auto* gpu = frame.gpu_images.empty() ? nullptr : frame.gpu_images[i];
            auto& upload = image_uploads_[i];
            upload.width = image.width;
            upload.height = image.height;
            upload.revision = revisioned ? frame.image_revisions[i] : 0;
            upload.gpu = gpu;
            upload.texture_owner = i;
            if (gpu) {
                if (gpu->runtime) {
                    const auto& runtime = *gpu->runtime;
                    if (runtime.source_width == 0 || runtime.source_height == 0 ||
                        runtime.nominal_width < runtime.source_width || runtime.nominal_height < runtime.source_height ||
                        runtime.nominal_width < image.width || runtime.nominal_height < image.height)
                        throw std::runtime_error("Invalid original UI nominal bitmap UV domain");
                    // Source BMAP +44/+48 are the nominal domain before
                    // allocation caps, not the physical GPU storage extent.
                    upload.uv_scale = {static_cast<float>(runtime.source_width) / runtime.nominal_width,
                                       static_cast<float>(runtime.source_height) / runtime.nominal_height};
                }
                // ponytail: setup-only quadratic identity scan; index pointers if asset counts make preparation costly.
                for (std::size_t previous = 0; previous < i; ++previous) {
                    if (image_uploads_[previous].gpu == gpu) {
                        upload.texture_owner = image_uploads_[previous].texture_owner;
                        break;
                    }
                }
            }
            if (upload.texture_owner != i) {
                image_textures_[i] = image_textures_[upload.texture_owner];
                continue;
            }
            glGenTextures(1, &resources_.textures[i]);
            if (!resources_.textures[i]) throw std::runtime_error("OpenGL could not allocate original UI texture");
            image_textures_[i] = resources_.textures[i];
            glBindTexture(GL_TEXTURE_2D, image_textures_[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (gpu) {
                const auto result = upload_bound_gpu_image(gl_, GL_TEXTURE_2D, *gpu, texture_capabilities_);
                ++source_gpu_texture_count_;
                if (result.implementation_precision_fallback) {
                    ++source_gpu_precision_fallback_count_;
                    if (!result.reason)
                        throw std::runtime_error("Original UI GPU precision fallback has no diagnostic reason");
                    source_gpu_fallback_reasons_.emplace_back(result.reason);
                }
            } else {
                // Only explicit live/generated slots use raw CPU RGBA8.
                GpuTextureUnpackState unpack{gl_};
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width),
                             static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            }
        }
        image_origin_ = frame.images.data();
        revisioned_ = revisioned;
        initialized_ = true;
    }
    void image_texture(std::uint32_t index, bool filtered) {
        gl_.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, image_textures_.at(index));
        auto& upload = image_uploads_.at(image_uploads_.at(index).texture_owner);
        if (upload.filtered != filtered) {
            const GLint filter = filtered ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            upload.filtered = filtered;
        }
    }

    void bind_quads() {
        gl_.glUseProgram(resources_.program);
        gl_.glBindBuffer(GL_ARRAY_BUFFER, resources_.buffers[0]);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources_.buffers[1]);
        gl_.glEnableVertexAttribArray(0);
        gl_.glEnableVertexAttribArray(1);
        for (GLuint index = 2; index < 5; ++index) gl_.glDisableVertexAttribArray(index);
        gl_.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        gl_.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, u)));
        glEnable(GL_BLEND);
        gl_.glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST);
    }

    void draw_quad(const ui::Quad& quad, const ui::Viewport& viewport, int drawable_width, int drawable_height) {
        const double half_width = static_cast<double>(quad.rect[2]) * 0.5;
        const double half_height = static_cast<double>(quad.rect[3]) * 0.5;
        const double center_x = quad.rect[0] + half_width;
        const double center_y = quad.rect[1] + half_height;
        const double cosine = quad.rotation == 0 ? 1 : std::cos(static_cast<double>(quad.rotation));
        const double sine = quad.rotation == 0 ? 0 : std::sin(static_cast<double>(quad.rotation));
        const std::array<std::array<double, 2>, 4> corners{{
            {-half_width, -half_height}, {half_width, -half_height},
            {half_width, half_height}, {-half_width, half_height}}};
        std::array<Vertex, 4> vertices{};
        const auto& uv_scale = image_uploads_.at(quad.image_index).uv_scale;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            // Positive source rotation is clockwise in the top-left canvas.
            const double x = center_x + corners[i][0] * cosine - corners[i][1] * sine;
            const double y = center_y + corners[i][0] * sine + corners[i][1] * cosine;
            vertices[i] = {
                static_cast<float>(2.0 * (viewport.x + x * viewport.scale) / drawable_width - 1.0),
                static_cast<float>(1.0 - 2.0 * (viewport.y + y * viewport.scale) / drawable_height),
                quad.uv[(i == 0 || i == 3) ? 0 : 2] * uv_scale[0],
                quad.uv[i < 2 ? 1 : 3] * uv_scale[1]};
            if (!std::isfinite(vertices[i].x) || !std::isfinite(vertices[i].y))
                throw std::runtime_error("Original UI quad projection exceeds coordinate range");
        }
        gl_.glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
        gl_.glUniform4fv(color_location_, 1, quad.color.data());
        image_texture(quad.image_index, quad.filtered);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        ++draw_calls_;
    }

public:
    explicit UiRenderer(InspectionGL& gl) : gl_(gl) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_);
        if (maximum_texture_ <= 0) throw std::runtime_error("OpenGL original UI texture limit unavailable");
        const char* vertex_source =
            "#version 120\n"
            "attribute vec2 position; attribute vec2 uv; varying vec2 texcoord;\n"
            "void main(){gl_Position=vec4(position,0.0,1.0);texcoord=uv;}\n";
        const char* fragment_source =
            "#version 120\n"
            "uniform sampler2D image; uniform vec4 color; varying vec2 texcoord;\n"
            "void main(){gl_FragColor=color*texture2D(image,texcoord);}\n";
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_source)};
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER, fragment_source)};
        resources_.program = gl_.glCreateProgram();
        if (!resources_.program) throw std::runtime_error("OpenGL could not allocate original UI program");
        gl_.glAttachShader(resources_.program, vertex.id);
        gl_.glAttachShader(resources_.program, fragment.id);
        gl_.glBindAttribLocation(resources_.program, 0, "position");
        gl_.glBindAttribLocation(resources_.program, 1, "uv");
        gl_.glLinkProgram(resources_.program);
        GLint linked = GL_FALSE;
        gl_.glGetProgramiv(resources_.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            std::array<char, 4096> message{};
            gl_.glGetProgramInfoLog(resources_.program, static_cast<GLsizei>(message.size()), nullptr, message.data());
            throw std::runtime_error("Original UI shader link: " + std::string(message.data()));
        }
        color_location_ = gl_.glGetUniformLocation(resources_.program, "color");
        const GLint image_location = gl_.glGetUniformLocation(resources_.program, "image");
        if (color_location_ < 0 || image_location < 0) {
            throw std::runtime_error("Original UI shader uniform missing");
        }
        gl_.glUseProgram(resources_.program);
        gl_.glUniform1i(image_location, 0);
        resources_.buffers.resize(2);
        gl_.glGenBuffers(2, resources_.buffers.data());
        if (!resources_.buffers[0] || !resources_.buffers[1]) {
            throw std::runtime_error("OpenGL could not allocate original UI buffers");
        }
        gl_.glBindBuffer(GL_ARRAY_BUFFER, resources_.buffers[0]);
        gl_.glBufferData(GL_ARRAY_BUFFER, sizeof(std::array<Vertex, 4>), nullptr, GL_DYNAMIC_DRAW);
        constexpr std::array<GLushort, 6> indices{0, 1, 2, 0, 2, 3};
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources_.buffers[1]);
        gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);
    }

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    UiRenderer(UiRenderer&&) = delete;
    UiRenderer& operator=(UiRenderer&&) = delete;

    // Candidate preparation uploads source assets without drawing/presenting.
    void prepare(const ui::UiFrame& frame) {
        // A source queue node has one drawable kind. Cross-family equality has
        // no recovered ordering, unlike stable glyph/child ties within a span.
        std::size_t quad_index{}, mesh_index{};
        while (quad_index < frame.quads.size() && mesh_index < frame.meshes.size()) {
            const auto& quad = frame.quads[quad_index];
            const auto& mesh = frame.meshes[mesh_index];
            if (quad.layer == mesh.layer && quad.source_order == mesh.source_order)
                throw std::runtime_error("Ambiguous original UI node ordering");
            if (quad.layer < mesh.layer ||
                (quad.layer == mesh.layer && quad.source_order > mesh.source_order))
                ++quad_index;
            else
                ++mesh_index;
        }
        gl_.glActiveTexture(GL_TEXTURE0);
        update_images(frame);
        mesh_renderer_.prepare(frame.prepared_meshes.empty() ? frame.meshes : frame.prepared_meshes);
    }

    void draw(const ui::UiFrame& frame, int drawable_width, int drawable_height) {
        if (drawable_width <= 0 || drawable_height <= 0 ||
            !std::isfinite(frame.canvas_width) || !std::isfinite(frame.canvas_height) ||
            frame.canvas_width <= 0 || frame.canvas_height <= 0) {
            throw std::runtime_error("Original UI requires positive finite canvas and drawable dimensions");
        }
        for (const auto& quad : frame.quads) {
            if (quad.image_index >= frame.images.size() || !std::isfinite(quad.rotation)) {
                throw std::runtime_error("Original UI quad image index or rotation invalid");
            }
            for (const float value : quad.rect) {
                if (!std::isfinite(value)) throw std::runtime_error("Original UI quad rectangle invalid");
            }
            for (const float value : quad.uv) {
                if (!std::isfinite(value)) throw std::runtime_error("Original UI quad UV invalid");
            }
            for (const float value : quad.color) {
                if (!std::isfinite(value)) throw std::runtime_error("Original UI quad color invalid");
            }
        }
        prepare(frame);
        if (frame.quads.empty() && frame.meshes.empty()) return;
        const auto viewport = ui::Viewport::fit(drawable_width, drawable_height,
                                                 frame.canvas_width, frame.canvas_height);
        GLint stencil_bits{};
        if (!frame.meshes.empty()) glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                     GL_POLYGON_BIT | GL_VIEWPORT_BIT | GL_STENCIL_BUFFER_BIT);
        struct RestoreOverlayState {
            InspectionGL& gl;
            ~RestoreOverlayState() { glPopAttrib(); gl.glDisableVertexAttribArray(2); }
        } restore{gl_};
        glViewport(0, 0, drawable_width, drawable_height);
        std::size_t quad_index{}, mesh_index{};
        bool quads_bound{}, private_layer_cleared{};
        int cleared_private_layer{};
        // NativeUi orders source nodes by layer ascending, creation order
        // descending (equal-layer INSERT_LIFO); merge without family priority.
        while (quad_index < frame.quads.size() || mesh_index < frame.meshes.size()) {
            const bool next_is_quad = quad_index < frame.quads.size() &&
                (mesh_index == frame.meshes.size() ||
                 frame.quads[quad_index].layer < frame.meshes[mesh_index].layer ||
                 (frame.quads[quad_index].layer == frame.meshes[mesh_index].layer &&
                  frame.quads[quad_index].source_order > frame.meshes[mesh_index].source_order));
            if (next_is_quad) {
                if (!quads_bound) {
                    bind_quads();
                    quads_bound = true;
                }
                draw_quad(frame.quads[quad_index++], viewport, drawable_width, drawable_height);
                continue;
            }
            const auto& mesh = frame.meshes[mesh_index++];
            if (!private_layer_cleared || cleared_private_layer != mesh.layer) {
                glDepthMask(GL_TRUE);
                glClearDepth(1.0);
                GLbitfield clear_bits = GL_DEPTH_BUFFER_BIT;
                if (stencil_bits > 0) {
                    glStencilMask(~GLuint{0});
                    glClearStencil(0);
                    clear_bits |= GL_STENCIL_BUFFER_BIT;
                }
                // The source private view clears its fitted authored rectangle,
                // preserving both scene color and letterbox depth/stencil.
                glPushAttrib(GL_SCISSOR_BIT);
                glEnable(GL_SCISSOR_TEST);
                const GLint left = static_cast<GLint>(std::floor(viewport.x));
                const GLint bottom = static_cast<GLint>(
                    std::floor(drawable_height - viewport.y - viewport.height));
                glScissor(left, bottom,
                    static_cast<GLsizei>(std::ceil(viewport.x + viewport.width)) - left,
                    static_cast<GLsizei>(std::ceil(drawable_height - viewport.y)) - bottom);
                glClear(clear_bits);
                glPopAttrib();
                cleared_private_layer = mesh.layer;
                private_layer_cleared = true;
            }
            image_texture(mesh.image_index, mesh.filtered);
            mesh_renderer_.draw(mesh, frame.images, frame.gpu_images, image_textures_,
                                viewport, drawable_width, drawable_height);
            quads_bound = false;
        }
    }

    std::uint64_t draw_calls() const noexcept { return draw_calls_ + mesh_renderer_.draw_calls(); }
    std::uint64_t source_gpu_texture_count() const noexcept { return source_gpu_texture_count_; }
    std::uint64_t source_gpu_precision_fallback_count() const noexcept {
        return source_gpu_precision_fallback_count_;
    }
    std::span<const std::string> source_gpu_fallback_reasons() const noexcept {
        return source_gpu_fallback_reasons_;
    }
};

} // namespace
} // namespace c4
