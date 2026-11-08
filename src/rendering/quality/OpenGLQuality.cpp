#include "OpenGLQuality.hpp"
#include "QualityShaders.hpp"
#include "MaterialShaders.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace c4::rendering::quality {
namespace {
template<class T> T entry(const char* core, const char* extension = nullptr) {
    auto p = SDL_GL_GetProcAddress(core);
    if (!p && extension) p = SDL_GL_GetProcAddress(extension);
    return reinterpret_cast<T>(p);
}
bool extension_present(std::string_view list, std::string_view name) {
    std::size_t at = 0;
    while ((at = list.find(name, at)) != std::string_view::npos) {
        if ((at == 0 || list[at - 1] == ' ') &&
            (at + name.size() == list.size() || list[at + name.size()] == ' ')) return true;
        at += name.size();
    }
    return false;
}
constexpr const char* fullscreen_vertex =
    "#version 120\nvarying vec2 uv; void main(){uv=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}\n";
constexpr const char* post_uniforms =
    "\nvarying vec2 uv; uniform sampler2D image,depthImage,normalImage,aoImage,bloomImage;"
    "uniform vec2 texel,axis; uniform mat4 projection,inverseProjection;"
    "uniform float exposure,aces,bloomStrength,radius,bias,sampleCount;\n";
constexpr const char* normal_fragment = R"GLSL(
#version 120
varying vec2 uv;
uniform sampler2D depthImage;
uniform vec2 texel;
uniform mat4 inverseProjection;
vec3 point(vec2 q) {
    vec4 p=inverseProjection*vec4(q*2.0-1.0,texture2D(depthImage,q).r*2.0-1.0,1.0);
    return p.xyz/p.w;
}
void main() {
    if(texture2D(depthImage,uv).r>=0.999999){gl_FragColor=vec4(0.5,0.5,1.0,1.0);return;}
    vec3 p=point(uv);
    vec3 right=point(uv+vec2(texel.x,0.0))-p;
    vec3 left=p-point(uv-vec2(texel.x,0.0));
    vec3 up=point(uv+vec2(0.0,texel.y))-p;
    vec3 down=p-point(uv-vec2(0.0,texel.y));
    vec3 dx=dot(right,right)<dot(left,left)?right:left;
    vec3 dy=dot(up,up)<dot(down,down)?up:down;
    vec3 n=cross(dx,dy); float length2=dot(n,n);
    if(length2<=1e-20){gl_FragColor=vec4(0.5,0.5,0.5,1.0);return;}
    n*=inversesqrt(length2); if(dot(n,-p)<0.0)n=-n;
    gl_FragColor=vec4(n*0.5+0.5,1.0);
}
)GLSL";

// Opaque casters only transform position. Cutouts evaluate source alpha, never
// source RGB lighting, fog, PBR, reflection RGB or post-processing.
const std::string shadow_vertex = std::string("#version 120\n") + normal_shader + R"GLSL(
attribute vec3 position, normal;
attribute vec2 uv, secondaryUV;
attribute vec4 tertiaryUV;
uniform mat4 camera, model;
uniform bool alphaTest;
uniform int technique;
uniform vec3 viewPosition, sunDirection, viewDirection, skill5;
varying vec2 uv0, uv1;
varying vec3 tangentView, tangentSun, cubeDirection;
void main() {
    vec4 sourcePosition = vec4(position.xzy,1.0);
    gl_Position = camera * model * sourcePosition;
    uv0 = uv; uv1 = secondaryUV;
    tangentView = tangentSun = cubeDirection = vec3(0.0);
    if (alphaTest && technique >= 1 && technique <= 9) {
        // model's input columns are XZY; output rows remain native Z-up.
        mat4 sourceWorld = mat4(model[0].xzyw,model[1].xzyw,model[2].xzyw,model[3].xzyw);
        vec4 unusedClip;
        c4_normal_vertex(sourceWorld,mat4(1.0),sourcePosition,normal.xzy,
            tertiaryUV.xyz,viewPosition,sunDirection,skill5,unusedClip,tangentView,tangentSun);
        if (technique >= 7)
            cubeDirection = c4_cube_normal_direction(sourceWorld,normal.xzy,viewDirection);
    }
}
)GLSL";
const std::string shadow_fragment = std::string("#version 120\n") + normal_shader + fixed_shader + R"GLSL(
uniform bool alphaTest, textured, modulateDiffuse;
uniform int technique;
uniform float diffuseAlpha, alphaReference, reflectionWeight;
uniform vec4 sourceAmbient, skills;
uniform vec2 passScale;
uniform mat4 textureTransform;
uniform sampler2D baseImage, normalImage, lightImage;
uniform samplerCube cubeImage;
varying vec2 uv0, uv1;
varying vec3 tangentView, tangentSun, cubeDirection;
void main() {
    if (alphaTest) {
        vec4 base = textured ? texture2D(baseImage,uv0) : vec4(1.0);
        float alpha;
        if (technique >= 1 && technique <= 9) {
            vec4 bump = texture2D(normalImage,uv0);
            float mask = (technique == 3 || technique == 6) ? bump.a : 1.0;
            if (technique == 5 || technique == 6)
                alpha = c4_normal_shadow_shade(base,bump,texture2D(lightImage,uv1),
                    tangentView,tangentSun,sourceAmbient,skills,mask).a;
            else if (technique >= 7) {
                vec4 cube = textureCube(cubeImage,cubeDirection);
                alpha = technique == 8
                    ? c4_cube_normal_car_shade(base,bump,cube,tangentView,tangentSun,
                        sourceAmbient,skills,reflectionWeight).a
                    : c4_cube_normal_shade(base,bump,cube,tangentView,tangentSun,
                        sourceAmbient,skills,reflectionWeight).a;
            } else alpha = c4_normal_shade(base,bump,tangentView,tangentSun,sourceAmbient,skills,mask).a;
        } else {
            float stage0 = technique == 12 ? texture2D(lightImage,uv1).a
                : technique == 19 ? texture2D(lightImage,c4TextureTransform(uv1,textureTransform)).a
                : technique >= 13 && technique <= 18 ? texture2D(baseImage,uv1*passScale).a : base.a;
            alpha = technique >= 13 && technique <= 18 ? stage0
                : c4FixedTextureAlpha(stage0,diffuseAlpha,modulateDiffuse);
        }
        // Canonical colour-key conversion is already in uploaded alpha.
        // GreaterEqual accepts equality, including authored alpha above one.
        if (alpha < alphaReference) discard;
    }
    gl_FragColor = vec4(1.0);
}
)GLSL";
}

struct OpenGLQuality::Impl {
#define FN(type, name) type name = entry<type>(#name)
    FN(PFNGLACTIVETEXTUREPROC, glActiveTexture);
    FN(PFNGLCREATESHADERPROC, glCreateShader);
    FN(PFNGLDELETESHADERPROC, glDeleteShader);
    FN(PFNGLSHADERSOURCEPROC, glShaderSource);
    FN(PFNGLCOMPILESHADERPROC, glCompileShader);
    FN(PFNGLGETSHADERIVPROC, glGetShaderiv);
    FN(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    FN(PFNGLCREATEPROGRAMPROC, glCreateProgram);
    FN(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
    FN(PFNGLATTACHSHADERPROC, glAttachShader);
    FN(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation);
    FN(PFNGLLINKPROGRAMPROC, glLinkProgram);
    FN(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
    FN(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
    FN(PFNGLUSEPROGRAMPROC, glUseProgram);
    FN(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    FN(PFNGLUNIFORM1IPROC, glUniform1i);
    FN(PFNGLUNIFORM1FPROC, glUniform1f);
    FN(PFNGLUNIFORM2FPROC, glUniform2f);
    FN(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
#undef FN
#define FBO(type, name) type name = entry<type>(#name, #name "EXT")
    FBO(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
    FBO(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
    FBO(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
    FBO(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
    FBO(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
    FBO(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers);
    FBO(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers);
    FBO(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer);
    FBO(PFNGLGETRENDERBUFFERPARAMETERIVPROC, glGetRenderbufferParameteriv);
    FBO(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample);
    FBO(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
    FBO(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer);
#undef FBO
    struct Target { GLuint fbo{}, color{}, depth{}, color_rb{}, depth_rb{}; };
    struct Program {
        GLuint id{};
        GLint texel{-1}, axis{-1}, projection{-1}, inverse{-1}, exposure{-1}, aces{-1},
              bloom{-1}, radius{-1}, bias{-1}, samples{-1};
    };
    Target scene_sdr, scene_hdr, normal, ao, ao_temp, composed, sdr, fxaa_target,
           bloom_a, bloom_b, multisample, shadow;
    Program copy, tone, fxaa, bright, blur, normals, ssao, bilateral, compose;
    GLuint shadow_program{};
    GLint shadow_camera{-1}, shadow_model{-1};
    std::array<GLint, ShadowUniformCount> shadow_alpha{};
    GraphicsCapabilities caps;
    ResolvedGraphics resolved;
    GraphicSettings settings;
    Diagnostics diag;
    bool fbo_available{}, floating_available{}, multisample_available{}, srgb_available{},
         pbr_connected{}, shadows_connected{}, saved{}, active{}, ended{};
    unsigned shadow_size{}, ms_samples{1};
    GLenum ms_format{GL_RGBA8};
    GLint saved_program{}, saved_active{}, saved_read{}, saved_draw{}, saved_rb{}, saved_draw_buffer{};
    std::array<GLint, 7> saved_textures{}, saved_cubes{};

    ~Impl() { restore(); release_targets(); release_programs(); }
    void save() {
        if (saved) return;
        glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_active);
        glGetIntegerv(GL_DRAW_BUFFER, &saved_draw_buffer);
        for (unsigned unit = 0; unit < saved_textures.size(); ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_textures[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &saved_cubes[unit]);
        }
        glActiveTexture(static_cast<GLenum>(saved_active));
        if (fbo_available) {
            glGetIntegerv(multisample_available ? GL_DRAW_FRAMEBUFFER_BINDING : GL_FRAMEBUFFER_BINDING, &saved_draw);
            if (multisample_available) glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_read);
            else saved_read = saved_draw;
            if (glBindRenderbuffer) glGetIntegerv(GL_RENDERBUFFER_BINDING, &saved_rb);
        }
        // Bindings are saved explicitly so resize cannot revive deleted textures.
        glPushAttrib(GL_ALL_ATTRIB_BITS & ~GL_TEXTURE_BIT);
        glActiveTexture(GL_TEXTURE0);
        saved = true;
    }
    void restore() {
        if (!saved) return;
        if (fbo_available) {
            if (multisample_available) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(saved_draw));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(saved_read));
            } else glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(saved_draw));
            if (glBindRenderbuffer) glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(saved_rb));
        }
        glPopAttrib();
        for (unsigned unit = 0; unit < saved_textures.size(); ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(saved_textures[unit]));
            glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(saved_cubes[unit]));
        }
        glActiveTexture(static_cast<GLenum>(saved_active));
        glUseProgram(static_cast<GLuint>(saved_program));
        saved = active = ended = false;
    }
    GLuint compile(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            std::array<char, 2048> log{};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            diag.fallback_reason = log.data();
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
    GLuint link(const char* vertex_source, const char* fragment_source) {
        GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
        GLuint fragment = vertex ? compile(GL_FRAGMENT_SHADER, fragment_source) : 0;
        if (!fragment) { if (vertex) glDeleteShader(vertex); return 0; }
        GLuint program = glCreateProgram();
        if (program) {
            glAttachShader(program, vertex); glAttachShader(program, fragment);
            glBindAttribLocation(program, 0, "position");
            glBindAttribLocation(program, 1, "uv");
            glBindAttribLocation(program, 2, "normal");
            glBindAttribLocation(program, 3, "secondaryUV");
            glBindAttribLocation(program, 4, "tertiaryUV");
            glLinkProgram(program);
            GLint ok = GL_FALSE; glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) {
                std::array<char, 2048> log{};
                glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
                diag.fallback_reason = log.data(); glDeleteProgram(program); program = 0;
            }
        }
        glDeleteShader(vertex); glDeleteShader(fragment);
        return program;
    }
    Program post(const char* body, bool helpers = true) {
        std::string source = "#version 120\n";
        if (helpers) source += post_shader;
        source += post_uniforms; source += body;
        Program p; p.id = link(fullscreen_vertex, source.c_str());
        if (!p.id) return p;
        const auto loc = [&](const char* name) { return glGetUniformLocation(p.id, name); };
        p.texel=loc("texel"); p.axis=loc("axis"); p.projection=loc("projection");
        p.inverse=loc("inverseProjection"); p.exposure=loc("exposure"); p.aces=loc("aces");
        p.bloom=loc("bloomStrength"); p.radius=loc("radius"); p.bias=loc("bias"); p.samples=loc("sampleCount");
        glUseProgram(p.id);
        constexpr std::array<const char*,5> samplers{"image","depthImage","normalImage","aoImage","bloomImage"};
        for (unsigned i=0;i<samplers.size();++i) glUniform1i(loc(samplers[i]),static_cast<GLint>(i));
        return p;
    }
    void release_programs() {
        for (auto* p : {&copy,&tone,&fxaa,&bright,&blur,&normals,&ssao,&bilateral,&compose}) {
            if (p->id && glDeleteProgram) glDeleteProgram(p->id);
            *p = {};
        }
        if (shadow_program && glDeleteProgram) glDeleteProgram(shadow_program);
        shadow_program=0;
    }
    void drop(Target& t) {
        if(saved) {
            // A resize must not resurrect deleted quality objects on restore.
            for(auto& binding:saved_textures)
                if((t.color&&static_cast<GLuint>(binding)==t.color)||(t.depth&&static_cast<GLuint>(binding)==t.depth))binding=0;
            if(t.fbo&&static_cast<GLuint>(saved_draw)==t.fbo)saved_draw=0;
            if(t.fbo&&static_cast<GLuint>(saved_read)==t.fbo)saved_read=0;
            if((t.color_rb&&static_cast<GLuint>(saved_rb)==t.color_rb)||(t.depth_rb&&static_cast<GLuint>(saved_rb)==t.depth_rb))saved_rb=0;
        }
        if (t.fbo && glDeleteFramebuffers) glDeleteFramebuffers(1,&t.fbo);
        if (t.color) glDeleteTextures(1,&t.color);
        if (t.depth) glDeleteTextures(1,&t.depth);
        if (t.color_rb && glDeleteRenderbuffers) glDeleteRenderbuffers(1,&t.color_rb);
        if (t.depth_rb && glDeleteRenderbuffers) glDeleteRenderbuffers(1,&t.depth_rb);
        t = {};
    }
    void release_targets() {
        for(auto* t : {&scene_sdr,&scene_hdr,&normal,&ao,&ao_temp,&composed,&sdr,&fxaa_target,
                       &bloom_a,&bloom_b,&multisample,&shadow}) drop(*t);
    }
    GLuint texture(unsigned w,unsigned h,GLenum format,bool depth=false) {
        GLuint texture=0; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,depth?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,depth?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(depth) glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glTexImage2D(GL_TEXTURE_2D,0,static_cast<GLint>(format),static_cast<GLsizei>(w),static_cast<GLsizei>(h),0,
                     depth?GL_DEPTH_COMPONENT:GL_RGBA,depth?GL_UNSIGNED_INT:(format==GL_RGBA16F?GL_FLOAT:GL_UNSIGNED_BYTE),nullptr);
        return texture;
    }
    bool allocation_error() {
        const GLenum error=glGetError();
        if(error==GL_NO_ERROR)return false;
        diag.fallback_reason="Quality attachment OpenGL error "+std::to_string(error);
        // Retire only errors generated by this failed quality resource attempt.
        while(glGetError()!=GL_NO_ERROR) {}
        return true;
    }
    bool target(Target& t,unsigned w,unsigned h,GLenum format,bool depth=false,bool depth_only=false) {
        drop(t); glGenFramebuffers(1,&t.fbo); glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
        if (!depth_only) {
            t.color=texture(w,h,format);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.color,0);
        }
        if (depth || depth_only) {
            t.depth=texture(w,h,GL_DEPTH_COMPONENT24,true);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,t.depth,0);
        }
        glDrawBuffer(depth_only?GL_NONE:GL_COLOR_ATTACHMENT0);
        glReadBuffer(depth_only?GL_NONE:GL_COLOR_ATTACHMENT0);
        const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        const bool error=allocation_error();
        if(!t.fbo || (!depth_only&&!t.color) || ((depth||depth_only)&&!t.depth) ||
           status!=GL_FRAMEBUFFER_COMPLETE || error) {drop(t);return false;}
        diag.attachments_created+=static_cast<unsigned>(!depth_only)+static_cast<unsigned>(depth||depth_only);
        return true;
    }
    bool ms_target(Target& t,unsigned samples,GLenum format,bool probing=false) {
        drop(t); glGenFramebuffers(1,&t.fbo); glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
        glGenRenderbuffers(1,&t.color_rb); glBindRenderbuffer(GL_RENDERBUFFER,t.color_rb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,static_cast<GLsizei>(samples),format,
            static_cast<GLsizei>(diag.width),static_cast<GLsizei>(diag.height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,t.color_rb);
        GLint color_samples=0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&color_samples);
        glGenRenderbuffers(1,&t.depth_rb); glBindRenderbuffer(GL_RENDERBUFFER,t.depth_rb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,static_cast<GLsizei>(samples),GL_DEPTH_COMPONENT24,
            static_cast<GLsizei>(diag.width),static_cast<GLsizei>(diag.height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,t.depth_rb);
        GLint depth_samples=0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&depth_samples);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        const bool error=allocation_error();
        const bool complete=t.fbo&&t.color_rb&&t.depth_rb&&status==GL_FRAMEBUFFER_COMPLETE&&
            !error&&depth_samples==color_samples;
        if(!complete || color_samples!=static_cast<GLint>(samples)) {
            // A complete rounded-up probe is not exact support or an allocation failure.
            if(!error&&(!probing||!complete||color_samples<static_cast<GLint>(samples)))
                diag.fallback_reason="MSAA framebuffer status "+std::to_string(status)+
                ", requested samples "+std::to_string(samples)+", actual color/depth samples "+
                std::to_string(color_samples)+"/"+std::to_string(depth_samples);
            drop(t);return false;
        }
        diag.attachments_created+=2;
        return true;
    }
    void update_caps() {
        caps.fxaa=copy.id&&fxaa.id&&scene_sdr.fbo&&sdr.fbo&&fxaa_target.fbo;
        caps.internal_hdr=copy.id&&tone.id&&scene_hdr.fbo&&sdr.fbo;
        caps.bloom=caps.internal_hdr&&bright.id&&blur.id&&bloom_a.fbo&&bloom_b.fbo;
        caps.ssao=copy.id&&compose.id&&normals.id&&ssao.id&&bilateral.id&&normal.fbo&&ao.fbo&&ao_temp.fbo&&composed.fbo&&scene_sdr.depth;
        caps.pbr=pbr_connected;
        caps.max_shadow_cascades=shadows_connected&&shadow.fbo&&shadow_program?4u:0u;
        caps.max_shadow_size=caps.max_shadow_cascades?shadow_size:0u;
        diag.floating_targets=caps.internal_hdr; diag.depth_texture=scene_sdr.depth!=0;
        diag.shadow_atlas=shadow.fbo!=0;
        resolved=resolve_graphics(settings,caps);
    }
    void prepare_scene() {
        if(!resolved.enhanced()||!scene_sdr.fbo||resolved.samples<=1) {diag.active_samples=1;return;}
        const GLenum format=resolved.internal_hdr?GL_RGBA16F:GL_RGBA8;
        if(multisample.fbo&&ms_samples==resolved.samples&&ms_format==format) {diag.active_samples=ms_samples;return;}
        const bool owns_state=!saved;
        save();
        try {
            if(!ms_target(multisample,resolved.samples,format))
                throw std::runtime_error("Preparing advertised OpenGL "+std::to_string(resolved.samples)+
                    "x MSAA scene framebuffer failed: "+diag.fallback_reason);
            ms_samples=resolved.samples;
            ms_format=format;
            diag.active_samples=ms_samples;
        } catch(...) {
            if(owns_state)restore();
            throw;
        }
        if(owns_state)restore();
    }
    void bind_texture(unsigned unit,GLuint texture) {
        glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,texture);
    }
    void pass(const Program& p,GLuint destination,GLuint image,const Matrix& projection,const Matrix& inverse,
              float axis_x=0,float axis_y=0) {
        glBindFramebuffer(GL_FRAMEBUFFER,destination);
        glDrawBuffer(destination==static_cast<GLuint>(saved_draw)?static_cast<GLenum>(saved_draw_buffer):GL_COLOR_ATTACHMENT0);
        glViewport(0,0,static_cast<GLsizei>(diag.width),static_cast<GLsizei>(diag.height));
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_STENCIL_TEST);
        glDisable(GL_LIGHTING); glDisable(GL_FOG); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        if(srgb_available) glDisable(GL_FRAMEBUFFER_SRGB);
        glUseProgram(p.id); bind_texture(0,image);
        glUniform2f(p.texel,1.0f/static_cast<float>(diag.width),1.0f/static_cast<float>(diag.height));
        glUniform2f(p.axis,axis_x,axis_y);
        glUniformMatrix4fv(p.projection,1,GL_FALSE,projection.data());
        glUniformMatrix4fv(p.inverse,1,GL_FALSE,inverse.data());
        glUniform1f(p.exposure,std::exp2(resolved.exposure_ev));
        glUniform1f(p.aces,resolved.tone_mapping==ToneMapping::Aces?1.0f:0.0f);
        glUniform1f(p.bloom,resolved.bloom?resolved.bloom_strength:0.0f);
        glUniform1f(p.radius,resolved.ssao_radius); glUniform1f(p.bias,resolved.ssao_bias);
        glUniform1f(p.samples,static_cast<float>(resolved.ssao_samples));
        glActiveTexture(GL_TEXTURE0);
        glBegin(GL_QUADS);
        glTexCoord2f(0,0);glVertex2f(-1,-1); glTexCoord2f(1,0);glVertex2f(1,-1);
        glTexCoord2f(1,1);glVertex2f(1,1); glTexCoord2f(0,1);glVertex2f(-1,1);
        glEnd(); ++diag.post_passes;
    }
};

OpenGLQuality::OpenGLQuality():impl_(std::make_unique<Impl>()) {}
OpenGLQuality::~OpenGLQuality()=default;
void OpenGLQuality::initialize(unsigned width,unsigned height) {
    auto& s=*impl_;
    const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    int major=0,minor=0; if(version) std::sscanf(version,"%d.%d",&major,&minor);
    const std::string_view ext=extensions?extensions:"";
    const bool core_fbo=major>=3||extension_present(ext,"GL_ARB_framebuffer_object");
    const bool fbo=core_fbo||extension_present(ext,"GL_EXT_framebuffer_object");
    if(fbo&&!core_fbo) {
        // A proc-address stub is not proof the core spelling is supported.
#define EXT(type, name) s.name=entry<type>(#name "EXT")
        EXT(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers);
        EXT(PFNGLDELETEFRAMEBUFFERSPROC,glDeleteFramebuffers);
        EXT(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer);
        EXT(PFNGLFRAMEBUFFERTEXTURE2DPROC,glFramebufferTexture2D);
        EXT(PFNGLCHECKFRAMEBUFFERSTATUSPROC,glCheckFramebufferStatus);
        EXT(PFNGLGENRENDERBUFFERSPROC,glGenRenderbuffers);
        EXT(PFNGLDELETERENDERBUFFERSPROC,glDeleteRenderbuffers);
        EXT(PFNGLBINDRENDERBUFFERPROC,glBindRenderbuffer);
        EXT(PFNGLGETRENDERBUFFERPARAMETERIVPROC,glGetRenderbufferParameteriv);
        EXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC,glRenderbufferStorageMultisample);
        EXT(PFNGLFRAMEBUFFERRENDERBUFFERPROC,glFramebufferRenderbuffer);
        EXT(PFNGLBLITFRAMEBUFFERPROC,glBlitFramebuffer);
#undef EXT
    }
    s.fbo_available=fbo&&s.glGenFramebuffers&&s.glDeleteFramebuffers&&s.glBindFramebuffer&&
        s.glFramebufferTexture2D&&s.glCheckFramebufferStatus;
    s.floating_available=major>=3||extension_present(ext,"GL_ARB_texture_float");
    s.srgb_available=major>=3||extension_present(ext,"GL_ARB_framebuffer_sRGB")||extension_present(ext,"GL_EXT_framebuffer_sRGB");
    s.multisample_available=s.fbo_available&&s.glBlitFramebuffer&&s.glGenRenderbuffers&&s.glDeleteRenderbuffers&&
        s.glBindRenderbuffer&&s.glRenderbufferStorageMultisample&&s.glFramebufferRenderbuffer&&s.glGetRenderbufferParameteriv&&
        (major>=3||extension_present(ext,"GL_ARB_framebuffer_object")||
        (extension_present(ext,"GL_EXT_framebuffer_multisample")&&extension_present(ext,"GL_EXT_framebuffer_blit")));
    const bool shader_api=s.glActiveTexture&&s.glCreateShader&&s.glDeleteShader&&s.glShaderSource&&s.glCompileShader&&
        s.glGetShaderiv&&s.glGetShaderInfoLog&&s.glCreateProgram&&s.glDeleteProgram&&s.glAttachShader&&
        s.glBindAttribLocation&&s.glLinkProgram&&s.glGetProgramiv&&s.glGetProgramInfoLog&&s.glUseProgram&&
        s.glGetUniformLocation&&s.glUniform1i&&s.glUniform1f&&s.glUniform2f&&s.glUniformMatrix4fv;
    if(!s.fbo_available||!shader_api) {
        s.diag.fallback_reason="Quality framebuffer/shader entrypoints unavailable; original OpenGL remains active";
        s.update_caps();return;
    }
    s.save(); s.release_programs();
    s.copy=s.post("void main(){gl_FragColor=texture2D(image,uv);}",false);
    s.tone=s.post("void main(){vec4 c=texture2D(image,uv);vec3 b=texture2D(bloomImage,uv).rgb;"
        "gl_FragColor=vec4(c4_quality_tone_map(c.rgb+b*bloomStrength,exposure,aces),c.a);}");
    s.fxaa=s.post("void main(){gl_FragColor=vec4(c4_quality_fxaa(image,uv,texel),texture2D(image,uv).a);}");
    s.bright=s.post("void main(){gl_FragColor=vec4(c4_quality_bloom_bright(texture2D(image,uv).rgb,1.0),1.0);}");
    s.blur=s.post("void main(){gl_FragColor=vec4(c4_quality_gaussian(image,uv,texel,axis),1.0);}");
    s.normals.id=s.link(fullscreen_vertex,normal_fragment);
    if(s.normals.id) {
        s.normals.texel=s.glGetUniformLocation(s.normals.id,"texel");
        s.normals.inverse=s.glGetUniformLocation(s.normals.id,"inverseProjection");
        s.glUseProgram(s.normals.id);s.glUniform1i(s.glGetUniformLocation(s.normals.id,"depthImage"),1);
    }
    s.ssao=s.post("void main(){float a=c4_quality_ssao(depthImage,normalImage,uv,texel,projection,"
        "inverseProjection,vec2(2.0,-1.0),1.0,radius,bias,int(sampleCount));gl_FragColor=vec4(a,a,a,1.0);}");
    s.bilateral=s.post("void main(){float a=c4_quality_ao_bilateral(image,depthImage,normalImage,uv,texel,axis,"
        "inverseProjection,vec2(2.0,-1.0),1.0,max(radius*0.1,0.001),0.8);gl_FragColor=vec4(a,a,a,1.0);}");
    s.compose=s.post("void main(){vec4 c=texture2D(image,uv);gl_FragColor=vec4(c.rgb*texture2D(aoImage,uv).r,c.a);}",false);
    s.shadow_program=s.link(shadow_vertex.c_str(),shadow_fragment.c_str());
    if(s.shadow_program) {
        s.shadow_camera=s.glGetUniformLocation(s.shadow_program,"camera");
        s.shadow_model=s.glGetUniformLocation(s.shadow_program,"model");
        constexpr std::array names{"alphaTest","technique","textured","modulateDiffuse","diffuseAlpha",
            "alphaReference","sourceAmbient","skills","reflectionWeight","viewPosition","sunDirection",
            "viewDirection","skill5","textureTransform","passScale"};
        for(unsigned i=0;i<names.size();++i)
            s.shadow_alpha[i]=s.glGetUniformLocation(s.shadow_program,names[i]);
        s.glUseProgram(s.shadow_program);
        for(const auto& [name,unit]:std::array<std::pair<const char*,int>,4>{
            {{"baseImage",0},{"normalImage",1},{"lightImage",2},{"cubeImage",3}}})
            s.glUniform1i(s.glGetUniformLocation(s.shadow_program,name),unit);
    }
    s.restore();resize(width,height);
}
void OpenGLQuality::resize(unsigned width,unsigned height) {
    auto& s=*impl_;finish();
    if(!s.fbo_available||!s.copy.id) {s.update_caps();return;}
    s.save();s.release_targets();s.diag.width=width;s.diag.height=height;
    s.caps.sample_count_mask=1;s.ms_samples=1;
    GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    if(!width||!height||maximum<=0||width>static_cast<unsigned>(maximum)||height>static_cast<unsigned>(maximum)) {
        s.diag.fallback_reason="Quality attachment dimensions exceed the active GPU limit";s.update_caps();s.restore();return;
    }
    const bool scene=s.target(s.scene_sdr,width,height,GL_RGBA8,true);
    if(scene) {
        s.target(s.sdr,width,height,GL_RGBA8);
        s.target(s.fxaa_target,width,height,GL_RGBA8);
        s.target(s.normal,width,height,GL_RGBA8);
        if(s.normal.color) {
            glBindTexture(GL_TEXTURE_2D,s.normal.color);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        s.target(s.ao,width,height,GL_RGBA8);s.target(s.ao_temp,width,height,GL_RGBA8);
        if(s.floating_available&&s.target(s.scene_hdr,width,height,GL_RGBA16F,true)) {
            s.target(s.bloom_a,width,height,GL_RGBA16F);s.target(s.bloom_b,width,height,GL_RGBA16F);
        }
        s.target(s.composed,width,height,s.scene_hdr.fbo?GL_RGBA16F:GL_RGBA8);
        if(s.multisample_available) {
            GLint max_samples=0;glGetIntegerv(GL_MAX_SAMPLES,&max_samples);
            s.diag.max_samples=static_cast<unsigned>(std::max(max_samples,0));
            for(unsigned count:{2u,4u,8u}) {
                if(count<=s.diag.max_samples&&s.ms_target(s.multisample,count,GL_RGBA8,true)) {
                    // HDR+MSAA must resolve the same internal format as the scene.
                    if(!s.scene_hdr.fbo||s.ms_target(s.multisample,count,GL_RGBA16F,true)) s.caps.sample_count_mask|=count;
                }
            }
            s.drop(s.multisample);
        }
        s.shadow_size=std::min(2048u,static_cast<unsigned>(maximum)/2u);
        if(s.shadow_size) s.target(s.shadow,s.shadow_size*2,s.shadow_size*2,GL_RGBA8,false,true);
    } else s.diag.fallback_reason="Quality scene framebuffer incomplete; original OpenGL remains active";
    s.update_caps();s.restore();s.prepare_scene();
}
void OpenGLQuality::set_settings(const GraphicSettings& settings) {
    impl_->settings=settings;impl_->update_caps();impl_->prepare_scene();
}
void OpenGLQuality::set_material_support(bool pbr,bool shadows) {
    impl_->pbr_connected=pbr;impl_->shadows_connected=shadows;impl_->update_caps();impl_->prepare_scene();
}
void OpenGLQuality::set_texture_support(bool trilinear,float max_anisotropy) {
    impl_->caps.trilinear=trilinear;
    impl_->caps.max_anisotropy=trilinear&&std::isfinite(max_anisotropy)?std::max(1.0f,max_anisotropy):1.0f;
    impl_->update_caps();impl_->prepare_scene();
}
const GraphicsCapabilities& OpenGLQuality::capabilities()const noexcept{return impl_->caps;}
const ResolvedGraphics& OpenGLQuality::resolved()const noexcept{return impl_->resolved;}
std::span<const ModernMaterialOverride> OpenGLQuality::material_overrides()const noexcept {
    return impl_->settings.material_overrides;
}
const OpenGLQuality::Diagnostics& OpenGLQuality::diagnostics()const noexcept{return impl_->diag;}
bool OpenGLQuality::begin_scene() {
    auto& s=*impl_;finish();
    if(!s.resolved.enhanced()||!s.scene_sdr.fbo||!s.copy.id) return false;
    s.save();s.active=true;s.ended=false;
    auto& scene=s.resolved.internal_hdr?s.scene_hdr:s.scene_sdr;
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(s.saved_textures[0]));
    s.glActiveTexture(static_cast<GLenum>(s.saved_active));
    s.glBindFramebuffer(GL_FRAMEBUFFER,s.resolved.samples>1?s.multisample.fbo:scene.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(s.resolved.samples>1) glEnable(GL_MULTISAMPLE);
    glDepthRange(0,1);
    glViewport(0,0,static_cast<GLsizei>(s.diag.width),static_cast<GLsizei>(s.diag.height));
    s.diag.active_samples=s.resolved.samples;++s.diag.scene_frames;return true;
}
void OpenGLQuality::end_scene(const Matrix& projection,const Matrix& inverse) {
    auto& s=*impl_;if(!s.active||s.ended)return;
    auto& scene=s.resolved.internal_hdr?s.scene_hdr:s.scene_sdr;
    if(s.resolved.samples>1) {
        s.glBindFramebuffer(GL_READ_FRAMEBUFFER,s.multisample.fbo);s.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,scene.fbo);
        s.glBlitFramebuffer(0,0,static_cast<GLint>(s.diag.width),static_cast<GLint>(s.diag.height),0,0,
            static_cast<GLint>(s.diag.width),static_cast<GLint>(s.diag.height),GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        ++s.diag.resolve_passes;
    }
    s.bind_texture(1,scene.depth);s.bind_texture(2,s.normal.color);
    GLuint color=scene.color;
    if(s.resolved.ssao) {
        s.pass(s.normals,s.normal.fbo,0,projection,inverse);
        s.pass(s.ssao,s.ao.fbo,0,projection,inverse);
        s.pass(s.bilateral,s.ao_temp.fbo,s.ao.color,projection,inverse,1,0);
        s.pass(s.bilateral,s.ao.fbo,s.ao_temp.color,projection,inverse,0,1);
        s.bind_texture(3,s.ao.color);s.pass(s.compose,s.composed.fbo,color,projection,inverse);
        color=s.composed.color;s.diag.ao_passes+=4;
    }
    s.bind_texture(4,0);
    if(s.resolved.internal_hdr) {
        if(s.resolved.bloom) {
            s.pass(s.bright,s.bloom_a.fbo,color,projection,inverse);
            s.pass(s.blur,s.bloom_b.fbo,s.bloom_a.color,projection,inverse,1,0);
            s.pass(s.blur,s.bloom_a.fbo,s.bloom_b.color,projection,inverse,0,1);
            s.bind_texture(4,s.bloom_a.color);s.diag.bloom_passes+=3;
        }
        s.pass(s.tone,s.sdr.fbo,color,projection,inverse);color=s.sdr.color;
    }
    if(s.resolved.post_aa==PostAA::Fxaa) {
        s.pass(s.fxaa,s.fxaa_target.fbo,color,projection,inverse);color=s.fxaa_target.color;
    }
    s.pass(s.copy,static_cast<GLuint>(s.saved_draw),color,projection,inverse);
    s.ended=true;
}
void OpenGLQuality::finish(){impl_->restore();}
void OpenGLQuality::draw_shadows(std::span<const Matrix> cascades,ShadowDraw draw,void* user) {
    auto& s=*impl_;
    if(!draw||s.active||!s.resolved.enhanced()||s.resolved.shadows==ShadowQuality::Off||!s.caps.max_shadow_cascades)return;
    s.save();s.glBindFramebuffer(GL_FRAMEBUFFER,s.shadow.fbo);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glDepthRange(0,1);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDisable(GL_POLYGON_OFFSET_FILL);
    const unsigned count=std::min({static_cast<unsigned>(cascades.size()),s.resolved.shadow_cascades,4u});
    for(unsigned i=0;i<count;++i) {
        const auto tile=shadow_tile(i);
        glViewport(static_cast<GLint>(tile[0]*static_cast<float>(s.shadow_size*2)),
                   static_cast<GLint>(tile[1]*static_cast<float>(s.shadow_size*2)),
                   static_cast<GLsizei>(s.resolved.shadow_size),static_cast<GLsizei>(s.resolved.shadow_size));
        s.glUseProgram(s.shadow_program);s.glUniformMatrix4fv(s.shadow_camera,1,GL_FALSE,cascades[i].data());
        try { draw(user,ShadowPass{s.shadow_program,s.shadow_model,cascades[i],i,s.shadow_alpha}); }
        catch(...) {s.restore();throw;}
        ++s.diag.shadow_passes;
    }
    s.restore();
}
unsigned OpenGLQuality::shadow_texture()const noexcept{return impl_->shadow.depth;}
std::array<float,4> OpenGLQuality::shadow_tile(unsigned cascade)const noexcept {
    const float scale=impl_->shadow_size?static_cast<float>(impl_->resolved.shadow_size)/static_cast<float>(impl_->shadow_size*2):0.0f;
    return {static_cast<float>(cascade%2)*0.5f,static_cast<float>(cascade/2)*0.5f,scale,scale};
}
} // namespace c4::rendering::quality
