#pragma once

#include "gl.hpp"
#include "../../assets/GpuImage.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace c4 {
namespace {

// Context-owned: construct once after making the renderer's GL context current.
struct GpuTextureCapabilities {
    bool bc1{}, bc2{}, bc3{};
    GpuTextureCapabilities() {
        const bool s3tc = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc");
        bc1 = s3tc || SDL_GL_ExtensionSupported("GL_EXT_texture_compression_dxt1");
        bc2 = s3tc || SDL_GL_ExtensionSupported("GL_ANGLE_texture_compression_dxt3");
        bc3 = s3tc || SDL_GL_ExtensionSupported("GL_ANGLE_texture_compression_dxt5");
    }
};

struct GpuTextureUpload {
    bool implementation_precision_fallback{};
    const char* reason{};
};

struct GpuTextureFormat {
    GLint internal;
    GLenum external, type;
    std::size_t unit_bytes;
    bool compressed;
};

inline GpuTextureFormat gpu_texture_format(assets::GpuImageFormat format) {
    switch (format) {
    case assets::GpuImageFormat::Rgba8Unorm:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false};
    case assets::GpuImageFormat::Rgb565Unorm:
        return {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2, false};
    case assets::GpuImageFormat::Argb1555Unorm:
        return {GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, false};
    case assets::GpuImageFormat::Bc1Unorm:
        return {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 8, true};
    case assets::GpuImageFormat::Bc2Unorm:
        return {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 16, true};
    case assets::GpuImageFormat::Bc3Unorm:
        return {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 16, true};
    }
    throw std::runtime_error("OpenGL GPU image: unsupported format");
}

inline void validate_gpu_texture(const assets::GpuImage& image, GLenum target,
                                 const GpuTextureFormat& format) {
    assets::validate_gpu_image(image);
    if (!((target == GL_TEXTURE_2D && image.face_count == 1) ||
          (target == GL_TEXTURE_CUBE_MAP && image.face_count == 6 && image.width == image.height)))
        throw std::runtime_error("OpenGL GPU image: target does not match face layout");
    for (const auto& level : image.levels) {
        const auto columns = format.compressed ? (level.width + 3) / 4 : level.width;
        const auto row_bytes = std::size_t(columns) * format.unit_bytes;
        if (format.compressed ? level.row_pitch != row_bytes : level.row_pitch % format.unit_bytes != 0)
            throw std::runtime_error("OpenGL GPU image: row pitch cannot be represented without repacking");
    }
}

struct GpuTextureUnpackState {
    InspectionGL& gl;
    static constexpr std::array<GLenum, 5> names{
        GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SWAP_BYTES};
    std::array<GLint, names.size()> values{};
    GLint buffer{};
    explicit GpuTextureUnpackState(InspectionGL& functions) : gl(functions) {
        for (std::size_t n = 0; n < names.size(); ++n) glGetIntegerv(names[n], &values[n]);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        // Canonical packed UNORM storage is little-endian, regardless of host.
        glPixelStorei(GL_UNPACK_SWAP_BYTES, std::endian::native == std::endian::big);
    }
    ~GpuTextureUnpackState() {
        for (std::size_t n = 0; n < names.size(); ++n) glPixelStorei(names[n], values[n]);
        gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
    }
};

// Caller owns/binds the texture and selects its recovered filter/wrap state.
// No asset loads, alpha inference, generated mips, or native-path CPU copies.
inline GpuTextureUpload upload_bound_gpu_image(InspectionGL& gl, GLenum target,
                                               const assets::GpuImage& image,
                                               const GpuTextureCapabilities& caps) {
    auto format = gpu_texture_format(image.format);
    validate_gpu_texture(image, target, format);
    bool supported = true;
    switch (image.format) {
    case assets::GpuImageFormat::Bc1Unorm: supported = caps.bc1; break;
    case assets::GpuImageFormat::Bc2Unorm: supported = caps.bc2; break;
    case assets::GpuImageFormat::Bc3Unorm: supported = caps.bc3; break;
    default: break;
    }
    GpuTextureUpload result;
    assets::Image decoded;
    if (!supported) {
        decoded = assets::decode_gpu_image(image);
        format = gpu_texture_format(assets::GpuImageFormat::Rgba8Unorm);
        result = {true, "Source BC format unsupported by OpenGL context; explicit RGBA8 decode changes implementation precision"};
    } else if (image.implementation_precision_fallback) {
        result = {true, "Canonical image carries an implementation precision fallback; bit-identical source GPU parity is not established"};
    }

    GpuTextureUnpackState unpack(gl);
    for (std::size_t lod = 0; lod < image.levels.size(); ++lod) {
        const auto& level = image.levels[lod];
        const auto* texels = image.texels.data() + level.offset;
        auto face_stride = level.face_stride;
        auto row_pitch = level.row_pitch;
        if (!supported) {
            texels = lod ? decoded.mip_rgba.data() + decoded.mip_levels[lod - 1].offset : decoded.rgba.data();
            row_pitch = std::size_t(level.width) * 4;
            face_stride = row_pitch * level.height;
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, format.compressed ? 0 : static_cast<GLint>(row_pitch / format.unit_bytes));
        for (std::uint32_t face = 0; face < image.face_count; ++face) {
            const GLenum face_target = image.face_count == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target;
            const auto* pixels = texels + face * face_stride;
            if (format.compressed) {
                const auto face_bytes = row_pitch * ((level.height + 3) / 4);
                gl.glCompressedTexImage2D(face_target, static_cast<GLint>(lod),
                    static_cast<GLenum>(format.internal), static_cast<GLsizei>(level.width),
                    static_cast<GLsizei>(level.height), 0, static_cast<GLsizei>(face_bytes), pixels);
            } else {
                glTexImage2D(face_target, static_cast<GLint>(lod), format.internal,
                    static_cast<GLsizei>(level.width), static_cast<GLsizei>(level.height),
                    0, format.external, format.type, pixels);
            }
        }
    }
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(image.levels.size() - 1));
    return result;
}

} // namespace
} // namespace c4
