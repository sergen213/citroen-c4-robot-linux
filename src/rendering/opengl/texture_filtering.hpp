#pragma once

#include "assets/GpuImage.hpp"
#include "rendering/opengl/gl.hpp"
#include "rendering/quality/GraphicSettings.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <span>
#include <string_view>

namespace c4 {
namespace {

// The source spans must be finalized before construction and outlive this cache.
// Like InspectionResources, destruction requires its owning GL context current.
class EnhancedTextureFiltering {
    using ResolvedGraphics = rendering::quality::ResolvedGraphics;
    using Filtering = rendering::quality::TextureFiltering;
    InspectionGL& gl_;
    std::span<const assets::GpuImage> images_;
    std::span<const GLuint> originals_;
    struct CachedTexture { GLuint id{}; float anisotropy{1.f}; bool source_compressed{}; };
    std::vector<CachedTexture> enhanced_;
    SDL_GLContext context_;
    PFNGLGENERATEMIPMAPEXTPROC generate_{};
    GLuint framebuffer_{};
    bool trilinear_{};
    float max_anisotropy_{1.f};
    struct ResolveProgram {
        InspectionGL& gl;
        GLuint id{};
        GLint source{-1}, lod{-1}, size{-1}, face{-1};
        ~ResolveProgram() { if (id) gl.glDeleteProgram(id); }
    };
    std::array<ResolveProgram, 2> resolve_{{{gl_}, {gl_}}};
    struct ResolveSampler {
        PFNGLGENSAMPLERSPROC generate{};
        PFNGLDELETESAMPLERSPROC destroy{};
        PFNGLBINDSAMPLERPROC bind{};
        PFNGLSAMPLERPARAMETERIPROC parameter{};
        GLuint id{};
        ~ResolveSampler() { if (id) destroy(1, &id); }
    } sampler_;

    static void check(const char* operation) {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(error));
    }
    void require_context() const {
        if (!context_ || SDL_GL_GetCurrentContext() != context_)
            throw std::runtime_error("Enhanced texture cache requires its owning OpenGL context");
    }
    struct SavedState {
        InspectionGL& gl;
        ResolveSampler& sampler_api;
        GLint framebuffer{}, active{}, texture_2d{}, texture_cube{}, unpack_buffer{}, pack_buffer{};
        GLint read_buffer{}, draw_buffer{}, program{}, sampler{};
        bool restored{};
        SavedState(InspectionGL& api, ResolveSampler& samplers) : gl(api), sampler_api(samplers) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_2d);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &texture_cube);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
            glGetIntegerv(GL_READ_BUFFER, &read_buffer);
            glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            if (sampler_api.bind) glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
            check("Capture texture filtering state");
            // Compatibility-profile stacks preserve every raster/current-vertex
            // attribute we change, and the complete pack/unpack pixel-store state.
            glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT |
                         GL_POLYGON_BIT | GL_SCISSOR_BIT | GL_VIEWPORT_BIT | GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Could not save texture resolve raster state");
            glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
            if (glGetError() != GL_NO_ERROR) {
                glPopAttrib();
                throw std::runtime_error("Could not save texture resolve pixel state");
            }
        }
        void restore() noexcept {
            if (restored) return;
            gl.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(framebuffer));
            glPopClientAttrib();
            glPopAttrib();
            glReadBuffer(static_cast<GLenum>(read_buffer));
            glDrawBuffer(static_cast<GLenum>(draw_buffer));
            gl.glUseProgram(static_cast<GLuint>(program));
            gl.glActiveTexture(static_cast<GLenum>(active));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_2d));
            glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(texture_cube));
            if (sampler_api.bind)
                sampler_api.bind(static_cast<GLuint>(active - GL_TEXTURE0), static_cast<GLuint>(sampler));
            gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer));
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pack_buffer));
            restored = true;
        }
        ~SavedState() { restore(); }
    };
    struct Scratch {
        InspectionGL& gl;
        std::array<GLuint, 4> textures{};
        GLuint framebuffer{};
        ~Scratch() {
            glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            if (framebuffer) gl.glDeleteFramebuffersEXT(1, &framebuffer);
        }
    };
    struct PendingTexture {
        GLuint id{};
        ~PendingTexture() { if (id) glDeleteTextures(1, &id); }
    };
    static GLenum face_target(GLenum target, unsigned face) {
        return target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
    }
    static unsigned last_level(unsigned width, unsigned height) {
        unsigned level{};
        while (width > 1 || height > 1) {
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            ++level;
        }
        return level;
    }
    static bool compressed(assets::GpuImageFormat format) {
        return format == assets::GpuImageFormat::Bc1Unorm ||
               format == assets::GpuImageFormat::Bc2Unorm ||
               format == assets::GpuImageFormat::Bc3Unorm;
    }
    void allocate_resolve_chain(GLuint destination, GLenum target, unsigned faces,
                                unsigned width, unsigned height) {
        glBindTexture(target, destination);
        gl_.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        const unsigned last = last_level(width, height);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(last));
        // Non-base framebuffer attachments require a complete mutable mip chain.
        // Allocate every face/level before resolving any authored image into it.
        for (unsigned level = 0; level <= last; ++level) {
            for (unsigned face = 0; face < faces; ++face)
                glTexImage2D(face_target(target, face), static_cast<GLint>(level), GL_RGBA8,
                             static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        check("Allocate enhanced texture resolve mip chain");
    }
    bool initialize_resolve_sampler() {
        if (!extension("GL_ARB_shader_texture_lod") || !extension("GL_ARB_sampler_objects")) return false;
        sampler_.generate = reinterpret_cast<PFNGLGENSAMPLERSPROC>(SDL_GL_GetProcAddress("glGenSamplers"));
        sampler_.destroy = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(SDL_GL_GetProcAddress("glDeleteSamplers"));
        sampler_.parameter = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(SDL_GL_GetProcAddress("glSamplerParameteri"));
        const auto bind = reinterpret_cast<PFNGLBINDSAMPLERPROC>(SDL_GL_GetProcAddress("glBindSampler"));
        if (!sampler_.generate || !sampler_.destroy || !sampler_.parameter || !bind) return false;
        sampler_.bind = bind;
        return true;
    }
    void create_resolve_program(unsigned kind) {
        auto& program = resolve_[kind];
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER,
            "#version 120\nvoid main() { gl_Position = gl_Vertex; }\n")};
        const char* fragment_2d = R"(#version 120
#extension GL_ARB_shader_texture_lod : require
uniform sampler2D source;
uniform float lod;
uniform vec2 size;
void main() { gl_FragColor = texture2DLod(source, gl_FragCoord.xy / size, lod); }
)";
        const char* fragment_cube = R"(#version 120
#extension GL_ARB_shader_texture_lod : require
uniform samplerCube source;
uniform float lod;
uniform vec2 size;
uniform int face;
void main() {
    vec2 p = 2.0 * gl_FragCoord.xy / size - 1.0;
    vec3 direction;
    if (face == 0) direction = vec3(1.0, -p.y, -p.x);
    else if (face == 1) direction = vec3(-1.0, -p.y, p.x);
    else if (face == 2) direction = vec3(p.x, 1.0, p.y);
    else if (face == 3) direction = vec3(p.x, -1.0, -p.y);
    else if (face == 4) direction = vec3(p.x, -p.y, 1.0);
    else direction = vec3(-p.x, -p.y, -1.0);
    gl_FragColor = textureCubeLod(source, direction, lod);
}
)";
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER,
                                                                 kind ? fragment_cube : fragment_2d)};
        program.id = gl_.glCreateProgram();
        if (!program.id) throw std::runtime_error("Could not allocate texture resolve program");
        gl_.glAttachShader(program.id, vertex.id);
        gl_.glAttachShader(program.id, fragment.id);
        gl_.glLinkProgram(program.id);
        GLint linked{};
        gl_.glGetProgramiv(program.id, GL_LINK_STATUS, &linked);
        check("Link texture resolve program");
        if (!linked) throw std::runtime_error("Texture resolve program linking failed");
        program.source = gl_.glGetUniformLocation(program.id, "source");
        program.lod = gl_.glGetUniformLocation(program.id, "lod");
        program.size = gl_.glGetUniformLocation(program.id, "size");
        if (kind) program.face = gl_.glGetUniformLocation(program.id, "face");
        check("Locate texture resolve uniforms");
        if (program.source < 0 || program.lod < 0 || program.size < 0 || (kind && program.face < 0))
            throw std::runtime_error("Texture resolve program uniforms unavailable");
    }
    void prepare_resolve_raster() {
        for (GLenum capability : {GL_ALPHA_TEST, GL_BLEND, GL_COLOR_LOGIC_OP, GL_CULL_FACE,
                                  GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_DITHER,
                                  GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE,
                                  GL_SAMPLE_COVERAGE, GL_POLYGON_OFFSET_FILL, GL_POLYGON_STIPPLE})
            glDisable(capability);
        GLint clip_planes{};
        glGetIntegerv(GL_MAX_CLIP_PLANES, &clip_planes);
        for (GLint plane = 0; plane < clip_planes; ++plane) glDisable(GL_CLIP_PLANE0 + plane);
        if (extension("GL_EXT_framebuffer_sRGB") || extension("GL_ARB_framebuffer_sRGB"))
            glDisable(GL_FRAMEBUFFER_SRGB);
        if (extension("GL_EXT_transform_feedback") || extension("GL_NV_transform_feedback") ||
            extension("GL_ARB_transform_feedback2")) glDisable(GL_RASTERIZER_DISCARD);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        check("Prepare texture resolve raster");
    }
    void resolve_level(GLuint source, GLuint destination, GLenum target, unsigned faces,
                       unsigned level, unsigned width, unsigned height) {
        const unsigned kind = target == GL_TEXTURE_CUBE_MAP ? 1 : 0;
        const auto& program = resolve_[kind];
        if (!program.id || !sampler_.id) throw std::runtime_error("Compressed texture resolve unavailable");
        // Use the caller's active unit; unit zero and its bindings stay untouched.
        GLint active{};
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        gl_.glUseProgram(program.id);
        sampler_.bind(static_cast<GLuint>(active - GL_TEXTURE0), sampler_.id);
        gl_.glUniform1i(program.source, active - GL_TEXTURE0);
        gl_.glUniform1f(program.lod, static_cast<GLfloat>(level));
        const std::array<GLfloat, 2> size{static_cast<GLfloat>(width), static_cast<GLfloat>(height)};
        gl_.glUniform2fv(program.size, 1, size.data());
        prepare_resolve_raster();
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        for (unsigned face = 0; face < faces; ++face) {
            const GLenum image_target = face_target(target, face);
            gl_.glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         image_target, destination, static_cast<GLint>(level));
            check("Attach enhanced texture mip for shader resolve");
            const GLenum status = gl_.glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
            check("Check enhanced texture resolve framebuffer");
            if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
                throw std::runtime_error("Enhanced texture resolve framebuffer incomplete: " +
                    std::to_string(status) + ", mip " + std::to_string(level) +
                    ", face " + std::to_string(face));
            glBindTexture(target, source);
            if (kind) gl_.glUniform1i(program.face, static_cast<GLint>(face));
            glBegin(GL_TRIANGLES);
            glVertex2f(-1.f, -1.f); glVertex2f(3.f, -1.f); glVertex2f(-1.f, 3.f);
            glEnd();
            check("Resolve original compressed texture mip on GPU");
        }
        // The creation caller generates/verifies the currently bound destination.
        glBindTexture(target, destination);
    }
    void copy_level(GLuint source, GLuint destination, GLenum target, unsigned faces,
                    unsigned level, unsigned width, unsigned height, assets::GpuImageFormat format) {
        glBindTexture(target, source);
        for (unsigned face = 0; face < faces; ++face) {
            GLint actual_width{}, actual_height{};
            const GLenum image_target = face_target(target, face);
            glGetTexLevelParameteriv(image_target, static_cast<GLint>(level), GL_TEXTURE_WIDTH, &actual_width);
            glGetTexLevelParameteriv(image_target, static_cast<GLint>(level), GL_TEXTURE_HEIGHT, &actual_height);
            check("Inspect original GPU mip dimensions");
            if (actual_width != static_cast<GLint>(width) || actual_height != static_cast<GLint>(height))
                throw std::runtime_error("Original GPU mip dimensions disagree with source descriptor");
        }
        if (compressed(format)) {
            resolve_level(source, destination, target, faces, level, width, height);
            return;
        }
        glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
        for (GLenum scale : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE}) glPixelTransferf(scale, 1.f);
        for (GLenum bias : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS}) glPixelTransferf(bias, 0.f);
        glBindTexture(target, destination);
        for (unsigned face = 0; face < faces; ++face) {
            const GLenum image_target = face_target(target, face);
            gl_.glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         image_target, source, static_cast<GLint>(level));
            check("Attach original texture mip for GPU copy");
            const GLenum status = gl_.glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
            check("Check original texture mip framebuffer");
            if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
                throw std::runtime_error("Texture mip copy framebuffer incomplete: " + std::to_string(status));
            glCopyTexImage2D(image_target, static_cast<GLint>(level), GL_RGBA8, 0, 0,
                             static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0);
            check("GPU copy original texture mip");
        }
    }
    static void verify_chain(GLenum target, unsigned faces, unsigned width, unsigned height) {
        const unsigned last = last_level(width, height);
        for (unsigned level = 0; level <= last; ++level) {
            for (unsigned face = 0; face < faces; ++face) {
                GLint actual_width{}, actual_height{}, format{};
                const GLenum image_target = face_target(target, face);
                glGetTexLevelParameteriv(image_target, static_cast<GLint>(level), GL_TEXTURE_WIDTH, &actual_width);
                glGetTexLevelParameteriv(image_target, static_cast<GLint>(level), GL_TEXTURE_HEIGHT, &actual_height);
                glGetTexLevelParameteriv(image_target, static_cast<GLint>(level), GL_TEXTURE_INTERNAL_FORMAT, &format);
                check("Inspect enhanced texture mip chain");
                if (actual_width != static_cast<GLint>(width) || actual_height != static_cast<GLint>(height) || format != GL_RGBA8)
                    throw std::runtime_error("Enhanced texture mip chain has incorrect dimensions or format");
            }
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
    }
    static void trilinear_sampler(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        check("Set enhanced trilinear sampler");
    }
    static bool extension(std::string_view name) {
        const auto* raw = glGetString(GL_EXTENSIONS);
        check("Query texture filtering extension");
        if (!raw) return false;
        std::string_view extensions(reinterpret_cast<const char*>(raw));
        const auto position = extensions.find(name);
        return position != std::string_view::npos && (position == 0 || extensions[position - 1] == ' ') &&
            (position + name.size() == extensions.size() || extensions[position + name.size()] == ' ');
    }
    void probe_compressed(GLuint source, unsigned kind, assets::GpuImageFormat format) {
        const GLenum target = kind ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        const unsigned faces = kind ? 6 : 1;
        const GLenum internal = format == assets::GpuImageFormat::Bc1Unorm ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
            format == assets::GpuImageFormat::Bc2Unorm ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        const unsigned block_size = format == assets::GpuImageFormat::Bc1Unorm ? 8 : 16;
        constexpr std::array<unsigned, 6> colors{0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f};
        constexpr std::array<unsigned, 6> channels{1, 2, 4, 3, 6, 5};
        glBindTexture(target, source);
        // Deliberately non-mipmapped: isolated sampler must still select authored LODs.
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 2);
        for (unsigned level = 0; level <= 2; ++level) {
            for (unsigned face = 0; face < faces; ++face) {
                std::array<GLubyte, 16> block{};
                const unsigned offset = block_size == 8 ? 0 : 8;
                if (format == assets::GpuImageFormat::Bc2Unorm)
                    std::fill_n(block.begin(), 8, GLubyte{255});
                else if (format == assets::GpuImageFormat::Bc3Unorm) block[0] = block[1] = 255;
                const unsigned color = colors[(level + face) % colors.size()];
                block[offset] = static_cast<GLubyte>(color);
                block[offset + 1] = static_cast<GLubyte>(color >> 8);
                block[offset + 2] = block[offset + 3] = 255;
                // Non-symmetric pattern checks cube face orientation and pixel centers.
                unsigned indices{};
                for (unsigned y = 0; y < 4; ++y)
                    for (unsigned x = 0; x < 4; ++x)
                        if (x == 1 || y == 2) indices |= 1u << (2 * (y * 4 + x));
                for (unsigned byte = 0; byte < 4; ++byte)
                    block[offset + 4 + byte] = static_cast<GLubyte>(indices >> (8 * byte));
                gl_.glCompressedTexImage2D(face_target(target, face), static_cast<GLint>(level), internal,
                                           4 >> level, 4 >> level, 0, static_cast<GLsizei>(block_size), block.data());
                check("Allocate synthetic compressed scratch mip");
            }
        }
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 1); // Probe a partial authored chain first.
        // A fresh destination catches allocation/completeness failures concealed
        // by the preceding uncompressed probe's already populated mip chain.
        PendingTexture resolved;
        glGenTextures(1, &resolved.id);
        check("Allocate compressed resolve probe destination");
        if (!resolved.id) throw std::runtime_error("Could not allocate compressed resolve probe destination");
        const GLuint destination = resolved.id;
        allocate_resolve_chain(destination, target, faces, 4, 4);
        copy_level(source, destination, target, faces, 0, 4, 4, format);
        generate_(target);
        check("Generate shader-resolved scratch mip chain");
        copy_level(source, destination, target, faces, 1, 2, 2, format);
        glBindTexture(target, source);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 2);
        copy_level(source, destination, target, faces, 2, 1, 1, format);
        verify_chain(target, faces, 4, 4);
        gl_.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
        for (GLenum scale : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE}) glPixelTransferf(scale, 1.f);
        for (GLenum bias : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS}) glPixelTransferf(bias, 0.f);
        for (unsigned level = 0; level <= 2; ++level) {
            for (unsigned face = 0; face < faces; ++face) {
                gl_.glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                             face_target(target, face), destination, static_cast<GLint>(level));
                std::array<GLubyte, 64> pixels{};
                const unsigned dimension = 4 >> level;
                glReadPixels(0, 0, static_cast<GLsizei>(dimension), static_cast<GLsizei>(dimension),
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                check("Read shader-resolved scratch mip");
                for (unsigned pixel = 0; pixel < dimension * dimension; ++pixel)
                    for (unsigned channel = 0; channel < 4; ++channel)
                        if (pixels[pixel * 4 + channel] !=
                            (channel == 3 || pixel % dimension == 1 || pixel / dimension == 2 ||
                             (channels[(level + face) % colors.size()] & (1u << channel)) != 0 ? 255 : 0))
                            throw std::runtime_error("Compressed scratch resolve did not preserve authored face/LOD");
            }
        }
        glBindTexture(target, destination);
    }
    void probe() {
        // A pre-existing renderer error is fatal, not an unsupported-capability fallback.
        check("Before enhanced texture capability probe");
        if (!generate_) return;
        std::array<std::array<bool, 3>, 2> needed{};
        {
            SavedState saved(gl_, sampler_);
            for (std::size_t index = 0; index < images_.size(); ++index) {
                const auto& image = images_[index];
                if (!originals_[index] || !compressed(image.format)) continue;
                const GLenum target = image.face_count == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glBindTexture(target, originals_[index]);
                GLint actual_compressed{};
                glGetTexLevelParameteriv(face_target(target, 0), 0, GL_TEXTURE_COMPRESSED, &actual_compressed);
                check("Inspect actual original compression for enhanced resolve");
                enhanced_[index].source_compressed = actual_compressed == GL_TRUE;
                if (!enhanced_[index].source_compressed) continue;
                const unsigned format = image.format == assets::GpuImageFormat::Bc1Unorm ? 0 :
                    image.format == assets::GpuImageFormat::Bc2Unorm ? 1 : 2;
                needed[image.face_count == 6 ? 1 : 0][format] = true;
            }
        }
        check("Restore original compression inspection state");
        const bool compressed_needed = std::any_of(needed.begin(), needed.end(), [](const auto& formats) {
            return std::any_of(formats.begin(), formats.end(), [](bool value) { return value; });
        });
        if (compressed_needed && !initialize_resolve_sampler()) return;
        SavedState saved(gl_, sampler_);
        Scratch scratch{gl_};
        try {
            gl_.glGenFramebuffersEXT(1, &scratch.framebuffer);
            glGenTextures(static_cast<GLsizei>(scratch.textures.size()), scratch.textures.data());
            check("Allocate texture capability probe");
            if (!scratch.framebuffer || std::any_of(scratch.textures.begin(), scratch.textures.end(),
                                                    [](GLuint id) { return id == 0; })) return;
            gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, scratch.framebuffer);
            glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
            gl_.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
            check("Bind texture capability probe");
            if (compressed_needed) {
                sampler_.generate(1, &sampler_.id);
                if (!sampler_.id) throw std::runtime_error("Could not allocate isolated texture resolve sampler");
                sampler_.parameter(sampler_.id, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
                sampler_.parameter(sampler_.id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
                    sampler_.parameter(sampler_.id, wrap, GL_CLAMP_TO_EDGE);
                check("Initialize isolated texture resolve sampler");
            }
            for (unsigned kind = 0; kind < 2; ++kind) {
                const GLenum target = kind ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                const unsigned faces = kind ? 6 : 1;
                const GLuint source = scratch.textures[kind * 2], destination = scratch.textures[kind * 2 + 1];
                glBindTexture(target, source);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 2);
                // Scratch storage only: no scene image is decoded or uploaded again.
                for (unsigned level = 0; level <= 2; ++level)
                    for (unsigned face = 0; face < faces; ++face)
                        glTexImage2D(face_target(target, face), static_cast<GLint>(level), GL_RGBA8,
                                     4 >> level, 4 >> level, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                check("Allocate scratch texture mip storage");
                glBindTexture(target, destination);
                glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 2);
                copy_level(source, destination, target, faces, 0, 4, 4, assets::GpuImageFormat::Rgba8Unorm);
                generate_(target);
                check("Generate scratch texture mips");
                verify_chain(target, faces, 4, 4);
                for (unsigned level = 1; level <= 2; ++level)
                    copy_level(source, destination, target, faces, level, 4 >> level, 4 >> level,
                               assets::GpuImageFormat::Rgba8Unorm);
                verify_chain(target, faces, 4, 4);
                trilinear_sampler(target);
                GLint filter{};
                glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &filter);
                check("Verify scratch trilinear sampler");
                if (filter != GL_LINEAR_MIPMAP_LINEAR) return;
                if (std::any_of(needed[kind].begin(), needed[kind].end(), [](bool value) { return value; })) {
                    create_resolve_program(kind);
                    for (unsigned format = 0; format < needed[kind].size(); ++format)
                        if (needed[kind][format])
                            probe_compressed(source, kind,
                                format == 0 ? assets::GpuImageFormat::Bc1Unorm :
                                format == 1 ? assets::GpuImageFormat::Bc2Unorm : assets::GpuImageFormat::Bc3Unorm);
                }
            }
            trilinear_ = true;
        } catch (const std::runtime_error&) {
            while (glGetError() != GL_NO_ERROR) {} // Drain only errors from our scratch operations.
            return; // Only operations on our scratch resources are allowed to fail here.
        }
        try {
            if (!extension("GL_EXT_texture_filter_anisotropic")) return;
            GLfloat maximum{};
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
            check("Query driver anisotropy limit");
            if (!std::isfinite(maximum) || maximum <= 1.f) return;
            for (unsigned kind = 0; kind < 2; ++kind) {
                const GLenum target = kind ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glBindTexture(target, scratch.textures[kind * 2 + 1]);
                glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, maximum);
                check("Probe actual anisotropy setter");
                GLfloat applied{};
                glGetTexParameterfv(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, &applied);
                check("Verify actual anisotropy setter");
                if (!std::isfinite(applied) || applied != maximum) return;
                glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.f);
                check("Reset scratch anisotropy");
            }
            max_anisotropy_ = maximum;
        } catch (const std::runtime_error&) {
            // Trilinear remains independently supported; anisotropy remains 1.
            while (glGetError() != GL_NO_ERROR) {} // Scratch anisotropy failures cannot poison restored caller state.
        }
    }

public:
    EnhancedTextureFiltering(InspectionGL& gl, std::span<const assets::GpuImage> images,
                             std::span<const GLuint> original_textures)
        : gl_(gl), images_(images), originals_(original_textures), enhanced_(images.size()),
          context_(SDL_GL_GetCurrentContext()),
          generate_(reinterpret_cast<PFNGLGENERATEMIPMAPEXTPROC>(SDL_GL_GetProcAddress("glGenerateMipmapEXT"))) {
        require_context();
        if (images.size() != original_textures.size())
            throw std::runtime_error("Enhanced texture image/handle span mismatch");
        probe();
        check("Restore texture capability probe state");
    }
    EnhancedTextureFiltering(const EnhancedTextureFiltering&) = delete;
    EnhancedTextureFiltering& operator=(const EnhancedTextureFiltering&) = delete;
    ~EnhancedTextureFiltering() {
        if (SDL_GL_GetCurrentContext() != context_) std::terminate();
        if (framebuffer_) gl_.glDeleteFramebuffersEXT(1, &framebuffer_);
        for (const auto& texture : enhanced_)
            if (texture.id) glDeleteTextures(1, &texture.id);
    }
    bool trilinear_support() const noexcept { return trilinear_; }
    float max_anisotropy() const noexcept { return max_anisotropy_; }

    GLuint texture(std::size_t index, const ResolvedGraphics& graphics) {
        if (index >= images_.size()) throw std::runtime_error("Enhanced texture index out of range");
        if (!originals_[index]) throw std::runtime_error("Selected original texture has no GPU handle");
        if (graphics.texture_filtering == Filtering::Original) return originals_[index];
        if (!trilinear_) throw std::runtime_error("Resolved enhanced texture filtering is unsupported");
        if (enhanced_[index].id) return enhanced_[index].id;
        require_context();
        check("Before enhanced texture creation");
        const auto& image = images_[index];
        if (!image.width || !image.height ||
            (image.face_count != 1 && image.face_count != 6) ||
            (image.face_count == 6 && image.width != image.height) || image.levels.empty() ||
            image.levels.front().width != image.width || image.levels.front().height != image.height)
            throw std::runtime_error("Invalid original image for enhanced GPU texture");
        const unsigned last = last_level(image.width, image.height);
        if (image.levels.size() > last + 1) throw std::runtime_error("Original texture has excess mip levels");
        unsigned width = image.width, height = image.height;
        for (std::size_t level = 1; level < image.levels.size(); ++level) {
            const auto& mip = image.levels[level];
            width = std::max(1u, width / 2); height = std::max(1u, height / 2);
            if (mip.width != width || mip.height != height)
                throw std::runtime_error("Original texture mip dimensions are inconsistent");
        }
        SavedState saved(gl_, sampler_);
        if (enhanced_[index].source_compressed) {
            const GLenum source_target = image.face_count == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            glBindTexture(source_target, originals_[index]);
            GLint base{}, maximum{};
            glGetTexParameteriv(source_target, GL_TEXTURE_BASE_LEVEL, &base);
            glGetTexParameteriv(source_target, GL_TEXTURE_MAX_LEVEL, &maximum);
            check("Inspect compressed source authored mip range");
            if (base != 0 || maximum != static_cast<GLint>(image.levels.size() - 1))
                throw std::runtime_error("Compressed source mip range disagrees with authored levels");
        }
        PendingTexture destination;
        if (!framebuffer_) gl_.glGenFramebuffersEXT(1, &framebuffer_);
        glGenTextures(1, &destination.id);
        check("Allocate enhanced texture copy resources");
        if (!framebuffer_ || !destination.id) throw std::runtime_error("OpenGL could not allocate enhanced texture copy");
        const GLenum target = image.face_count == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer_);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glBindTexture(target, destination.id);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(last));
        glTexParameteri(target, GL_TEXTURE_WRAP_S, image.face_count == 6 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, image.face_count == 6 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        if (image.face_count == 6) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        check("Initialize enhanced texture copy");
        const auto format = enhanced_[index].source_compressed ? image.format : assets::GpuImageFormat::Rgba8Unorm;
        if (compressed(format))
            allocate_resolve_chain(destination.id, target, image.face_count, image.width, image.height);
        copy_level(originals_[index], destination.id, target, image.face_count, 0, image.width, image.height, format);
        if (image.levels.size() < last + 1) {
            generate_(target); // Never bind or generate on originals_[index].
            check("Generate missing enhanced texture mip chain");
        }
        // Generation can overwrite authored lower mips: copy every stored source
        // mip back afterwards. Full authored chains never undergo generation.
        for (std::size_t mip = 1; mip < image.levels.size(); ++mip) {
            const auto& level = image.levels[mip];
            copy_level(originals_[index], destination.id, target, image.face_count,
                       static_cast<unsigned>(mip), level.width, level.height, format);
        }
        verify_chain(target, image.face_count, image.width, image.height);
        trilinear_sampler(target);
        gl_.glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, 0, 0);
        check("Detach texture after enhanced GPU copy");
        saved.restore();
        check("Restore enhanced texture creation state");
        enhanced_[index].id = destination.id;
        destination.id = 0;
        return enhanced_[index].id;
    }
    // Parent binds texture(index, graphics) and applies only original address
    // setters to enhanced objects, never their original MIN/MAG filter setters.
    // Original mode deliberately makes no GL calls or mutations.
    void apply_sampler(std::size_t index, GLenum target, const ResolvedGraphics& graphics) {
        if (graphics.texture_filtering == Filtering::Original) return;
        if (index >= enhanced_.size() || !enhanced_[index].id ||
            target != (images_[index].face_count == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D))
            throw std::runtime_error("Enhanced sampler has no matching cached texture");
        if (graphics.texture_filtering == Filtering::Anisotropic &&
            (!std::isfinite(graphics.anisotropy) || graphics.anisotropy < 1.f || max_anisotropy_ <= 1.f))
            throw std::runtime_error("Resolved anisotropic texture filtering is unsupported");
        const float anisotropy = graphics.texture_filtering == Filtering::Anisotropic
            ? std::min(graphics.anisotropy, max_anisotropy_) : 1.f;
        auto& cached = enhanced_[index];
        if (cached.anisotropy == anisotropy) return;
        require_context();
        check("Before enhanced texture anisotropy change");
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
        check("Set enhanced texture anisotropy");
        cached.anisotropy = anisotropy;
    }
};

} // namespace
} // namespace c4
