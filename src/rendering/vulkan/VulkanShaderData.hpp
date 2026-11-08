#pragma once
#include "rendering/Material.hpp"
#include "rendering/quality/ModernMaterial.hpp"
#include <array>
#include <cstdint>
namespace c4::rendering {
struct VulkanGlobals {
    std::array<float,16> view_projection{};
    std::array<float,4> view_position{},sun_direction{},sun_color{},ambient{},fog_color{},fog_params{},bloom_params{},bloom_extra{},view_direction{};
    std::array<float,16> world_to_view{};
    std::array<float,4> render_options{};
};
struct VulkanMaterial {
    std::array<std::uint32_t,4> config{};
    std::array<float,4> diffuse{},ambient{},specular{},emissive{};
    std::array<std::array<float,4>,5> skills{};
    std::array<float,4> params{};
    std::array<float,16> texture_transform{};
    std::array<float,4> bump_matrix{},texture_factor{},extra{};
    // Native-only metallic/roughness approximation and cached authored IBL.
    std::array<float,4> modern{},environment{};
};
inline VulkanMaterial vulkan_material_data(const NativeMaterialState& source,
        std::string_view material_name={},
        std::span<const quality::ModernMaterialOverride> overrides={}) noexcept {
    VulkanMaterial material{};
    material.config[0]=static_cast<std::uint32_t>(source.technique);
    material.config[2]=source.complete?1u:0u;
    material.diffuse=source.diffuse;material.ambient=source.ambient;
    material.specular=source.specular;material.emissive=source.emissive;
    for(unsigned j=0;j<20;++j)material.skills[j/4][j%4]=source.skills[j];
    material.params={source.power,source.entity_alpha,source.entity_ambient_percent,source.entity_albedo_percent};
    material.texture_transform=source.texture_transform;material.bump_matrix=source.bump_environment_matrix;
    material.texture_factor={float((source.texture_factor>>16)&255)/255,float((source.texture_factor>>8)&255)/255,float(source.texture_factor&255)/255,float(source.texture_factor>>24)/255};
    const bool pbr_known=material_requires_tangent(source.technique)?(source.skills_known_mask&0xcu)==0xcu:((source.color_known_mask&0x1c0u)==0x1c0u&&source.power_known);
    material.extra={source.material_albedo_percent,float(source.bump_matrix_known),float(pbr_known),1};
    material.modern=quality::modern_material_data(source,material_name,overrides);
    if(material.modern[3]==2.f)material.extra[2]=1.f;
    return material;
}
struct VulkanDraw {
    std::array<float,16> model{};
    std::array<float,4> diffuse{1,1,1,1},uv{0,0,1,1};
    std::array<std::uint32_t,4> config{};
    std::array<float,4> params{1,1,1,0};
};
static_assert(sizeof(VulkanGlobals)==288);
static_assert(sizeof(VulkanMaterial)==320);
static_assert(sizeof(VulkanDraw)==128);
}
