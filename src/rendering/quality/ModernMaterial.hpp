#pragma once
#include "rendering/Material.hpp"
#include "GraphicSettings.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace c4::rendering::quality {
// Native metallic/roughness approximation. Source state remains immutable;
// provenance is 0 unknown, 1 source-derived approximation, 2 explicit override.
inline std::array<float,4> modern_material_data(const NativeMaterialState& source,
        std::string_view material_name={},
        std::span<const ModernMaterialOverride> overrides={}) noexcept {
    const bool normal=material_requires_tangent(source.technique);
    const bool known=normal?(source.skills_known_mask&0xcu)==0xcu:
        ((source.color_known_mask&0x1c0u)==0x1c0u&&source.power_known);
    const float power=normal?source.skills[3]:source.power;
    const float specular=normal?source.skills[2]:
        std::max({source.specular[0],source.specular[1],source.specular[2]});
    const auto scalar=[](float value,float fallback) {
        return std::isfinite(value)?std::clamp(value,0.f,1.f):fallback;
    };
    const bool cube=material_uses_cube(source.technique);
    const bool fixed_cube=source.technique==MaterialTechnique::EnvCubeMix||
        source.technique==MaterialTechnique::EnvCubeAlpha;
    float reflection=cube?(fixed_cube?float(source.texture_factor>>24)/255.f:
        ((source.skills_known_mask&(1u<<8))?source.skills[8]:0.f)):
        (known?scalar(specular,0.f)*.25f:0.f);
    // Authored reflective actor bindings establish priority, not physical metals.
    const bool actor=cube&&(material_name=="Mtl_car3"||material_name=="Mtl_robot_cs2"||
        material_name=="Mtl_robot_kaporta"||material_name=="Mtl_kul_car"||
        material_name=="Mtl_kul_robot"||material_name=="Mtl_siyah_car"||
        material_name=="Mtl_siyah_robot");
    reflection=scalar(reflection,0.f);
    if(actor)reflection=std::max(reflection,.35f);
    std::array<float,4> result{known?scalar((specular-.04f)/.96f,0.f):0.f,
        known&&std::isfinite(power)?std::clamp(std::sqrt(2.f/(std::max(power,0.f)+2.f)),.045f,1.f):1.f,
        reflection,known||cube?1.f:0.f};
    for(const auto& native_override:overrides) {
        if(native_override.material_name!=material_name)continue;
        if(!std::isfinite(native_override.metallic)||!std::isfinite(native_override.roughness)||
            native_override.metallic<0.f||native_override.metallic>1.f||
            native_override.roughness<0.f||native_override.roughness>1.f||
            (native_override.reflectivity&&(!std::isfinite(*native_override.reflectivity)||
                *native_override.reflectivity<0.f||*native_override.reflectivity>1.f)))continue;
        result={native_override.metallic,native_override.roughness,
            native_override.reflectivity.value_or(reflection),2.f};
    }
    return result;
}
} // namespace c4::rendering::quality
