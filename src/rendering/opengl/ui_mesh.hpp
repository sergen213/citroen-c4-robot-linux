#pragma once

#include "game/ui/NativeUi.hpp"
#include "rendering/opengl/gl.hpp"
#include "MaterialShaders.hpp"
#include "rendering/SourceLighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace c4 {
namespace {

// NativeUi owns these spans for the renderer lifetime. The owning GL context
// must be current during construction, preparation, drawing and destruction.
class OriginalUiMeshRenderer {
    struct VertexUpload {
        const assets::Vertex* origin;
        std::size_t count;
        GLuint buffer;
        std::uint64_t revision;
    };
    struct IndexUpload {
        const std::uint32_t* origin;
        std::size_t count;
        GLuint buffer;
        std::uint32_t maximum;
    };
    InspectionGL& gl_;
    InspectionResources resources_{gl_};
    std::vector<VertexUpload> vertices_;
    std::vector<IndexUpload> indices_;
    GLint maximum_texture_{};
    GLint model_location_{-1}, projection_location_{-1}, fit_location_{-1};
    GLint image_uv_scale_location_{-1};
    GLint color_multiplier_location_{-1}, texture_alpha_location_{-1}, alpha_reference_location_{-1};
    std::array<std::array<GLint, 7>, 8> light_locations_{};
    std::array<GLint, 13> lighting_locations_{};
    std::uint64_t draw_calls_{};
    struct RasterBindings {
        const rendering::RasterState* raster;
        GLenum source_blend, destination_blend;
        float alpha_reference;
    };
    static RasterBindings validate_source(const ui::OverlayMesh& mesh) {
        if (!mesh.lighting || !mesh.material_state)
            throw std::runtime_error("Original UI mesh source material or lighting missing");
        validate_finite(mesh.model_to_view);
        validate_finite(mesh.view_projection);
        if (mesh.color_multiplier != 1.0f && mesh.color_multiplier != 2.0f)
            throw std::runtime_error("Invalid original UI mesh texture-stage color multiplier");
        const auto& lighting = *mesh.lighting;
        if (lighting.status[0] == 0 || lighting.status[1] > lighting.lights.size())
            throw std::runtime_error("Incomplete original UI mesh fixed lighting");
        validate_finite(lighting.world_to_view);
        const std::array lighting_vectors{
            &lighting.view_position, &lighting.global_ambient, &lighting.fog_color, &lighting.fog_params,
            &lighting.diffuse, &lighting.ambient, &lighting.specular, &lighting.emissive, &lighting.material_params
        };
        for (const auto* values : lighting_vectors) validate_finite(*values);
        for (std::size_t light = 0; light < lighting.status[1]; ++light) {
            const auto& value = lighting.lights[light];
            validate_finite(value.diffuse);
            validate_finite(value.specular);
            validate_finite(value.ambient);
            validate_finite(value.position_range);
            validate_finite(value.direction_type);
            validate_finite(value.attenuation_falloff);
            validate_finite(value.cone_enabled);
        }
        if (lighting.options[1] != 1 || (lighting.options[2] != 2 && lighting.options[2] != 4))
            throw std::runtime_error("Incomplete original UI mesh alpha operation");
        if (mesh.source_effects_enabled &&
            mesh.material_state->pass_count > mesh.material_state->passes.size())
            throw std::runtime_error("Invalid original UI mesh effect pass count");
        const auto draw = rendering::original_material_draw_state(
            *mesh.material_state, mesh.source_effects_enabled);
        if (draw.technique != rendering::MaterialTechnique::FixedFunction || draw.passes.size() != 1)
            throw std::runtime_error("Original UI mesh effective draw is not single-pass fixed-function");
        const auto& raster = draw.passes.front().raster;
        if (!raster.defaults_recovered || raster.color_key || !std::isfinite(raster.alpha_reference))
            throw std::runtime_error("Incomplete original UI mesh raster or color-key state");
        float alpha_reference = -1.0f;
        switch (raster.alpha_comparison) {
        case rendering::AlphaComparison::Always: break;
        case rendering::AlphaComparison::GreaterEqual:
            if (raster.alpha_reference < 0 || raster.alpha_reference > 1)
                throw std::runtime_error("Invalid original UI mesh alpha reference");
            alpha_reference = raster.alpha_reference;
            break;
        default: throw std::runtime_error("Invalid original UI mesh alpha comparison");
        }
        if (mesh.source_cull_mode < 1 || mesh.source_cull_mode > 3)
            throw std::runtime_error("Unknown original UI mesh D3DCULL mode");
        return {&raster, blend_factor(raster.source_blend), blend_factor(raster.destination_blend), alpha_reference};
    }

    static constexpr std::array<const char*, 13> lighting_names_{
        "worldToView", "viewPosition", "globalAmbient", "fogColor", "fogParams",
        "diffuse", "ambient", "specular", "emissive", "materialParams", "controls", "status", "options"
    };
    template<std::size_t N>
    static void validate_finite(const std::array<float, N>& values) {
        for (const float value : values)
            if (!std::isfinite(value))
                throw std::runtime_error("Original UI mesh contains nonfinite source state");
    }
    void bind_lighting(const rendering::SourceLightingUniforms& lighting) {
        constexpr std::array members{
            &rendering::SourceLight::diffuse, &rendering::SourceLight::specular,
            &rendering::SourceLight::ambient, &rendering::SourceLight::position_range,
            &rendering::SourceLight::direction_type, &rendering::SourceLight::attenuation_falloff,
            &rendering::SourceLight::cone_enabled
        };
        for (std::size_t light = 0; light < lighting.status[1]; ++light)
            for (std::size_t field = 0; field < members.size(); ++field)
                gl_.glUniform4fv(light_locations_[light][field], 1,
                                 (lighting.lights[light].*members[field]).data());
        gl_.glUniformMatrix4fv(lighting_locations_[0], 1, GL_FALSE, lighting.world_to_view.data());
        const std::array vectors{
            &lighting.view_position, &lighting.global_ambient, &lighting.fog_color, &lighting.fog_params,
            &lighting.diffuse, &lighting.ambient, &lighting.specular, &lighting.emissive, &lighting.material_params
        };
        for (std::size_t field = 0; field < vectors.size(); ++field)
            gl_.glUniform4fv(lighting_locations_[field + 1], 1, vectors[field]->data());
        const std::array flags{&lighting.controls, &lighting.status, &lighting.options};
        for (std::size_t field = 0; field < flags.size(); ++field) {
            std::array<float, 4> values{};
            for (std::size_t channel = 0; channel < values.size(); ++channel)
                values[channel] = static_cast<float>((*flags[field])[channel]);
            gl_.glUniform4fv(lighting_locations_[field + 10], 1, values.data());
        }
    }
    static GLenum blend_factor(rendering::BlendFactor factor) {
        using Factor = rendering::BlendFactor;
        switch (factor) {
        case Factor::Zero: return GL_ZERO;
        case Factor::One: return GL_ONE;
        case Factor::SourceAlpha: return GL_SRC_ALPHA;
        case Factor::InverseSourceAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        }
        throw std::runtime_error("Invalid original UI mesh blend factor");
    }

    auto vertex_upload(const ui::OverlayMesh& mesh) {
        return std::find_if(vertices_.begin(), vertices_.end(), [&](const auto& upload) {
            return upload.origin == mesh.vertices.data() && upload.count == mesh.vertices.size();
        });
    }
    auto index_upload(const ui::OverlayMesh& mesh) {
        return std::find_if(indices_.begin(), indices_.end(), [&](const auto& upload) {
            return upload.origin == mesh.indices.data() && upload.count == mesh.indices.size();
        });
    }
    GLuint allocate_buffer() {
        if (resources_.buffers.size() >= static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
            throw std::runtime_error("Too many original UI mesh buffers");
        // Own the slot before generating the resource, including allocation failures.
        resources_.buffers.push_back(0);
        gl_.glGenBuffers(1, &resources_.buffers.back());
        if (!resources_.buffers.back())
            throw std::runtime_error("OpenGL could not allocate original UI mesh buffer");
        return resources_.buffers.back();
    }
    static void validate_counts(const ui::OverlayMesh& mesh) {
        if (mesh.vertices.empty() || mesh.indices.empty() ||
            !mesh.vertices.data() || !mesh.indices.data() || mesh.indices.size() % 3 != 0 ||
            mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
            mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(assets::Vertex) ||
            mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(std::uint32_t))
            throw std::runtime_error("Invalid original UI mesh triangle counts or GPU buffer size");
    }
    static void validate_vertices(std::span<const assets::Vertex> vertices) {
        for (const auto& vertex : vertices) {
            const std::array values{vertex.x, vertex.y, vertex.z, vertex.u, vertex.v,
                                    vertex.nx, vertex.ny, vertex.nz};
            for (const float value : values)
                if (!std::isfinite(value))
                    throw std::runtime_error("Original UI mesh contains nonfinite vertex data");
        }
    }

public:
    explicit OriginalUiMeshRenderer(InspectionGL& gl) : gl_(gl) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_);
        if (maximum_texture_ <= 0)
            throw std::runtime_error("OpenGL original UI mesh texture limit unavailable");
        const std::string vertex_source = std::string("#version 120\n") +
            rendering::source_lighting_shader + R"GLSL(
attribute vec3 position, normal;
attribute vec2 uv;
uniform mat4 modelToView, viewProjection;
uniform vec4 clipFit;
uniform vec2 imageUvScale;
uniform C4SourceLighting sourceLighting;
varying vec2 texcoord;
varying vec4 sourceClip, vertexDiffuse;
varying vec3 vertexSpecular;
varying float vertexFog;
void main() {
    sourceClip = viewProjection * modelToView * vec4(position, 1.0);
    gl_Position = sourceClip;
    gl_Position.xy = sourceClip.xy * clipFit.xy + sourceClip.w * clipFit.zw;
    texcoord = uv * imageUvScale;
    // SourceLighting's ViewEntity factory uses this native model transform;
    // only lighting inputs use engine X,Z,Y. Clip inputs remain native XYZ.
    vec3 sourceWorld = (modelToView * vec4(position, 1.0)).xyz.xzy;
    // Both authored HUD entities have unit scale; their inverse-transpose
    // normal transform is the same rotation, without its translation.
    vec3 sourceNormal = (modelToView * vec4(normal, 0.0)).xyz.xzy;
    vertexDiffuse = c4_source_vertex_diffuse(sourceLighting, sourceWorld, sourceNormal, vertexSpecular);
    vertexFog = c4_source_vertex_fog(sourceLighting, sourceWorld);
}
)GLSL";
        const std::string fragment_source = std::string("#version 120\n") +
            rendering::fixed_shader + rendering::source_lighting_shader + R"GLSL(
uniform sampler2D image;
uniform float colorMultiplier, alphaReference;
uniform bool textureAlphaOnly;
uniform C4SourceLighting sourceLighting;
varying vec2 texcoord;
varying vec4 sourceClip, vertexDiffuse;
varying vec3 vertexSpecular;
varying float vertexFog;
void main() {
    // Preserve the source-camera frustum after fitting it into the drawable.
    if (abs(sourceClip.x) > sourceClip.w || abs(sourceClip.y) > sourceClip.w) discard;
    vec4 sampleColor = texture2D(image, texcoord);
    vec4 result = vec4(clamp(colorMultiplier * sampleColor.rgb * vertexDiffuse.rgb
                            + vertexSpecular, 0.0, 1.0),
                       c4FixedTextureAlpha(sampleColor.a, vertexDiffuse.a, !textureAlphaOnly));
    if (alphaReference >= 0.0 && result.a < alphaReference) discard;
    gl_FragColor = c4_source_apply_fog(sourceLighting, result, vertexFog);
}
)GLSL";
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_source.c_str())};
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER, fragment_source.c_str())};
        resources_.program = gl_.glCreateProgram();
        if (!resources_.program)
            throw std::runtime_error("OpenGL could not allocate original UI mesh program");
        gl_.glAttachShader(resources_.program, vertex.id);
        gl_.glAttachShader(resources_.program, fragment.id);
        gl_.glBindAttribLocation(resources_.program, 0, "position");
        gl_.glBindAttribLocation(resources_.program, 1, "uv");
        gl_.glBindAttribLocation(resources_.program, 2, "normal");
        gl_.glLinkProgram(resources_.program);
        GLint linked = GL_FALSE;
        gl_.glGetProgramiv(resources_.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            std::array<char, 4096> message{};
            gl_.glGetProgramInfoLog(resources_.program, static_cast<GLsizei>(message.size()), nullptr, message.data());
            throw std::runtime_error("Original UI mesh shader link: " + std::string(message.data()));
        }
        const auto uniform = [&](const char* name) {
            const GLint location = gl_.glGetUniformLocation(resources_.program, name);
            if (location < 0)
                throw std::runtime_error("Original UI mesh shader uniform missing: " + std::string(name));
            return location;
        };
        model_location_ = uniform("modelToView");
        projection_location_ = uniform("viewProjection");
        fit_location_ = uniform("clipFit");
        image_uv_scale_location_ = uniform("imageUvScale");
        color_multiplier_location_ = uniform("colorMultiplier");
        texture_alpha_location_ = uniform("textureAlphaOnly");
        alpha_reference_location_ = uniform("alphaReference");
        constexpr std::array<const char*, 7> light_names{
            "diffuse", "specular", "ambient", "positionRange", "directionType", "attenuationFalloff", "coneEnabled"
        };
        for (std::size_t light = 0; light < light_locations_.size(); ++light)
            for (std::size_t field = 0; field < light_names.size(); ++field) {
                const auto name = "sourceLighting.lights[" + std::to_string(light) + "]." + light_names[field];
                light_locations_[light][field] = gl_.glGetUniformLocation(resources_.program, name.c_str());
            }
        for (std::size_t field = 0; field < lighting_names_.size(); ++field) {
            const auto name = std::string("sourceLighting.") + lighting_names_[field];
            // Unused members of the canonical payload can be optimized away.
            lighting_locations_[field] = gl_.glGetUniformLocation(resources_.program, name.c_str());
        }
        gl_.glUseProgram(resources_.program);
        gl_.glUniform1i(uniform("image"), 0);
    }
    OriginalUiMeshRenderer(const OriginalUiMeshRenderer&) = delete;
    OriginalUiMeshRenderer& operator=(const OriginalUiMeshRenderer&) = delete;
    OriginalUiMeshRenderer(OriginalUiMeshRenderer&&) = delete;
    OriginalUiMeshRenderer& operator=(OriginalUiMeshRenderer&&) = delete;

    // Prepare the whole source frame before its layer merge. Shared MDL material
    // batches reuse one VBO; a needle roll changes only its matrix, not its VBO.
    void prepare(std::span<const ui::OverlayMesh> meshes) {
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            const auto& mesh = meshes[i];
            validate_counts(mesh);
            validate_source(mesh);
            for (std::size_t previous = 0; previous < i; ++previous) {
                if (meshes[previous].vertices.data() == mesh.vertices.data() &&
                    meshes[previous].vertices.size() == mesh.vertices.size() &&
                    meshes[previous].pose_revision != mesh.pose_revision)
                    throw std::runtime_error("Shared original UI vertices have conflicting pose revisions");
            }
            auto vertex = vertex_upload(mesh);
            if (vertex == vertices_.end()) {
                validate_vertices(mesh.vertices);
                const GLuint buffer = allocate_buffer();
                gl_.glBindBuffer(GL_ARRAY_BUFFER, buffer);
                gl_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size_bytes()),
                                 mesh.vertices.data(), GL_DYNAMIC_DRAW);
                vertices_.push_back({mesh.vertices.data(), mesh.vertices.size(), buffer, mesh.pose_revision});
            } else if (vertex->revision != mesh.pose_revision) {
                validate_vertices(mesh.vertices);
                gl_.glBindBuffer(GL_ARRAY_BUFFER, vertex->buffer);
                gl_.glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(mesh.vertices.size_bytes()),
                                    mesh.vertices.data());
                vertex->revision = mesh.pose_revision;
            }
            auto index = index_upload(mesh);
            if (index == indices_.end()) {
                const auto maximum = *std::max_element(mesh.indices.begin(), mesh.indices.end());
                if (maximum >= mesh.vertices.size())
                    throw std::runtime_error("Original UI mesh index exceeds source vertices");
                const GLuint buffer = allocate_buffer();
                gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
                gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size_bytes()),
                                 mesh.indices.data(), GL_STATIC_DRAW);
                indices_.push_back({mesh.indices.data(), mesh.indices.size(), buffer, maximum});
            } else if (index->maximum >= mesh.vertices.size()) {
                throw std::runtime_error("Shared original UI mesh indices exceed source vertices");
            }
        }
    }

    // UiRenderer owns image uploads, filtering and the ordered per-layer depth
    // clears. This helper owns only the indexed geometry and source shader.
    void draw(const ui::OverlayMesh& mesh, std::span<const assets::Image> images,
              std::span<const assets::GpuImage* const> gpu_images,
              std::span<const GLuint> textures, const ui::Viewport& viewport,
              int drawable_width, int drawable_height) {
        if (drawable_width <= 0 || drawable_height <= 0 ||
            images.size() != textures.size() || mesh.image_index >= images.size() ||
            (!gpu_images.empty() && gpu_images.size() != images.size()) ||
            !textures[mesh.image_index] || !mesh.lighting || !mesh.material_state)
            throw std::runtime_error("Invalid original UI mesh image, lighting or drawable");
        const auto& image = images[mesh.image_index];
        const auto* gpu_record = gpu_images.empty() ? nullptr : gpu_images[mesh.image_index];
        const auto texels = static_cast<std::uint64_t>(image.width) * image.height;
        if (image.width == 0 || image.height == 0 || image.face_count != 1 ||
            (!gpu_record && (image.width > static_cast<std::uint32_t>(maximum_texture_) ||
                             image.height > static_cast<std::uint32_t>(maximum_texture_) ||
                             texels > std::numeric_limits<std::size_t>::max() / 4 ||
                             texels * 4 != image.rgba.size())))
            throw std::runtime_error("Invalid original UI mesh logical image or live RGBA size");
        std::array<float, 2> image_uv_scale{1.0f, 1.0f};
        if (gpu_record) {
            const auto& gpu = *gpu_record;
            if (gpu.face_count != 1 || gpu.width == 0 || gpu.height == 0 ||
                gpu.width > static_cast<std::uint32_t>(maximum_texture_) ||
                gpu.height > static_cast<std::uint32_t>(maximum_texture_))
                throw std::runtime_error("Invalid original UI mesh GPU storage dimensions");
            if (gpu.runtime) {
                const auto& runtime = *gpu.runtime;
                if (runtime.source_width == 0 || runtime.source_height == 0 ||
                    runtime.nominal_width < image.width || runtime.nominal_height < image.height ||
                    runtime.nominal_width < runtime.source_width ||
                    runtime.nominal_height < runtime.source_height)
                    throw std::runtime_error("Invalid original UI mesh nominal texture domain");
                // Source UVs use the original nominal bitmap domain before
                // allocation caps/resizing, not the physical texture dimensions.
                image_uv_scale = {static_cast<float>(runtime.source_width) / runtime.nominal_width,
                                  static_cast<float>(runtime.source_height) / runtime.nominal_height};
            }
        }
        const std::array viewport_values{viewport.x, viewport.y, viewport.width, viewport.height, viewport.scale};
        validate_finite(viewport_values);
        if (viewport.width <= 0 || viewport.height <= 0 || viewport.scale <= 0)
            throw std::runtime_error("Invalid original UI mesh viewport");
        const auto vertex = vertex_upload(mesh);
        const auto index = index_upload(mesh);
        if (vertex == vertices_.end() || index == indices_.end() ||
            vertex->revision != mesh.pose_revision || index->maximum >= mesh.vertices.size())
            throw std::runtime_error("Original UI mesh was not prepared for this pose");
        const auto source = validate_source(mesh);
        const auto& lighting = *mesh.lighting;
        const auto& raster = *source.raster;
        // Source model-to-world and camera matrices already map native XYZ to GL clip.
        // Only fit the authored clip rectangle; retain its homogeneous depth.
        const std::array fit{
            viewport.width / drawable_width, viewport.height / drawable_height,
            (2.0f * viewport.x + viewport.width) / drawable_width - 1.0f,
            1.0f - (2.0f * viewport.y + viewport.height) / drawable_height
        };
        validate_finite(fit);
        glViewport(0, 0, drawable_width, drawable_height);
        gl_.glUseProgram(resources_.program);
        gl_.glUniformMatrix4fv(model_location_, 1, GL_FALSE, mesh.model_to_view.data());
        gl_.glUniformMatrix4fv(projection_location_, 1, GL_FALSE, mesh.view_projection.data());
        gl_.glUniform4fv(fit_location_, 1, fit.data());
        gl_.glUniform2fv(image_uv_scale_location_, 1, image_uv_scale.data());
        gl_.glUniform1f(color_multiplier_location_, mesh.color_multiplier);
        gl_.glUniform1i(texture_alpha_location_, lighting.options[2] == 2 ? 1 : 0);
        gl_.glUniform1f(alpha_reference_location_, source.alpha_reference);
        bind_lighting(lighting);
        gl_.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[mesh.image_index]);
        gl_.glBindBuffer(GL_ARRAY_BUFFER, vertex->buffer);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index->buffer);
        gl_.glEnableVertexAttribArray(0);
        gl_.glEnableVertexAttribArray(1);
        gl_.glEnableVertexAttribArray(2);
        gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex), nullptr);
        gl_.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex),
                                  reinterpret_cast<const void*>(offsetof(assets::Vertex, u)));
        gl_.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex),
                                  reinterpret_cast<const void*>(offsetof(assets::Vertex, nx)));
        if (raster.depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(raster.depth_write ? GL_TRUE : GL_FALSE);
        if (raster.blend) {
            glEnable(GL_BLEND);
            gl_.glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(source.source_blend, source.destination_blend);
        } else glDisable(GL_BLEND);
        if (mesh.source_cull_mode == 1) {
            glDisable(GL_CULL_FACE);
        } else {
            // Both backends describe visual screen winding; Vulkan's shader
            // Y correction does not invert D3DCULL_CCW's clockwise front.
            glEnable(GL_CULL_FACE);
            glFrontFace(mesh.source_cull_mode == 3 ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        glDisable(GL_ALPHA_TEST); // Shader tests actual final RGBA.
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        ++draw_calls_;
    }

    std::uint64_t draw_calls() const noexcept { return draw_calls_; }
};

} // namespace
} // namespace c4
