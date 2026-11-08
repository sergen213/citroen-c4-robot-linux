#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace c4 {
namespace {

template<class Function>
Function gl_function(const char* name) {
    auto address = SDL_GL_GetProcAddress(name);
    if (!address) throw std::runtime_error(std::string(name) + ": " + SDL_GetError());
    return reinterpret_cast<Function>(address);
}

struct InspectionGL {
#define C4_GL_FUNCTION(type, name) type name = gl_function<type>(#name)
    C4_GL_FUNCTION(PFNGLGENBUFFERSPROC, glGenBuffers);
    C4_GL_FUNCTION(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
    C4_GL_FUNCTION(PFNGLBINDBUFFERPROC, glBindBuffer);
    C4_GL_FUNCTION(PFNGLBUFFERDATAPROC, glBufferData);
    C4_GL_FUNCTION(PFNGLACTIVETEXTUREPROC, glActiveTexture);
    C4_GL_FUNCTION(PFNGLBUFFERSUBDATAPROC, glBufferSubData);
    C4_GL_FUNCTION(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D);
    C4_GL_FUNCTION(PFNGLCREATESHADERPROC, glCreateShader);
    C4_GL_FUNCTION(PFNGLDELETESHADERPROC, glDeleteShader);
    C4_GL_FUNCTION(PFNGLSHADERSOURCEPROC, glShaderSource);
    C4_GL_FUNCTION(PFNGLCOMPILESHADERPROC, glCompileShader);
    C4_GL_FUNCTION(PFNGLGETSHADERIVPROC, glGetShaderiv);
    C4_GL_FUNCTION(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    C4_GL_FUNCTION(PFNGLCREATEPROGRAMPROC, glCreateProgram);
    C4_GL_FUNCTION(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
    C4_GL_FUNCTION(PFNGLATTACHSHADERPROC, glAttachShader);
    C4_GL_FUNCTION(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation);
    C4_GL_FUNCTION(PFNGLLINKPROGRAMPROC, glLinkProgram);
    C4_GL_FUNCTION(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
    C4_GL_FUNCTION(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
    C4_GL_FUNCTION(PFNGLUSEPROGRAMPROC, glUseProgram);
    C4_GL_FUNCTION(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    C4_GL_FUNCTION(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
    C4_GL_FUNCTION(PFNGLUNIFORM1IPROC, glUniform1i);
    C4_GL_FUNCTION(PFNGLUNIFORM4FVPROC, glUniform4fv);
    C4_GL_FUNCTION(PFNGLBLENDEQUATIONPROC, glBlendEquation);
    C4_GL_FUNCTION(PFNGLUNIFORM1FPROC, glUniform1f);
    C4_GL_FUNCTION(PFNGLUNIFORM2FVPROC, glUniform2fv);
    C4_GL_FUNCTION(PFNGLUNIFORM3FVPROC, glUniform3fv);
    C4_GL_FUNCTION(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray);
    C4_GL_FUNCTION(PFNGLVERTEXATTRIB2FPROC, glVertexAttrib2f);
    C4_GL_FUNCTION(PFNGLVERTEXATTRIB3FPROC, glVertexAttrib3f);
    C4_GL_FUNCTION(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f);
    C4_GL_FUNCTION(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT);
    C4_GL_FUNCTION(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT);
    C4_GL_FUNCTION(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT);
    C4_GL_FUNCTION(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT);
    C4_GL_FUNCTION(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT);
    C4_GL_FUNCTION(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT);
    C4_GL_FUNCTION(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT);
    C4_GL_FUNCTION(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT);
    C4_GL_FUNCTION(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT);
    C4_GL_FUNCTION(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT);
    C4_GL_FUNCTION(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
    C4_GL_FUNCTION(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
#undef C4_GL_FUNCTION
};

struct InspectionResources {
    InspectionGL& gl;
    std::vector<GLuint> buffers;
    std::vector<GLuint> textures;
    GLuint program = 0;
    ~InspectionResources() {
        if (!buffers.empty()) gl.glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (!textures.empty()) glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        if (program) gl.glDeleteProgram(program);
    }
};

struct InspectionShader {
    InspectionGL& gl;
    GLuint id;
    ~InspectionShader() { if (id) gl.glDeleteShader(id); }
};

inline GLuint compile_inspection_shader(InspectionGL& gl, GLenum type, const char* source) {
    const GLuint id = gl.glCreateShader(type);
    if (!id) throw std::runtime_error("OpenGL could not allocate inspection shader");
    InspectionShader shader{gl, id};
    gl.glShaderSource(id, 1, &source, nullptr);
    gl.glCompileShader(id);
    GLint success = GL_FALSE;
    gl.glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        std::array<char, 4096> message{};
        gl.glGetShaderInfoLog(id, static_cast<GLsizei>(message.size()), nullptr, message.data());
        throw std::runtime_error("Inspection shader compilation: " + std::string(message.data()));
    }
    shader.id = 0;
    return id;
}

} // namespace
} // namespace c4
