#pragma once

#include "core/render_scene.hpp"
#include "game/effects/EffectFrame.hpp"
#include "rendering/opengl/gl.hpp"
#include "QualityShaders.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace c4 {
namespace {

class OriginalEffectRenderer {
    InspectionGL& gl_;
    InspectionResources resources_{gl_};
    const assets::Image* texture_origin_ = nullptr;
    GLint maximum_texture_ = 0;
    GLint camera_location_ = -1;
    GLint alpha_test_location_ = -1;
    GLint linear_output_location_ = -1;
    std::uint64_t revision_ = 0;
    std::size_t vertex_count_ = 0;
    std::size_t index_count_ = 0;
    bool uploaded_ = false;
    std::uint64_t draw_calls_ = 0;
    float maximum_sampler_anisotropy_ = 1;
    float sampler_anisotropy_level_ = 1;
    bool sampler_anisotropy_supported_ = false;
    bool sampler_initialized_ = false;
    std::int32_t source_anisotropy_raw_ = 0;
    std::size_t sampler_texture_count_ = 0;

    void upload_images(const game::EffectFrame& frame) {
        if (texture_origin_) {
            if (texture_origin_ != frame.textures.data() || resources_.textures.size() != frame.textures.size()) {
                throw std::runtime_error("Original effect image lifetime does not match uploaded assets");
            }
            return;
        }
        if (frame.textures.empty()) return;
        if (frame.textures.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
            throw std::runtime_error("Too many original effect images for OpenGL");
        }
        for (const auto& image : frame.textures) {
            if (image.face_count != 1 || !image.width || !image.height ||
                image.width > static_cast<std::uint32_t>(maximum_texture_) ||
                image.height > static_cast<std::uint32_t>(maximum_texture_) ||
                static_cast<std::uint64_t>(image.width) * image.height * 4 != image.rgba.size()) {
                throw std::runtime_error("Invalid original effect image dimensions or GPU texture limit exceeded");
            }
        }
        resources_.textures.resize(frame.textures.size());
        glGenTextures(static_cast<GLsizei>(resources_.textures.size()), resources_.textures.data());
        for (std::size_t i = 0; i < frame.textures.size(); ++i) {
            if (!resources_.textures[i]) throw std::runtime_error("OpenGL could not allocate original effect texture");
            const auto& image = frame.textures[i];
            glBindTexture(GL_TEXTURE_2D, resources_.textures[i]);
            // Original particle prep selects linear filtering and clamp addressing.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // All five actual original particle BMAPs allocate exactly one GPU level.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width),
                         static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        }
        texture_origin_ = frame.textures.data();
    }

    void update_sampler(const game::EffectFrame& frame) {
        if (sampler_initialized_ && source_anisotropy_raw_ == frame.source_anisotropy_raw &&
            sampler_texture_count_ == resources_.textures.size()) return;
        source_anisotropy_raw_ = frame.source_anisotropy_raw;
        sampler_anisotropy_level_ = source_anisotropy_raw_ != 0 && sampler_anisotropy_supported_
            ? maximum_sampler_anisotropy_ : 1;
        if (sampler_anisotropy_supported_) {
            for (const auto texture : resources_.textures) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, sampler_anisotropy_level_);
            }
        }
        sampler_texture_count_ = resources_.textures.size();
        sampler_initialized_ = true;
    }

    void upload_geometry(const game::EffectFrame& frame) {
        if (uploaded_ && revision_ == frame.revision) {
            if (vertex_count_ != frame.vertices.size() || index_count_ != frame.indices.size()) {
                throw std::runtime_error("Original effect geometry changed without a revision");
            }
            return;
        }
        if (frame.vertices.size() > game::effect_vertex_capacity || frame.indices.size() > game::effect_index_capacity) {
            throw std::runtime_error("Original effect geometry exceeds source pool capacity");
        }
        for (const auto& vertex : frame.vertices) {
            for (const auto value : vertex.position) {
                if (!std::isfinite(value)) throw std::runtime_error("Original effect position is not finite");
            }
            for (const auto value : vertex.uv) {
                if (!std::isfinite(value)) throw std::runtime_error("Original effect UV is not finite");
            }
            for (const auto value : vertex.color) {
                if (!std::isfinite(value)) throw std::runtime_error("Original effect color is not finite");
            }
        }
        for (const auto index : frame.indices) {
            if (index >= frame.vertices.size()) throw std::runtime_error("Original effect vertex index out of range");
        }
        if (!frame.vertices.empty()) {
            gl_.glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(frame.vertices.size_bytes()), frame.vertices.data());
        }
        if (!frame.indices.empty()) {
            gl_.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(frame.indices.size_bytes()), frame.indices.data());
        }
        revision_ = frame.revision;
        vertex_count_ = frame.vertices.size();
        index_count_ = frame.indices.size();
        uploaded_ = true;
    }

public:
    // ANO1 requests a float-bit DWORD, not integer anisotropy 2; native capability
    // translation is supported by bounded Wine/llvmpipe observations only.
    static constexpr std::string_view unresolved_source_semantics =
        "Native maximum-capability anisotropy translation matches 18 bounded Wine/llvmpipe cases; "
        "this does not establish universal Windows-driver or internal-filter equivalence.";

    explicit OriginalEffectRenderer(InspectionGL& gl) : gl_(gl) {
        static_assert(std::is_standard_layout_v<game::EffectVertex>);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_);
        if (maximum_texture_ <= 0) throw std::runtime_error("OpenGL original effect texture limit unavailable");
        sampler_anisotropy_supported_ =
            SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic") ||
            SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic");
        if (sampler_anisotropy_supported_) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum_sampler_anisotropy_);
            if (!std::isfinite(maximum_sampler_anisotropy_) || maximum_sampler_anisotropy_ < 1) {
                sampler_anisotropy_supported_ = false;
                maximum_sampler_anisotropy_ = 1;
            }
        }
        const char* vertex_source =
            "#version 120\n"
            "attribute vec3 position; attribute vec2 uv; attribute vec4 color;\n"
            "uniform mat4 camera; varying vec2 texcoord; varying vec4 vertexColor;\n"
            "void main(){gl_Position=camera*vec4(position,1.0);texcoord=uv;vertexColor=color;}\n";
        const std::string fragment_source =
            std::string("#version 120\n") + rendering::quality::pbr_shader +
            "uniform sampler2D image; uniform bool alphaTest; uniform bool linearOutput;\n"
            "varying vec2 texcoord; varying vec4 vertexColor;\n"
            // Source vertices contain MODULATE2X scaling; the fixed-function result clamps before blending.
            // Active C4.wdl sets d3d_alpharef=100, overriding the engine default of 127.
            "void main(){vec4 result=vertexColor*texture2D(image,texcoord);result.rgb=clamp(result.rgb,0.0,1.0);"
            "if(alphaTest && result.a<100.0/255.0)discard;"
            "if(linearOutput)result.rgb=c4_quality_srgb_to_linear(result.rgb);gl_FragColor=result;}\n";
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_source)};
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER, fragment_source.c_str())};
        resources_.program = gl_.glCreateProgram();
        if (!resources_.program) throw std::runtime_error("OpenGL could not allocate original effect program");
        gl_.glAttachShader(resources_.program, vertex.id);
        gl_.glAttachShader(resources_.program, fragment.id);
        gl_.glBindAttribLocation(resources_.program, 0, "position");
        gl_.glBindAttribLocation(resources_.program, 1, "uv");
        gl_.glBindAttribLocation(resources_.program, 2, "color");
        gl_.glLinkProgram(resources_.program);
        GLint linked = GL_FALSE;
        gl_.glGetProgramiv(resources_.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            std::array<char, 4096> message{};
            gl_.glGetProgramInfoLog(resources_.program, static_cast<GLsizei>(message.size()), nullptr, message.data());
            throw std::runtime_error("Original effect shader link: " + std::string(message.data()));
        }
        camera_location_ = gl_.glGetUniformLocation(resources_.program, "camera");
        alpha_test_location_ = gl_.glGetUniformLocation(resources_.program, "alphaTest");
        linear_output_location_ = gl_.glGetUniformLocation(resources_.program, "linearOutput");
        const GLint image_location = gl_.glGetUniformLocation(resources_.program, "image");
        if (camera_location_ < 0 || alpha_test_location_ < 0 || linear_output_location_ < 0 || image_location < 0) {
            throw std::runtime_error("Original effect shader uniform missing");
        }
        gl_.glUseProgram(resources_.program);
        gl_.glUniform1i(image_location, 0);
        resources_.buffers.resize(2);
        gl_.glGenBuffers(2, resources_.buffers.data());
        if (!resources_.buffers[0] || !resources_.buffers[1]) {
            throw std::runtime_error("OpenGL could not allocate original effect buffers");
        }
        gl_.glBindBuffer(GL_ARRAY_BUFFER, resources_.buffers[0]);
        gl_.glBufferData(GL_ARRAY_BUFFER, game::effect_vertex_capacity * sizeof(game::EffectVertex), nullptr, GL_DYNAMIC_DRAW);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources_.buffers[1]);
        gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER, game::effect_index_capacity * sizeof(std::uint32_t), nullptr, GL_DYNAMIC_DRAW);
    }

    OriginalEffectRenderer(const OriginalEffectRenderer&) = delete;
    OriginalEffectRenderer& operator=(const OriginalEffectRenderer&) = delete;
    OriginalEffectRenderer(OriginalEffectRenderer&&) = delete;
    OriginalEffectRenderer& operator=(OriginalEffectRenderer&&) = delete;

    void draw(const game::EffectFrame& frame, const rendering::CameraState& camera, bool linear_output = false) {
        for (const auto& batch : frame.batches) {
            if (batch.texture_index >= frame.textures.size() || batch.first_index > frame.indices.size() ||
                batch.index_count > frame.indices.size() - batch.first_index || batch.index_count % 3 != 0) {
                throw std::runtime_error("Original effect batch image or triangle range invalid");
            }
        }
        gl_.glActiveTexture(GL_TEXTURE0);
        upload_images(frame);
        update_sampler(frame);
        gl_.glBindBuffer(GL_ARRAY_BUFFER, resources_.buffers[0]);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources_.buffers[1]);
        upload_geometry(frame);
        if (frame.batches.empty()) return;
        gl_.glUseProgram(resources_.program);
        gl_.glUniformMatrix4fv(camera_location_, 1, GL_FALSE, camera.view_projection.data());
        gl_.glUniform1i(linear_output_location_, linear_output ? 1 : 0);
        gl_.glEnableVertexAttribArray(0);
        gl_.glEnableVertexAttribArray(1);
        gl_.glEnableVertexAttribArray(2);
        gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(game::EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(game::EffectVertex, position)));
        gl_.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(game::EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(game::EffectVertex, uv)));
        gl_.glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(game::EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(game::EffectVertex, color)));
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        struct RestoreState {
            InspectionGL& gl;
            ~RestoreState() { glPopAttrib(); gl.glDisableVertexAttribArray(2); }
        } restore{gl_};
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_CULL_FACE);
        // Original particle prep explicitly disables fog; source diffuse is already unlit.
        glDisable(GL_FOG);
        gl_.glBlendEquation(GL_FUNC_ADD);
        for (const auto& batch : frame.batches) {
            if (!batch.index_count) continue;
            // bright preserves source identity; additive is the resolved blend flag.
            if (batch.transparent || batch.additive) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, batch.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }
            glDepthMask(batch.depth_write ? GL_TRUE : GL_FALSE);
            gl_.glUniform1i(alpha_test_location_, batch.alpha_test ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, resources_.textures[batch.texture_index]);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.index_count), GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(static_cast<std::uintptr_t>(batch.first_index) * sizeof(std::uint32_t)));
            ++draw_calls_;
        }
    }

    std::uint64_t draw_calls() const noexcept { return draw_calls_; }
    std::int32_t source_anisotropy_raw() const noexcept { return source_anisotropy_raw_; }
    float sampler_anisotropy_level() const noexcept { return sampler_anisotropy_level_; }
    float sampler_anisotropy_maximum() const noexcept { return maximum_sampler_anisotropy_; }
    bool sampler_anisotropy_supported() const noexcept { return sampler_anisotropy_supported_; }
    std::string_view sampler_anisotropy_fallback_reason() const noexcept {
        return source_anisotropy_raw_ != 0 && !sampler_anisotropy_supported_
            ? "OpenGL anisotropic sampling unavailable or invalid maximum; original capability-guarded linear filtering"
            : "";
    }
};

} // namespace
} // namespace c4
