#pragma once

#include "MaterialShaders.hpp"
#include "rendering/Material.hpp"
#include "rendering/opengl/gl.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace c4 {
namespace {

// Source order: world/effects -> Down/H/V/final -> panels -> display ramp.
// All calls and destruction require the owning OpenGL 2.1 context to be current.
class OriginalComposition {
    enum Target : std::size_t { Scene, Panels, Down, Horizontal, Vertical, TargetCount };
    enum Pass : std::size_t { DownPass, HorizontalPass, VerticalPass, CompositePass, GammaPass, PassCount };
    struct Program {
        GLuint id{};
        GLint image{-1}, auxiliary{-1}, threshold{-1}, width{-1}, highlight{-1}, scene{-1}, glow{-1};
    };
    struct Resources {
        InspectionGL& gl;
        std::array<GLuint, TargetCount> textures{}, framebuffers{};
        std::array<Program, PassCount> programs{};
        GLuint depth{}, ramp{};
        ~Resources() {
            for (const auto& program : programs) if (program.id) gl.glDeleteProgram(program.id);
            glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            if (ramp) glDeleteTextures(1, &ramp);
            gl.glDeleteFramebuffersEXT(static_cast<GLsizei>(framebuffers.size()), framebuffers.data());
            if (depth) gl.glDeleteRenderbuffersEXT(1, &depth);
        }
    };
    // Compatibility-profile attribute stacks preserve raster/texture state, while
    // program/FBO bindings are explicitly restored (they are not stack attributes).
    struct State {
        InspectionGL& gl;
        GLint program{}, framebuffer{}, renderbuffer{}, active_texture{}, unpack_alignment{};
        explicit State(InspectionGL& api) : gl(api) {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
            glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT, &renderbuffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
            glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT |
                         GL_VIEWPORT_BIT | GL_TEXTURE_BIT | GL_POLYGON_BIT);
            gl.glActiveTexture(GL_TEXTURE0);
        }
        ~State() {
            gl.glUseProgram(static_cast<GLuint>(program));
            gl.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(framebuffer));
            gl.glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, static_cast<GLuint>(renderbuffer));
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
            glPopAttrib();
            gl.glActiveTexture(static_cast<GLenum>(active_texture));
        }
    };
    InspectionGL& gl_;
    Resources resources_{gl_};
    std::array<std::array<int, 2>, TargetCount> sizes_{};
    std::array<std::array<std::uint16_t, 256>, 3> uploaded_ramp_{};
    rendering::OriginalBloomState bloom_{};
    const rendering::OriginalGammaState* gamma_{}; // Borrowed, stable through finish().
    std::array<GLint, 4> output_viewport_{};
    GLint output_framebuffer_{};
    GLint maximum_texture_{};
    int width_{}, height_{};
    bool active_{}, world_composed_{}, ramp_uploaded_{};
    std::uint64_t source_bloom_passes_{}, gamma_passes_{};

    GLint uniform(GLuint program, const char* name) {
        const GLint location = gl_.glGetUniformLocation(program, name);
        if (location < 0) throw std::runtime_error(std::string("Original composition uniform missing: ") + name);
        return location;
    }

    void link(Pass pass, GLuint vertex, const std::string& source) {
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER, source.c_str())};
        auto& program = resources_.programs[pass];
        program.id = gl_.glCreateProgram();
        if (!program.id) throw std::runtime_error("OpenGL could not allocate original composition program");
        gl_.glAttachShader(program.id, vertex);
        gl_.glAttachShader(program.id, fragment.id);
        gl_.glLinkProgram(program.id);
        GLint linked = GL_FALSE;
        gl_.glGetProgramiv(program.id, GL_LINK_STATUS, &linked);
        if (!linked) {
            std::array<char, 4096> message{};
            gl_.glGetProgramInfoLog(program.id, static_cast<GLsizei>(message.size()), nullptr, message.data());
            throw std::runtime_error("Original composition shader link: " + std::string(message.data()));
        }
        program.image = uniform(program.id, "image");
        gl_.glUseProgram(program.id);
        gl_.glUniform1i(program.image, 0);
    }

    void initialize_bloom() {
        if (resources_.programs[CompositePass].id) return;
        const std::string helpers = std::string("#version 120\n") + rendering::bloom_shader;
        const std::string vertex_source = helpers +
            "\nvarying vec2 uv; void main(){uv=gl_MultiTexCoord0.xy;gl_Position=c4BloomClipPosition(uv);}\n";
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_source.c_str())};
        const std::string sampling =
            "\nuniform sampler2D image; varying vec2 uv;\n"
            "vec4 sampleImage(vec2 p){return texture2D(image,vec2(p.x,1.0-p.y));}\n";
        link(DownPass, vertex.id, helpers + sampling +
            "uniform float threshold; void main(){gl_FragColor=c4BloomDown("
            "sampleImage(c4BloomDownCoordinate(uv,0)),sampleImage(c4BloomDownCoordinate(uv,1)),"
            "sampleImage(c4BloomDownCoordinate(uv,2)),sampleImage(c4BloomDownCoordinate(uv,3)),threshold);}\n");
        resources_.programs[DownPass].threshold = uniform(resources_.programs[DownPass].id, "threshold");
        link(HorizontalPass, vertex.id, helpers + sampling +
            "uniform float blurWidth; void main(){vec4 color=vec4(0.0);"
            "for(int i=0;i<=20;++i)color+=c4BloomTap(sampleImage(c4BloomHCoordinate(uv,i,blurWidth)));"
            "gl_FragColor=color;}\n");
        link(VerticalPass, vertex.id, helpers + sampling +
            "uniform float blurWidth; void main(){vec4 color=vec4(0.0);"
            "for(int i=0;i<=20;++i)color+=c4BloomTap(sampleImage(c4BloomVCoordinate(uv,i,blurWidth)));"
            "gl_FragColor=color;}\n");
        for (const auto pass : {HorizontalPass, VerticalPass}) {
            resources_.programs[pass].width = uniform(resources_.programs[pass].id, "blurWidth");
        }
        link(CompositePass, vertex.id, helpers + sampling +
            "uniform sampler2D blurImage; uniform float highlight,sceneIntensity,glow;"
            "void main(){gl_FragColor=c4BloomComposite(sampleImage(uv),"
            "texture2D(blurImage,vec2(uv.x,1.0-uv.y)),highlight,sceneIntensity,glow);}\n");
        auto& final = resources_.programs[CompositePass];
        final.auxiliary = uniform(final.id, "blurImage");
        final.highlight = uniform(final.id, "highlight");
        final.scene = uniform(final.id, "sceneIntensity");
        final.glow = uniform(final.id, "glow");
        gl_.glUseProgram(final.id);
        gl_.glUniform1i(final.auxiliary, 1);
    }

    void initialize_gamma() {
        if (resources_.programs[GammaPass].id) return;
        // The final display transfer has no bloom quad enlargement and no
        // exponent. RGB16 normalized samples are the source uint16 / 65535.
        const char* vertex_source =
            "#version 120\nvarying vec2 uv;void main(){uv=gl_MultiTexCoord0.xy;"
            "gl_Position=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,0.0,1.0);}\n";
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_source)};
        link(GammaPass, vertex.id,
            "#version 120\nuniform sampler2D image,ramp;varying vec2 uv;"
            "void main(){vec4 color=texture2D(image,vec2(uv.x,1.0-uv.y));"
            "vec3 index=floor(clamp(color.rgb,0.0,1.0)*255.0+0.5);"
            "gl_FragColor=vec4(texture2D(ramp,vec2((index.r+0.5)/256.0,0.5)).r,"
            "texture2D(ramp,vec2((index.g+0.5)/256.0,0.5)).g,"
            "texture2D(ramp,vec2((index.b+0.5)/256.0,0.5)).b,color.a);}\n");
        auto& program = resources_.programs[GammaPass];
        program.auxiliary = uniform(program.id, "ramp");
        gl_.glUniform1i(program.auxiliary, 1);
    }

    void resize_target(Target target, int width, int height) {
        if (sizes_[target] == std::array<int, 2>{width, height}) return;
        auto& texture = resources_.textures[target];
        auto& framebuffer = resources_.framebuffers[target];
        if (!texture) glGenTextures(1, &texture);
        if (!framebuffer) gl_.glGenFramebuffersEXT(1, &framebuffer);
        if (!texture || !framebuffer) throw std::runtime_error("OpenGL could not allocate original composition target");
        glBindTexture(GL_TEXTURE_2D, texture);
        const GLint filter = target == Panels ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Original bmap_createblack(...,32) is established, but its exact engine
        // attachment format and rounding are not. RGBA8 is this native backend's
        // explicit storage choice, not a claim of recovered format parity or HDR.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
        gl_.glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        if (target == Scene) {
            if (!resources_.depth) gl_.glGenRenderbuffersEXT(1, &resources_.depth);
            if (!resources_.depth) throw std::runtime_error("OpenGL could not allocate original scene depth");
            gl_.glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, resources_.depth);
            gl_.glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, width, height);
            gl_.glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                                            GL_RENDERBUFFER_EXT, resources_.depth);
        } else if (target == Panels) {
            // Full-resolution HUD views reuse the scene depth allocation. Their
            // source layer transitions clear it before indexed needle/fill draws;
            // bloom has already consumed the world, so its depth is no longer used.
            gl_.glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                                            GL_RENDERBUFFER_EXT, resources_.depth);
        }
        if (gl_.glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
            throw std::runtime_error("Original composition framebuffer incomplete");
        }
        sizes_[target] = {width, height};
    }

    void upload_gamma() {
        const auto& ramp = gamma_->corrected_active ? gamma_->corrected : gamma_->defaults;
        if (ramp_uploaded_ && ramp == uploaded_ramp_) return;
        std::array<std::uint16_t, 256 * 3> pixels{};
        for (std::size_t i = 0; i < 256; ++i) {
            for (std::size_t channel = 0; channel < 3; ++channel) pixels[i * 3 + channel] = ramp[channel][i];
        }
        if (!resources_.ramp) {
            glGenTextures(1, &resources_.ramp);
            if (!resources_.ramp) throw std::runtime_error("OpenGL could not allocate original gamma ramp");
            glBindTexture(GL_TEXTURE_2D, resources_.ramp);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, 256, 1, 0, GL_RGB, GL_UNSIGNED_SHORT, pixels.data());
        } else {
            glBindTexture(GL_TEXTURE_2D, resources_.ramp);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_SHORT, pixels.data());
        }
        uploaded_ramp_ = ramp;
        ramp_uploaded_ = true;
    }

    static void quad() {
        // Source helpers take top-left UVs. FBO texture sampling flips Y only at
        // the sampler boundary, retaining the original positive-Y H tap order.
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(-1, 1);
        glTexCoord2f(0, 1); glVertex2f(-1, -1);
        glTexCoord2f(1, 0); glVertex2f(1, 1);
        glTexCoord2f(1, 1); glVertex2f(1, -1);
        glEnd();
    }

    void draw(Pass pass, GLuint input, GLuint output, int width, int height, bool presenting = false) {
        gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, output);
        if (presenting) {
            glViewport(output_viewport_[0], output_viewport_[1], output_viewport_[2], output_viewport_[3]);
        } else {
            glViewport(0, 0, width, height);
        }
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_COLOR_LOGIC_OP);
        glDisable(GL_DITHER);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input);
        gl_.glUseProgram(resources_.programs[pass].id);
        quad();
        if (pass == GammaPass) ++gamma_passes_;
        else ++source_bloom_passes_;
    }

    void bind_output() {
        gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(output_framebuffer_));
        glViewport(output_viewport_[0], output_viewport_[1], output_viewport_[2], output_viewport_[3]);
    }

public:
    explicit OriginalComposition(InspectionGL& gl) : gl_(gl) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_);
    }
    OriginalComposition(const OriginalComposition&) = delete;
    OriginalComposition& operator=(const OriginalComposition&) = delete;
    OriginalComposition(OriginalComposition&&) = delete;
    OriginalComposition& operator=(OriginalComposition&&) = delete;

    std::uint64_t source_bloom_passes() const noexcept { return source_bloom_passes_; }
    std::uint64_t gamma_passes() const noexcept { return gamma_passes_; }

    void begin(int width, int height, const rendering::OriginalBloomState* bloom,
               const rendering::OriginalGammaState* gamma) {
        if (active_) throw std::runtime_error("Original composition begin without finish");
        bloom_ = bloom ? *bloom : rendering::OriginalBloomState{};
        gamma_ = gamma;
        world_composed_ = false;
        if (!bloom_.enabled && !gamma_) return;
        if (width <= 0 || height <= 0) throw std::runtime_error("Original composition dimensions must be positive");
        if (bloom_.enabled && (bloom_.width != 256 || bloom_.height != 256)) {
            throw std::runtime_error("Original bloom requires 256x256 intermediates");
        }
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &output_framebuffer_);
        glGetIntegerv(GL_VIEWPORT, output_viewport_.data());
        {
            State restore{gl_};
            if (width > maximum_texture_ || height > maximum_texture_ || maximum_texture_ < 256) {
                throw std::runtime_error("Original composition dimensions exceed GPU texture limit");
            }
            resize_target(Scene, width, height);
            if (bloom_.enabled) {
                initialize_bloom();
                for (const auto target : {Down, Horizontal, Vertical}) resize_target(target, 256, 256);
                if (gamma_) resize_target(Panels, width, height);
            }
            if (gamma_) {
                initialize_gamma();
                upload_gamma();
            }
        }
        width_ = width;
        height_ = height;
        active_ = true;
        gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, resources_.framebuffers[Scene]);
        glViewport(0, 0, width, height);
    }

    // Invoke after world/effects, BEFORE UiRenderer::draw. The selected target
    // stays bound for panels; bloom never sees those panels in scene_bmap.
    void compose_world() {
        if (!active_ || world_composed_) return;
        if (bloom_.enabled) {
            {
                State restore{gl_};
                auto& down = resources_.programs[DownPass];
                gl_.glUseProgram(down.id);
                gl_.glUniform1f(down.threshold, bloom_.threshold);
                draw(DownPass, resources_.textures[Scene], resources_.framebuffers[Down], 256, 256);
                for (const auto pass : {HorizontalPass, VerticalPass}) {
                    gl_.glUseProgram(resources_.programs[pass].id);
                    gl_.glUniform1f(resources_.programs[pass].width, bloom_.blur_width);
                    const auto input = pass == HorizontalPass ? Down : Horizontal;
                    const auto output = pass == HorizontalPass ? Horizontal : Vertical;
                    draw(pass, resources_.textures[input], resources_.framebuffers[output], 256, 256);
                }
                auto& final = resources_.programs[CompositePass];
                gl_.glUseProgram(final.id);
                gl_.glUniform1f(final.highlight, bloom_.highlight);
                gl_.glUniform1f(final.scene, bloom_.scene);
                gl_.glUniform1f(final.glow, bloom_.glow);
                gl_.glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, resources_.textures[Vertical]);
                const GLuint output = gamma_ ? resources_.framebuffers[Panels] : static_cast<GLuint>(output_framebuffer_);
                draw(CompositePass, resources_.textures[Scene], output, width_, height_, !gamma_);
            }
            if (gamma_) {
                gl_.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, resources_.framebuffers[Panels]);
                glViewport(0, 0, width_, height_);
            } else {
                bind_output();
            }
        }
        world_composed_ = true;
    }

    // Invoke AFTER UiRenderer::draw, before default-framebuffer readback/swap.
    void finish() {
        if (!active_) return;
        if (bloom_.enabled && !world_composed_) {
            throw std::runtime_error("Original bloom must compose before UI");
        }
        if (gamma_) {
            {
                State restore{gl_};
                gl_.glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, resources_.ramp);
                const auto input = bloom_.enabled ? Panels : Scene;
                draw(GammaPass, resources_.textures[input], static_cast<GLuint>(output_framebuffer_), width_, height_, true);
            }
        }
        bind_output();
        active_ = false;
        gamma_ = nullptr;
    }
};

} // namespace
} // namespace c4
