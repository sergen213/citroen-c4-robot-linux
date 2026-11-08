#include "rendering/Material.hpp"
#include "rendering/MaterialEvents.hpp"
#include "rendering/SourceLighting.hpp"
#include "assets/World.hpp"
#include "physics/NativeNumeric.hpp"
#include "game/GameState.hpp"
#include "game/CameraProjection.hpp"
#include "game/ui/SourceViewSprite.hpp"
#include "MaterialShaders.hpp"
#include "rendering/quality/ModernEnvironment.hpp"
#include "rendering/vulkan/VulkanShaderData.hpp"
#include "rendering/opengl/inspection.hpp"
#include "rendering/opengl/ui.hpp"
#include <bit>
#include <limits>
#include <SDL3/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <array>
#include <cmath>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Pixel = std::array<float, 4>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
Function load(const char* name, const char* alternate = nullptr) {
    auto address = SDL_GL_GetProcAddress(name);
    if (!address && alternate) address = SDL_GL_GetProcAddress(alternate);
    require(address != nullptr, std::string("missing OpenGL procedure: ") + name);
    return reinterpret_cast<Function>(address);
}

struct Gl {
    PFNGLCREATESHADERPROC create_shader = load<PFNGLCREATESHADERPROC>("glCreateShader");
    PFNGLSHADERSOURCEPROC shader_source = load<PFNGLSHADERSOURCEPROC>("glShaderSource");
    PFNGLCOMPILESHADERPROC compile_shader = load<PFNGLCOMPILESHADERPROC>("glCompileShader");
    PFNGLGETSHADERIVPROC shader_status = load<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    PFNGLGETSHADERINFOLOGPROC shader_log = load<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    PFNGLDELETESHADERPROC delete_shader = load<PFNGLDELETESHADERPROC>("glDeleteShader");
    PFNGLCREATEPROGRAMPROC create_program = load<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    PFNGLATTACHSHADERPROC attach_shader = load<PFNGLATTACHSHADERPROC>("glAttachShader");
    PFNGLLINKPROGRAMPROC link_program = load<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    PFNGLGETPROGRAMIVPROC program_status = load<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    PFNGLGETPROGRAMINFOLOGPROC program_log = load<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    PFNGLUSEPROGRAMPROC use_program = load<PFNGLUSEPROGRAMPROC>("glUseProgram");
    PFNGLDELETEPROGRAMPROC delete_program = load<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    PFNGLGETUNIFORMLOCATIONPROC uniform_location = load<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    PFNGLUNIFORM1IPROC uniform_int = load<PFNGLUNIFORM1IPROC>("glUniform1i");
    PFNGLGENFRAMEBUFFERSPROC gen_framebuffers = load<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers", "glGenFramebuffersEXT");
    PFNGLBINDFRAMEBUFFERPROC bind_framebuffer = load<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer", "glBindFramebufferEXT");
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture = load<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    PFNGLCHECKFRAMEBUFFERSTATUSPROC framebuffer_status = load<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
    PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffers = load<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers", "glDeleteFramebuffersEXT");

    GLuint shader(GLenum kind, const std::string& source) const {
        GLuint shader = create_shader(kind);
        require(shader != 0, "glCreateShader failed");
        const char* text = source.c_str();
        shader_source(shader, 1, &text, nullptr);
        compile_shader(shader);
        GLint good = GL_FALSE;
        shader_status(shader, GL_COMPILE_STATUS, &good);
        if (!good) {
            GLint length = 0;
            shader_status(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
            shader_log(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            delete_shader(shader);
            throw std::runtime_error("shader compilation failed: " + log);
        }
        return shader;
    }

    GLuint program(const std::string& fragment) const {
        GLuint vertex = shader(GL_VERTEX_SHADER, "#version 120\nvoid main() { gl_Position = gl_Vertex; }\n");
        GLuint pixel = shader(GL_FRAGMENT_SHADER, fragment);
        GLuint program = create_program();
        require(program != 0, "glCreateProgram failed");
        attach_shader(program, vertex);
        attach_shader(program, pixel);
        link_program(program);
        delete_shader(vertex);
        delete_shader(pixel);
        GLint good = GL_FALSE;
        program_status(program, GL_LINK_STATUS, &good);
        if (!good) {
            GLint length = 0;
            program_status(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
            program_log(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            delete_program(program);
            throw std::runtime_error("shader linking failed: " + log);
        }
        return program;
    }
};

const char* fragment_checks = R"GLSL(
uniform int checkIndex;
uniform sampler2D sourceMap;
void main() {
    vec4 normal = texture2D(sourceMap, vec2(0.5));
    vec4 color = vec4(0.2, 0.4, 0.6, 0.8);
    vec4 ambient = vec4(0.1, 0.2, 0.3, 0.4);
    vec4 skills = vec4(2.0, 3.0, 4.0, 8.0);
    vec3 view = vec3(0.0, 0.0, 2.0);
    vec3 sun = vec3(0.0, 0.0, 1.0);
    vec4 light = vec4(0.25, 0.5, 0.75, 0.8);
    vec4 cube = vec4(0.8, 0.6, 0.4, 0.2);
    if (checkIndex == 0 || checkIndex == 1) {
        vec4 clip;
        vec3 tangentView, tangentSun;
        c4_normal_vertex(mat4(1.0), mat4(1.0), vec4(0.0,0.0,0.0,1.0),
            vec3(0.0,0.0,1.0), vec3(1.0,0.0,0.0), vec3(0.0,1.0,0.0),
            vec3(0.0,-1.0,0.0), checkIndex == 0 ? vec3(0.0) : vec3(1.0,0.0,0.0),
            clip, tangentView, tangentSun);
        gl_FragColor = checkIndex == 0 ? vec4(tangentView, clip.w) : vec4(tangentSun, clip.w);
    } else if (checkIndex == 2) {
        gl_FragColor = c4_normal_shade(color,normal,view,sun,ambient,skills,1.0);
    } else if (checkIndex == 3) {
        gl_FragColor = c4_normal_shade(color,normal,view,sun,ambient,skills,normal.a);
    } else if (checkIndex == 4) {
        gl_FragColor = c4_normal_shadow_shade(color,normal,light,view,sun,ambient,skills,1.0);
    } else if (checkIndex == 5) {
        gl_FragColor = c4_normal_shadow_shade(color,normal,light,view,sun,ambient,skills,normal.a);
    } else if (checkIndex == 6) {
        gl_FragColor = c4_cube_normal_shade(color,normal,cube,view,sun,ambient,skills,0.25);
    } else if (checkIndex == 7) {
        gl_FragColor = c4_cube_normal_car_shade(color,normal,cube,view,sun,ambient,skills,0.25);
    } else if (checkIndex == 8) {
        gl_FragColor = vec4(c4_cube_normal_fresnel(normal,vec3(1.0,0.0,0.0)),
            c4_cube_normal_fresnel(normal,-view), c4_cube_normal_fresnel(normal,view),
            c4_cube_normal_fresnel(normal,vec3(0.8660254038,0.0,0.5)));
    } else if (checkIndex == 9) {
        gl_FragColor = c4_normal_shade(vec4(1.0),normal,view,sun,vec4(0.0),vec4(0.0,1.0,0.0,8.0),1.0);
    } else if (checkIndex == 10) {
        vec3 terrain = c4TerrainBase(vec3(0.2,0.4,0.6),vec3(0.5,0.25,1.0));
        terrain = c4TerrainLayer(terrain,vec3(0.8,0.4,0.2),vec3(0.5,0.25,1.0),0.25);
        terrain = c4TerrainLayer(terrain,vec3(0.4,0.8,0.9),vec3(0.5,0.25,1.0),0.6);
        // Fixed-function helper establishes RGB only; attachment alpha is explicit here.
        gl_FragColor = vec4(terrain,1.0);
    } else if (checkIndex == 11) {
        vec3 prior = vec3(0.1,0.2,0.3);
        vec3 source = vec3(0.8,0.6,0.4);
        gl_FragColor = vec4(c4TerrainLayer(prior,source,vec3(1.0),1.0).rg,
                            c4TerrainLayer(prior,source,vec3(1.0),0.0).rg);
    } else if (checkIndex == 12) {
        gl_FragColor = c4BloomDown(vec4(0.4,0.4,0.4,0.0),vec4(0.6,0.6,0.6,0.2),
            vec4(0.8,0.8,0.8,0.4),vec4(0.76,0.76,0.76,0.8),0.28);
    } else if (checkIndex == 13) {
        vec4 blur = vec4(0.0);
        for (int tap=0; tap<21; ++tap)
            blur += c4BloomTap(texture2D(sourceMap,c4BloomHCoordinate(vec2(0.5),tap,0.188)));
        gl_FragColor = blur;
    } else if (checkIndex == 14) {
        gl_FragColor = c4BloomComposite(vec4(0.2,0.4,0.6,0.7),vec4(0.1,0.2,0.3,0.5),0.469,0.516,0.094);
    } else if (checkIndex == 15) {
        gl_FragColor = vec4(c4BloomHCoordinate(vec2(0.5),0,0.188),
                            c4BloomVCoordinate(vec2(0.5),20,0.188));
    } else if (checkIndex == 16) {
        gl_FragColor = vec4(c4_cube_normal_direction(mat4(1.0),vec3(0.0,0.0,1.0),vec3(0.0,0.0,-2.0)),1.0);
    } else if (checkIndex == 17) {
        gl_FragColor = c4BloomDown(normal,normal,normal,normal,0.28);
    } else if (checkIndex == 18) {
        gl_FragColor = vec4(c4BloomDownCoordinate(vec2(0.5),1),c4BloomDownCoordinate(vec2(0.5),3));
    } else if (checkIndex == 19) {
        vec4 blur = vec4(0.0);
        for (int tap=0; tap<21; ++tap)
            blur += c4BloomTap(texture2D(sourceMap,c4BloomVCoordinate(vec2(0.5),tap,0.188)));
        gl_FragColor = blur;
    } else if (checkIndex == 20) {
        mat4 waterTransform = mat4(vec4(40.0,0.0,0.0,0.0),vec4(0.0,40.0,0.0,0.0),
            vec4(0.125,0.25,0.0,0.0),vec4(0.0,0.0,0.0,1.0));
        gl_FragColor = vec4(c4TextureTransform(vec2(0.25,0.5),waterTransform),
            c4WaterBumpCoordinates(vec2(0.25,0.5),vec2(0.2,-0.3),vec4(0.5,0.25,-0.4,0.8)));
    } else if (checkIndex == 21) {
        C4SourceLight light;
        light.diffuse = light.specular = light.ambient = vec4(0.0);
        light.positionRange = vec4(0.0,0.0,0.0,10.0);
        light.directionType = vec4(0.0,0.0,-1.0,1.0);
        light.attenuationFalloff = vec4(1.0,0.0,0.0,2.0);
        light.coneEnabled = vec4(0.0,0.0,1.0,0.0);
        float atRange = c4_source_attenuation(light,10.0);
        float beyondRange = c4_source_attenuation(light,10.001);
        light.directionType.w = 2.0;
        gl_FragColor = vec4(atRange,beyondRange,
            c4_source_spot(light,vec3(0.0,0.0,1.0)),c4_source_spot(light,vec3(1.0,0.0,0.0)));
    } else if (checkIndex == 22) {
        C4SourceLighting lighting;
        lighting.fogColor = vec4(0.8,0.6,0.2,0.9);
        lighting.status = vec4(1.0,0.0,1.0,3.0);
        lighting.fogParams = vec4(10.0,20.0,1.0,0.0);
        gl_FragColor = c4_source_fog(lighting,vec4(0.2,0.4,0.8,0.3),15.0);
    } else if (checkIndex == 23) {
        gl_FragColor = vec4(c4WaterBumpCoordinates(vec2(0.25,0.5),normal.rg,
            vec4(0.5,0.25,-0.4,0.8)),0.0,1.0);
    } else if (checkIndex == 24) {
        gl_FragColor = vec4(c4FixedTextureAlpha(0.8,0.25,false),
            c4FixedTextureAlpha(0.8,0.25,true),c4FixedTextureAlpha(1.5,0.25,false),
            c4FixedTextureAlpha(1.5,2.0,true));
    } else if (checkIndex == 25) {
        gl_FragColor = vec4(c4FixedModel(vec3(0.5),vec3(0.5),true).r,
            c4FixedModel(vec3(0.5),vec3(0.5),false).r,c4TerrainBase(vec3(0.5),vec3(0.5)).r,
            c4FixedModel(vec3(0.8),vec3(0.8),true).r);
    }
}
)GLSL";

struct Check {
    const char* name;
    int index;
    Pixel expected;
    Pixel input{0.5f,0.5f,1.0f,0.25f};
};

void event_checks() {
    using namespace c4::rendering;
    using c4::physics::Var;
    const auto check_view_event = [](std::string_view name,MaterialViewEvent expected) {
        require(source_material_view_event(name) == expected,"source material callback classification: " + std::string(name));
        std::string uppercase(name);
        for (char& character : uppercase)
            if (character >= 'a' && character <= 'z') character = static_cast<char>(character-'a'+'A');
        require(source_material_view_event(uppercase) == expected,"uppercase material callback classification: " + uppercase);
        for (std::size_t index=1; index<uppercase.size(); index+=2)
            if (uppercase[index] >= 'A' && uppercase[index] <= 'Z')
                uppercase[index] = static_cast<char>(uppercase[index]-'A'+'a');
        require(source_material_view_event(uppercase) == expected,"mixed-case material callback classification: " + uppercase);
    };
    for (const auto name : {"Mtl_arka_far","Mtl_bayrak2","Mtl_ghost","Mtl_c_ball",
                            "Mtl_kapsul_ust2","Mtl_gise_reklam","Mtl_kulube_cam",
                            "Mtl_silindir","Mtl_kule_tut","Mtl_korkuluk","Mtl_arac_farcam",
                            "Mtl_ic_tavan","Mtl_ic_panel","Mtl_c4_ayna_22","Mtl_tentem"})
        check_view_event(name,MaterialViewEvent::Unconditional);
    for (const auto name : {"Mtl_loop_reklam","Mtl_robot_cs2","Mtl_vinc_mod","Mtl_afis_site",
                            "Mtl_kul_car","Mtl_kul_robot","Mtl_siyah_car","Mtl_siyah_robot",
                            "Mtl_car3","Mtl_robot_kaporta","Mtl_arac_cam","Mtl_hell",
                            "Mtl_spooot","Mtl_yeni_atmos_sag","Mtl_yeni_atmos_sol"})
        check_view_event(name,MaterialViewEvent::NonzeroSkill1);
    for (const auto name : {"Mtl_tabela","Mtl_r_kaporta"})
        check_view_event(name,MaterialViewEvent::CubeConversionOnly);
    for (const auto name : {"","not_a_source_material","Mtl_loop_reklam_bez","Mtl_denizwater"})
        check_view_event(name,MaterialViewEvent::None);
    NativeMaterialState material;
    for (unsigned index=0; index<16; ++index) material.texture_transform[index] = static_cast<float>(index)+0.125f;
    auto factory = material;
    factory.technique = MaterialTechnique::Water;
    factory.source_profile = c4::assets::high_source_profile;
    factory.diffuse = {0.1f,0.2f,0.3f,0.4f};
    factory.power = 31;
    factory.skills.fill(17);
    factory.entity_skins[0] = 41;
    factory.material_skins[0] = 42;
    const auto factory_metadata = factory;
    WaterMaterialEventState factory_event{Var{-12345},Var{67890},7};
    initialize_water_material_source_defaults(factory_event,factory);
    require(factory_event.skill1.raw == 0 && factory_event.skill2.raw == 0 &&
            factory_event.active_processes == 0,"fresh water MATERIAL clears counters and process registrations");
    for (float value : factory.texture_transform)
        require(value == 0 && !std::signbit(value),"water MATERIAL factory clears all matrix entries to positive zero, not identity");
    require(factory.technique == factory_metadata.technique && factory.source_profile == factory_metadata.source_profile &&
            factory.diffuse == factory_metadata.diffuse && factory.power == factory_metadata.power &&
            factory.skills == factory_metadata.skills && factory.entity_skins == factory_metadata.entity_skins &&
            factory.material_skins == factory_metadata.material_skins,"water source-default initializer preserves other material metadata");
    initialize_water_material_transform(factory);
    for (unsigned index=0; index<16; ++index)
        require(factory.texture_transform[index] == (index == 0 || index == 5 ? 40.0f : 0.0f),
                "water assignment follows zeroed source factory matrix with only matrix11/22 scale");
    const auto before_initialization = material.texture_transform;
    initialize_water_material_transform(material);
    for (unsigned index=0; index<16; ++index)
        require(material.texture_transform[index] ==
                (index == 0 || index == 5 ? 40.0f : before_initialization[index]),
                "water assignment initializer sets only source matrix11 and matrix22 to 40");
    const auto untouched = material.texture_transform;
    WaterMaterialEventState water;
    start_water_material_event(water,material,Var{1025});
    require(water.active_processes == 1 && water.skill1.raw == 512 && water.skill2.raw == 512,
            "first water event registers and immediately executes one Q10 iteration");
    require(material.texture_transform[8] == 0.001953125f && material.texture_transform[9] == 0.001953125f,
            "water floatd preserves fractional Q10 value before dividing by 256");
    start_water_material_event(water,material,Var{1025});
    require(water.active_processes == 2 && water.skill1.raw == 1024 && water.skill2.raw == 1024,
            "second entity starts another process on the shared material counters");
    require(material.texture_transform[8] == 0.00390625f && material.texture_transform[9] == 0.00390625f,
            "second initial water iteration reaches shared transform");
    // The consumer selected both known waiting records for this frame.
    resume_water_material_event(water,material,Var{1025});
    resume_water_material_event(water,material,Var{1025});
    require(water.active_processes == 2 && water.skill1.raw == 2048 && water.skill2.raw == 2048,
            "two selected waiting records each resume once without registering new processes");
    require(material.texture_transform[8] == 0.0078125f && material.texture_transform[9] == 0.0078125f,
            "two water processes advance shared floatd translation");
    for (unsigned index=0; index<16; ++index)
        if (index != 8 && index != 9)
            require(material.texture_transform[index] == untouched[index],"water event preserves nontranslation matrix fields");
    // Next frame only one of the two waiting receivers is eligible.
    constexpr unsigned eligible_receiver_count = 1;
    for (unsigned receiver = 0; receiver < eligible_receiver_count; ++receiver)
        resume_water_material_event(water,material,Var{1025});
    require(water.active_processes == 2 && water.skill1.raw == 2560 && water.skill2.raw == 2560 &&
            material.texture_transform[8] == 0.009765625f &&
            material.texture_transform[9] == 0.009765625f,
            "one eligible receiver out of two performs exactly one Var update and preserves registrations");
    WaterMaterialEventState reverse;
    start_water_material_event(reverse,material,Var{-1025});
    require(reverse.skill1.raw == -512 && reverse.skill2.raw == -512 &&
            material.texture_transform[8] == -0.001953125f,"water negative fractional multiplication truncates towards zero");
    WaterMaterialEventState wrapped;
    wrapped.skill1 = Var{2147483647};
    wrapped.skill2 = Var{2147483647};
    start_water_material_event(wrapped,material,Var{1024});
    require(wrapped.skill1.raw == -2147483137 && wrapped.skill2.raw == -2147483137,
            "water material counters retain signed Q10 wrapping addition");
    require(material.texture_transform[8] == -8191.998046875f &&
            material.texture_transform[9] == -8191.998046875f,"water floatd converts wrapped Q10 to original float precision");
    std::array<float,16> inverse_view{};
    for (unsigned index=0; index<16; ++index) inverse_view[index] = static_cast<float>(index)+0.25f;
    update_environment_material_view(material,inverse_view);
    for (unsigned index=0; index<16; ++index)
        require(material.texture_transform[index] == (index >= 12 && index <= 14 ? 0.0f : inverse_view[index]),
                "environment material copies the complete inverse-view matrix with translation cleared");
}

void source_water_material_action_checks() {
    using namespace c4::rendering;
    for (const std::uint8_t known_mask : {0u, 0xfu}) {
        NativeMaterialState assignment;
        assignment.material_skins_known_mask = known_mask;
        apply_source_water_material_action(assignment,73u);
        require(assignment.material_skins[0] == 73u &&
                assignment.material_skins_known_mask == (known_mask | 1u),
                "water bitmap assignment proves only slot0 and preserves existing pointer knowledge");
    }
    NativeMaterialState water;
    water.source_profile = c4::assets::original_source_profile;
    configure_material_state(water,"Mtl_denizwater",0);
    initialize_source_material_event_state(water,"Mtl_denizwater");
    require(water.source_event_known && water.source_event == SourceMaterialEvent::WaterLoop,
            "water action fixture starts with the actual source MATERIAL factory EVENT");
    water.source_flags |= 0x40000u;
    water.material_skins[1] = 19;
    water.diffuse = {0.125f,0.25f,0.5f,0.75f};
    water.power = 31;
    water.skills.fill(17);
    water.skills[0] = 32;
    water.skills[1] = 64;
    for (unsigned index=0; index<16; ++index)
        water.texture_transform[index] = static_cast<float>(index)+0.125f;
    water.texture_transform[8] = 0.125f;
    water.texture_transform[9] = 0.25f;
    const auto original_matrix = water.texture_transform;
    const auto original_skills = water.skills;
    const auto original_flags = water.source_flags;
    const auto original_flags_known = water.source_flags_known_mask;
    for (const std::uint32_t actual_bitmap_id : {73u,91u}) {
        apply_source_water_material_action(water,actual_bitmap_id);
        require(water.technique == MaterialTechnique::Water &&
                water.material_skins[0] == actual_bitmap_id && water.material_skins[1] == 19,
                "repeated source action on the same MATERIAL replaces the actual MTI bitmap and effect only");
        for (unsigned index=0; index<16; ++index)
            require(water.texture_transform[index] ==
                    (index == 0 || index == 5 ? 40.0f : original_matrix[index]),
                    "water action rewrites only matrix11/22 and preserves warm scrolling counters");
        require(water.source_event_known && water.source_event == SourceMaterialEvent::WaterLoop &&
                water.source_flags == original_flags && water.source_flags_known_mask == original_flags_known &&
                water.skills == original_skills && water.diffuse == std::array<float,4>{0.125f,0.25f,0.5f,0.75f} &&
                water.power == 31,
                "water action preserves registered EVENT, runtime flags, skills and unrelated material values");
        require(original_material_draw_state(water,false).technique == MaterialTechnique::FixedFunction &&
                water.technique == MaterialTechnique::Water && water.material_skins[0] == actual_bitmap_id,
                "NOSHADER draw selection cannot erase source action writes or suppress a repeated same-identity action");
    }
}

void source_material_event_protocol_checks() {
    using namespace c4::rendering;
    constexpr std::uint32_t guard_bits = 0xe0000u;
    constexpr std::uint32_t enable_view = 0x40000u;
    NativeMaterialState water;
    water.source_profile = c4::assets::original_source_profile;
    configure_material_state(water,"Mtl_denizwater",0);
    initialize_source_material_event_state(water,"Mtl_denizwater");
    require(water.technique == MaterialTechnique::FixedFunction &&
            water.source_event_known && water.source_event == SourceMaterialEvent::WaterLoop &&
            (water.source_flags_known_mask & guard_bits) == guard_bits &&
            (water.source_flags & guard_bits) == 0 &&
            source_material_default_event_eligible(water),
            "ALL0 water registers its source EVENT independently of its fixed-function effect");
    NativeMaterialState atmosphere;
    atmosphere.source_profile = c4::assets::original_source_profile;
    // c4_materials.wdl:1289-1310 selects this FX even with ALL0 selectors.
    atmosphere.technique = material_from_effect("EnvCube_Mix.fx");
    configure_material_state(atmosphere,"Mtl_yeni_atmos_sol",0);
    initialize_source_material_event_state(atmosphere,"Mtl_yeni_atmos_sol");
    require(atmosphere.technique == MaterialTechnique::EnvCubeMix &&
            atmosphere.texture_factor_known && atmosphere.texture_factor == 0x0a000000u &&
            atmosphere.source_event_known &&
            atmosphere.source_event == SourceMaterialEvent::EnvironmentInitializeForGatedView &&
            source_material_default_event_eligible(atmosphere),
            "ALL0 atmosphere packs authored nonzero reflection and registers its eligible gated-view initializer");
    for (float value : atmosphere.texture_transform)
        require(value == 0 && !std::signbit(value),
                "ALL0 atmosphere retains constructor positive-zero matMtl before its first source VIEW");
    c4::assets::WorldScene world;
    world.source_profile = atmosphere.source_profile;
    const auto first_view = source_inverse_view_matrix(
        source_game_visual_state(world,{123,456,789},{37.25f,-19.5f,0}));
    update_environment_material_view(atmosphere,first_view);
    const auto first_matrix = atmosphere.texture_transform;
    require(first_matrix != std::array<float,16>{},
            "ALL0 atmosphere first source VIEW replaces cold matMtl with the source camera transform");
    const auto second_view = source_inverse_view_matrix(
        source_game_visual_state(world,{-321,654,987},{12.75f,8.5f,2}));
    update_environment_material_view(atmosphere,second_view);
    require(atmosphere.texture_transform != first_matrix,
            "ALL0 atmosphere matMtl changes with the next actual source VIEW, not a constructor default");
    for (const auto& matrix : {first_matrix,atmosphere.texture_transform})
        for (unsigned index = 12; index <= 14; ++index)
            require(matrix[index] == 0 && !std::signbit(matrix[index]),
                    "ALL0 atmosphere source VIEW clears translation to positive zero");
    for (const auto name : {"not_a_source_material",""}) {
        NativeMaterialState unknown;
        configure_material_state(unknown,name,0);
        initialize_source_material_event_state(unknown,name);
        require(unknown.source_event == SourceMaterialEvent::None &&
                !source_material_default_event_eligible(unknown),
                "missing or empty material cannot acquire a default source EVENT dispatch");
        if (*name)
            require(!unknown.source_event_known,
                    "undefined material EVENT remains unknown rather than proven absent");
    }
    NativeMaterialState environment;
    configure_material_state(environment,"Mtl_arka_far",0);
    initialize_source_material_event_state(environment,"Mtl_arka_far");
    require(environment.source_event_known &&
            environment.source_event == SourceMaterialEvent::EnvironmentInitialize &&
            source_material_default_event_eligible(environment),
            "authored environment initializer is initially eligible for default dispatch");
    environment.source_event = SourceMaterialEvent::EnvironmentView;
    environment.source_flags |= enable_view;
    require(!source_material_default_event_eligible(environment),
            "environment view callback and ENABLE_VIEW stop default dispatch");
    const auto persistent_flags = environment.source_flags;
    const auto persistent_mask = environment.source_flags_known_mask;
    configure_material_state(environment,"Mtl_arka_far",1u << 10,25);
    require(environment.source_event_known &&
            environment.source_event == SourceMaterialEvent::EnvironmentView &&
            environment.source_flags == persistent_flags &&
            environment.source_flags_known_mask == persistent_mask &&
            environment.entity_alpha == 0.25f &&
            !source_material_default_event_eligible(environment),
            "same-identity alpha and entity flag updates preserve runtime MATERIAL EVENT and flags");
    (void)original_material_draw_state(environment,true);
    (void)original_material_draw_state(environment,false);
    (void)original_material_draw_state(water,false);
    require(environment.source_event == SourceMaterialEvent::EnvironmentView &&
            environment.source_event_known && environment.source_flags == persistent_flags &&
            environment.source_flags_known_mask == persistent_mask &&
            !source_material_default_event_eligible(environment) &&
            water.source_event == SourceMaterialEvent::WaterLoop &&
            source_material_default_event_eligible(water),
            "NOSHADER draw descriptors neither replace registered EVENT nor change dispatch eligibility");
    NativeMaterialState gated_environment;
    configure_material_state(gated_environment,"Mtl_loop_reklam",0);
    gated_environment.skills[0] = 0;
    initialize_source_material_event_state(gated_environment,"Mtl_loop_reklam");
    require(gated_environment.source_event_known &&
            gated_environment.source_event == SourceMaterialEvent::EnvironmentInitializeForGatedView &&
            gated_environment.skills[0] == 0 &&
            source_material_default_event_eligible(gated_environment),
            "gated-view initializer remains unconditional even when skill1 is zero");
    gated_environment.source_event = SourceMaterialEvent::EnvironmentViewNonzeroSkill1;
    gated_environment.source_flags |= enable_view;
    configure_material_state(gated_environment,"Mtl_loop_reklam",1u << 10,25);
    require(gated_environment.source_event == SourceMaterialEvent::EnvironmentViewNonzeroSkill1 &&
            gated_environment.source_event != environment.source_event &&
            (gated_environment.source_flags & enable_view) != 0 &&
            !source_material_default_event_eligible(gated_environment),
            "skill1-gated view callback retains its distinct EVENT identity across reconfiguration");
    water.source_flags_known_mask = guard_bits & ~enable_view;
    require(water.source_event_known && !source_material_default_event_eligible(water),
            "known EVENT with incomplete default-guard flag proof fails closed");
}

void source_material_tangent_request_checks() {
    using namespace c4::rendering;
    NativeMaterialState unknown;
    unknown.technique = MaterialTechnique::CubeNormalCar;
    unknown.source_flags = 0x10000u;
    require(!source_material_tangent_request(&unknown).has_value(),
            "a normal shader cannot prove an unknown MATERIAL tangent bit");
    require(source_material_tangent_request(nullptr) == false,
            "absent source MATERIAL pointers do not request tangent generation");
    NativeMaterialState cylinder;
    cylinder.source_profile = c4::assets::high_source_profile;
    configure_material_state(cylinder,"Mtl_silindir",0);
    initialize_source_material_event_state(cylinder,"Mtl_silindir");
    require(source_material_tangent_request(&cylinder) == true &&
            source_material_tangent_request(&unknown,&cylinder) == true,
            "authored high cylinder bit requests tangents independently of an unknown current MATERIAL");
    cylinder.source_flags |= 0x40000u;
    const auto original_flags = cylinder.source_flags;
    configure_material_state(cylinder,"Mtl_silindir",0);
    require(cylinder.source_material_initialized && cylinder.source_flags == original_flags &&
            source_material_tangent_request(&cylinder) == true,
            "draw reconfiguration preserves the actual tangent and callback bits");
    NativeMaterialState imported;
    imported.source_profile = c4::assets::high_source_profile;
    configure_material_state(imported,"Mtl_SH_anayol",0);
    initialize_source_material_event_state(imported,"Mtl_SH_anayol");
    imported.source_flags |= 0x40000u;
    const auto before_import_flags = imported.source_flags;
    require(source_imported_mesh_material("shm_anayol") == "Mtl_SH_anayol" &&
            apply_source_imported_mesh_tangent_off(imported,"SHM_anayol") &&
            source_material_tangent_request(&imported) == false &&
            imported.source_flags == (before_import_flags & ~0x10000u),
            "recovered bmesh action clears only its declared shared MATERIAL tangent bit");
    require(!source_material_tangent_request(&imported,&unknown).has_value() &&
            source_material_tangent_request(&imported,&cylinder) == true,
            "imported tangent-off does not bypass an unknown or requesting current MATERIAL");
    const auto imported_flags = imported.source_flags;
    require(!apply_source_imported_mesh_tangent_off(imported,"SHM_tup_yol") &&
            !apply_source_imported_mesh_tangent_off(imported,"unknown_action") &&
            imported.source_flags == imported_flags,
            "non-import and unknown actions cannot acquire an invented tangent-off write");
    for (const auto name : {"Mtl_kul_car", "Mtl_kul_robot"}) {
        NativeMaterialState car;
        car.source_profile.shq = 1;
        car.source_cube_capable = false;
        configure_material_state(car,name,0);
        initialize_source_material_event_state(car,name);
        require(source_material_tangent_request(&car) == true,
                "CubeNormal car and robot authored tangent writes precede the cube capability gate");
        car.source_shader_model2 = false;
        configure_material_state(car,name,0);
        initialize_source_material_event_state(car,name);
        require(source_material_tangent_request(&car) == false,
                "unsupported source shader branch leaves the recovered car and robot tangent bit off");
    }
    NativeMaterialState original;
    configure_material_state(original,"Mtl_silindir",0);
    initialize_source_material_event_state(original,"Mtl_silindir");
    require(source_material_tangent_request(&original) == false,
            "ALL0 cylinder does not execute its high-branch tangent write");
    original.source_profile.shq = 1;
    configure_material_state(original,"Mtl_silindir",0);
    initialize_source_material_event_state(original,"Mtl_silindir");
    require(source_material_tangent_request(&original) == false,
            "SHQ1 cylinder stays off even while SHQ1 car and robot are on");
    for (const auto name : {"Mtl_yol_kum", "Mtl_r_kaporta", "Mtl_tabela"}) {
        for (unsigned stq : {0u, 3u}) {
            NativeMaterialState normal;
            normal.source_profile = c4::assets::high_source_profile;
            normal.source_profile.stq = static_cast<std::uint8_t>(stq);
            normal.source_cube_capable = false;
            configure_material_state(normal,name,0);
            initialize_source_material_event_state(normal,name);
            require(source_material_tangent_request(&normal) == true &&
                    (normal.source_flags_known_mask & 0x10000u) != 0 &&
                    (normal.source_flags & 0x10000u) != 0,
                    "authored high tangent writes precede shadow and cube capability gates");
            if (name != std::string_view{"Mtl_yol_kum"}) {
                require(normal.source_material_initialized &&
                        !normal.material_constants_known && !normal.source_event_known &&
                        (normal.source_flags_known_mask & 0xe0000u) == 0,
                        "proven CubeNormalMix tangent writes cannot establish incomplete constants or EVENT guards");
            }
        }
    }
    NativeMaterialState invalid;
    invalid.source_profile = c4::assets::high_source_profile;
    invalid.source_profile.shq = 3;
    configure_material_state(invalid,"Mtl_tabela",0);
    initialize_source_material_event_state(invalid,"Mtl_tabela");
    require(!source_material_tangent_request(&invalid).has_value() &&
            (invalid.source_flags_known_mask & 0x10000u) == 0,
            "an invalid source profile cannot prove an authored tangent branch");
    for (const auto name : {"Mtl_arka_far", "mtl_denizwater", "mat_multi_anadolu"}) {
        NativeMaterialState off;
        off.source_profile = c4::assets::high_source_profile;
        configure_material_state(off,name,0);
        initialize_source_material_event_state(off,name);
        require(source_material_tangent_request(&off) == false,
                "complete recovered EnvCube, water and terrain declarations have no tangent activation");
    }
}

void gamma_checks() {
    using c4::rendering::decode_original_gamma;
    std::array<std::uint8_t,1280> bytes{};
    for (unsigned plane=0; plane<4; ++plane)
        for (unsigned index=0; index<256; ++index)
            bytes[plane*256+index] = static_cast<std::uint8_t>(index);
    const auto identity = decode_original_gamma(bytes,0);
    require(!identity.corrected_active,"gamma correction starts inactive");
    for (unsigned channel=0; channel<3; ++channel)
        for (unsigned index=0; index<256; ++index) {
            require(identity.corrected[channel][index] == index*256,"identity source gamma ramp");
            require(identity.defaults[channel][index] == index*256,"identity default gamma ramp");
        }
    for (unsigned index=0; index<256; ++index)
        bytes[256+index] = static_cast<std::uint8_t>(255-index);
    const auto reversed = decode_original_gamma(bytes,0);
    for (unsigned index=0; index<256; ++index) {
        require(reversed.corrected[0][index] == (255-index)*256,"red gamma plane controls LUT index");
        require(reversed.corrected[1][index] == index*256 &&
                reversed.corrected[2][index] == index*256,"red index must not alter green or blue");
        require(reversed.defaults[0][index] == index*256,"default gamma ignores source remapping");
    }
    const auto bright = decode_original_gamma(bytes,5);
    require(bright.corrected[0][0] == 65280 && bright.corrected[0][5] == 65280 &&
            bright.corrected[0][255] == 1280,"positive brightness clamps remapped gamma at 255");
    require(bright.corrected[1][0] == 1280 && bright.corrected[1][250] == 65280 &&
            bright.corrected[1][255] == 65280,"positive brightness clamps identity gamma at 255");
    require(bright.defaults[0][0] == 1280 && bright.defaults[2][255] == 65280,
            "default gamma ramp includes brightness");
    const auto dark = decode_original_gamma(bytes,-5);
    require(dark.corrected[1][0] == 0 && dark.corrected[1][5] == 0 &&
            dark.corrected[1][6] == 256,"negative brightness clamps gamma at zero");
    bytes[7] = 33;
    const auto remapped = decode_original_gamma(bytes,0);
    require(remapped.corrected[0][248] == 8448 && remapped.corrected[1][7] == 8448 &&
            remapped.corrected[2][7] == 8448,"gamma index resolves through shared lookup plane");
    for (unsigned index=1024; index<bytes.size(); ++index) bytes[index] = 255;
    const auto trailing = decode_original_gamma(bytes,0);
    const auto minimum = decode_original_gamma(std::span<const std::uint8_t>(bytes.data(),1024),0);
    require(trailing.corrected == remapped.corrected && trailing.defaults == remapped.defaults &&
            minimum.corrected == remapped.corrected && minimum.defaults == remapped.defaults,
            "gamma consumes only the first 1024 bytes of the original 1280-byte resource");
    bool rejected = false;
    try {
        (void)decode_original_gamma(std::span<const std::uint8_t>(bytes.data(),1023),0);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected,"gamma must reject a truncated 1023-byte resource");
}

void builtin_material_constructor_draw_checks() {
    using namespace c4::rendering;
    struct Builtin {
        const char* name;
        const char* mixed_case;
        double ambient, diffuse, specular, emissive;
        float power, albedo;
    };
    constexpr Builtin builtins[] = {
        {"mat_model","mAt_MoDeL",200,200,0,0,0,50},
        {"mat_sprite","mAt_SpRiTe",200,128,0,0,0,0},
        {"mat_metal","mAt_MeTaL",100,100,255,0,10,100},
        {"mat_terrain","mAt_TeRrAiN",200,200,0,0,0,50},
        {"mat_unlit","mAt_UnLiT",0,0,0,128,0,0}
    };
    const auto rgba = [](double value) {
        const float encoded = static_cast<float>(value*0.003921568859368563);
        return std::array<float,4>{encoded,encoded,encoded,1.0f};
    };
    for (auto profile : {c4::assets::original_source_profile,c4::assets::high_source_profile})
        for (const auto& builtin : builtins)
            for (const char* name : {builtin.name,builtin.mixed_case}) {
                NativeMaterialState material;
                material.source_profile = profile;
                material.technique = MaterialTechnique::Unknown;
                material.skills.fill(-0.0f);
                material.texture_transform.fill(-0.0f);
                material.source_event = SourceMaterialEvent::EnvironmentView;
                material.source_flags = material.source_flags_known_mask = 0xffffffffu;
                material.material_skins.fill(73u);
                material.material_skins_known_mask = 0;
                require(initialize_builtin_material_state(material,name),
                        "exact case-insensitive engine builtin constructor lookup");
                require(material.technique == MaterialTechnique::Unknown && material.source_profile == profile,
                        "builtin source construction preserves the caller's effect selection and source profile");
                require(material.source_material_assigned && material.material_constants_known &&
                        material.ambient == rgba(builtin.ambient) && material.diffuse == rgba(builtin.diffuse) &&
                        material.specular == rgba(builtin.specular) && material.emissive == rgba(builtin.emissive) &&
                        material.color_known_mask == 0xfff && material.ambient_alpha_known &&
                        material.power_known && material.power == builtin.power &&
                        material.material_albedo_known && material.material_albedo_percent == builtin.albedo,
                        "builtin constructor uses recovered double-to-float RGB encoding, alpha and source fields");
                require(material.skills_known_mask == 0xfffff,
                        "all twenty builtin skills have recovered raw-zero constructor proof");
                require(material.material_skins_known_mask == 0xfu,
                        "fresh builtin allocation proves all four source bitmap pointers are known");
                for (const auto skin : material.material_skins)
                    require(!skin,"fresh builtin source bitmap pointers are NULL");
                for (float value : material.skills)
                    require(value == 0 && !std::signbit(value),"fresh builtin skills encode native positive zero");
                for (float value : material.texture_transform)
                    require(value == 0 && !std::signbit(value),"fresh builtin matrix is positive zero, not identity");
                require(material.source_event_known && material.source_event == SourceMaterialEvent::None &&
                        material.source_flags == 0 && material.source_flags_known_mask == 0xf0000u &&
                        !source_material_default_event_eligible(material),
                        "fresh builtin has known NULL EVENT, clear tangent and clear default-dispatch guard bits");
                material.technique = MaterialTechnique::FixedFunction;
                configure_material_state(material,name,0);
                require(material.technique == MaterialTechnique::FixedFunction && material.source_profile == profile &&
                        material.ambient == rgba(builtin.ambient) && material.diffuse == rgba(builtin.diffuse) &&
                        material.specular == rgba(builtin.specular) && material.emissive == rgba(builtin.emissive) &&
                        material.power == builtin.power && material.material_albedo_percent == builtin.albedo &&
                        material.source_event_known && material.source_event == SourceMaterialEvent::None &&
                        material.source_flags == 0 && material.source_flags_known_mask == 0xf0000u,
                        "ALL0 and high profiles do not infer an FX or replace builtin constructor state");

                // An effect/event write on the same live MATERIAL is not another constructor.
                material.technique = MaterialTechnique::EnvCubeMix;
                material.source_event = SourceMaterialEvent::EnvironmentView;
                material.source_flags = 0x40000u;
                material.source_flags_known_mask = 0xffffffffu;
                material.material_skins[0] = 91u;
                material.material_skins_known_mask = 1u;
                for (std::size_t index=0; index<material.skills.size(); ++index)
                    material.skills[index] = static_cast<float>(index)+0.5f;
                for (std::size_t index=0; index<material.texture_transform.size(); ++index)
                    material.texture_transform[index] = static_cast<float>(index)+0.125f;
                const auto live = material;
                configure_material_state(material,name,1u << 10,25);
                (void)original_material_draw_state(material,true);
                (void)original_material_draw_state(material,false);
                require(material.technique == live.technique && material.skills == live.skills &&
                        material.skills_known_mask == live.skills_known_mask &&
                        material.texture_transform == live.texture_transform &&
                        material.source_event == live.source_event && material.source_event_known &&
                        material.source_flags == live.source_flags &&
                        material.source_flags_known_mask == live.source_flags_known_mask &&
                        material.material_skins == live.material_skins &&
                        material.material_skins_known_mask == live.material_skins_known_mask &&
                        material.entity_alpha == 0.25f && !source_material_default_event_eligible(material),
                        "draw reconfiguration preserves live builtin counters, matrix, EVENT, flags and selected effect");

                auto palette = material;
                palette.model_material_present = true;
                palette.ambient = {0.1f,0.2f,0.3f,0.4f};
                palette.diffuse = {0.5f,0.6f,0.7f,0.8f};
                palette.specular = {0.2f,0.3f,0.4f,0.5f};
                palette.emissive = {0.6f,0.7f,0.8f,0.9f};
                palette.power = 17;
                palette.material_albedo_percent = 99;
                palette.material_albedo_known = false;
                const auto prior_palette = palette;
                configure_material_state(palette,name,0);
                require(palette.source_material_assigned && palette.material_constants_known &&
                        palette.ambient == prior_palette.ambient && palette.diffuse == prior_palette.diffuse &&
                        palette.specular == prior_palette.specular && palette.emissive == prior_palette.emissive &&
                        palette.power == prior_palette.power && palette.color_known_mask == prior_palette.color_known_mask &&
                        palette.ambient_alpha_known == prior_palette.ambient_alpha_known &&
                        palette.power_known == prior_palette.power_known &&
                        palette.material_albedo_known && palette.material_albedo_percent == builtin.albedo,
                        "active palette RGBA and power take precedence while builtin source albedo stays known");
                const auto alpha = original_model_alpha_decision(
                    {true,prior_palette.ambient[3],1,false,false,100,false,false,false,prior_palette.diffuse[3]});
                require(alpha.alpha_blend && !alpha.depth_write && alpha.diffuse_alpha == prior_palette.ambient[3],
                        "model alpha reads the explicitly prior-bound palette, not selected builtin opaque alpha");
                palette.ambient_alpha_known = palette.power_known = false;
                palette.color_known_mask = 0;
                configure_material_state(palette,name,0);
                require(!palette.ambient_alpha_known && !palette.power_known && palette.color_known_mask == 0,
                        "builtin draw cannot fabricate proof for an active palette's unknown color or alpha fields");
            }
    NativeMaterialState named_unlit;
    require(initialize_builtin_material_state(named_unlit,"mat_unlit"),"named mat_unlit is an actual builtin MATERIAL");
    NativeMaterialState empty;
    require(!initialize_builtin_material_state(empty,""),"empty source identity is not a builtin constructor");
    configure_material_state(empty,"",0);
    require(named_unlit.source_material_assigned && !empty.source_material_assigned &&
            named_unlit.ambient == empty.ambient && named_unlit.diffuse == empty.diffuse &&
            named_unlit.specular == empty.specular && named_unlit.emissive == empty.emissive &&
            !source_material_default_event_eligible(named_unlit) && !source_material_default_event_eligible(empty),
            "named mat_unlit and NULL source material remain distinct despite identical draw fallback colors");
    for (const char* name : {"mat_model_suffix","prefix_mat_model","mat_model.fx"}) {
        auto rejected = named_unlit;
        require(!initialize_builtin_material_state(rejected,name) &&
                rejected.source_material_assigned == named_unlit.source_material_assigned &&
                rejected.emissive == named_unlit.emissive && rejected.skills == named_unlit.skills &&
                rejected.texture_transform == named_unlit.texture_transform &&
                rejected.source_event_known == named_unlit.source_event_known &&
                rejected.source_flags_known_mask == named_unlit.source_flags_known_mask,
                "builtin constructor rejects nonexact names without mutating source state");
    }
}

void profile_checks() {
    using namespace c4::rendering;
    using c4::assets::SourceProfile;
    const auto rgb = [](double red,double green,double blue) {
        constexpr double original_scale = 0.003921568859368563;
        return std::array<float,4>{static_cast<float>(red*original_scale),
            static_cast<float>(green*original_scale),static_cast<float>(blue*original_scale),1.0f};
    };
    NativeMaterialState low;
    low.entity_skins[0] = 0;
    configure_material_state(low,"Mtl_kuleler",0);
    require(low.source_material_assigned && low.material_constants_known,"default Original profile selects named material constants");
    require(low.ambient == rgb(126,126,132) && low.diffuse == rgb(255,255,255) &&
            low.specular == rgb(4,0,0) && low.emissive == rgb(3,0,0),"Original tower material exact engine RGB normalization");
    require(low.color_known_mask == 0xfff && low.skills_known_mask == 0xfffff &&
            low.power_known && low.power == 0 && low.material_albedo_known &&
            low.material_albedo_percent == 20,"Original tower material field presence and low-profile albedo");
    require(low.skills == std::array<float,20>{} && low.texture_transform == std::array<float,16>{},
            "defined low source material starts with proven zeroed factory skills and matrix");
    NativeMaterialState high;
    high.source_profile = c4::assets::high_source_profile;
    high.entity_skins[0] = 0;
    high.model_material_present = true;
    high.ambient = {0.1f,0.2f,0.3f,0.4f};
    high.diffuse = {0.4f,0.5f,0.6f,0.7f};
    high.specular = {0.4f,0.5f,0.6f,0.7f};
    high.emissive = {0.4f,0.5f,0.6f,0.7f};
    high.skills.fill(17);
    high.texture_transform.fill(19);
    high.power = 13;
    configure_material_state(high,"mTl_KuLeLeR",0);
    require(high.source_material_assigned && high.material_constants_known,"high profile and case-insensitive source material lookup");
    require(high.ambient == rgb(149,143,145) && high.diffuse == std::array<float,4>{1,1,1,1},
            "high tower constants replace palette ambient and retain proven factory-white diffuse");
    constexpr std::array<float,7> high_skills{0.9560546875f,0.5732421875f,0.669921875f,8,-440,-440,-440};
    for (std::size_t index=0; index<high_skills.size(); ++index)
        require(high.skills[index] == high_skills[index],"high tower source Q10 skill payload");
    require(high.skills_known_mask == 0xfffff && high.color_known_mask == 0xfff &&
            high.power_known && high.power == 0 && high.material_albedo_known &&
            high.material_albedo_percent == 50.2177734375f,
            "high tower retains all proven factory fields and exact constructor albedo");
    require(high.specular == std::array<float,4>{0,0,0,1} &&
            high.emissive == std::array<float,4>{0,0,0,1} &&
            high.texture_transform == std::array<float,16>{},"defined source material replaces stale palette and matrix factory fields");
    for (std::size_t index=high_skills.size(); index<high.skills.size(); ++index)
        require(high.skills[index] == 0,"undefined source skill assignments retain known factory zero");
    for (bool cube_capable : {false,true}) {
        NativeMaterialState fallback;
        fallback.source_profile = c4::assets::high_source_profile;
        fallback.source_shader_model2 = false;
        fallback.source_cube_capable = cube_capable;
        configure_material_state(fallback,"Mtl_kuleler",0);
        require(fallback.ambient == low.ambient && fallback.diffuse == low.diffuse &&
                fallback.specular == low.specular && fallback.emissive == low.emissive &&
                fallback.skills == low.skills && fallback.material_albedo_percent == 20,
                "high profile without source shader model2 uses original low material constants");
        NativeMaterialState car;
        car.source_profile = c4::assets::high_source_profile;
        car.source_cube_capable = cube_capable;
        configure_material_state(car,"Mtl_car3",0);
        require(car.material_constants_known && car.skills_known_mask == 0xfffff &&
                car.skills[8] == (cube_capable ? 0.2021484375f : 0.0f),
                "source cube capability selects car skill9 assignment rather than an invented default");
    }
    NativeMaterialState without_cube;
    without_cube.source_profile = c4::assets::high_source_profile;
    without_cube.source_cube_capable = false;
    configure_material_state(without_cube,"Mtl_kuleler",0);
    require(without_cube.ambient == high.ambient && without_cube.skills == high.skills,
            "tower normal shader constants do not depend on cube capability");
    constexpr std::uint8_t SourceProfile::* selectors[] = {
        &SourceProfile::atq,&SourceProfile::mtq,&SourceProfile::stq,&SourceProfile::cgm,
        &SourceProfile::shq,&SourceProfile::ano,&SourceProfile::bloom
    };
    constexpr std::uint8_t out_of_bounds[] = {3,3,4,4,3,2,2};
    for (std::size_t index=0; index<7; ++index) {
        auto invalid = high;
        invalid.source_profile.*selectors[index] = out_of_bounds[index];
        invalid.complete = true;
        configure_material_state(invalid,"Mtl_kuleler",0);
        require(invalid.technique == MaterialTechnique::Unknown && !invalid.complete &&
                !invalid.material_constants_known,"every out-of-range profile selector must fail closed");
    }
    NativeMaterialState palette;
    palette.model_material_present = true;
    palette.ambient = {0.1f,0.2f,0.3f,0.4f};
    palette.diffuse = {0.5f,0.6f,0.7f,0.8f};
    palette.specular = {0.2f,0.3f,0.4f,0.5f};
    palette.emissive = {0.6f,0.7f,0.8f,0.9f};
    palette.power = 17;
    palette.material_albedo_percent = 99;
    const auto original_palette = palette;
    configure_material_state(palette,"",0);
    require(!palette.source_material_assigned && palette.material_constants_known &&
            palette.material_albedo_known && palette.material_albedo_percent == 0,
            "unassigned entity material selects known zero-albedo mat_unlit");
    require(palette.ambient == original_palette.ambient && palette.diffuse == original_palette.diffuse &&
            palette.specular == original_palette.specular && palette.emissive == original_palette.emissive &&
            palette.power == original_palette.power,"unassigned material retains an actual MDL palette");
    auto absent = original_palette;
    absent.model_material_present = false;
    configure_material_state(absent,"",0);
    require(absent.ambient == std::array<float,4>{0,0,0,1} &&
            absent.diffuse == std::array<float,4>{0,0,0,1} &&
            absent.specular == std::array<float,4>{0,0,0,1} &&
            absent.emissive == rgb(128,128,128) && absent.power == 0,
            "absent source palette uses exact mat_unlit RGB, opaque alpha, and zero power");
    require(absent.ambient_alpha_known && absent.color_known_mask == 0xfff &&
            absent.power_known && absent.material_albedo_known && absent.material_albedo_percent == 0,
            "mat_unlit recovered fields are explicitly known");
    NativeMaterialState unknown;
    configure_material_state(unknown,"not_an_active_source_material",0);
    require(unknown.source_material_assigned && !unknown.material_constants_known,
            "an unknown nonempty source material name remains assigned without invented constants");
    for (auto profile : {c4::assets::original_source_profile,c4::assets::high_source_profile})
        for (const auto name : {"ENGEL_ORTA","ShaderEditor"}) {
            NativeMaterialState missing;
            missing.source_profile = profile;
            missing.ambient = {0.1f,0.2f,0.3f,0.4f};
            missing.skills[8] = 17;
            missing.texture_transform[0] = 19;
            missing.power = 13;
            const auto before_missing = missing;
            configure_material_state(missing,name,0);
            require(missing.source_material_assigned && !missing.material_constants_known &&
                    missing.skills_known_mask == 0 && missing.color_known_mask == 0,
                    "referenced but undefined source materials cannot inherit fabricated factory proof");
            require(missing.ambient == before_missing.ambient && missing.skills == before_missing.skills &&
                    missing.texture_transform == before_missing.texture_transform && missing.power == before_missing.power,
                    "missing source definition must not execute a named-material factory");
        }
}

void state_checks() {
    using namespace c4::rendering;
    for (auto technique : {MaterialTechnique::FixedFunction,MaterialTechnique::EnvCubeMix,
                           MaterialTechnique::EnvCubeAlpha,MaterialTechnique::ShadowMap,MaterialTechnique::Water})
        for (bool transparent : {false,true}) {
            NativeMaterialState state;
            state.technique = technique;
            configure_material_state(state,"",transparent ? 1u << 10 : 0u,25);
            require(state.fixed_alpha_known && state.fixed_alpha_modulates_diffuse == transparent,
                    "fixed texture alpha SELECT/MODULATE follows source TRANSPARENT, not FX blend override");
            require(state.entity_alpha == 0.25f,"source entity alpha is retained once for the diffuse-alpha boundary");
            if (technique == MaterialTechnique::Water) {
                require(state.bump_matrix_known && state.bump_environment_matrix == std::array<float,4>{1,0,0,1},
                        "source water bump TSS matrix is proven identity including BUMPENVMAT11");
                require(!state.raster.blend,"water FX disables blending without overriding source alpha-stage operation");
            }
        }
    for (auto technique : {MaterialTechnique::NormalOptim,MaterialTechnique::NormalTSpecShadow,
                           MaterialTechnique::CubeNormalCar,MaterialTechnique::CubeNormalMix}) {
        NativeMaterialState state;
        state.technique = technique;
        configure_material_state(state,"",1u << 10,25);
        require(!state.fixed_alpha_known,"normal pixel shader alpha must not inherit fixed alpha-stage modulation");
    }
    for (auto technique : {MaterialTechnique::CubeNormalOptim, MaterialTechnique::CubeNormalCar,
                           MaterialTechnique::CubeNormalMix}) {
        NativeMaterialState state;
        state.technique = technique;
        state.entity_skins[0] = 0; // Index zero is a real image, not an absent optional.
        state.entity_skins[1] = 20;
        state.material_skins[0] = 30;
        state.material_skins[1] = 40;
        configure_material_state(state,"",1u << 10);
        require(material_texture(state,TextureRole::Color) == 0,"CubeNormal color must use entSkin1");
        require(material_texture(state,TextureRole::Normal) ==
            (technique == MaterialTechnique::CubeNormalMix ? 20u : 40u),"CubeNormal normal texture slot");
        require(material_texture(state,TextureRole::Environment) == 30,"CubeNormal cube must use mtlSkin1");
        require(state.complete && material_has_required_textures(state),"complete cube texture bindings");
        require(!state.raster.blend,"CubeNormal FX overrides entity transparency");
        auto missing = state;
        missing.entity_skins[0].reset();
        require(!material_has_required_textures(missing),"CubeNormal requires entSkin1 color");
        missing = state;
        missing.material_skins[0].reset();
        require(!material_has_required_textures(missing),"CubeNormal requires cube texture");
        missing = state;
        if (technique == MaterialTechnique::CubeNormalMix) missing.entity_skins[1].reset();
        else missing.material_skins[1].reset();
        require(!material_has_required_textures(missing),"CubeNormal requires normal texture");
    }
    for (auto technique : {MaterialTechnique::NormalTOptShadow,MaterialTechnique::NormalTSpecShadow}) {
        NativeMaterialState state;
        state.technique = technique;
        state.entity_skins[0] = 10;
        state.material_skins[0] = 20;
        state.material_skins[1] = 30;
        configure_material_state(state,"",0);
        require(material_texture(state,TextureRole::Color) == 10,"NormalTShadow color entSkin1");
        require(material_texture(state,TextureRole::Lightmap) == 20,"NormalTShadow lightmap mtlSkin1");
        require(material_texture(state,TextureRole::Normal) == 30,"NormalTShadow normal mtlSkin2");
        require(material_requires_uv1(technique) && material_requires_tangent(technique),"NormalTShadow UV1/tangent contract");
        require(state.complete,"complete NormalTShadow bindings");
        for (unsigned slot=0; slot<2; ++slot) {
            auto missing = state;
            missing.material_skins[slot].reset();
            require(!material_has_required_textures(missing),"NormalTShadow requires both material skins");
        }
        auto missing = state;
        missing.entity_skins[0].reset();
        require(!material_has_required_textures(missing),"NormalTShadow requires color");
    }
    constexpr MaterialTechnique terrains[] = {
        MaterialTechnique::Terrain4A,MaterialTechnique::Terrain4B,MaterialTechnique::Terrain4C,
        MaterialTechnique::Terrain5A,MaterialTechnique::Terrain5B,MaterialTechnique::Terrain5C
    };
    constexpr float scales[3][5] = {{90,15,17.5f,5,130},{120,30,35,10,130},{90,60,70,20,130}};
    for (unsigned variant=0; variant<6; ++variant) {
        NativeMaterialState state;
        state.technique = terrains[variant];
        state.entity_skins[0] = 0;
        for (unsigned slot=0; slot<4; ++slot) state.material_skins[slot] = slot+1;
        configure_material_state(state,"",1u << 12);
        const unsigned passes = variant < 3 ? 4 : 5;
        require(state.pass_count == passes,"terrain pass count");
        require(state.complete && material_has_required_textures(state),"terrain required texture bindings");
        require(material_requires_uv1(state.technique) && !material_requires_tangent(state.technique),"terrain UV1 without tangent");
        require(material_texture(state,TextureRole::Color) == 0,"terrain base entSkin1");
        require(material_texture(state,TextureRole::TerrainLayer) == 1,"terrain first layer mtlSkin1");
        for (unsigned index=0; index<passes; ++index) {
            const auto& pass = state.passes[index];
            require(pass.material_slot == index,"terrain source texture pass order");
            require(pass.role == (index == 0 ? TextureRole::Color : TextureRole::TerrainLayer),"terrain texture role");
            require(pass.color_uv == 1 && pass.alpha_uv == 0,"terrain tiled RGB and untiled alpha UVs");
            require(pass.uv_scale == std::array<float,2>{scales[variant%3][index],scales[variant%3][index]},"terrain exact source UV scale");
            require(pass.sampler.u == AddressMode::Wrap && pass.sampler.v == AddressMode::Wrap &&
                    pass.sampler.linear && pass.sampler.filter_explicit,"terrain source sampler");
            require(pass.raster.alpha_comparison == AlphaComparison::Always,"terrain disables alpha test");
            require(pass.raster.blend == (index != 0),"terrain base/overlay blend enable");
            if (index != 0) {
                require(pass.raster.source_blend == BlendFactor::InverseSourceAlpha &&
                        pass.raster.destination_blend == BlendFactor::SourceAlpha,"terrain reverse-alpha factors");
                auto missing = state;
                missing.material_skins[index-1].reset();
                require(!material_has_required_textures(missing),"terrain requires every overlay image");
            }
        }
        if (passes == 4) {
            auto minimal = state;
            minimal.material_skins[3].reset();
            require(material_has_required_textures(minimal),"four-layer terrain must not require a fifth texture");
        }
        state.entity_skins[0].reset();
        require(!material_has_required_textures(state),"terrain requires base image");
    }
    NativeMaterialState unknown;
    unknown.technique = material_from_effect("unrecognized_native_material.fx");
    unknown.entity_skins[0] = 0;
    for (auto& skin : unknown.material_skins) skin = 1;
    configure_material_state(unknown,"unrecognized_native_material",0);
    require(unknown.technique == MaterialTechnique::Unknown,"unknown effect must not become a known technique");
    require(!unknown.complete && !material_has_required_textures(unknown),"unknown effect cannot claim complete bindings");
    require(!unknown.material_constants_known,"unknown effect cannot acquire invented material constants");
}

void lighting_cache_producer_checks() {
    using namespace c4::rendering;
    c4::assets::WorldScene world;
    world.info_flags = 4;
    world.source_empty_collision_root_known = true;
    world.models.resize(2);
    world.entities.resize(2);
    const auto decoded = std::make_shared<c4::assets::Model>();
    world.models[0].model = decoded;
    world.models[0].resource = "fixture_model.mdl";
    world.models[1].sprite.width = world.models[1].sprite.height = 1;
    world.models[1].sprite.rgba = {255,255,255,255};
    world.entities[0].model_index = 0;
    world.entities[1].model_index = 1;
    const SourceFloorTraceControls controls{0xf000,1024,1024,true,true};
    require(source_scene_no_floor_hits(world,controls),"decoded MDL and sprite qualify under proven source empty-root controls");
    world.source_empty_collision_root_known = false;
    require(!source_scene_no_floor_hits(world,controls),"unproven collision root cannot qualify entity cache defaults");
    world.source_empty_collision_root_known = true;
    world.info_flags = 0;
    require(!source_scene_no_floor_hits(world,controls),"source floor path requires WMB INFO bit04");
    world.info_flags = 4;
    auto unknown = controls;
    unknown.shadow_mode_known = false;
    require(!source_scene_no_floor_hits(world,unknown),"unknown source SHADOW_MODE cannot prove decoded-class trace exclusion");
    unknown = controls;
    unknown.normal_world_load_known = false;
    require(!source_scene_no_floor_hits(world,unknown),"generic world load cannot claim source normal-loader root clearing");
    auto unsupported_shadow = controls;
    ++unsupported_shadow.shadow_mode_raw;
    require(!source_scene_no_floor_hits(world,unsupported_shadow),"unsupported source SHADOW_MODE fails qualification");
    for (std::int32_t trace_floor_raw : {0,1024,1025})
        for (std::int32_t polycollision_raw : {0,1024,1025}) {
            auto varied = controls;
            varied.trace_floor_raw = trace_floor_raw;
            varied.enable_polycollision_raw = polycollision_raw;
            require(source_scene_no_floor_hits(world,varied),
                    "TRACE_FLOOR skip and all proven legacy/modern poly routes preserve the same no-cache-write invariant");
        }
    world.entities[0].model_index = 2;
    require(!source_scene_no_floor_hits(world,controls),"out-of-range model binding cannot prove source resource class");
    world.entities[0].model_index = c4::assets::no_world_model;
    require(source_scene_no_floor_hits(world,controls),"resource-empty unbound helper contributes no source collision class");
    world.entities[0].resource = "fixture_unresolved_resource";
    require(!source_scene_no_floor_hits(world,controls),"unbound nonempty resource lacks decoded source class proof");
    world.entities[0].resource.clear();
    world.entities[0].model_index = 0;
    world.models[0].model.reset();
    world.models[0].resource = "fixture_unclassified_map";
    require(!source_scene_no_floor_hits(world,controls),"unclassified map resource is not decoded MDL or sprite proof");
    world.models[0].model = decoded;
    world.models[0].resource = "fixture_model.mdl";
    world.models[1].sprite.width = 0;
    require(!source_scene_no_floor_hits(world,controls),"sprite class requires proven positive dimensions");
    world.models[1].sprite.width = 1;
    world.models[1].sprite.height = 0;
    require(!source_scene_no_floor_hits(world,controls),"sprite class also requires a proven positive height");
    world.models[1].sprite.height = 1;
    world.models[1].imported_resource = "fixture_import";
    require(!source_scene_no_floor_hits(world,controls),"sprite import without retained MDL class cannot qualify");
    world.models[1].imported_resource.clear();
    world.models[0].imported_resource = "fixture_import";
    require(source_scene_no_floor_hits(world,controls),"imported geometry retains its actual decoded MDL class");
    world.models[0].imported_resource.clear();
    world.models[0].morphed_resource = "fixture_morph.mdl";
    require(!source_scene_no_floor_hits(world,controls),"unresolved morph cannot claim decoded source resource class");
    world.models[0].morphed_model = decoded;
    require(source_scene_no_floor_hits(world,controls),"resolved MDL morph retains qualifying source class");
    world.models[0].morphed_resource.clear();
    world.models[0].morphed_model.reset();
    for (bool shadow : {false,true})
        for (bool passable : {false,true}) {
            for (auto& entity : world.entities) {
                entity.flags = shadow ? 0x40000u : 0u;
                entity.source_flags_set = passable ? 0x200u : 0u;
            }
            require(source_scene_no_floor_hits(world,controls),"SHADOW and action-added PASSABLE retain decoded-class floor exclusion");
        }
    for (auto& entity : world.entities) entity.source_flags_clear = 0x40000u;
    const bool qualified = source_scene_no_floor_hits(world,controls);
    require(qualified,"source action clearing SHADOW preserves no-hit qualification");
    require(!decoded->source_no_linked_brightness_known && !source_scene_no_linked_brightness_writes(world),
            "a manual decoded class alone does not prove independent linked-brightness neutrality");
    decoded->source_no_linked_brightness_known = true;
    require(source_scene_no_linked_brightness_writes(world),"validated no-linked-brightness MDL fact and sprite class prove the cold scene");
    world.source_empty_collision_root_known = false;
    require(!source_scene_no_linked_brightness_writes(world),"missing empty section17 proof cannot exclude embedded linked-brightness models");
    world.source_empty_collision_root_known = true;
    world.entities[0].model_index = 2;
    require(!source_scene_no_linked_brightness_writes(world),"invalid model index cannot prove independent brightness preservation");
    world.entities[0].model_index = 0;
    world.models[0].model.reset();
    require(!source_scene_no_linked_brightness_writes(world),"unclassified resource cannot prove independent brightness preservation");
    world.models[0].model = decoded;
    const auto unproved_morph = std::make_shared<c4::assets::Model>();
    world.models[0].morphed_resource = "fixture_morph.mdl";
    world.models[0].morphed_model = unproved_morph;
    require(source_scene_no_floor_hits(world,controls) && !source_scene_no_linked_brightness_writes(world),
            "resolved MDL morph proves floor exclusion but needs its own linked-brightness metadata");
    unproved_morph->source_no_linked_brightness_known = true;
    require(source_scene_no_linked_brightness_writes(world),"validated primary and morphed MDL metadata both preserve constructor brightness");
    world.models[0].morphed_resource.clear();
    world.models[0].morphed_model.reset();
    world.models[1].imported_resource = "fixture_import";
    require(!source_scene_no_linked_brightness_writes(world),"sprite import without MDL metadata cannot prove linked-brightness preservation");
    world.models[1].imported_resource.clear();
    world.models[0].imported_resource = "fixture_import";
    require(source_scene_no_linked_brightness_writes(world),"imported geometry retains validated no-linked-brightness MDL resource metadata");
    world.models[0].imported_resource.clear();
    const bool brightness_preserves = source_scene_no_linked_brightness_writes(world);
    SourceLightingState publication_frame;
    require(publication_frame.view_ambient == 0 && !publication_frame.view_ambient_known,
            "frame VIEW ambient has a numeric constructor value without fabricated producer proof");
    std::array<SourceEntityLightingCache,2> caches{};
    initialize_source_entity_lighting_caches(caches,qualified,brightness_preserves);
    for (const auto& cache : caches)
        require(cache.environment == 127 && cache.chroma_bgr == std::array<std::int16_t,3>{} &&
                cache.environment_known && cache.chroma_known &&
                cache.source_color_raw_bgr == std::array<std::int32_t,3>{131072,131072,131072} &&
                cache.light_range_raw == 0 && !cache.source_color_known && !cache.light_range_known,
                "qualified creation proves only persistent environment/chroma constructor defaults");
    std::array<SourceEntityLightingCache,1> unqualified{};
    unqualified[0].environment = 12;
    initialize_source_entity_lighting_caches(unqualified,false,brightness_preserves);
    require(unqualified[0].environment == 127 && !unqualified[0].environment_known &&
            !unqualified[0].chroma_known,"unqualified constructor values remain numeric but unproven");
    std::array<SourceEntityLightingCache,1> floor_only{};
    initialize_source_entity_lighting_caches(floor_only,true,false);
    require(floor_only[0].environment == 127 && !floor_only[0].environment_known &&
            floor_only[0].chroma_bgr == std::array<std::int16_t,3>{} && floor_only[0].chroma_known,
            "floor miss proves constructor chroma but cannot prove ENV without independent resource-brightness preservation");
    caches[0].environment = 84;
    caches[0].chroma_bgr = {-4,5,6};
    caches[1].environment = 117;
    caches[1].chroma_bgr = {1,2,3};
    publish_source_view_ambient(publication_frame,41,true);
    require(publication_frame.view_ambient == 41 && publication_frame.view_ambient_known,
            "actual VIEW publication updates one shared frame scalar with independent knownness");
    for (const auto& cache : caches)
        require(!cache.source_color_known && !cache.light_range_known,"VIEW publication does not fabricate per-entity color or range facts");
    require(caches[0].environment == 84 && caches[0].chroma_bgr == std::array<std::int16_t,3>{-4,5,6} &&
            caches[1].environment == 117 && caches[1].chroma_bgr == std::array<std::int16_t,3>{1,2,3} &&
            caches[0].environment_known && caches[0].chroma_known &&
            caches[1].environment_known && caches[1].chroma_known,
            "known VIEW publication preserves each modified private environment/chroma cache");
    publish_source_view_ambient(publication_frame,-7,false);
    require(caches[0].environment == 84 && caches[0].chroma_bgr == std::array<std::int16_t,3>{-4,5,6} &&
            caches[1].environment == 117 && caches[1].chroma_bgr == std::array<std::int16_t,3>{1,2,3} &&
            caches[0].environment_known && caches[0].chroma_known &&
            publication_frame.view_ambient == -7 && !publication_frame.view_ambient_known,
            "VIEW updates preserve modified persistent entity caches rather than resetting misses");
    const std::array<std::int32_t,3> raw_color{-512,131073,-2048};
    publish_source_entity_light_color(caches[0],raw_color,true);
    require(caches[0].source_color_raw_bgr == raw_color && caches[0].source_color_known &&
            !caches[0].light_range_known && !publication_frame.view_ambient_known,
            "source light color publication retains exact Q10 words without proving other channels");
    publish_source_entity_light_range(caches[0],1,true);
    require(caches[0].light_range_raw == 1 && caches[0].light_range_known &&
            caches[0].source_color_known && caches[0].source_color_raw_bgr == raw_color,
            "source range publication has independent raw value and knownness");
    publish_source_entity_light_color(caches[0],{0,1,2},false);
    require(caches[0].source_color_raw_bgr == std::array<std::int32_t,3>{0,1,2} &&
            !caches[0].source_color_known && caches[0].light_range_known,
            "unknown color publication does not revoke independently proven range");
    publish_source_entity_light_range(caches[0],-33,false);
    require(caches[0].light_range_raw == -33 && !caches[0].light_range_known &&
            caches[0].environment == 84 && caches[0].chroma_bgr == std::array<std::int16_t,3>{-4,5,6} &&
            caches[1].source_color_raw_bgr == std::array<std::int32_t,3>{131072,131072,131072} &&
            !caches[1].source_color_known && !caches[1].light_range_known,
            "per-entity color/range publication preserves other entities and private cache channels");
}

void sun_pose_checks() {
    using namespace c4::rendering;
    c4::assets::WorldScene world;
    world.sun_azimuth = 37.25f;
    world.sun_elevation = -19.5f;
    const auto axis = c4::physics::rotation_axes({world.sun_azimuth,world.sun_elevation,0})[0];
    // Recovered raw 0x61a8000 Q10 distance publishes exactly 100000 as FLOAT.
    constexpr float distance = 100000.0f;
    const std::array<float,3> expected{
        static_cast<float>(double(axis.x)*distance),
        static_cast<float>(double(axis.z)*distance),
        static_cast<float>(double(axis.y)*distance)
    };
    const auto visuals = source_game_visual_state(world,{},{});
    require(visuals.sun_position_known && visuals.sun_position == expected,
            "SUN pose scales recovered unnormalized positive U by original distance and packs XZY once");
    const auto lighting = source_lighting_state(world,visuals);
    require(lighting.sun_position_known &&
            lighting.lights[0].position_range == std::array<float,4>{expected[0],expected[1],expected[2],0},
            "source SUN slot copies the known pose without changing directional Range");
    require(lighting.lights[0].direction_type ==
            std::array<float,4>{visuals.sun_direction[0],visuals.sun_direction[1],visuals.sun_direction[2],3},
            "SUN pose remains separate from the directional light's negative unit ray");
    auto unknown = visuals;
    unknown.sun_position = {123,456,789};
    unknown.sun_position_known = false;
    const auto unknown_lighting = source_lighting_state(world,unknown);
    require(!unknown_lighting.sun_position_known &&
            unknown_lighting.lights[0].position_range == std::array<float,4>{} &&
            unknown_lighting.lights[0].direction_type == lighting.lights[0].direction_type,
            "unproven SUN position is not published and does not replace proven directional lighting");
}

void lighting_cache_checks() {
    using namespace c4::rendering;
    SourceLightingState frame;
    frame.lights_known = frame.view_known = frame.fog_known = true;
    SourceLightingDeviceState direct;
    direct.sourceMaxTextureWidth = 1024;
    direct.supportedADD = direct.supportedMOD2X = direct.capabilitiesKnown = true;
    NativeMaterialState material;
    material.color_known_mask = 0xfff;
    material.power_known = material.material_albedo_known = true;
    material.ambient = {1,1,1,1};
    material.diffuse = material.specular = material.emissive = {0,0,0,1};
    material.material_albedo_percent = 0;
    material.entity_ambient_percent = -0.5f;
    SourceEntityLightingCache cache;
    cache.environment = 127;
    cache.chroma_bgr = {-1,0,1};
    publish_source_view_ambient(frame,0,true);
    cache.environment_known = cache.chroma_known = true;
    const auto uniform = source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&direct);
    require(uniform.status[0] == 1,"proven per-entity cache completes ambient-dependent lighting");
    require(uniform.global_ambient == std::array<float,4>{static_cast<float>(127.0/255),
            static_cast<float>(126.0/255),static_cast<float>(125.0/255),0},
            "signed source integer publication floors negative half ambient and reverses cached BGR to RGB");
    require(source_ambient_direct_capable(direct),"known ADD/MOD2X and width above512 prove DIRECT ambient encoding");
    SourceLightingDeviceState unknown_device;
    require(!unknown_device.capabilitiesKnown && !unknown_device.sourceLUTKnown &&
            !source_ambient_direct_capable(unknown_device),"device constructor never fabricates capability or lookup proof");
    require(source_lighting_uniforms(frame,material,0,nullptr,&cache).status[0] == 0 &&
            source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&unknown_device).status[0] == 0,
            "ambient-dependent material fails closed without actual device encoding facts");
    SourceLightingDeviceState lookup;
    lookup.sourceMaxTextureWidth = 512;
    lookup.supportedADD = lookup.supportedMOD2X = lookup.capabilitiesKnown = true;
    lookup.sourceLUT = source_default_ambient_lookup();
    lookup.sourceLUTKnown = true;
    // These recipe invariants do not claim captured original-CRT byte parity.
    require(lookup.sourceLUT[0] > 0 && lookup.sourceLUT[127] > 127 && lookup.sourceLUT[255] == 255,
            "canonical source GAMMA1126 lookup preserves its positive origin, brightens midtones, and saturates its endpoint");
    for (std::size_t index=1; index<lookup.sourceLUT.size(); ++index)
        require(lookup.sourceLUT[index] >= lookup.sourceLUT[index-1],
                "positive recovered gamma exponent produces monotone quantized source ambient lookup");
    const std::array<float,4> lookup_expected{
        static_cast<float>(double(lookup.sourceLUT[127])/255),
        static_cast<float>(double(lookup.sourceLUT[126])/255),
        static_cast<float>(double(lookup.sourceLUT[125])/255),0
    };
    const auto at512 = source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup);
    require(!source_ambient_direct_capable(lookup) && at512.status[0] == 1 && at512.global_ambient == lookup_expected,
            "width512 uses the actual canonical source GAMMA lookup at each scratch RGB index");
    lookup.sourceMaxTextureWidth = 513;
    const auto at513 = source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup);
    require(source_ambient_direct_capable(lookup) && at513.status[0] == 1 &&
            at513.global_ambient == uniform.global_ambient,"width513 selects DIRECT rather than a guessed >=512 threshold");
    lookup.supportedADD = false;
    const auto missing_add = source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup);
    require(!source_ambient_direct_capable(lookup) && missing_add.status[0] == 1 &&
            missing_add.global_ambient == lookup_expected,"missing ADD capability selects proven lookup encoding");
    lookup.supportedADD = true;
    lookup.supportedMOD2X = false;
    const auto missing_mod2x = source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup);
    require(!source_ambient_direct_capable(lookup) && missing_mod2x.status[0] == 1 &&
            missing_mod2x.global_ambient == lookup_expected,"missing MOD2X capability selects proven lookup encoding");
    lookup.supportedMOD2X = true;
    lookup.sourceMaxTextureWidth = 512;
    lookup.capabilitiesKnown = false;
    require(source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup).status[0] == 0,
            "a known lookup table does not prove an unknown device's encoding branch");
    lookup.capabilitiesKnown = true;
    lookup.sourceLUTKnown = false;
    require(source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&lookup).status[0] == 0,
            "known lookup-capability branch requires actual source table proof");
    lookup.sourceLUTKnown = true;
    auto packed_material = material;
    packed_material.entity_ambient_percent = 0;
    auto packed_cache = cache;
    packed_cache.environment = 0;
    packed_cache.chroma_bgr = {-1,0,0};
    const auto signed_pack = source_lighting_uniforms(frame,packed_material,0,nullptr,&packed_cache,nullptr,&direct);
    require(signed_pack.status[0] == 1 && signed_pack.global_ambient == std::array<float,4>{1,1,1,0},
            "negative blue is ORed as a whole signed source DWORD and bleeds into red and green bytes");
    require(source_lighting_uniforms(frame,packed_material,0,nullptr,&packed_cache,nullptr,&lookup).status[0] == 0,
            "negative whole scratch integers cannot be guessed as clamped or wrapped LUT indices");
    packed_cache.chroma_bgr = {0,0,0};
    for (auto technique : {MaterialTechnique::Terrain4A,MaterialTechnique::Terrain4B,MaterialTechnique::Terrain4C,
                           MaterialTechnique::Terrain5A,MaterialTechnique::Terrain5B,MaterialTechnique::Terrain5C}) {
        packed_material.technique = technique;
        packed_cache.environment = 1;
        const auto terrain = source_lighting_uniforms(frame,packed_material,0,nullptr,&packed_cache,nullptr,&direct);
        const float scaled = static_cast<float>(10.0/255);
        require(terrain.status[0] == 1 && terrain.global_ambient == std::array<float,4>{scaled,scaled,scaled,0},
                "every terrain technique applies source vecLight times ten after packed-byte conversion");
        packed_cache.environment = 26;
        const auto saturated = source_lighting_uniforms(frame,packed_material,0,nullptr,&packed_cache,nullptr,&direct);
        require(saturated.status[0] == 1 && saturated.global_ambient == std::array<float,4>{1,1,1,0},
                "terrain source ambient conversion saturates byte26 times ten at255");
    }
    packed_material.technique = MaterialTechnique::NormalOptim;
    packed_cache.environment = 1;
    const float unscaled = static_cast<float>(1.0/255);
    require(source_lighting_uniforms(frame,packed_material,0,nullptr,&packed_cache,nullptr,&direct).global_ambient ==
            std::array<float,4>{unscaled,unscaled,unscaled,0},
            "nonterrain shader ambient does not inherit the fixed terrain times-ten override");
    require(uniform.options[1] == 0 && uniform.options[2] == 0,
            "known ambient cache does not fabricate independent draw-alpha proof");
    const auto unknown_alpha = source_lighting_uniforms(frame,material,1u << 10,nullptr,&cache,nullptr,&direct);
    require(unknown_alpha.options[1] == 0 && unknown_alpha.options[2] == 0 && unknown_alpha.diffuse[3] == 1,
            "source TRANSPARENT flag alone cannot replace typed draw-time alpha proof");
    const auto alpha = original_model_alpha_decision({false,1.0f,1.0f,false,true,25.0f,false,false,false,0.0f});
    const auto with_alpha = source_lighting_uniforms(frame,material,0,&alpha,&cache,nullptr,&direct);
    require(with_alpha.options[1] == 1 && with_alpha.options[2] == 4 && with_alpha.diffuse[3] == 0.25f,
            "typed draw-alpha decision publishes MODULATE and diffuse alpha without a second entity-alpha multiply");
    cache.environment_known = false;
    require(source_lighting_uniforms(frame,material,0,nullptr,&cache,nullptr,&direct).status[0] == 0,
            "ambient-dependent material fails closed when entity environment is unknown");
    material.ambient = {0,0,0,1};
    const float unlit = static_cast<float>(128.0/255);
    material.emissive = {unlit,unlit,unlit,1};
    cache.chroma_known = false;
    frame.view_ambient_known = false;
    const auto emissive = source_lighting_uniforms(frame,material,0,nullptr,&cache);
    require(emissive.status[0] == 1 && emissive.emissive == material.emissive,
            "known emissive-only material does not require unrelated unknown entity lighting cache");
    frame.fog_enabled = true;
    frame.fog_mode = 3;
    frame.fog_params = {10,20,0,0};
    const auto alternate_emissive = source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache);
    require(alternate_emissive.status[0] == 1 && alternate_emissive.status[2] == 0,
            "emissive-only alternative VIEW is complete without cache proof and disables fog");
    material.ambient = {1,1,1,1};
    cache.environment_known = cache.chroma_known = true;
    frame.view_ambient_known = true;
    require(source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache,nullptr,&direct).status[0] == 0,
            "alternative VIEW ambient requires independent source-color proof rather than a known normal cache");
    cache.environment_known = cache.chroma_known = false;
    frame.view_ambient_known = false;
    publish_source_entity_light_color(cache,{128000,129024,130048},true);
    const auto alternate_color = source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache,nullptr,&direct);
    require(alternate_color.status[0] == 1 && alternate_color.status[2] == 0 &&
            alternate_color.global_ambient == uniform.global_ambient,
            "alternative VIEW uses independently published public BGR color without normal cache or VIEW ambient proof");
    const auto alternate_self_lit = source_lighting_uniforms(frame,material,0x08080000u,nullptr,&cache,nullptr,&direct);
    require(alternate_self_lit.status[0] == 1 && alternate_self_lit.global_ambient == uniform.global_ambient,
            "alternative VIEW public color bypasses normal SELF_LIT chroma/range arithmetic");
    const auto alternate_lookup = source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache,nullptr,&lookup);
    require(alternate_lookup.status[0] == 1 && alternate_lookup.global_ambient == lookup_expected,
            "alternative VIEW public scratch RGB uses the same proven device lookup branch");
    publish_source_entity_light_color(cache,{262144,129024,130048},true);
    require(source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache,nullptr,&lookup).status[0] == 0,
            "LUT scratch index256 is unknown rather than an out-of-bounds read or invented saturation");
    publish_source_entity_light_color(cache,{-1024,129024,130048},true);
    require(source_lighting_uniforms(frame,material,0x08000000u,nullptr,&cache,nullptr,&lookup).status[0] == 0,
            "alternative VIEW negative public color cannot index the source LUT");
}

void model_alpha_checks() {
    using namespace c4::rendering;
    const auto opaque = original_model_alpha_decision({false,1.0f,1.0f,false,false,100.0f,false,false,false,0.0f});
    require(!opaque.modulate_diffuse && !opaque.alpha_blend && opaque.depth_write && opaque.diffuse_alpha == 1.0f,
            "opaque model selects texture alpha without blending");
    // The bitmap's actual 0x100 bit is independent of its other source flags.
    constexpr unsigned texture_flags = 0x101u;
    const auto textured = original_model_alpha_decision({false,1.0f,0.75f,
        (texture_flags & 0x100u) != 0,false,100.0f,false,false,false,0.0f});
    require(textured.modulate_diffuse && textured.alpha_blend && !textured.depth_write && textured.diffuse_alpha == 0.75f,
            "actual bitmap alpha flag selects modulation and blend path");
    const auto prior = original_model_alpha_decision({true,0.4f,0.75f,false,false,100.0f,false,false,false,0.0f});
    require(prior.modulate_diffuse && prior.alpha_blend && !prior.depth_write && prior.diffuse_alpha == 0.4f,
            "preexisting ambient alpha selects blend and caps bound diffuse alpha");
    const auto view_context = original_model_alpha_decision({true,0.4f,1.0f,false,false,100.0f,false,false,true,0.75f});
    require(view_context.modulate_diffuse && view_context.alpha_blend && !view_context.depth_write &&
            view_context.diffuse_alpha == 0.75f &&
            view_context.source_blend == BlendFactor::SourceAlpha &&
            view_context.destination_blend == BlendFactor::InverseSourceAlpha,
            "active VIEW material context preserves prior diffuse alpha independently of the ambient alpha cap");
    for (const float prior_diffuse : {1.0f,0.6f}) {
        const auto preserved = original_model_alpha_decision({true,0.4f,1.0f,false,true,50.0f,false,false,true,prior_diffuse});
        require(preserved.modulate_diffuse && preserved.alpha_blend && !preserved.depth_write &&
                preserved.diffuse_alpha == prior_diffuse,
                "active VIEW material context preserves diffuse alpha above both ambient and entity alpha caps");
    }
    const auto transparent = original_model_alpha_decision({false,1.0f,0.75f,false,true,50.0f,false,false,false,0.0f});
    require(transparent.modulate_diffuse && transparent.alpha_blend && !transparent.depth_write && transparent.diffuse_alpha == 0.5f,
            "transparent entity percentage is capped by source view alpha");
    const auto faded = original_model_alpha_decision({false,1.0f,0.5f,
        (0x1u & 0x100u) != 0,false,100.0f,false,false,false,0.0f});
    require(faded.modulate_diffuse && faded.alpha_blend && faded.depth_write && faded.diffuse_alpha == 0.5f &&
            faded.source_blend == BlendFactor::SourceAlpha && faded.destination_blend == BlendFactor::InverseSourceAlpha,
            "view-only fade uses alpha blending but retains opaque-path depth writes");
    const auto future = original_model_alpha_decision({false,0.4f,1.0f,false,false,100.0f,false,false,false,0.0f});
    require(!future.modulate_diffuse && !future.alpha_blend && future.depth_write && future.diffuse_alpha == 1.0f,
            "unbound future material ambient alpha must not enter the prior-material predicate");
    constexpr double source_threshold = 0.9990000128746033;
    const float boundary = static_cast<float>(source_threshold);
    const float below = std::nextafter(boundary,0.0f);
    require(static_cast<double>(boundary) == source_threshold && static_cast<double>(below) < source_threshold,
            "threshold checks compare binary32 inputs against the recovered binary64 constant");
    const auto at_threshold = original_model_alpha_decision({false,1.0f,boundary,false,false,100.0f,false,false,false,0.0f});
    const auto below_threshold = original_model_alpha_decision({false,1.0f,below,false,false,100.0f,false,false,false,0.0f});
    require(!at_threshold.modulate_diffuse && !at_threshold.alpha_blend && at_threshold.depth_write && at_threshold.diffuse_alpha == 1.0f &&
            below_threshold.modulate_diffuse && below_threshold.alpha_blend && below_threshold.depth_write && below_threshold.diffuse_alpha == below,
            "source view alpha threshold separates opaque from depth-writing fade");
    const auto prior_boundary = original_model_alpha_decision({true,boundary,1.0f,false,false,100.0f,false,false,false,0.0f});
    const auto prior_below = original_model_alpha_decision({true,below,1.0f,false,false,100.0f,false,false,false,0.0f});
    require(!prior_boundary.modulate_diffuse && !prior_boundary.alpha_blend && prior_boundary.depth_write && prior_boundary.diffuse_alpha == boundary &&
            prior_below.modulate_diffuse && prior_below.alpha_blend && !prior_below.depth_write && prior_below.diffuse_alpha == below,
            "prior ambient threshold is strict and diffuse remains capped after selection");
    const auto additive = original_model_alpha_decision({false,1.0f,0.75f,false,true,50.0f,true,true,false,0.0f});
    const auto disabled_additive = original_model_alpha_decision({false,1.0f,0.75f,false,true,50.0f,true,false,false,0.0f});
    const auto not_bright = original_model_alpha_decision({false,1.0f,0.75f,false,true,50.0f,false,true,false,0.0f});
    const auto bright_fade = original_model_alpha_decision({false,1.0f,0.5f,false,false,100.0f,true,true,false,0.0f});
    require(additive.alpha_blend && !additive.depth_write && additive.source_blend == BlendFactor::SourceAlpha &&
            additive.destination_blend == BlendFactor::One &&
            disabled_additive.destination_blend == BlendFactor::InverseSourceAlpha &&
            not_bright.destination_blend == BlendFactor::InverseSourceAlpha &&
            bright_fade.destination_blend == BlendFactor::InverseSourceAlpha && bright_fade.depth_write,
            "additive destination requires both source gates and the transparent-material path");
}

void effect_raster_alpha_checks() {
    using namespace c4::rendering;
    // Actual bitmap alpha selects transparency even without entity TRANSPARENT or a VIEW material context.
    const auto transparent = original_model_alpha_decision(
        {false,1.0f,1.0f,true,false,100.0f,false,false,false,0.0f});
    require(transparent.alpha_blend && !transparent.depth_write &&
            transparent.source_blend == BlendFactor::SourceAlpha &&
            transparent.destination_blend == BlendFactor::InverseSourceAlpha,
            "actual source bitmap alpha supplies the shared transparent draw decision");
    NativeMaterialState normal;
    normal.technique = MaterialTechnique::NormalOptim;
    configure_material_state(normal,"",1u << 12);
    require(normal.raster.effect_override_mask == 0 &&
            normal.raster.alpha_comparison == AlphaComparison::GreaterEqual &&
            normal.raster.alpha_reference == 100.0f / 255.0f,
            "NormalOptim inherits engine raster state while retaining source OVERLAY alpha test");
    const auto inherited = apply_original_model_alpha(normal.raster,transparent);
    require(inherited.blend && inherited.depth_test && !inherited.depth_write &&
            inherited.source_blend == BlendFactor::SourceAlpha &&
            inherited.destination_blend == BlendFactor::InverseSourceAlpha,
            "NormalOptim uses actual source alpha rather than entity transparency flags");
    require(inherited.alpha_comparison == normal.raster.alpha_comparison &&
            inherited.alpha_reference == normal.raster.alpha_reference &&
            inherited.color_key == normal.raster.color_key &&
            inherited.defaults_recovered == normal.raster.defaults_recovered,
            "shared draw alpha leaves OVERLAY comparison, reference and raster proof fields unchanged");
    for (const auto technique : {MaterialTechnique::CubeNormalOptim,MaterialTechnique::EnvCubeAlpha}) {
        NativeMaterialState state;
        state.technique = technique;
        configure_material_state(state,"",0);
        require(state.raster.effect_override_mask == (BlendEnable | DepthWrite),
                "cube and environment FX mark only authored blend and depth-write overrides");
        const auto raster = apply_original_model_alpha(state.raster,transparent);
        require(raster.blend == (technique == MaterialTechnique::EnvCubeAlpha) &&
                raster.depth_test && raster.depth_write &&
                raster.source_blend == BlendFactor::SourceAlpha &&
                raster.destination_blend == BlendFactor::InverseSourceAlpha,
                "cube/environment FX preserve authored blending and depth writes despite transparent source alpha");
    }
    NativeMaterialState terrain;
    terrain.technique = MaterialTechnique::Terrain5A;
    configure_material_state(terrain,"",0);
    require(terrain.pass_count == 5,"terrain alpha override regression covers base and all overlay passes");
    for (unsigned index = 0; index < terrain.pass_count; ++index) {
        const auto& pass = terrain.passes[index];
        require(pass.raster.effect_override_mask ==
                    (BlendEnable | DepthTest | DepthWrite | AlphaTestEnable |
                     (index == 0 ? 0 : SourceBlend | DestinationBlend)),
                "terrain marks authored blend factors only on overlay passes");
        const auto raster = apply_original_model_alpha(pass.raster,transparent);
        require(raster.blend == (index != 0) && raster.depth_test && raster.depth_write,
                "terrain base remains opaque and every overlay retains depth testing and writes");
        require(raster.source_blend == (index == 0 ? BlendFactor::SourceAlpha : BlendFactor::InverseSourceAlpha) &&
                raster.destination_blend == (index == 0 ? BlendFactor::InverseSourceAlpha : BlendFactor::SourceAlpha),
                "terrain overlays retain FX reverse-alpha factors while base factors inherit the source decision");
    }
    NativeMaterialState water;
    water.technique = MaterialTechnique::Water;
    configure_material_state(water,"",1u << 10);
    require(water.raster.effect_override_mask == (BlendEnable | DepthTest | DepthWrite),
            "water records only authored blend and depth overrides");
    const auto water_raster = apply_original_model_alpha(water.raster,transparent);
    require(!water_raster.blend && water_raster.depth_test && water_raster.depth_write &&
            water.fixed_alpha_known && water.fixed_alpha_modulates_diffuse,
            "water FX remains opaque and depth-writing without replacing source MODULATE alpha operation");
}

void source_bitmap_alpha_checks() {
    using namespace c4::rendering;
    const auto transparent = original_model_alpha_decision(
        {false,1.0f,1.0f,true,false,100.0f,false,false,false,0.0f});
    for (const auto technique : {MaterialTechnique::NormalOptim,MaterialTechnique::Terrain5A}) {
        NativeMaterialState state;
        state.technique = technique;
        configure_material_state(state,"",0x1000u);
        state.source_pass.raster.color_key = true;
        state.raster.color_key = true;
        for (unsigned index = 0; index < state.pass_count; ++index)
            state.passes[index].raster.color_key = true;
        for (const bool overlay : {true,false}) {
            // Rebind the same material: removal of flag 0x100 must restore the source threshold.
            for (const bool source_alpha_flag : {true,false}) {
                configure_source_bitmap_raster(state,overlay,source_alpha_flag);
                const auto expected = overlay && !source_alpha_flag ?
                    AlphaComparison::GreaterEqual : AlphaComparison::Always;
                const auto source = original_material_draw_state(state,false);
                require(source.passes.size() == 1 && source.passes.data() == &state.source_pass &&
                        source.passes[0].raster.alpha_comparison == expected &&
                        source.passes[0].raster.alpha_reference == 100.0f / 255.0f &&
                        source.passes[0].raster.effect_override_mask == 0,
                        "NOSHADER uses effective OVERLAY and actual bitmap flag 0x100 on the saved source pass");
                const auto fx_expected = technique == MaterialTechnique::Terrain5A ?
                    AlphaComparison::Always : expected;
                require(state.raster.alpha_comparison == fx_expected && state.raster.color_key &&
                        state.source_pass.raster.color_key,
                        "binding preserves authored FX AlphaTestEnable and independent color-key provenance");
                const auto enabled = original_material_draw_state(state,true);
                for (const auto& pass : enabled.passes) {
                    require(pass.raster.alpha_comparison == fx_expected && pass.raster.color_key &&
                            pass.raster.alpha_reference == 100.0f / 255.0f,
                            "all terrain FX passes remain AlphaAlways while inherited normal passes follow source bitmap alpha");
                    const auto raster = apply_original_model_alpha(pass.raster,transparent);
                    require(raster.alpha_comparison == pass.raster.alpha_comparison &&
                            raster.alpha_reference == pass.raster.alpha_reference &&
                            raster.effect_override_mask == pass.raster.effect_override_mask &&
                            raster.color_key == pass.raster.color_key,
                            "model draw-alpha application cannot erode bitmap-gated or authored FX alpha testing");
                }
                const auto raster = apply_original_model_alpha(source.passes[0].raster,transparent);
                require(raster.alpha_comparison == expected &&
                        raster.alpha_reference == 100.0f / 255.0f && raster.color_key,
                        "source draw alpha preserves GE reference and never reclassifies RGB color key as bitmap alpha");
            }
        }
    }
}

void no_shader_draw_checks() {
    using namespace c4::rendering;
    NativeMaterialState terrain;
    terrain.technique = MaterialTechnique::Terrain5A;
    terrain.entity_skins[0] = 0;
    for (unsigned slot = 0; slot < 4; ++slot) terrain.material_skins[slot] = slot + 1;
    configure_material_state(terrain,"",0x1000u | 0x400u);
    const auto enabled = original_material_draw_state(terrain,true);
    require(enabled.technique == MaterialTechnique::Terrain5A && enabled.passes.size() == 5 &&
            enabled.passes.data() == terrain.passes.data(),
            "enabled effects retain the original terrain definition and all five passes");
    require(enabled.passes[0].raster.alpha_comparison == AlphaComparison::Always &&
            enabled.passes[0].raster.effect_override_mask == (BlendEnable | DepthTest | DepthWrite | AlphaTestEnable),
            "enabled terrain base keeps its authored alpha-test and raster overrides");
    const auto disabled = original_material_draw_state(terrain,false);
    require(disabled.technique == MaterialTechnique::FixedFunction && disabled.passes.size() == 1 &&
            disabled.passes.data() == &terrain.source_pass,
            "NOSHADER selects the saved source BASE pass without copying the definition");
    const auto& base = disabled.passes[0];
    require(base.role == TextureRole::Color && base.material_slot == 0 &&
            base.color_uv == 0 && base.alpha_uv == 0 &&
            base.uv_scale == std::array<float,2>{1,1},
            "NOSHADER restores entSkin1 Color and untiled source UV0");
    require(base.raster.alpha_comparison == AlphaComparison::GreaterEqual &&
            base.raster.alpha_reference == 100.0f / 255.0f &&
            base.raster.effect_override_mask == 0 &&
            !base.sampler.filter_explicit && base.sampler.u == AddressMode::Unspecified,
            "NOSHADER restores source OVERLAY threshold and inherited sampler, not terrain FX state");
    const auto alpha = original_model_alpha_decision(
        {false,1.0f,1.0f,false,true,50.0f,false,false,false,0.0f});
    const auto raster = apply_original_model_alpha(base.raster,alpha);
    require(raster.blend && raster.depth_test && !raster.depth_write &&
            raster.source_blend == BlendFactor::SourceAlpha &&
            raster.destination_blend == BlendFactor::InverseSourceAlpha &&
            raster.alpha_comparison == AlphaComparison::GreaterEqual &&
            raster.alpha_reference == 100.0f / 255.0f,
            "NOSHADER retains typed source transparency and OVERLAY without FX precedence");
    terrain.material_skins[0].reset();
    require(!material_has_required_textures(terrain,true) &&
            material_has_required_textures(terrain,false),
            "missing terrain FX image blocks only an effects-enabled draw");
    const auto restored = original_material_draw_state(terrain,true);
    require(restored.technique == enabled.technique && restored.passes.data() == enabled.passes.data() &&
            restored.passes.size() == 5 &&
            restored.passes[0].raster.alpha_comparison == AlphaComparison::Always,
            "a dynamic NOSHADER toggle does not mutate cached terrain FX state");

    NativeMaterialState normal;
    normal.technique = MaterialTechnique::NormalOptim;
    normal.entity_skins[0] = 0;
    configure_material_state(normal,"",0);
    require(!material_has_required_textures(normal,true) &&
            material_has_required_textures(normal,false),
            "NOSHADER NormalOptim requires only BASE and treats image index zero as present");
    const auto fixed = original_material_draw_state(normal,false);
    require(fixed.technique == MaterialTechnique::FixedFunction &&
            !material_requires_tangent(fixed.technique) && !material_requires_uv1(fixed.technique) &&
            !material_uses_cube(fixed.technique),
            "effective NOSHADER family needs neither tangents, UV1 nor cube bindings");
    NativeMaterialState cube;
    cube.technique = MaterialTechnique::CubeNormalOptim;
    cube.entity_skins[0] = 0;
    configure_material_state(cube,"",0);
    require(material_uses_cube(original_material_draw_state(cube,true).technique) &&
            !material_uses_cube(original_material_draw_state(cube,false).technique) &&
            !material_has_required_textures(cube,true) && material_has_required_textures(cube,false),
            "NOSHADER cube family skips missing FX cube and normal images but retains source BASE");
    normal.entity_skins[0].reset();
    require(!material_has_required_textures(normal,false),
            "NOSHADER still rejects a genuinely missing source BASE image");
}

void native_quality_light_consumer_checks(const Gl& gl, GLuint framebuffer) {
    using namespace c4::rendering;
    using namespace c4::rendering::quality;
    const auto fragment = std::string("#version 120\n") + normal_shader + fixed_shader +
        pbr_shader + shadow_shader + R"GLSL(
uniform int checkIndex;
uniform sampler2D depthMap;
uniform vec2 depthBias;
uniform float depthRange;
uniform vec2 pcfUv;
uniform vec4 pcfTile;
uniform float pcfNormal;
void main() {
    vec3 base = vec3(.2,.4,.6), tint = vec3(.8,.6,.9);
    vec3 ambient = vec3(.1,.2,.15), sun = vec3(.3,.35,.2);
    vec3 n = vec3(0,0,1), v = normalize(vec3(.4,0,1)), l = normalize(vec3(.2,0,1));
    vec3 full, residual;
    if (checkIndex == 0) {
        full = c4FixedModel(base,tint*(ambient+sun),true);
        residual = c4FixedModel(base,tint*ambient,true);
    } else if (checkIndex == 1) {
        vec3 lightmap = vec3(.2,.7,.5);
        full = c4FixedShadow(lightmap,tint*(ambient+sun),base);
        residual = c4FixedShadow(lightmap,tint*ambient,base);
    } else if (checkIndex == 2 || checkIndex == 3) {
        vec4 bump = vec4(.5,.5,1,.25), skills = vec4(1,.8,.25,8);
        vec4 lightmap = vec4(checkIndex == 2 ? .8 : .2,.7,.5,.6);
        full = c4_normal_shadow_shade(vec4(base,.6),bump,lightmap,v,l,
            vec4(ambient,.5),skills,bump.a).rgb;
        residual = full - c4_normal_shadow_shade(vec4(base,.6),bump,lightmap,v,l,
            vec4(0),skills,bump.a).rgb;
    } else {
        full = c4TerrainBase(base,tint*(ambient+sun));
        residual = c4TerrainBase(base,tint*ambient);
    }
    vec3 direct = full - residual;
    if (checkIndex < 5) {
        vec3 restored = c4_quality_srgb_to_linear(residual) +
            c4_quality_source_sun_metallic(direct,residual,0,.8,0,n,v,l);
        gl_FragColor = vec4(restored-c4_quality_srgb_to_linear(full),1);
    } else if (checkIndex == 5) {
        vec3 matte = c4_quality_source_sun_metallic(direct,residual,0,.9,1,n,n,n);
        vec3 metal = c4_quality_source_sun_metallic(direct,residual,1,.9,1,n,n,n);
        vec3 smooth = c4_quality_source_sun_metallic(direct,residual,0,.2,1,n,n,n);
        vec3 back = c4_quality_source_sun_metallic(direct,residual,0,.8,0,-n,v,l);
        gl_FragColor = vec4(length(matte-metal),length(matte-smooth),length(back),1);
    } else if (checkIndex == 6) {
        // Receiver is ten world units behind its caster, independent of padding.
        float depth = .25 + 10.0/depthRange;
        float visible = c4_quality_shadow_pcf(depthMap,vec4(0,0,depth*2.0-1.0,1),
            vec4(0,0,1,1),vec2(1.0),vec2(.5,.5),1.0,depthBias.x,depthBias.y);
        gl_FragColor = vec4(visible,visible,visible,1);
    } else if (checkIndex == 9) {
        float visible = c4_quality_shadow_pcf(depthMap,vec4(pcfUv*2.0-1.0,0,1),
            pcfTile,vec2(.25),vec2(.5,.5),pcfNormal,depthBias.x,depthBias.y);
        gl_FragColor = vec4(visible,visible,visible,1);
    } else {
        // Source albedo already colours full/residual SUN energy. A metallic
        // redistribution must scale its white response exactly once by that
        // linear energy, for both dark grey and saturated source materials.
        vec3 sourceAlbedo = checkIndex == 7 ? vec3(.08) : vec3(.06,.8,.15);
        vec3 whiteFull = c4FixedModel(vec3(1),ambient+sun,true);
        vec3 whiteResidual = c4FixedModel(vec3(1),ambient,true);
        vec3 tintedFull = c4FixedModel(sourceAlbedo,ambient+sun,true);
        vec3 tintedResidual = c4FixedModel(sourceAlbedo,ambient,true);
        vec3 whiteEnergy = c4_quality_srgb_to_linear(whiteFull)
            - c4_quality_srgb_to_linear(whiteResidual);
        vec3 tintedEnergy = c4_quality_srgb_to_linear(tintedFull)
            - c4_quality_srgb_to_linear(tintedResidual);
        vec3 whiteMetal = c4_quality_source_sun_metallic(whiteFull-whiteResidual,
            whiteResidual,1,.4,1,n,v,l);
        vec3 tintedMetal = c4_quality_source_sun_metallic(tintedFull-tintedResidual,
            tintedResidual,1,.4,1,n,v,l);
        gl_FragColor = vec4(tintedMetal-whiteMetal*tintedEnergy/whiteEnergy,length(tintedMetal));
    }
}
)GLSL";
    const GLuint program = gl.program(fragment);
    const auto uniform_float = load<PFNGLUNIFORM1FPROC>("glUniform1f");
    const auto uniform_vec2 = load<PFNGLUNIFORM2FVPROC>("glUniform2fv");
    const auto uniform_vec4 = load<PFNGLUNIFORM4FVPROC>("glUniform4fv");
    gl.bind_framebuffer(GL_FRAMEBUFFER,framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,1,1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    gl.use_program(program);
    const auto render = [&](int index) {
        gl.uniform_int(gl.uniform_location(program,"checkIndex"),index);
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(-1,1); glVertex2f(1,1);
        glEnd();
        Pixel result{};
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result.data());
        require(glGetError() == GL_NO_ERROR,"native SUN energy/PCF consumer OpenGL error");
        return result;
    };
    for (int index=0; index<5; ++index) {
        const auto result = render(index);
        require(std::abs(result[0])+std::abs(result[1])+std::abs(result[2]) < .00001f &&
            result[3] == 1.f,"matte SUN preserves full source energy across MOD2X/lightmap/Normal/terrain");
    }
    const auto shape = render(5);
    require(shape[0] > .001f && shape[1] > .001f && shape[2] == 0.f,
        "energy-matched SUN still responds to metallic/roughness and excludes backfaces");
    for (const int index : {7,8}) {
        const auto result = render(index);
        require(std::abs(result[0])+std::abs(result[1])+std::abs(result[2]) < .00001f &&
                result[3] > .00001f,
            "metallic SUN scales dark and saturated source albedo exactly once through recovered linear energy");
    }
    GLuint depth_texture{};
    glGenTextures(1,&depth_texture);
    glBindTexture(GL_TEXTURE_2D,depth_texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const float stored_depth = .25f;
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE32F_ARB,1,1,0,GL_LUMINANCE,GL_FLOAT,&stored_depth);
    gl.uniform_int(gl.uniform_location(program,"depthMap"),0);
    constexpr std::array<float,16> projection{1,0,0,0, 0,1,0,0, 0,0,-21.f/19,-1, 0,0,-40.f/19,0};
    const auto inverse = *c4::quality_matrix_inverse(projection);
    for (const float padding : {0.f,300000.f}) {
        const auto cascades = build_shadow_cascades(inverse,{0,0,-1},1,20,-1,1,1,1024,.5f,padding);
        require(cascades.has_value(),"PCF consumer cascade fit");
        const auto& matrix = cascades->matrices[0];
        const float range = 2.f/std::hypot(matrix[2],matrix[6],matrix[10]);
        uniform_float(gl.uniform_location(program,"depthRange"),range);
        uniform_vec2(gl.uniform_location(program,"depthBias"),1,cascades->depth_bias[0].data());
        require(render(6) == Pixel{0,0,0,1},
                "PCF shadows a fixed world separation despite heavily padded cascade depth");
    }
    const std::array<float,16> edge_depths{
        .25f,.25f,.75f,.75f, .25f,.75f,.75f,.75f,
        .25f,.75f,.75f,.75f, .25f,.75f,.75f,.75f};
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE32F_ARB,4,4,0,GL_LUMINANCE,GL_FLOAT,edge_depths.data());
    const std::array<float,2> no_bias{};
    uniform_vec2(gl.uniform_location(program,"depthBias"),1,no_bias.data());
    uniform_float(gl.uniform_location(program,"pcfNormal"),1.f);
    // The right atlas tile is lit; the first texel column in the left is
    // shadowed. Signed Y scales exercise the Vulkan atlas coordinate adapter.
    for (const bool flipped : {false,true}) {
        const std::array<float,4> tile{0.f,flipped?1.f:0.f,.5f,flipped?-1.f:1.f};
        uniform_vec4(gl.uniform_location(program,"pcfTile"),1,tile.data());
        float previous = -1.f;
        for (unsigned step = 0; step <= 16; ++step) {
            const float fraction = static_cast<float>(step)/16.f;
            const float y = .125f + fraction*.25f;
            const std::array<float,2> uv{.25f+fraction*.5f,flipped?1.f-y:y};
            uniform_vec2(gl.uniform_location(program,"pcfUv"),1,uv.data());
            const float visible = render(9)[0];
            require(std::abs(visible-fraction*fraction) < .00001f &&
                    visible >= previous && (step==0 || visible>previous),
                    "PCF gives monotonic bilinear intermediate coverage under subtexel motion");
            previous = visible;
        }
        const std::array<float,2> tile_edge{0.f,.5f};
        uniform_vec2(gl.uniform_location(program,"pcfUv"),1,tile_edge.data());
        require(render(9) == Pixel{0,0,0,1},"PCF clamps all taps inside the selected atlas tile");
        const std::array<float,2> grazing_bias{.01f,.04f};
        uniform_vec2(gl.uniform_location(program,"depthBias"),1,grazing_bias.data());
        uniform_float(gl.uniform_location(program,"pcfNormal"),0.f);
        require(render(9) == Pixel{0,0,0,1},"PCF grazing bias stays within one receiver-slope texel");
        uniform_vec2(gl.uniform_location(program,"depthBias"),1,no_bias.data());
        uniform_float(gl.uniform_location(program,"pcfNormal"),1.f);
    }
    glDeleteTextures(1,&depth_texture);
    gl.use_program(0);
    gl.delete_program(program);
}

void native_quality_ao_consumer_checks(const Gl& gl, GLuint framebuffer) {
    using namespace c4::rendering::quality;
    const auto fragment = std::string("#version 120\n") + post_shader + R"GLSL(
uniform sampler2D depthMap, normalMap;
uniform mat4 projection, inverseProjection;
uniform vec2 clipScaleBias;
uniform int sampleCount;
void main() {
    float visibility = c4_quality_ssao(depthMap,normalMap,vec2(.5),vec2(1.0/65.0),
        projection,inverseProjection,clipScaleBias,1.0,1.0,.025,sampleCount);
    gl_FragColor = vec4(visibility,visibility,visibility,1);
}
)GLSL";
    const GLuint program = gl.program(fragment);
    const auto active_texture = load<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    const auto uniform_matrix = load<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    const auto uniform_vec2 = load<PFNGLUNIFORM2FVPROC>("glUniform2fv");
    std::array<GLuint,2> textures{};
    glGenTextures(2,textures.data());
    for (unsigned unit=0; unit<textures.size(); ++unit) {
        active_texture(GL_TEXTURE0+unit);
        glBindTexture(GL_TEXTURE_2D,textures[unit]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    gl.bind_framebuffer(GL_FRAMEBUFFER,framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,1,1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    gl.use_program(program);
    gl.uniform_int(gl.uniform_location(program,"depthMap"),0);
    gl.uniform_int(gl.uniform_location(program,"normalMap"),1);
    const auto render = [&](bool reversed) {
        active_texture(GL_TEXTURE1);
        const std::array<float,3> normal{.5f,.5f,reversed?0.f:1.f};
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F_ARB,1,1,0,GL_RGB,GL_FLOAT,normal.data());
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(-1,1); glVertex2f(1,1);
        glEnd();
        Pixel result{};
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result.data());
        require(glGetError() == GL_NO_ERROR,"native SSAO visible-face consumer OpenGL error");
        return result;
    };
    // Identical depth encoding, with GL [-1,1] and Vulkan [0,1] clip adapters.
    constexpr float plane_depth = 10.f/9 - 10.f/(9*4);
    constexpr float blocker_depth = 10.f/9 - 10.f/(9*3.5f);
    std::array<float,65*65> depths{};
    for (const bool vulkan_clip : {false,true}) {
        const std::array<float,16> projection{1,0,0,0, 0,1,0,0,
            0,0,vulkan_clip?-10.f/9:-11.f/9,-1,
            0,0,vulkan_clip?-10.f/9:-20.f/9,0};
        const auto inverse = *c4::quality_matrix_inverse(projection);
        const std::array<float,2> clip{vulkan_clip?1.f:2.f,vulkan_clip?0.f:-1.f};
        uniform_matrix(gl.uniform_location(program,"projection"),1,GL_FALSE,projection.data());
        uniform_matrix(gl.uniform_location(program,"inverseProjection"),1,GL_FALSE,inverse.data());
        uniform_vec2(gl.uniform_location(program,"clipScaleBias"),1,clip.data());
        for (const int count : {16,32}) {
            gl.uniform_int(gl.uniform_location(program,"sampleCount"),count);
            depths.fill(plane_depth);
            active_texture(GL_TEXTURE0);
            glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE32F_ARB,65,65,0,
                GL_LUMINANCE,GL_FLOAT,depths.data());
            const auto front = render(false);
            const auto back = render(true);
            require(front == Pixel{1,1,1,1} && back == front,
                "SSAO never self-occludes a visible flat plane with reversed source normals");
            // A nearer surrounding surface blocks the hemisphere, but the
            // center texel remains the receiver at its original view-space Z.
            depths.fill(blocker_depth);
            depths[32*65+32] = plane_depth;
            active_texture(GL_TEXTURE0);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,65,65,GL_LUMINANCE,GL_FLOAT,depths.data());
            const auto blocked_front = render(false);
            const auto blocked_back = render(true);
            require(blocked_front[0] >= 0 && blocked_front[0] < .9f &&
                    blocked_front[3] == 1 &&
                    std::abs(blocked_front[0]-blocked_back[0]) < .00001f,
                "SSAO visible-face orientation preserves genuine external occlusion");
        }
    }
    active_texture(GL_TEXTURE0);
    glDeleteTextures(2,textures.data());
    gl.use_program(0);
    gl.delete_program(program);
}

void native_shadow_consumer_checks(const Gl& gl, GLuint color_framebuffer) {
    using namespace c4::rendering;
    using namespace c4::rendering::quality;
    constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    RenderScene scene;
    scene.posed_vertices.push_back({{-1,-1,.35f,.5f,.5f,0,0,1},
                                   {1,-1,.35f,.5f,.5f,0,0,1},
                                   {0,1,.35f,.5f,.5f,0,0,1}});
    const std::array<std::uint32_t,3> indices{0,1,2};
    const std::array<std::array<float,4>,3> tangents{{{1,0,0,1},{1,0,0,1},{1,0,0,1}}};
    scene.meshes.push_back({scene.posed_vertices[0],indices,0,{},tangents,
        c4::assets::TangentProvenance::ImportedSource});
    scene.textures.resize(2);
    c4::assets::GpuImage image;
    image.width = image.height = 1;
    image.source_format = c4::assets::ImageSourceFormat::Dds;
    image.source_bits_per_pixel = 32;
    image.source_alpha_known = true;
    image.source_alpha_flag = true;
    image.texels = {64,128,192,128};
    image.levels.push_back({1,1,0,4,4,4});
    scene.gpu_textures.push_back(image);
    scene.referenced_texture_indices.push_back(0);
    auto normal_image = image;
    normal_image.texels = {128,128,255,64};
    scene.gpu_textures.push_back(normal_image);
    scene.referenced_texture_indices.push_back(1);
    scene.has_bounds = true;
    scene.bounds_min = {-1,-1,-10};
    scene.bounds_max = {1,1,0};
    SourceEntityLightingCache cache;
    cache.environment_known = cache.chroma_known = true;
    Instance instance;
    instance.model_to_world = identity;
    instance.model_to_world[5] = 1.2f;
    instance.model_to_world[10] = .8f;
    instance.model_to_world[12] = .2f;
    instance.model_to_world[13] = .4f;
    instance.model_to_world[14] = -4.f;
    instance.material = "mat_model";
    instance.source = "native shadow depth fixture";
    instance.material_state.entity_skins[0] = 0;
    configure_material_state(instance.material_state,instance.material,0);
    instance.source_lighting_cache = &cache;
    instance.original_alpha = OriginalModelAlphaDecision{false,false,true,
        BlendFactor::One,BlendFactor::Zero,1};
    scene.instances.push_back(instance);
    NativeGameVisualState visuals;
    visuals.view_inputs_known = true;
    visuals.world_to_view = identity;
    visuals.view_position = {0,0,0};
    visuals.view_direction = {0,-1,0};
    visuals.sun_direction = {1,0,0}; // Must not replace the selected source SUN.
    SourceLightingState lighting;
    lighting.lights_known = lighting.ambient_known = lighting.fog_known = lighting.view_known = true;
    lighting.source_effects_known = true;
    lighting.world_to_view = identity;
    lighting.light_count = lighting.sun_slot_plus_one = 1;
    lighting.lights[0].direction_type = {0,-1,0,3};
    lighting.lights[0].cone_enabled[2] = 1;
    lighting.lights[0].diffuse = {.6f,.5f,.4f,0};
    publish_source_view_ambient(lighting,0,true);
    CameraState camera;
    camera.view = identity;
    camera.projection = {1,0,0,0, 0,1,0,0, 0,0,-11.f/9,-1, 0,0,-20.f/9,0};
    camera.view_projection = camera.projection;
    camera.inverse_projection = *c4::quality_matrix_inverse(camera.projection);
    camera.clip_near = 1;
    camera.clip_far = 10;
    camera.projection_known = true;
    FrameSnapshot frame{.scene=scene,.instances=scene.instances,.camera=camera,
        .visuals=&visuals,.source_lighting=&lighting,.source_effects_enabled=false};
    c4::InspectionGL production_gl;
    OpenGLQuality quality;
    quality.initialize(1,1);
    c4::InspectionScene renderer(scene,production_gl,&quality);
    GraphicSettings settings;
    settings.preset = Preset::Custom;
    settings.shadows = ShadowQuality::High;
    settings.pbr = settings.internal_hdr = settings.ssao = settings.bloom = false;
    settings.samples = 1;
    settings.post_aa = PostAA::Off;
    quality.set_settings(settings);
    require(quality.resolved().shadow_cascades == 4,"native High shadow consumer retains four cascades");
    const auto cascades = build_shadow_cascades(*c4::quality_matrix_inverse(camera.view_projection),
        {0,0,-1},1,10,-1,1,4,quality.resolved().shadow_size,native_shadow_split_lambda,std::sqrt(108.f));
    require(cascades.has_value(),"native shadow fixture cascade fit");
    GLuint depth_framebuffer{};
    gl.gen_framebuffers(1,&depth_framebuffer);
    const auto render = [&](bool cast, const char* message, bool fragments = true) {
        gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT); // A stale caller/sprite state must not cull ordinary casters.
        const auto before = renderer.shadow_draw_calls();
        const auto passes = quality.diagnostics().shadow_passes;
        renderer.draw(frame);
        require(renderer.shadow_draw_calls()-before == (cast ? 4u : 0u),message);
        require(quality.diagnostics().shadow_passes-passes == 4,"selected SUN executes all four cascade passes");
        gl.bind_framebuffer(GL_FRAMEBUFFER,depth_framebuffer);
        gl.framebuffer_texture(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,quality.shadow_texture(),0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        require(gl.framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,"shadow readback depth attachment");
        const auto& model = scene.instances[0].model_to_world;
        const float x = model[12], y = model[13] - .4f;
        const float z = model[14] + model[10]*scene.posed_vertices[0][0].z;
        for (unsigned cascade=0; cascade<4; ++cascade) {
            const auto& matrix = cascades->matrices[cascade];
            const auto row = [&](unsigned r) {
                return matrix[r]*x + matrix[4+r]*y + matrix[8+r]*z + matrix[12+r];
            };
            const auto tile = quality.shadow_tile(cascade);
            const unsigned atlas_size = quality.capabilities().max_shadow_size*2;
            const GLint px = static_cast<GLint>((tile[0]+(row(0)*.5f+.5f)*tile[2])*atlas_size);
            const GLint py = static_cast<GLint>((tile[1]+(row(1)*.5f+.5f)*tile[3])*atlas_size);
            float depth{};
            glReadPixels(px,py,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
            require(cast && fragments ? std::abs(depth-(row(2)*.5f+.5f)) < .0001f : depth == 1.f,
                    std::string(message)+" source-coordinate atlas depth");
        }
        require(glGetError() == GL_NO_ERROR,"native shadow consumer OpenGL error");
    };
    render(true,"opaque source model casts depth");
    auto& live = scene.instances[0];
    auto& raster = live.material_state.source_pass.raster;
    raster.alpha_comparison = AlphaComparison::GreaterEqual;
    raster.alpha_reference = 128.f/255*.5f;
    live.original_alpha->modulate_diffuse = true;
    live.original_alpha->diffuse_alpha = .5f;
    render(true,"cutout accepts actual texture times draw alpha at equality");
    raster.alpha_reference += .01f;
    render(true,"cutout rejects modulated final alpha, not raw texture alpha",false);
    raster.alpha_reference = 128.f/255*.5f;
    raster.color_key = true;
    raster.alpha_comparison = AlphaComparison::Always;
    render(true,"colour-key caster consumes converted upload alpha at equality");
    raster.alpha_reference += .01f;
    render(true,"colour-key caster rejects converted alpha below threshold",false);
    raster.color_key = false;
    raster.alpha_comparison = AlphaComparison::Always;
    live.flags = 1u<<10;
    render(false,"TRANSLUCENT balloons do not cast opaque sheets");
    live.flags = 0;
    live.original_alpha->modulate_diffuse = false;
    live.original_alpha->alpha_blend = true;
    live.original_alpha->destination_blend = BlendFactor::One;
    render(false,"additive alpha draw does not cast opaque sheets");
    live.original_alpha = instance.original_alpha;
    live.original_alpha->alpha_blend = true;
    live.original_alpha->source_blend = BlendFactor::SourceAlpha;
    live.original_alpha->destination_blend = BlendFactor::InverseSourceAlpha;
    live.original_alpha->depth_write = false;
    render(false,"blended no-depth-write shadow proxy never casts a solid rectangle");
    raster.alpha_comparison = AlphaComparison::GreaterEqual;
    raster.alpha_reference = .25f;
    render(true,"legitimate alpha-cutout remains a caster with no depth write");
    raster.alpha_comparison = AlphaComparison::Always;
    live.original_alpha = instance.original_alpha;
    scene.posed_vertices[0][0].z = scene.posed_vertices[0][1].z = scene.posed_vertices[0][2].z = .8f;
    ++scene.meshes[0].pose_revision;
    render(true,"shadow path preserves dynamic source pose uploads");
    live.original_alpha.reset();
    render(false,"unproven model draw transparency is excluded");
    live.original_alpha = instance.original_alpha;
    frame.source_effects_enabled = true;
    visuals.sun_direction = {0,-1,0};
    live.material_state.technique = MaterialTechnique::NormalTileSpec;
    live.material_state.material_skins[0] = 1;
    configure_material_state(live.material_state,live.material,1u<<12);
    live.material_state.skills = {};
    live.material_state.skills[1] = 1;
    live.material_state.skills[3] = 8;
    live.material_state.skills_known_mask = 0x7fu;
    live.flags = 1u<<12;
    auto& shader_raster = live.material_state.passes[0].raster;
    shader_raster.alpha_comparison = AlphaComparison::GreaterEqual;
    shader_raster.alpha_reference = .45f;
    render(true,"authored Normal FX accepts its recovered lit alpha");
    shader_raster.alpha_reference = .46f;
    render(true,"authored Normal FX rejects final alpha even when bitmap alpha passes",false);
    frame.source_effects_enabled = false;
    live = instance;
    for (std::uint8_t stq : {1,2,3}) {
        scene.source_profile.stq = stq;
        const auto passes = quality.diagnostics().shadow_passes;
        const auto shadows = renderer.shadow_draw_calls();
        const auto material_draws = renderer.draw_calls();
        const Pixel clear{1,0,1,1};
        GLint previous_front_face{};
        glGetIntegerv(GL_FRONT_FACE,&previous_front_face);
        const auto raster_pixel = [&](bool cull, GLenum front_face, GLenum culled_face) {
            gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glViewport(0,0,1,1);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(clear[0],clear[1],clear[2],clear[3]);
            glClear(GL_COLOR_BUFFER_BIT);
            glFrontFace(front_face);
            glCullFace(culled_face);
            if (cull) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);
            renderer.draw(frame);
            Pixel pixel{};
            glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel.data());
            require(glGetError() == GL_NO_ERROR,"authored STQ main-pass pixel readback");
            return pixel;
        };
        const auto reference = raster_pixel(false,GL_CCW,GL_FRONT);
        require(std::abs(reference[0]-clear[0]) + std::abs(reference[1]-clear[1]) +
                std::abs(reference[2]-clear[2]) > .1f,
                "authored STQ ordinary triangle covers the center pixel, not just the clear colour");
        // The projected triangle is CCW: either seeded pair would discard it without mesh isolation.
        for (const GLenum culled_face : {GL_FRONT,GL_BACK}) {
            const auto pixel = raster_pixel(true,culled_face == GL_FRONT ? GL_CCW : GL_CW,culled_face);
            for (unsigned channel=0; channel<4; ++channel)
                require(std::abs(pixel[channel]-reference[channel]) < .0001f,
                        "authored STQ ordinary triangle pixels ignore stale sprite/external cull state");
        }
        glFrontFace(static_cast<GLenum>(previous_front_face));
        require(quality.resolved().shadows == ShadowQuality::High,
                "authored shadow authority does not rewrite requested native High capacity");
        require(quality.diagnostics().shadow_passes == passes && renderer.shadow_draw_calls() == shadows,
                "loaded authored STQ records no supplemental SUN depth pass");
        require(renderer.draw_calls() > material_draws,
                "authored shadow authority retains the original material draw path");
        c4::RuntimeDiagnostics diagnostics;
        renderer.diagnostics(diagnostics);
        require(diagnostics.authored_source_shadows &&
                diagnostics.active_shadow_cascades == 0 && diagnostics.active_shadow_size == 0,
                "authored shadow diagnostics distinguish active source art from requested CSM capacity");
    }
    scene.source_profile.stq = 0;
    render(true,"explicit STQ0 restores supplemental SUN depth path");
    for (const unsigned slot : {0u,2u}) {
        lighting.sun_slot_plus_one = slot;
        const auto before = quality.diagnostics().shadow_passes;
        gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
        renderer.draw(frame);
        require(quality.diagnostics().shadow_passes == before,"unselected/out-of-range source SUN never runs shadows");
    }
    lighting.sun_slot_plus_one = 1;
    lighting.lights[0].cone_enabled[2] = 0;
    const auto before = quality.diagnostics().shadow_passes;
    gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
    renderer.draw(frame);
    require(quality.diagnostics().shadow_passes == before,"disabled selected source SUN never runs shadows");
    lighting.lights[0].cone_enabled[2] = 1;
    live.kind = InstanceKind::Image; // Asset-only colour fixture does not require source lighting.
    lighting.lights_known = false;
    gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
    renderer.draw(frame);
    require(quality.diagnostics().shadow_passes == before,"unknown source light data never creates a SUN");
    lighting.lights_known = true;
    frame.source_lighting = nullptr;
    gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
    renderer.draw(frame);
    require(quality.diagnostics().shadow_passes == before,"visual SUN direction alone never creates shadows");
    gl.bind_framebuffer(GL_FRAMEBUFFER,color_framebuffer);
    gl.delete_framebuffers(1,&depth_framebuffer);
}

void native_material_consumer_checks(const Gl& gl, GLuint framebuffer) {
    using namespace c4::rendering;
    using namespace c4::rendering::quality;
    constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const std::array<c4::assets::Vertex,3> vertices{{
        {-1,-1,0,.5f,.5f,0,0,1}, {3,-1,0,.5f,.5f,0,0,1}, {-1,3,0,.5f,.5f,0,0,1}
    }};
    const RenderScene scene = [&] {
        RenderScene result;
        result.meshes.push_back({vertices,{}});
        const auto image = [](std::array<std::uint8_t,4> color, unsigned faces) {
            c4::assets::GpuImage result;
            result.width = result.height = 1;
            result.face_count = faces;
            result.source_format = c4::assets::ImageSourceFormat::Dds;
            result.source_bits_per_pixel = 32;
            result.source_alpha_known = true;
            result.implementation_precision_fallback = true;
            result.texels.resize(4 * faces);
            for (unsigned face = 0; face < faces; ++face)
                for (unsigned channel = 0; channel < 4; ++channel)
                    result.texels[4 * face + channel] = color[channel];
            result.levels.push_back({1,1,0,result.texels.size(),4,4});
            return result;
        };
        result.gpu_textures.reserve(2);
        result.gpu_textures.push_back(image({64,102,153,192},1));
        result.gpu_textures.push_back(image({204,51,26,64},6));
        return result;
    }();
    const SourceEntityLightingCache cache = [] {
        SourceEntityLightingCache result;
        result.environment_known = result.chroma_known = true;
        return result;
    }();
    const NativeGameVisualState visuals = [&] {
        NativeGameVisualState result;
        result.world_to_view = identity;
        result.view_position = {0,2,0}; // Source shader axes; native eye is (0,0,2).
        result.view_direction = {0,-1,0};
        result.sun_direction = {0,-1,0};
        result.sun_color = {.6f,.5f,.4f,1};
        result.view_inputs_known = true;
        return result;
    }();
    const SourceLightingState lighting = [&] {
        SourceLightingState result;
        result.world_to_view = identity;
        result.view_position = {0,2,0,1};
        result.lights_known = result.ambient_known = result.fog_known = result.view_known = true;
        result.source_effects_known = true;
        publish_source_view_ambient(result,0,true);
        result.light_count = result.sun_slot_plus_one = 1;
        result.lights[0].diffuse = {.6f,.5f,.4f,0};
        result.lights[0].specular = result.lights[0].diffuse;
        result.lights[0].direction_type = {0,-1,0,3};
        result.lights[0].attenuation_falloff = {1,0,0,0};
        result.lights[0].cone_enabled = {0,0,1,0};
        return result;
    }();
    const auto alpha = original_model_alpha_decision(
        {true,.75f,1,scene.gpu_textures[0].source_alpha_flag,false,100,false,false,false,1});
    const Instance instance = [&] {
        Instance result;
        result.material = "Mtl_car3";
        result.source = "native material pixel fixture";
        result.flags = 1u << 12; // OVERLAY compares the actual final source alpha.
        result.model_to_world = identity;
        result.source_lighting_cache = &cache;
        result.original_alpha = alpha;
        auto& material = result.material_state;
        material.technique = MaterialTechnique::EnvCubeMix;
        material.entity_skins[0] = 0;
        material.material_skins[0] = 1;
        configure_material_state(material,result.material,result.flags);
        material.texture_transform = identity;
        configure_source_bitmap_raster(material,true,scene.gpu_textures[0].source_alpha_flag);
        return result;
    }();
    const FrameSnapshot frame{.scene = scene, .instances = {&instance,1},
        .camera = {identity,"native material pixel fixture"}, .visuals = &visuals,
        .source_lighting = &lighting};
    c4::InspectionGL production_gl;
    c4::OriginalMaterialProgram program(production_gl);
    SourceLightingDeviceState device;
    device.sourceMaxTextureWidth = 1024;
    device.supportedADD = device.supportedMOD2X = device.capabilitiesKnown = true;
    program.set_lighting_device(device);
    OpenGLQuality quality;
    quality.initialize(1,1);
    quality.set_material_support(program.qualitySupport(),false);

    c4::InspectionResources resources{production_gl};
    resources.textures.resize(2);
    glGenTextures(2,resources.textures.data());
    for (unsigned slot = 0; slot < 2; ++slot) {
        const auto& image = scene.gpu_textures[slot];
        const GLenum target = slot == 0 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
        production_gl.glActiveTexture(slot == 0 ? GL_TEXTURE0 : GL_TEXTURE3);
        glBindTexture(target,resources.textures[slot]);
        glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(target,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        for (unsigned face = 0; face < image.face_count; ++face)
            glTexImage2D(slot == 0 ? target : GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,image.texels.data() + 4 * face);
    }
    production_gl.glActiveTexture(GL_TEXTURE0);
    production_gl.glBindBuffer(GL_ARRAY_BUFFER,0);
    production_gl.glEnableVertexAttribArray(0);
    production_gl.glEnableVertexAttribArray(2);
    production_gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(vertices[0]),&vertices[0].x);
    production_gl.glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(vertices[0]),&vertices[0].nx);
    production_gl.glVertexAttrib2f(1,.5f,.5f);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);

    GraphicSettings settings;
    settings.preset = Preset::Custom;
    settings.pbr = true;
    settings.internal_hdr = settings.ssao = settings.bloom = false;
    settings.samples = 1;
    settings.post_aa = PostAA::Off;
    settings.shadows = ShadowQuality::Off;
    settings.material_overrides = {{"Mtl_car3",0,1,0}};
    const auto draw = original_material_draw_state(instance.material_state,true);
    const auto render = [&](const GraphicSettings& requested) {
        quality.set_settings(requested);
        gl.bind_framebuffer(GL_FRAMEBUFFER,framebuffer);
        glViewport(0,0,1,1);
        program.bind_frame(frame);
        program.bind_quality_frame(&quality,nullptr);
        (void)program.bind_instance(instance,draw.passes[0],0,&scene.gpu_textures[0],
            &*instance.original_alpha,draw,true,quality.material_overrides());
        require(program.incomplete_source_state(),
                "native draw retains Original raster/texture parity diagnostics");
        glClearColor(-7,-7,-7,-7);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES,0,3);
        Pixel pixel{};
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel.data());
        require(glGetError() == GL_NO_ERROR,"production native material draw: OpenGL error");
        for (float channel : pixel)
            require(std::isfinite(channel),"production native material draw produced nonfinite pixel");
        require(std::abs(pixel[3] - .75f * (192.f / 255.f)) < .0003f,
                "production native material preserves actual source bitmap/draw alpha");
        return pixel;
    };
    const auto same_alpha = [](const Pixel& a, const Pixel& b) {
        return std::bit_cast<std::uint32_t>(a[3]) == std::bit_cast<std::uint32_t>(b[3]);
    };
    const auto different_rgb = [&](const Pixel& a, const Pixel& b, const char* message) {
        require(std::abs(a[0]-b[0]) + std::abs(a[1]-b[1]) + std::abs(a[2]-b[2]) > .01f &&
                same_alpha(a,b),message);
    };
    const Pixel matte = render(settings);
    settings.material_overrides[0] = {"Mtl_car3",1,.05f,1};
    const Pixel metal = render(settings);
    different_rgb(matte,metal,"Custom PBR matte/metal overrides must change actual RGB, not source alpha");
    settings.material_overrides[0] = {"Mtl_car3",0,1,1};
    different_rgb(matte,render(settings),
                  "reflectivity alone must change actual authored cube RGB, not source alpha");
    for (const Preset preset : {Preset::Custom,Preset::Original}) {
        settings = GraphicSettings{};
        settings.preset = preset;
        if (preset == Preset::Custom) settings.pbr = false;
        const Pixel baseline = render(settings);
        require(same_alpha(matte,baseline),
                "PBR-off/Original preserve the same source alpha bits as Custom PBR");
        for (const ModernMaterialOverride& override : {
                 ModernMaterialOverride{"Mtl_car3",0,1,0}, ModernMaterialOverride{"Mtl_car3",1,.05f,1}}) {
            settings.material_overrides = {override};
            require(std::bit_cast<std::array<std::uint32_t,4>>(render(settings)) ==
                    std::bit_cast<std::array<std::uint32_t,4>>(baseline),
                    "PBR-off/Original must ignore named overrides bit-identically in actual RGBA");
        }
    }
    settings.preset = Preset::Custom;
    settings.pbr = true;
    settings.material_overrides = {{"Mtl_car3",1,.05f,1}};
    quality.set_settings(settings);
    program.bind_frame(frame);
    program.bind_quality_frame(&quality,nullptr);
    const auto rejects = [&](const Instance& selected, const OriginalModelAlphaDecision* decision) {
        const auto selected_draw = original_material_draw_state(selected.material_state,true);
        bool rejected = false;
        try {
            (void)program.bind_instance(selected,selected_draw.passes[0],0,&scene.gpu_textures[0],
                decision,selected_draw,true,quality.material_overrides());
        } catch (const std::runtime_error&) { rejected = true; }
        require(rejected,"native overrides must not bypass missing critical original inputs");
    };
    rejects(instance,nullptr); // OVERLAY requires the actual source alpha decision.
    auto missing_cube = instance;
    missing_cube.material_state.material_skins[0].reset();
    rejects(missing_cube,&alpha);
    production_gl.glDisableVertexAttribArray(0);
    production_gl.glDisableVertexAttribArray(2);
}

void shader_checks() {
    const Gl gl;
    const std::string fragment = std::string("#version 120\n")
        + std::string(c4::rendering::normal_shader) + "\n" + std::string(c4::rendering::fixed_shader) + "\n"
        + std::string(c4::rendering::bloom_shader) + "\n"
        + std::string(c4::rendering::source_lighting_shader) + "\n" + fragment_checks;
    GLuint program = gl.program(fragment);
    gl.use_program(program);
    GLint check_uniform = gl.uniform_location(program,"checkIndex");
    GLint source_uniform = gl.uniform_location(program,"sourceMap");
    require(check_uniform >= 0 && source_uniform >= 0,"shader harness uniforms missing");
    gl.uniform_int(source_uniform,0);

    GLuint textures[2]{};
    glGenTextures(2,textures);
    for (GLuint texture : textures) {
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,nullptr);
    }
    GLuint framebuffer = 0;
    gl.gen_framebuffers(1,&framebuffer);
    gl.bind_framebuffer(GL_FRAMEBUFFER,framebuffer);
    gl.framebuffer_texture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
    require(gl.framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,"RGBA32F framebuffer incomplete");
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    glViewport(0,0,1,1);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);

    const Check checks[] = {
        {"tangent handedness",0,{0,-1,0,1}},
        {"sun negation and skill5",1,{-0.7071067812f,-0.7071067812f,0,1}},
        {"normal RGBA lighting",2,{1.38f,2.84f,4.56f,6}},
        {"normal alpha specular mask",3,{0.78f,1.64f,2.76f,3.6f}},
        {"signed shadow specular and RGBA lightmap",4,{0.045f,0.22f,0.72f,0.96f}},
        {"combined normal alpha and shadow mask",5,{0.12f,0.52f,1.395f,1.92f}},
        {"cube interpolation RGBA",6,{1.68f,2.52f,3.355f,3.705f}},
        {"car Fresnel weights",7,{1.32f,2.38f,3.5075f,4.1325f}},
        {"Fresnel grazing front back and oblique",8,{1,0.5f,0.5f,0.96875f}},
        {"bump normal is not normalized",9,{0.45f,0.45f,0.5f,0.45f},{0.5f,0.5f,0.75f,0.9f}},
        {"ordered terrain reverse-alpha layers",10,{0.275f,0.14f,0.54f,1}},
        {"terrain alpha endpoint direction",11,{0.1f,0.2f,0.8f,0.6f}},
        {"bloom alpha-only smooth threshold",12,{0.64f,0.64f,0.64f,0.5f}},
        {"bloom H 21-tap scalar RGBA gain",13,{0.672f,0.672f,0.672f,0.525f},{0.64f,0.64f,0.64f,0.5f}},
        {"bloom composite broadcast and forced alpha",14,{0.3471f,0.4597f,0.5723f,1}},
        {"bloom source axis and fixed offsets",15,{0.5f,0.4718f,0.5282f,0.5f}},
        {"cube vertex reflection",16,{0,0,1,1}},
        {"bloom below threshold preserves RGB",17,{0.1f,0.1f,0.1f,0},{0.1f,0.1f,0.1f,0.9f}},
        {"bloom above threshold preserves HDR RGB",17,{1.2f,1.2f,1.2f,1},{1.2f,1.2f,1.2f,0.1f}},
        {"bloom threshold lower boundary",17,{0.28f,0.28f,0.28f,0},{0.28f,0.28f,0.28f,1}},
        {"bloom threshold upper boundary",17,{1,1,1,1},{1,1,1,0}},
        {"bloom four-tap fixed UV offsets",18,{0.51f,0.5f,0.51f,0.51f}},
        {"bloom V 21-tap scalar RGBA gain",19,{0.672f,0.672f,0.672f,0.525f},{0.64f,0.64f,0.64f,0.5f}},
        {"water column matrix COUNT2 translation and untransformed bump UV",20,{10.125f,20.25f,0.47f,0.31f}},
        {"source point light range boundary and collapsed spot cone",21,{1,0,1,0}},
        {"source linear fog midpoint preserves material alpha",22,{0.5f,0.5f,0.5f,0.3f}},
        {"unsigned DXT1 RG uses bump matrix columns without signed decoding",23,{0.23f,0.79f,0,1},{0.2f,0.3f,0.4f,0.5f}},
        {"fixed alpha selects opaque texture or modulates diffuse exactly once",24,{0.8f,0.2f,1,1}},
        {"fixed model MODULATE2X capability, fallback, terrain independence and saturation",25,{0.5f,0.25f,0.25f,1}}
    };
    for (const Check& check : checks) {
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,check.input.data());
        gl.uniform_int(check_uniform,check.index);
        glClearColor(-7,-7,-7,-7);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(-1,1); glVertex2f(1,1);
        glEnd();
        Pixel actual{};
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
        require(glGetError() == GL_NO_ERROR,std::string(check.name) + ": OpenGL error");
        for (std::size_t channel=0; channel<actual.size(); ++channel) {
            if (!std::isfinite(actual[channel]) || std::abs(actual[channel]-check.expected[channel]) > 0.0003f)
                throw std::runtime_error(std::string(check.name) + " channel " + std::to_string(channel)
                    + ": expected " + std::to_string(check.expected[channel]) + ", got " + std::to_string(actual[channel]));
        }
    }
    native_material_consumer_checks(gl,framebuffer);
    native_shadow_consumer_checks(gl,framebuffer);
    native_quality_light_consumer_checks(gl,framebuffer);
    native_quality_ao_consumer_checks(gl,framebuffer);
    gl.use_program(0);
    gl.delete_program(program);
    gl.bind_framebuffer(GL_FRAMEBUFFER,0);
    gl.delete_framebuffers(1,&framebuffer);
    glDeleteTextures(2,textures);
}

void source_wait1_context_checks() {
    using c4::game::source_wait1_context_ready;
    using c4::physics::Var;
    for (const bool saved_my_is_null : {false,true}) {
        require(source_wait1_context_ready(Var{1023},Var{1},saved_my_is_null),
                "freeze raw 1023 admits either saved MY context");
        require(source_wait1_context_ready(Var{1024},Var{1},saved_my_is_null) == saved_my_is_null &&
                source_wait1_context_ready(Var{2047},Var{1},saved_my_is_null) == saved_my_is_null,
                "freeze raw 1024 through 2047 admits only NULL saved MY");
        require(!source_wait1_context_ready(Var{2048},Var{1},saved_my_is_null),
                "freeze raw 2048 blocks even NULL saved MY");
        require(!source_wait1_context_ready(Var{-1},Var{0},saved_my_is_null) &&
                !source_wait1_context_ready(Var{-1},Var{-1},saved_my_is_null) &&
                source_wait1_context_ready(Var{-1},Var{1},saved_my_is_null),
                "time_frame must be strictly positive and negative freeze is not clamped");
    }
    constexpr c4::game::EntityId saved_my = 0;
    constexpr bool saved_my_is_null = false;
    require(saved_my != c4::game::no_entity &&
            !source_wait1_context_ready(Var{1024},Var{1},saved_my_is_null),
            "entity ID zero is a nonnull saved MY context, represented by an explicit boolean");

    c4::rendering::NativeMaterialState material;
    c4::rendering::WaterMaterialEventState water;
    c4::rendering::start_water_material_event(water,material,Var{1024});
    c4::rendering::start_water_material_event(water,material,Var{1024});
    Var live_freeze{1023};
    if (source_wait1_context_ready(live_freeze,Var{1},saved_my_is_null))
        c4::rendering::resume_water_material_event(water,material,Var{1024});
    live_freeze = Var{1024};
    if (source_wait1_context_ready(live_freeze,Var{1},saved_my_is_null))
        c4::rendering::resume_water_material_event(water,material,Var{1024});
    require(!source_wait1_context_ready(live_freeze,Var{1},saved_my_is_null) &&
            water.active_processes == 2 && water.skill1.raw == 1536 && water.skill2.raw == 1536 &&
            material.texture_transform[8] == 0.005859375f &&
            material.texture_transform[9] == 0.005859375f,
            "same-frame live freeze write pauses the second receiver; one resume updates once without registering");
}

// Native-only environment convolution and scalar override regression section.
void modern_environment_checks() {
    using c4::rendering::quality::prepare_modern_environment;
    c4::assets::GpuImage authored;
    authored.width = authored.height = 8;
    authored.face_count = 6;
    authored.texels.resize(8 * 8 * 4 * 6);
    authored.levels.push_back({8,8,0,authored.texels.size(),8 * 4,8 * 8 * 4});
    authored.authored_dds = {0x44,0x44,0x53,0x20};
    const std::array<std::uint8_t,4> color{64,128,192,255};
    for (std::size_t i = 0; i < authored.texels.size(); ++i)
        authored.texels[i] = color[i % 4];
    const auto source_bytes = authored.texels;
    const auto retained_authored_bytes = authored.authored_dds;
    const auto environment = prepare_modern_environment(authored);
    require(environment.irradiance.width == 8 && environment.irradiance.height == 8 &&
            environment.irradiance.face_count == 6 && environment.irradiance.mip_levels.empty() &&
            environment.prefiltered.width == 32 && environment.prefiltered.height == 32 &&
            environment.prefiltered.face_count == 6 && environment.prefiltered.mip_levels.size() == 5,
            "native IBL has separate irradiance and complete roughness cube chain");
    std::array<int,4> linear{};
    for (unsigned channel = 0; channel < 3; ++channel) {
        const float s = color[channel] / 255.f;
        linear[channel] = static_cast<int>(std::lround(255.f *
            (s <= .04045f ? s / 12.92f : std::pow((s + .055f) / 1.055f,2.4f))));
    }
    linear[3] = 255;
    for (const auto* bytes : {&environment.irradiance.rgba,&environment.prefiltered.rgba,
                              &environment.prefiltered.mip_rgba})
        for (std::size_t i = 0; i < bytes->size(); ++i)
            require(std::abs(static_cast<int>((*bytes)[i]) - linear[i % 4]) <= 1,
                    "constant authored sRGB cube preserves linear irradiance/pi and every GGX mip");
    std::size_t offset = 0;
    unsigned dimension = 16;
    for (const auto& mip : environment.prefiltered.mip_levels) {
        require(mip.width == dimension && mip.height == dimension && mip.offset == offset,
                "roughness mip offsets retain six tightly packed faces");
        offset += static_cast<std::size_t>(dimension) * dimension * 4 * 6;
        dimension /= 2;
    }
    require(offset == environment.prefiltered.mip_rgba.size() && authored.texels == source_bytes &&
            authored.authored_dds == retained_authored_bytes && authored.width == 8 &&
            authored.height == 8 && authored.face_count == 6 && authored.levels.size() == 1 &&
            authored.levels[0].face_stride == 8 * 8 * 4,
            "convolution owns complete mip bytes without changing authored storage");
    for (std::size_t i = 0; i < authored.texels.size(); i += 4) {
        const auto value = static_cast<std::uint8_t>(i < 8 * 8 * 4 ? 255 : 0);
        authored.texels[i] = authored.texels[i+1] = authored.texels[i+2] = value;
    }
    const auto directional_bytes = authored.texels;
    const auto directional = prepare_modern_environment(authored);
    const auto raw_center = directional.prefiltered.rgba[(16 * 32 + 16) * 4];
    const auto rough_center = directional.prefiltered.mip_rgba[
        directional.prefiltered.mip_levels.back().offset];
    require(raw_center == 255 && rough_center > 0 && rough_center < raw_center &&
            authored.texels == directional_bytes && authored.authored_dds == retained_authored_bytes,
            "directional authored cube genuinely blurs with roughness rather than renaming raw data");
    const auto rejects = [&](const c4::assets::GpuImage& malformed) {
        bool rejected = false;
        try { (void)prepare_modern_environment(malformed); }
        catch (const std::exception&) { rejected = true; }
        require(rejected,"native IBL rejects malformed authored cube");
    };
    auto malformed = authored;
    malformed.face_count = 1; rejects(malformed);
    malformed = authored; malformed.height = 4; rejects(malformed);
    malformed = authored; malformed.width = 0; rejects(malformed);
    malformed = authored; malformed.texels.clear(); rejects(malformed);
    malformed = authored; malformed.levels.clear(); rejects(malformed);
    malformed = authored; malformed.texels.pop_back(); rejects(malformed);
}

void modern_material_override_checks() {
    using namespace c4::rendering;
    NativeMaterialState source;
    source.technique = MaterialTechnique::EnvCubeMix;
    source.specular = {.2f,.3f,.4f,1.f};
    source.power = 32.f;
    source.power_known = true;
    source.color_known_mask = 0x1c0u;
    source.texture_factor = 0;
    const auto original = vulkan_material_data(source,"Mtl_car3");
    require(original.modern[2] >= .35f && original.modern[3] == 1.f,
            "known authored car cube receives explicit native reflection priority");
    std::array<quality::ModernMaterialOverride,1> overrides{{{"Mtl_car3",.7f,.2f,.8f}}};
    const auto changed = vulkan_material_data(source,"Mtl_car3",overrides);
    require(changed.modern == std::array<float,4>{.7f,.2f,.8f,2.f} &&
            changed.config == original.config && changed.diffuse == original.diffuse &&
            changed.specular == original.specular && changed.params == original.params &&
            changed.skills == original.skills && changed.texture_factor == original.texture_factor,
            "exact-name native override changes modern scalars, not original material data");
    const auto unmatched = vulkan_material_data(source,"different",overrides);
    const auto unchanged = vulkan_material_data(source,"different");
    require(unmatched.modern == unchanged.modern && unmatched.extra == unchanged.extra,
            "unmatched material name leaves source-derived output unchanged");
    overrides[0].metallic = std::numeric_limits<float>::quiet_NaN();
    require(vulkan_material_data(source,"Mtl_car3",overrides).modern == original.modern,
            "producer ignores nonfinite native material overrides");
    overrides[0].metallic = .7f; overrides[0].roughness = 1.1f;
    require(vulkan_material_data(source,"Mtl_car3",overrides).modern == original.modern,
            "producer ignores out-of-range native roughness overrides");
    overrides[0].roughness = .2f; overrides[0].reflectivity = -.1f;
    require(vulkan_material_data(source,"Mtl_car3",overrides).modern == original.modern,
            "producer ignores out-of-range native reflectivity overrides");
}

// Consumer geometry, not an authenticated MDL/TGA or a source-game pixel witness.
// Exercise higher-tier canvas fitting, private depth clear and two-sided visibility.
void original_ui_canvas_visibility_checks() {
    using namespace c4::rendering;
    constexpr int width = 64, height = 80;
    constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const std::array<c4::assets::Vertex,4> vertices{{
        {-.75f,-.75f,0,.5f,.5f,0,0,1}, {.75f,-.75f,0,.5f,.5f,0,0,1},
        {.75f,.75f,0,.5f,.5f,0,0,1}, {-.75f,.75f,0,.5f,.5f,0,0,1}
    }};
    constexpr std::array<std::uint32_t,6> ccw{0,1,2,0,2,3}, cw{0,2,1,0,3,2};
    std::array<c4::assets::Image,1> images;
    images[0].width = images[0].height = images[0].face_count = 1;
    images[0].rgba = {32,48,64,255}; // Deliberately dark, colored fill, not white outlines.
    NativeMaterialState material;
    material.entity_skins[0] = 0;
    constexpr std::uint32_t source_flags = 0x08000000u; // Recovered HUD fill ENTITY, no OVERLAY.
    configure_material_state(material,"",source_flags); // Recovered zero-albedo mat_unlit fallback.
    configure_source_bitmap_raster(material,false,false); // Opaque fill skin, no bitmap alpha flag.
    const auto alpha = original_model_alpha_decision(
        {false,1,1,false,false,100,false,false,false,1});
    material.raster = apply_original_model_alpha(material.raster,alpha);
    material.raster.color_key = false;
    material.raster.defaults_recovered = true; // Same recovered opaque fill state as HudMeshes.
    material.source_pass.raster = material.raster;
    material.passes[0].raster = material.raster;
    const auto lighting = source_view_entity_uniforms(material,source_flags,&alpha);
    require(lighting.status[0] == 1 && lighting.options[1] == 1,
            "UI pixel fixture has complete production private-VIEW lighting and alpha");
    c4::ui::OverlayMesh mesh;
    mesh.vertices = vertices;
    mesh.model_to_view = mesh.view_projection = identity;
    mesh.material_state = &material;
    mesh.lighting = &lighting;
    mesh.source_flags = source_flags;
    mesh.source_effects_enabled = true; // Actual private cam_ibre VIEW flags are zero.
    mesh.filtered = false;
    c4::ui::UiFrame frame;
    frame.images = images;
    frame.meshes = {&mesh,1};
    frame.canvas_width = 1024;
    frame.canvas_height = 768;
    const auto fit = c4::ui::Viewport::fit(width,height,frame.canvas_width,frame.canvas_height);
    require(fit.x == 0 && fit.y == 16 && fit.width == 64 && fit.height == 48 &&
            fit.scale == 1.f / 16,
            "higher-tier authored canvas fits once with preserved letterbox");

    const Gl gl;
    c4::InspectionGL production_gl;
    struct Target {
        const Gl& gl;
        c4::InspectionGL& production_gl;
        GLuint framebuffer{}, color{}, depth{};
        ~Target() {
            gl.bind_framebuffer(GL_FRAMEBUFFER,0);
            gl.delete_framebuffers(1,&framebuffer);
            glDeleteTextures(1,&color);
            production_gl.glDeleteRenderbuffersEXT(1,&depth);
        }
    } target{gl,production_gl};
    gl.gen_framebuffers(1,&target.framebuffer);
    gl.bind_framebuffer(GL_FRAMEBUFFER,target.framebuffer);
    production_gl.glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&target.color);
    glBindTexture(GL_TEXTURE_2D,target.color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    gl.framebuffer_texture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.color,0);
    production_gl.glGenRenderbuffersEXT(1,&target.depth);
    production_gl.glBindRenderbufferEXT(GL_RENDERBUFFER,target.depth);
    production_gl.glRenderbufferStorageEXT(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
    production_gl.glFramebufferRenderbufferEXT(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                                               GL_RENDERBUFFER,target.depth);
    require(gl.framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "production UI visible-fill framebuffer incomplete");
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    c4::UiRenderer renderer(production_gl);
    constexpr Pixel clear{.75f,.5f,.25f,1};
    const Pixel fill{32.f/255 * lighting.emissive[0],48.f/255 * lighting.emissive[1],
                     64.f/255 * lighting.emissive[2],1};
        mesh.source_cull_mode = 1; // Source D3DCULL_NONE; no enum/winding oracle is inferred.
        for (const bool counterclockwise : {true,false}) {
            mesh.indices = counterclockwise ? std::span<const std::uint32_t>(ccw) :
                                             std::span<const std::uint32_t>(cw);
            glClearColor(clear[0],clear[1],clear[2],clear[3]);
            glClearDepth(0); // UiRenderer must clear private-view depth before its draw.
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.draw(frame,width,height);
            std::array<Pixel,width * height> pixels{};
            glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
            require(glGetError() == GL_NO_ERROR,"production UI visible-fill OpenGL error");
            const std::string label = counterclockwise ? "UI two-sided CCW" : "UI two-sided CW";
            for (std::size_t channel = 0; channel < 4; ++channel) {
                require(std::abs(pixels[(height/2)*width + width/2][channel] - fill[channel]) < .006f,
                        label + ": actual center pixel retains dark source-colored geometry");
                require(std::abs(pixels[8*width + width/2][channel] - clear[channel]) < .006f,
                        label + ": higher-tier fit preserves actual letterbox pixels");
            }
            std::size_t filled{};
            for (const auto& pixel : pixels)
                if (std::abs(pixel[0] - clear[0]) > .02f) ++filled;
            require(filled >= 1000,
                    label + ": canvas fitting retains a broad visible area");
        }
}

// Raster-only consumer: recovered sprite corners, source entity rotation and
// gameplay projection, not an authenticated resource/source-game pixel witness.
void original_world_sprite_winding_checks() {
    using namespace c4::rendering;
    const Gl gl;
    c4::InspectionGL production_gl;
    struct Target {
        const Gl& gl;
        GLuint framebuffer{}, color{};
        ~Target() {
            gl.bind_framebuffer(GL_FRAMEBUFFER,0);
            gl.delete_framebuffers(1,&framebuffer);
            glDeleteTextures(1,&color);
        }
    } target{gl};
    gl.gen_framebuffers(1,&target.framebuffer);
    gl.bind_framebuffer(GL_FRAMEBUFFER,target.framebuffer);
    production_gl.glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&target.color);
    glBindTexture(GL_TEXTURE_2D,target.color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,nullptr);
    gl.framebuffer_texture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.color,0);
    require(gl.framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "source-projected sprite framebuffer incomplete");
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,1,1);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const auto vertices = c4::ui::original_view_sprite_vertices(4,4);
    constexpr std::array<std::uint32_t,6> fan{0,1,2,0,2,3}, reversed{0,2,1,0,3,2};
    c4::assets::WorldEntity entity;
    entity.position = {4,0,0};
    entity.angles = {180,0,0}; // +X sprite face looks back toward the source camera.
    entity.scale = {1,1,1};
    const auto camera = c4::game::perspective_camera({}, {},90,1,100,1,640,480);
    constexpr Pixel clear{.8f,.6f,.4f,1}, fill{.1f,.2f,.3f,1};
    for (const bool original_fan : {true,false}) {
        RenderScene scene;
        scene.meshes.push_back({vertices,original_fan ? std::span<const std::uint32_t>(fan) :
                                                      std::span<const std::uint32_t>(reversed)});
        Instance instance;
        instance.model_to_world = c4::assets::world_entity_transform(entity);
        instance.kind = InstanceKind::Image; // Neutral color isolates raster winding from lighting.
        instance.diffuse = fill;
        instance.source_sprite = instance.source_sprite_state_known = true;
        configure_material_state(instance.material_state,"mat_sprite",0);
        instance.material_state.source_pass.raster.depth_test = false;
        instance.material_state.source_pass.raster.depth_write = false;
        scene.instances.push_back(instance);
        c4::InspectionScene renderer(scene,production_gl);
        FrameSnapshot frame{.scene=scene,.instances=scene.instances,.camera=camera,
                            .source_effects_enabled=false};
        for (const std::uint32_t mode : {3u,2u,1u}) {
            scene.instances[0].source_cull_mode = mode;
            glClearColor(clear[0],clear[1],clear[2],clear[3]);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.draw(frame);
            Pixel pixel{};
            glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel.data());
            require(glGetError() == GL_NO_ERROR,"source-projected sprite OpenGL error");
            // Source fan on the facing +X side is clockwise after the actual
            // +X/right=-Y/up=+Z camera, so D3DCULL_CCW keeps it.
            const auto& expected = mode == 1 || (mode == 3) == original_fan ? fill : clear;
            for (unsigned channel=0;channel<4;++channel)
                require(std::abs(pixel[channel]-expected[channel]) < .0003f,
                    "source-projected sprite D3DCULL "+std::to_string(mode)+
                    (original_fan ? " original fan pixel" : " reversed fan pixel"));
        }
    }
}

} // namespace

int main() {
    try {
        modern_environment_checks();
        modern_material_override_checks();
        event_checks();
        source_wait1_context_checks();
        source_material_event_protocol_checks();
        source_material_tangent_request_checks();
        source_water_material_action_checks();
        gamma_checks();
        profile_checks();
        builtin_material_constructor_draw_checks();
        state_checks();
        model_alpha_checks();
        effect_raster_alpha_checks();
        no_shader_draw_checks();
        source_bitmap_alpha_checks();
        lighting_cache_checks();
        sun_pose_checks();
        lighting_cache_producer_checks();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SKIP: SDL video unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_Window* window = SDL_CreateWindow("authored material regression",1,1,SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::cerr << "SKIP: OpenGL 2.1 context unavailable: " << SDL_GetError() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        require(SDL_GL_MakeCurrent(window,context),std::string("making context current failed: ") + SDL_GetError());
        shader_checks();
        original_ui_canvas_visibility_checks();
        original_world_sprite_winding_checks();
        std::cout << "authored material GLSL numerical regressions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
