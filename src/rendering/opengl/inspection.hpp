#pragma once

#include "core/render_scene.hpp"
#include "game/ui/NativeUi.hpp"
#include "rendering/opengl/runtime_diagnostics.hpp"
#include "rendering/opengl/gl.hpp"
#include "rendering/opengl/material_program.hpp"
#include "rendering/opengl/effects.hpp"
#include "rendering/opengl/texture_filtering.hpp"
#include "rendering/opengl/gpu_texture.hpp"
#include "rendering/quality/OpenGLQuality.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <utility>

namespace c4 {
namespace {

// One upload per shared source span, with entity-specific animated spans kept
// separate. Material batches reuse these buffers and their original indices.
class InspectionScene {
    const rendering::RenderScene& scene_;
    InspectionGL& gl_;
    InspectionResources resources_{gl_};
    OriginalMaterialProgram material_{gl_};
    OriginalEffectRenderer effects_{gl_};
    rendering::quality::OpenGLQuality* quality_{};
    GpuTextureCapabilities gpu_texture_capabilities_;
    std::uint64_t source_gpu_texture_count_{}, source_gpu_precision_fallback_count_{};
    std::vector<std::string> source_gpu_fallback_reasons_;
    std::optional<EnhancedTextureFiltering> texture_filtering_;
    struct VertexUpload {
        GLuint buffer;
        std::uint64_t revision;
        GLuint tangent_buffer{};
        bool mutable_pose{};
    };
    std::vector<VertexUpload> vertex_uploads_;
    std::vector<std::size_t> vertex_upload_indices_;
    std::vector<GLuint> index_buffers_, secondary_buffers_;
    std::array<rendering::Instance, 6> sky_instances_;
    std::vector<std::uint32_t> draw_order_; // Reused per frame: source entity list order.
    std::uint64_t draw_calls_{}, world_draw_calls_{}, mesh_draw_calls_{}, image_draw_calls_{};
    std::uint64_t incomplete_material_draw_calls_{}, shadow_draw_calls_{}, pbr_draw_calls_{};
    unsigned active_shadow_cascades_{}, active_shadow_size_{};
    float caster_reach_{};
    struct ShadowContext { InspectionScene& renderer; const rendering::FrameSnapshot& frame; };
    static void sprite_cull(const rendering::Instance& instance) {
        if (!instance.source_sprite) return;
        if (!instance.source_sprite_state_known)
            throw std::runtime_error("Missing original sprite raster state");
        // Preserve original RS22 screen winding; the XYZ/XZY basis swaps
        // cancel between sprite vertices and WVP, not the source fan order.
        switch (instance.source_cull_mode) {
        case 1: glDisable(GL_CULL_FACE); break;
        case 2: glFrontFace(GL_CCW); glCullFace(GL_BACK); glEnable(GL_CULL_FACE); break;
        case 3: glFrontFace(GL_CW); glCullFace(GL_BACK); glEnable(GL_CULL_FACE); break;
        default: throw std::runtime_error("Invalid original sprite RS22 cull mode");
        }
    }

    static void draw_shadow(void* user, const rendering::quality::OpenGLQuality::ShadowPass& pass) {
        using Q = rendering::quality::OpenGLQuality;
        auto& context = *static_cast<ShadowContext*>(user);
        auto& renderer = context.renderer;
        const auto& frame = context.frame;
        renderer.gl_.glUseProgram(pass.program);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        for (const auto& instance : frame.instances) {
            if (!rendering::instance_visible(instance)) continue;
            const auto& state = instance.material_state;
            const auto& mesh = renderer.scene_.meshes.at(instance.mesh_index);
            if (frame.source_effects_enabled && (state.pass_count == 0 || state.pass_count > state.passes.size()))
                throw std::runtime_error("Invalid source shadow material pass count");
            const auto draw = rendering::original_material_draw_state(state, frame.source_effects_enabled);
            if (draw.passes.empty()) throw std::runtime_error("Missing original shadow draw pass");
            const auto& source_pass = draw.passes[0];
            const auto* alpha = instance.original_alpha ? &*instance.original_alpha : nullptr;
            auto raster = source_pass.raster;
            if (alpha) {
                auto source_alpha = *alpha;
                if (instance.source_sprite_no_depth_write && instance.source_sprite)
                    source_alpha.depth_write = false;
                raster = rendering::apply_original_model_alpha(raster, source_alpha);
            } else if (instance.source_sprite && instance.source_sprite_no_depth_write &&
                       (raster.effect_override_mask & rendering::DepthWrite) == 0)
                raster.depth_write = false;
            if (!rendering::raster_shadow_caster(instance, draw.technique, raster, alpha).value_or(false)) continue;
            const bool cutout = raster.color_key || raster.alpha_comparison != rendering::AlphaComparison::Always;
            renderer.gl_.glUniform1i(pass.alpha_uniforms[Q::AlphaTest], cutout);
            // Match Original's shaderPosition=position.xzy and native model
            // coordinates: permute input columns only; light clip output stays native.
            const auto model = quality_source_matrix_to_native(instance.model_to_world);
            renderer.gl_.glUniformMatrix4fv(pass.model_uniform, 1, GL_FALSE, model.data());
            renderer.bind_mesh(instance.mesh_index); // Includes live animated/sprite uploads.
            if (instance.source_sprite) sprite_cull(instance);
            else glDisable(GL_CULL_FACE); // Shared native mesh caster policy; no stale sprite cull.
            const bool terrain = draw.technique >= rendering::MaterialTechnique::Terrain4A &&
                                 draw.technique <= rendering::MaterialTechnique::Terrain5C;
            if (terrain || draw.technique == rendering::MaterialTechnique::Water) glDisable(GL_CULL_FACE);
            if (cutout) {
                const bool normal = rendering::material_requires_tangent(draw.technique);
                const auto base = frame.source_effects_enabled
                    ? rendering::material_texture(state, rendering::TextureRole::Color) : state.entity_skins[0];
                if (base && (raster.color_key || (instance.flags & (1u << 12)) != 0) &&
                    !renderer.scene_.gpu_textures.at(*base).source_alpha_known)
                    throw std::runtime_error("Shadow cutout requires canonical first-upload source alpha");
                if (!normal && !terrain && !alpha)
                    throw std::runtime_error("Shadow cutout requires actual source draw alpha decision");
                renderer.texture(0, base, frame.source_effects_enabled
                    ? renderer.sampler_for(state, base) : source_pass.sampler);
                renderer.gl_.glUniform1i(pass.alpha_uniforms[Q::Technique], static_cast<int>(draw.technique));
                renderer.gl_.glUniform1i(pass.alpha_uniforms[Q::Textured], base.has_value());
                renderer.gl_.glUniform1i(pass.alpha_uniforms[Q::ModulateDiffuse], alpha && alpha->modulate_diffuse);
                renderer.gl_.glUniform1f(pass.alpha_uniforms[Q::DiffuseAlpha], alpha ? alpha->diffuse_alpha : 1.f);
                renderer.gl_.glUniform1f(pass.alpha_uniforms[Q::AlphaReference], raster.alpha_reference);
                if (normal) {
                    if (!frame.visuals || !frame.visuals->view_inputs_known ||
                        !state.ambient_alpha_known || (state.skills_known_mask & 0x7fu) != 0x7fu ||
                        mesh.tertiary_uv.size() != mesh.vertices.size() ||
                        (mesh.tangent_provenance != assets::TangentProvenance::ImportedSource &&
                         mesh.tangent_provenance != assets::TangentProvenance::SourceAlgorithmNativePort) ||
                        !rendering::material_has_required_textures(state, true) ||
                        (draw.technique >= rendering::MaterialTechnique::CubeNormalOptim &&
                         (state.skills_known_mask & (1u << 8)) == 0))
                        throw std::runtime_error("Shadow cutout requires original shader alpha inputs");
                    for (const auto& [unit, role] : std::array{
                        std::pair{1u, rendering::TextureRole::Normal},
                        std::pair{2u, rendering::TextureRole::Lightmap},
                        std::pair{3u, rendering::TextureRole::Environment}}) {
                        const auto index = rendering::material_texture(state, role);
                        renderer.texture(unit, index, renderer.sampler_for(state, index), unit == 3);
                    }
                    renderer.gl_.glUniform4fv(pass.alpha_uniforms[Q::SourceAmbient], 1, state.ambient.data());
                    renderer.gl_.glUniform4fv(pass.alpha_uniforms[Q::Skills], 1, state.skills.data());
                    renderer.gl_.glUniform1f(pass.alpha_uniforms[Q::ReflectionWeight], state.skills[8]);
                    renderer.gl_.glUniform3fv(pass.alpha_uniforms[Q::ViewPosition], 1, frame.visuals->view_position.data());
                    renderer.gl_.glUniform3fv(pass.alpha_uniforms[Q::SunDirection], 1, frame.visuals->sun_direction.data());
                    renderer.gl_.glUniform3fv(pass.alpha_uniforms[Q::ViewDirection], 1, frame.visuals->view_direction.data());
                    renderer.gl_.glUniform3fv(pass.alpha_uniforms[Q::Skill5], 1, state.skills.data() + 4);
                } else if (draw.technique == rendering::MaterialTechnique::ShadowMap ||
                           draw.technique == rendering::MaterialTechnique::Water) {
                    const auto role = draw.technique == rendering::MaterialTechnique::Water
                        ? rendering::TextureRole::BumpDuDv : rendering::TextureRole::Lightmap;
                    const auto index = rendering::material_texture(state, role);
                    renderer.texture(2, index, renderer.sampler_for(state, index));
                    renderer.gl_.glUniformMatrix4fv(pass.alpha_uniforms[Q::TextureTransform], 1, GL_FALSE,
                                                   state.texture_transform.data());
                } else if (terrain) {
                    const auto index = source_pass.material_slot == 0 ? state.entity_skins[0]
                        : state.material_skins[source_pass.material_slot - 1];
                    renderer.texture(0, index, source_pass.sampler);
                    renderer.gl_.glUniform2fv(pass.alpha_uniforms[Q::PassScale], 1, source_pass.uv_scale.data());
                }
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
            ++renderer.shadow_draw_calls_;
        }
    }

    GLuint allocate_buffer() {
        GLuint buffer{};
        gl_.glGenBuffers(1, &buffer);
        if (!buffer) throw std::runtime_error("OpenGL could not allocate source mesh buffer");
        resources_.buffers.push_back(buffer);
        return buffer;
    }
    static GLenum blend_factor(rendering::BlendFactor factor) {
        using F = rendering::BlendFactor;
        switch (factor) {
        case F::Zero: return GL_ZERO;
        case F::One: return GL_ONE;
        case F::SourceAlpha: return GL_SRC_ALPHA;
        case F::InverseSourceAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        }
        throw std::runtime_error("Unknown source blend factor");
    }
    static void raster(const rendering::RasterState& state) {
        if (state.depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
        if (state.blend) {
            glEnable(GL_BLEND);
            glBlendFunc(blend_factor(state.source_blend), blend_factor(state.destination_blend));
        } else glDisable(GL_BLEND);
        // Alpha comparison is performed on the actual shader RGBA output, not
        // on an unrelated fixed-function texture stage.
        glDisable(GL_ALPHA_TEST);
    }
    static void address(GLenum target, GLenum coordinate, rendering::AddressMode mode) {
        using A = rendering::AddressMode;
        switch (mode) {
        case A::Wrap: glTexParameteri(target, coordinate, GL_REPEAT); break;
        case A::Clamp: glTexParameteri(target, coordinate, GL_CLAMP_TO_EDGE); break;
        case A::Mirror: glTexParameteri(target, coordinate, GL_MIRRORED_REPEAT); break;
        case A::Unspecified: break; // Preserve inherited state; not a parity assertion.
        }
    }
    void texture(unsigned unit, std::optional<std::uint32_t> index,
                 const rendering::SamplerState& sampler, bool cube = false) {
        gl_.glActiveTexture(GL_TEXTURE0 + unit);
        const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        if (!index) { glBindTexture(target, 0); return; }
        if (*index >= scene_.textures.size()) throw std::runtime_error("Source material texture index out of range");
        const auto& image = scene_.gpu_textures[*index];
        if ((image.face_count == 6) != cube) throw std::runtime_error("Source material sampler/image dimensionality mismatch");
        const auto id = texture_filtering_ && quality_
            ? texture_filtering_->texture(*index, quality_->resolved()) : resources_.textures[*index];
        if (!id) throw std::runtime_error("Source material refers to an unprepared GPU texture");
        glBindTexture(target, id);
        address(target, GL_TEXTURE_WRAP_S, sampler.u);
        address(target, GL_TEXTURE_WRAP_T, sampler.v);
        if (cube) address(target, GL_TEXTURE_WRAP_R, sampler.w);
        const bool original_filtering = !quality_ ||
            quality_->resolved().texture_filtering == rendering::quality::TextureFiltering::Original;
        if (sampler.filter_explicit && original_filtering) {
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, sampler.linear ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, image.levels.size() <= 1
                ? (sampler.linear ? GL_LINEAR : GL_NEAREST)
                : (sampler.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST));
        }
        if (texture_filtering_ && quality_)
            texture_filtering_->apply_sampler(*index, target, quality_->resolved());
    }
    rendering::SamplerState sampler_for(const rendering::NativeMaterialState& state,
                                       std::optional<std::uint32_t> index) const {
        for (unsigned i = 0; i < 4; ++i) if (index && state.entity_skins[i] == index) return state.entity_samplers[i];
        for (unsigned i = 0; i < 4; ++i) if (index && state.material_skins[i] == index) return state.material_samplers[i];
        return {};
    }
    void bind_textures(const rendering::NativeMaterialState& state, const rendering::MaterialPass& pass,
                       bool source_effects_enabled) {
        if (!source_effects_enabled) {
            texture(0, state.entity_skins[0], pass.sampler);
            for (unsigned unit = 1; unit < 6; ++unit)
                texture(unit, std::nullopt, pass.sampler, unit == 3);
            gl_.glActiveTexture(GL_TEXTURE0);
            return;
        }
        using R = rendering::TextureRole;
        for (unsigned unit = 0; unit < 5; ++unit) {
            const std::array roles{R::Color, R::Normal, R::Lightmap, R::Environment, R::BumpDuDv};
            const auto index = rendering::material_texture(state, roles[unit]);
            texture(unit, index, sampler_for(state, index), unit == 3);
        }
        const auto layer = pass.material_slot == 0 ? state.entity_skins[0] : state.material_skins[pass.material_slot - 1];
        texture(5, layer, pass.sampler);
        gl_.glActiveTexture(GL_TEXTURE0);
    }
    void bind_mesh(std::size_t index) {
        const auto& mesh = scene_.meshes[index];
        auto& upload = vertex_uploads_[vertex_upload_indices_[index]];
        gl_.glBindBuffer(GL_ARRAY_BUFFER, upload.buffer);
        if (upload.revision != mesh.pose_revision) {
            if (upload.mutable_pose) {
                gl_.glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(mesh.vertices.size_bytes()),
                                    mesh.vertices.data());
            } else if (!mesh.tangent_binding ||
                       mesh.tangent_provenance != assets::TangentProvenance::SourceAlgorithmNativePort) {
                throw std::runtime_error("Unexpected pose revision for immutable source vertices");
            }
            if (!mesh.tertiary_uv.empty()) {
                if (mesh.tertiary_uv.size() != mesh.vertices.size())
                    throw std::runtime_error("Invalid live original source tangent span");
                if (!upload.tangent_buffer) {
                    upload.tangent_buffer = allocate_buffer();
                    gl_.glBindBuffer(GL_ARRAY_BUFFER, upload.tangent_buffer);
                    gl_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.tertiary_uv.size_bytes()),
                                     mesh.tertiary_uv.data(), GL_DYNAMIC_DRAW);
                } else {
                    gl_.glBindBuffer(GL_ARRAY_BUFFER, upload.tangent_buffer);
                    gl_.glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(mesh.tertiary_uv.size_bytes()),
                                        mesh.tertiary_uv.data());
                }
                gl_.glBindBuffer(GL_ARRAY_BUFFER, upload.buffer);
            }
            upload.revision = mesh.pose_revision;
        }
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffers_[index]);
        gl_.glEnableVertexAttribArray(0);
        gl_.glEnableVertexAttribArray(1);
        gl_.glEnableVertexAttribArray(2);
        gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex), reinterpret_cast<const void*>(offsetof(assets::Vertex, x)));
        gl_.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex), reinterpret_cast<const void*>(offsetof(assets::Vertex, u)));
        gl_.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(assets::Vertex), reinterpret_cast<const void*>(offsetof(assets::Vertex, nx)));
        if (secondary_buffers_[index]) {
            gl_.glBindBuffer(GL_ARRAY_BUFFER, secondary_buffers_[index]);
            gl_.glEnableVertexAttribArray(3);
            gl_.glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(std::array<float, 2>), nullptr);
        } else {
            gl_.glDisableVertexAttribArray(3);
            gl_.glVertexAttrib2f(3, 0, 0);
        }
        if (upload.tangent_buffer && !mesh.tertiary_uv.empty()) {
            gl_.glBindBuffer(GL_ARRAY_BUFFER, upload.tangent_buffer);
            gl_.glEnableVertexAttribArray(4);
            gl_.glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(std::array<float, 4>), nullptr);
        } else {
            gl_.glDisableVertexAttribArray(4);
            gl_.glVertexAttrib4f(4, 0, 0, 0, 0);
        }
    }

    void draw_sky(const rendering::FrameSnapshot& frame, bool quality_active) {
        if (!scene_.sky || !frame.sky) return;
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT |
                     GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
        struct RestoreRaster { ~RestoreRaster() { glPopAttrib(); } } restore;
        material_.bind_sky_camera(frame.sky->camera, quality_active ? quality_ : nullptr);
        // Six original bitmap faces, not an environment/reflection cubemap.
        for (std::size_t face = 0; face < sky_instances_.size(); ++face) {
            auto& instance = sky_instances_[face];
            instance.model_to_world = frame.sky->model_to_world[face];
            const auto& mesh = scene_.meshes[instance.mesh_index];
            const auto& state = instance.material_state;
            const auto draw = rendering::original_material_draw_state(state, false);
            bind_mesh(instance.mesh_index);
            for (std::size_t pass_index = 0; pass_index < draw.passes.size(); ++pass_index) {
                const auto& pass = draw.passes[pass_index];
                bind_textures(state, pass, false);
                auto sky_raster = material_.bind_instance(instance, pass, pass_index,
                    &scene_.gpu_textures[*instance.texture_index], &*instance.original_alpha, draw, false);
                // Original private20000 sprite draw overrides depth after alpha.
                sky_raster.depth_test = true;
                sky_raster.depth_write = false;
                raster(sky_raster);
                glDisable(GL_CULL_FACE);
                gl_.glBlendEquation(GL_FUNC_ADD);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
                if (material_.incomplete_source_state()) ++incomplete_material_draw_calls_;
                ++draw_calls_;
                ++image_draw_calls_;
            }
        }
        material_.bind_frame(frame); // Restore the selected world projection and lighting.
    }

public:
    InspectionScene(const rendering::RenderScene& scene, InspectionGL& gl,
                    rendering::quality::OpenGLQuality* quality = nullptr)
        : scene_(scene), gl_(gl), quality_(quality) {
        const auto maximum = static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());
        if (scene.meshes.size() > maximum / 4 || scene.textures.size() > maximum)
            throw std::runtime_error("Too many source mesh/image resources for OpenGL");
        vertex_upload_indices_.resize(scene.meshes.size());
        index_buffers_.resize(scene.meshes.size());
        secondary_buffers_.resize(scene.meshes.size());
        vertex_uploads_.reserve(scene.meshes.size());
        resources_.buffers.reserve(scene.meshes.size() * 4);
        struct SharedBuffer { std::size_t bytes, index; };
        std::unordered_map<const void*, SharedBuffer> uploaded_vertices, uploaded_attributes, uploaded_indices;
        const auto attribute = [&](auto span, GLenum usage = GL_STATIC_DRAW) -> GLuint {
            if (span.empty()) return 0;
            const auto found = uploaded_attributes.find(span.data());
            if (found != uploaded_attributes.end() && found->second.bytes == span.size_bytes())
                return static_cast<GLuint>(found->second.index);
            const auto buffer = allocate_buffer();
            gl_.glBindBuffer(GL_ARRAY_BUFFER, buffer);
            gl_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(span.size_bytes()), span.data(), usage);
            uploaded_attributes.emplace(span.data(), SharedBuffer{span.size_bytes(), buffer});
            return buffer;
        };
        for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
            const auto& mesh = scene.meshes[i];
            if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 || mesh.indices.size() > maximum ||
                mesh.vertices.size_bytes() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
                mesh.indices.size_bytes() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
                (!mesh.secondary_uv.empty() && mesh.secondary_uv.size() != mesh.vertices.size()) ||
                (!mesh.tertiary_uv.empty() && mesh.tertiary_uv.size() != mesh.vertices.size()))
                throw std::runtime_error("Invalid original source mesh/attribute span");
            for (const auto index : mesh.indices) if (index >= mesh.vertices.size())
                throw std::runtime_error("Source mesh index out of range");
            const auto found = uploaded_vertices.find(mesh.vertices.data());
            if (found != uploaded_vertices.end() && found->second.bytes == mesh.vertices.size_bytes()) {
                vertex_upload_indices_[i] = found->second.index;
            } else {
                const GLuint buffer = allocate_buffer();
                vertex_upload_indices_[i] = vertex_uploads_.size();
                uploaded_vertices.emplace(mesh.vertices.data(), SharedBuffer{mesh.vertices.size_bytes(), vertex_uploads_.size()});
                const bool mutable_pose = std::any_of(scene.posed_vertices.begin(), scene.posed_vertices.end(),
                    [&](const auto& vertices) {
                        return vertices.data() == mesh.vertices.data() && vertices.size() == mesh.vertices.size();
                    }) || std::any_of(scene.sprite_bindings.begin(), scene.sprite_bindings.end(),
                    [&](const auto& binding) {
                        const auto& vertices = scene.meshes.at(binding.mesh_index).vertices;
                        return vertices.data() == mesh.vertices.data() && vertices.size() == mesh.vertices.size();
                    });
                vertex_uploads_.push_back({buffer, mesh.pose_revision, 0, mutable_pose});
                gl_.glBindBuffer(GL_ARRAY_BUFFER, buffer);
                gl_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size_bytes()),
                                 mesh.vertices.data(), mutable_pose ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
            }
            const auto indices = uploaded_indices.find(mesh.indices.data());
            if (indices != uploaded_indices.end() && indices->second.bytes == mesh.indices.size_bytes()) {
                index_buffers_[i] = static_cast<GLuint>(indices->second.index);
            } else {
                index_buffers_[i] = allocate_buffer();
                uploaded_indices.emplace(mesh.indices.data(), SharedBuffer{mesh.indices.size_bytes(), index_buffers_[i]});
                gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffers_[i]);
                gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size_bytes()), mesh.indices.data(), GL_STATIC_DRAW);
            }
            secondary_buffers_[i] = attribute(mesh.secondary_uv);
            auto& upload = vertex_uploads_[vertex_upload_indices_[i]];
            if (!upload.tangent_buffer)
                upload.tangent_buffer = attribute(mesh.tertiary_uv, GL_DYNAMIC_DRAW);
        }
        GLint maximum_texture{}, maximum_cube{}, texture_units{};
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture);
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximum_cube);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
        if (texture_units < 6) throw std::runtime_error("Original material shader requires six texture image units");
        rendering::SourceLightingDeviceState lighting_device;
        lighting_device.sourceMaxTextureWidth = maximum_texture > 0 ? static_cast<std::uint32_t>(maximum_texture) : 0;
        lighting_device.supportedADD = material_.program() != 0;
        lighting_device.supportedMOD2X = material_.program() != 0;
        lighting_device.capabilitiesKnown = material_.program() != 0 && maximum_texture > 0;
        material_.set_lighting_device(lighting_device);
        resources_.textures.resize(scene.textures.size());
        gl_.glActiveTexture(GL_TEXTURE0);
        if (scene.gpu_textures.size() != scene.textures.size())
            throw std::runtime_error("Source GPU image records must match scene texture identities");
        const auto prepare_texture = [&](std::uint32_t i) {
            if (i >= scene.gpu_textures.size())
                throw std::runtime_error("Source GPU texture reference out of range");
            const auto& image = scene.gpu_textures[i];
            const bool cube = image.face_count == 6;
            const auto limit = cube ? maximum_cube : maximum_texture;
            if ((image.face_count != 1 && !cube) || !image.width || !image.height || limit <= 0 ||
                image.width > static_cast<unsigned>(limit) || image.height > static_cast<unsigned>(limit) ||
                (cube && image.width != image.height))
                throw std::runtime_error("Invalid actual source image or GPU texture size limit");
            if (resources_.textures[i]) throw std::runtime_error("Duplicate source GPU texture preparation reference");
            glGenTextures(1, &resources_.textures[i]);
            if (!resources_.textures[i]) throw std::runtime_error("OpenGL could not allocate source texture");
            const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            glBindTexture(target, resources_.textures[i]);
            // Inherited filtering remains explicitly incomplete where no
            // source FX state declares it; authored levels stay accessible.
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, image.levels.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, cube ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, cube ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            if (cube) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            const auto upload = upload_bound_gpu_image(gl_, target, image, gpu_texture_capabilities_);
            ++source_gpu_texture_count_;
            if (upload.implementation_precision_fallback) {
                ++source_gpu_precision_fallback_count_;
                if (upload.reason && std::find(source_gpu_fallback_reasons_.begin(), source_gpu_fallback_reasons_.end(),
                                               upload.reason) == source_gpu_fallback_reasons_.end())
                    source_gpu_fallback_reasons_.emplace_back(upload.reason);
            }
        };
        for (const auto i : scene.referenced_texture_indices) prepare_texture(i);
        if (scene.sky) {
            for (std::size_t face = 0; face < scene.sky->faces.size(); ++face) {
                const auto& source = scene.sky->faces[face];
                if (source.mesh_index >= scene.meshes.size() || source.texture_index >= scene.gpu_textures.size() ||
                    scene.gpu_textures[source.texture_index].face_count != 1 ||
                    source.material_state.entity_skins[0] != source.texture_index)
                    throw std::runtime_error("Invalid prepared original sky bitmap face");
                const auto draw = rendering::original_material_draw_state(source.material_state, false);
                if (draw.technique != rendering::MaterialTechnique::FixedFunction || draw.passes.empty())
                    throw std::runtime_error("Original sky requires the fixed bitmap material branch");
                if (!resources_.textures[source.texture_index]) prepare_texture(source.texture_index);
                auto& instance = sky_instances_[face];
                instance.mesh_index = source.mesh_index;
                instance.texture_index = source.texture_index;
                instance.source = scene.sky->resource;
                instance.material = "mat_sprite";
                instance.kind = rendering::InstanceKind::Image;
                instance.flags = scene.sky->authored_flags;
                instance.material_state = source.material_state;
                instance.source_lighting_cache = &scene.sky->source_lighting_cache;
                instance.original_alpha = source.original_alpha;
            }
        }
        for (const auto& instance : scene.instances) if (instance.mesh_index >= scene.meshes.size())
            throw std::runtime_error("Source scene instance mesh index out of range");
        const auto error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("Upload source scene: OpenGL error " + std::to_string(error));
        if (quality_) quality_->set_material_support(material_.qualitySupport(), material_.qualitySupport());
        if (quality_) {
            texture_filtering_.emplace(gl_, std::span<const assets::GpuImage>(scene_.gpu_textures),
                                      std::span<const GLuint>(resources_.textures));
            quality_->set_texture_support(texture_filtering_->trilinear_support(),
                                          texture_filtering_->max_anisotropy());
        }
        if (quality_ && scene_.has_bounds && !rendering::authored_source_shadows(scene_)) {
            double squared{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double extent = static_cast<double>(scene_.bounds_max[axis]) - scene_.bounds_min[axis];
                squared += extent * extent;
            }
            caster_reach_ = static_cast<float>(std::sqrt(squared));
        }
        prepare_quality();
    }
    InspectionScene(const InspectionScene&) = delete;
    InspectionScene& operator=(const InspectionScene&) = delete;
    // Called during candidate preparation, before an active-backend swap.
    void prepare_quality() {
        if (!quality_ || !texture_filtering_ ||
            quality_->resolved().texture_filtering == rendering::quality::TextureFiltering::Original) return;
        glPushAttrib(GL_TEXTURE_BIT);
        struct RestoreTextures { ~RestoreTextures() { glPopAttrib(); } } restore;
        gl_.glActiveTexture(GL_TEXTURE0);
        for (std::size_t i = 0; i < scene_.textures.size(); ++i) {
            if (!resources_.textures[i]) continue;
            const auto id = texture_filtering_->texture(i, quality_->resolved());
            const GLenum target = scene_.gpu_textures[i].face_count == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            glBindTexture(target, id);
            texture_filtering_->apply_sampler(i, target, quality_->resolved());
        }
    }

    void draw(const rendering::FrameSnapshot& frame) {
        if (&frame.scene != &scene_) throw std::runtime_error("Source snapshot does not match uploaded scene");
        active_shadow_cascades_ = active_shadow_size_ = 0;
        if (frame.overlay && !frame.overlay->draw_world) return;
        const bool effects_enabled = frame.source_effects_enabled;
        const auto camera = quality_ && quality_->resolved().enhanced()
            ? quality_camera_frame(frame) : std::optional<QualityCameraFrame>{};
        std::optional<rendering::quality::ShadowCascades> cascades;
        if (quality_ && quality_->resolved().enhanced() && !camera)
            throw std::runtime_error("Enhanced OpenGL requires the actual camera projection/view decomposition");
        const auto* source_lighting = frame.source_lighting;
        const auto sun_slot = source_lighting ? source_lighting->sun_slot_plus_one : 0u;
        const bool source_sun = source_lighting && source_lighting->lights_known &&
            sun_slot > 0 && sun_slot <= source_lighting->light_count &&
            sun_slot <= source_lighting->lights.size() &&
            source_lighting->lights[sun_slot - 1].cone_enabled[2] != 0.f &&
            source_lighting->lights[sun_slot - 1].direction_type[3] == 3.f;
        if (!rendering::authored_source_shadows(scene_) && quality_ && camera &&
            frame.visuals && frame.visuals->view_inputs_known &&
            source_sun && quality_->resolved().shadow_cascades) {
            const auto& sun = source_lighting->lights[sun_slot - 1].direction_type;
            const std::array<float, 3> native_sun{sun[0], sun[2], sun[1]};
            const auto& settings = quality_->resolved();
            cascades = rendering::quality::build_shadow_cascades(camera->inverseViewProjection,
                native_sun, camera->nearDepth, camera->farDepth, camera->clipNear, camera->clipFar,
                settings.shadow_cascades, settings.shadow_size,
                rendering::quality::native_shadow_split_lambda, caster_reach_);
            if (cascades) {
                ShadowContext context{*this, frame};
                quality_->draw_shadows(std::span<const QualityMatrix>(cascades->matrices.data(), cascades->count),
                                       draw_shadow, &context);
                active_shadow_cascades_ = cascades->count;
                active_shadow_size_ = settings.shadow_size;
            }
        }
        const bool quality_active = quality_ && camera && quality_->begin_scene();
        struct FinishQuality {
            rendering::quality::OpenGLQuality* helper;
            ~FinishQuality() { if (helper) helper->finish(); }
        } finish_quality{quality_active ? quality_ : nullptr};
        if (quality_active) {
            glDepthMask(GL_TRUE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        material_.use();
        material_.bind_frame(frame);
        material_.bind_quality_frame(quality_active ? quality_ : nullptr, cascades ? &*cascades : nullptr,
                                     camera ? &camera->positiveDepthRow : nullptr);
        gl_.glBlendEquation(GL_FUNC_ADD);
        const auto draw_instance = [&](const rendering::Instance& instance) {
            if (!rendering::instance_visible(instance)) return;
            if (instance.mesh_index >= scene_.meshes.size()) throw std::runtime_error("Source frame instance mesh out of range");
            const auto& mesh = scene_.meshes[instance.mesh_index];
            const auto& state = instance.material_state;
            if (effects_enabled && (state.pass_count == 0 || state.pass_count > state.passes.size()))
                throw std::runtime_error("Invalid original material pass count");
            const auto draw = rendering::original_material_draw_state(state, effects_enabled);
            if (draw.passes.empty()) throw std::runtime_error("Missing original draw pass");
            bind_mesh(instance.mesh_index);
            if (instance.source_sprite) sprite_cull(instance);
            else glDisable(GL_CULL_FACE); // Ordinary meshes must not inherit a preceding sprite's RS22.
            const auto image_index = state.entity_skins[0];
            const auto* image = image_index ? &scene_.gpu_textures.at(*image_index) : nullptr;
            const auto* alpha = instance.original_alpha ? &*instance.original_alpha : nullptr;
            const bool terrain = draw.technique >= rendering::MaterialTechnique::Terrain4A && draw.technique <= rendering::MaterialTechnique::Terrain5C;
            if (terrain || draw.technique == rendering::MaterialTechnique::Water) {
                glDisable(GL_CULL_FACE); glEnable(GL_DITHER);
            }
            for (std::size_t pass_index = 0; pass_index < draw.passes.size(); ++pass_index) {
                const auto& pass = draw.passes[pass_index];
                bind_textures(state, pass, effects_enabled);
                raster(material_.bind_instance(instance, pass, pass_index, image, alpha, draw, effects_enabled,
                    quality_active ? quality_->material_overrides()
                                   : std::span<const rendering::quality::ModernMaterialOverride>{}));
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
                if (material_.incomplete_source_state()) ++incomplete_material_draw_calls_;
                if (quality_active && material_.pbr_active()) ++pbr_draw_calls_;
                ++draw_calls_;
                switch (instance.kind) {
                case rendering::InstanceKind::World: ++world_draw_calls_; break;
                case rendering::InstanceKind::Model: ++mesh_draw_calls_; break;
                case rendering::InstanceKind::Image: ++image_draw_calls_; break;
                }
            }
        };
        const auto opaque = rendering::source_entity_draw_order(frame, draw_order_);
        for (std::size_t k = 0; k < opaque; ++k) draw_instance(frame.instances[draw_order_[k]]);
        if (scene_.sky && frame.sky) {
            draw_sky(frame, quality_active);
            material_.bind_quality_frame(quality_active ? quality_ : nullptr, cascades ? &*cascades : nullptr,
                                         camera ? &camera->positiveDepthRow : nullptr);
        }
        for (std::size_t k = opaque; k < draw_order_.size(); ++k) draw_instance(frame.instances[draw_order_[k]]);
        glDepthMask(GL_TRUE);
        if (frame.effects) effects_.draw(*frame.effects, frame.camera,
            quality_active && quality_->resolved().internal_hdr);
        if (quality_active) quality_->end_scene(camera->projection, camera->inverseProjection);
    }
    std::uint64_t draw_calls() const noexcept { return draw_calls_; }
    std::uint64_t effect_draw_calls() const noexcept { return effects_.draw_calls(); }
    std::uint64_t shadow_draw_calls() const noexcept { return shadow_draw_calls_; }
    std::uint64_t pbr_draw_calls() const noexcept { return pbr_draw_calls_; }
    std::uint64_t source_gpu_texture_count() const noexcept { return source_gpu_texture_count_; }
    std::uint64_t source_gpu_precision_fallback_count() const noexcept { return source_gpu_precision_fallback_count_; }
    std::span<const std::string> source_gpu_fallback_reasons() const noexcept { return source_gpu_fallback_reasons_; }
    std::int32_t source_anisotropy_raw() const noexcept { return effects_.source_anisotropy_raw(); }
    float sampler_anisotropy_level() const noexcept { return effects_.sampler_anisotropy_level(); }
    float sampler_anisotropy_maximum() const noexcept { return effects_.sampler_anisotropy_maximum(); }
    bool sampler_anisotropy_supported() const noexcept { return effects_.sampler_anisotropy_supported(); }
    std::string_view sampler_anisotropy_fallback_reason() const noexcept { return effects_.sampler_anisotropy_fallback_reason(); }
    void diagnostics(RuntimeDiagnostics& value) const {
        value.draw_calls = draw_calls_ + effects_.draw_calls() + shadow_draw_calls_;
        value.material_draw_calls = draw_calls_;
        value.incomplete_material_draw_calls = incomplete_material_draw_calls_;
        value.particle_draw_calls = effects_.draw_calls();
        value.shadow_draw_calls = shadow_draw_calls_;
        value.authored_source_shadows = rendering::authored_source_shadows(scene_);
        value.active_shadow_cascades = active_shadow_cascades_;
        value.active_shadow_size = active_shadow_size_;
        value.pbr_draw_calls = pbr_draw_calls_;
        value.world_draw_calls = world_draw_calls_;
        value.mesh_draw_calls = mesh_draw_calls_;
        value.image_draw_calls = image_draw_calls_;
    }
};

} // namespace
} // namespace c4
