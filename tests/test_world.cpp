#include "World.hpp"
#include "Resources.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t header_size = 156;
constexpr std::size_t objects_at = header_size + 4;
constexpr std::size_t objects_header = 4 + 15 * 8;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = (value >> (8 * i)) & 255;
}
std::uint32_t get32(const Bytes& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(at + i)) << (8 * i);
    return value;
}
void put_float(Bytes& bytes, std::size_t at, float value) {
    put32(bytes, at, std::bit_cast<std::uint32_t>(value));
}
void put_text(Bytes& bytes, std::size_t at, std::size_t width, std::string_view value) {
    require(value.size() < width, "test string exceeds authored record field");
    std::fill_n(bytes.begin() + at, width, 0);
    std::copy(value.begin(), value.end(), bytes.begin() + at);
}
Bytes info_record() {
    Bytes bytes(60);
    put32(bytes, 0, 5);
    put_float(bytes, 16, 123.5f);
    put_float(bytes, 20, -35.25f);
    put32(bytes, 24, 0x12345678);
    put_float(bytes, 28, 7.01f);
    bytes[32] = 128;
    bytes[33] = 2;
    put32(bytes, 36, 0x00112233);
    put32(bytes, 40, 0x00445566);
    for (std::size_t i = 0; i < 4; ++i) put32(bytes, 44 + i * 4, 0x00010203 + i);
    return bytes;
}
Bytes entity_record(bool old) {
    Bytes bytes(old ? 136 : 372);
    put32(bytes, 0, old ? 3 : 7);
    const std::array<float, 9> transform{12.5f, -24.25f, 3.75f, 90.f, -15.f, 30.f, 2.f, 3.f, 4.f};
    for (std::size_t i = 0; i < transform.size(); ++i) put_float(bytes, 4 + i * 4, transform[i]);
    put_text(bytes, 40, old ? 20 : 33, old ? "old placement" : "modern placement");
    put_text(bytes, old ? 60 : 73, old ? 13 : 33, old ? "old.MDL" : "modern.MDL");
    put_text(bytes, old ? 73 : 106, old ? 20 : 33, old ? "old_action" : "modern_action");
    for (std::size_t i = 0; i < (old ? 8U : 20U); ++i)
        put_float(bytes, (old ? 96 : 140) + i * 4, float(i) + 0.5f);
    put32(bytes, old ? 128 : 220, 0x01020304);
    put_float(bytes, old ? 132 : 224, 42.5f);
    if (!old) {
        put_float(bytes, 228, 17.25f);
        put32(bytes, 232, 2);
        put32(bytes, 236, 1);
        put_text(bytes, 240, 33, "authored_material");
        put_text(bytes, 273, 33, "first string");
        put_text(bytes, 306, 33, "second string");
    }
    return bytes;
}
// Authored bytes only: the compiler-7.01 nineteen-list header, an empty texture
// list, and an object list with relative offsets and fixed-size disk records.
Bytes world(std::initializer_list<Bytes> records) {
    Bytes bytes(objects_at + 4 + records.size() * 4);
    std::copy_n("WMB7", 4, bytes.begin());
    put32(bytes, 4 + 2 * 8, header_size);
    put32(bytes, 8 + 2 * 8, 4);
    put32(bytes, objects_header, objects_at);
    put32(bytes, objects_at, static_cast<std::uint32_t>(records.size()));
    std::size_t index = 0;
    for (const auto& record : records) {
        put32(bytes, objects_at + 4 + index++ * 4, static_cast<std::uint32_t>(bytes.size() - objects_at));
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    put32(bytes, objects_header + 4, static_cast<std::uint32_t>(bytes.size() - objects_at));
    return bytes;
}
void rejects(const Bytes& bytes, std::string_view reason) {
    try { (void)c4::assets::parse_world(bytes); }
    catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("malformed world accepted: " + std::string(reason));
}
void source_profiles() {
    using c4::assets::SourceProfile;
    using Axis = std::uint8_t SourceProfile::*;
    constexpr std::array<Axis, 7> axes{
        &SourceProfile::atq, &SourceProfile::mtq, &SourceProfile::stq, &SourceProfile::cgm,
        &SourceProfile::shq, &SourceProfile::ano, &SourceProfile::bloom};
    constexpr std::array<unsigned, 7> counts{3, 3, 4, 4, 3, 2, 2};
    require(SourceProfile{} == c4::assets::original_source_profile &&
            SourceProfile{} == SourceProfile{0, 0, 0, 0, 0, 0, 0},
            "Original source profile is not seven raw zeros");
    require(c4::assets::high_source_profile == SourceProfile{2, 2, 3, 0, 2, 0, 0},
            "explicit prior high inspection profile changed");
    require(c4::assets::original_world_sprite_upload.overlay &&
            c4::assets::original_world_sprite_upload.texture_depth == 16 &&
            c4::assets::original_world_sprite_upload.alpha_depth == 32,
            "ordinary original C4 world first-upload policy changed");
    for (unsigned encoded = 0; encoded < 1728; ++encoded) {
        auto remaining = encoded;
        SourceProfile profile;
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
            profile.*axes[axis] = static_cast<std::uint8_t>(remaining % counts[axis]);
            remaining /= counts[axis];
        }
        require(c4::assets::valid_source_profile(profile), "legal raw source selector combination rejected");
    }
    const auto rejected = [](auto&& operation) {
        try { operation(); }
        catch (const std::runtime_error& error) {
            require(std::string_view(error.what()).find("invalid source profile") != std::string_view::npos,
                    "invalid source profile reached asset IO or produced the wrong diagnostic");
            return;
        }
        throw std::runtime_error("invalid raw source selector accepted by public asset API");
    };
    const std::filesystem::path absent{"missing-profile-test-owner.wmb"};
    const std::array<std::string_view, 1> names{"C_skin_gri_img"};
    const c4::assets::Model model;
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        for (const unsigned value : {counts[axis], 255U}) {
            SourceProfile invalid;
            invalid.*axes[axis] = static_cast<std::uint8_t>(value);
            require(!c4::assets::valid_source_profile(invalid), "invalid raw source axis considered legal");
            rejected([&] { (void)c4::assets::load_world(absent, invalid); });
            rejected([&] { (void)c4::assets::load_world(absent, invalid, {false, 32, 16}); });
            rejected([&] { (void)c4::assets::resolve_resources(absent, names, invalid); });
            rejected([&] { (void)c4::assets::resolve_material_resources(absent, names, invalid); });
            rejected([&] { (void)c4::assets::resolve_model_skins(absent, model, invalid); });
        }
    }
}
void acceptance() {
    // The Source Newton world root fact does not claim external MDL colliders are absent.
    require(!c4::assets::WorldScene{}.source_empty_collision_root_known,
            "manual world scene claimed a validated empty Source Newton world root");
    require(c4::assets::WorldModel{}.material_skins_known_mask == 0 &&
            c4::assets::WorldMaterialResources{}.material_skins_known_mask == 0 &&
            c4::assets::WorldModel{}.material_skin_cube_mask == 0 &&
            c4::assets::WorldMaterialResources{}.material_skin_cube_mask == 0,
            "manual asset binding claimed recovered material slots or cube recipes");
    c4::assets::WorldEntity manual;
    manual.native_prepared_inactive = true;
    require(!manual.source_initial_material_known && manual.source_initial_material_identity.empty() &&
            !manual.source_previous_material_known && manual.source_previous_material_identity.empty(),
            "manual unborn entity claimed proven current or previous material");
    c4::assets::WorldModel unvalidated;
    unvalidated.resource = "manual.MDL";
    unvalidated.material = "mat_model";
    unvalidated.model = std::make_shared<c4::assets::Model>();
    const auto manual_recipe = c4::assets::source_fresh_material_identity(unvalidated);
    require(!manual_recipe.known && manual_recipe.identity.empty(),
            "unvalidated manual model claimed a fresh attachment material");
    require(!c4::assets::source_fresh_material_identity(c4::assets::WorldModel{}).known,
            "empty manual binding claimed a proven fresh attachment recipe");
    const auto bytes = world({info_record(), entity_record(false), entity_record(true)});
    const auto scene = c4::assets::parse_world(bytes);
    require(scene.source_empty_collision_root_known,
            "validated empty-section fixture lost the Source Newton world root fact");
    require(scene.source_profile == c4::assets::original_source_profile,
            "authored parser default silently selected high source assets");
    require(scene.object_count == 3 && scene.entities.size() == 2, "authored world object counts mismatch");
    require(scene.models.empty() && !scene.has_palette, "parsing fabricated geometry or palette");
    require(scene.compiler_version == 7.01f && scene.info_flags == 0x12345678,
            "world compiler/info fields mismatch");
    require(scene.sun_azimuth == 123.5f && scene.sun_elevation == -35.25f &&
            scene.gamma == 128 && scene.lightmap_size_code == 2, "world lighting fields mismatch");
    require(scene.sun_color == 0x00112233 && scene.ambient_color == 0x00445566 &&
            scene.fog_colors == std::array<std::uint32_t, 4>{0x00010203, 0x00010204, 0x00010205, 0x00010206},
            "world color fields mismatch");
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const auto& entity = scene.entities[i];
        require(entity.object_index == i + 1 && entity.old_record == (i == 1), "entity disk identity mismatch");
        require(entity.model_index == c4::assets::no_world_model, "parser resolved an unrequested model");
        require(!entity.source_initial_material_known && entity.source_initial_material_identity.empty() &&
                !entity.source_previous_material_known && entity.source_previous_material_identity.empty(),
                "disk parser guessed current or previous material without resource attachment");
        require(!entity.source_position_words, "disk parser manufactured runtime Q10 position words");
        require(entity.position == std::array<float, 3>{12.5f, -24.25f, 3.75f} &&
                entity.angles == std::array<float, 3>{90.f, -15.f, 30.f} &&
                entity.scale == std::array<float, 3>{2.f, 3.f, 4.f}, "entity disk transform mismatch");
        require(entity.flags == 0x01020304 && entity.ambient == 42.5f, "entity flags/ambient mismatch");
        require(entity.alpha_percent == 50 && !entity.source_ambient && !entity.source_albedo,
                "parser replaced documented alpha default or applied source appearance");
        for (std::size_t skill = 0; skill < 20; ++skill)
            require(entity.skills[skill] == (i == 0 || skill < 8 ? float(skill) + 0.5f : 0.f),
                    "entity skill layout mismatch");
    }
    const auto& modern = scene.entities[0];
    require(modern.name == "modern placement" && modern.resource == "modern.MDL" &&
            modern.action == "modern_action" && modern.material == "authored_material" &&
            modern.string1 == "first string" && modern.string2 == "second string", "modern strings mismatch");
    require(modern.albedo == 17.25f && modern.path == 2 && modern.attached_entity == 1,
            "modern albedo/attachment fields mismatch");
    const auto& old = scene.entities[1];
    require(old.name == "old placement" && old.resource == "old.MDL" && old.action == "old_action" &&
            old.albedo == 50 && old.path == 0 && old.attached_entity == 0 && old.material.empty(),
            "old record layout/defaults mismatch");

    auto extended_old = entity_record(true);
    put_text(extended_old, 60, 33, "authored_long_model.MDL");
    const auto extended = c4::assets::parse_world(world({info_record(), extended_old}));
    require(extended.entities[0].resource == "authored_long_model.MDL" && extended.entities[0].action.empty(),
            "old filename overflow into unused action not preserved");

    auto palette_world = bytes;
    put32(palette_world, 4, static_cast<std::uint32_t>(palette_world.size()));
    put32(palette_world, 8, 784);
    const auto palette_at = palette_world.size();
    palette_world.resize(palette_at + 784);
    for (std::size_t i = 0; i < 768; ++i) palette_world[palette_at + 16 + i] = i % 256;
    const auto palette_scene = c4::assets::parse_world(palette_world);
    require(palette_scene.has_palette, "authored palette missing");
    for (std::size_t i = 0; i < 768; ++i)
        require(palette_scene.palette[i] == i % 256, "palette byte order/boundary mismatch");
}
void malformed() {
    const auto valid = world({info_record(), entity_record(false), entity_record(true)});
    const auto modern_at = objects_at + get32(valid, objects_at + 8);
    const auto old_at = objects_at + get32(valid, objects_at + 12);
    const auto info_at = objects_at + get32(valid, objects_at + 4);
    const auto change32 = [&](std::size_t at, std::uint32_t value, std::string_view reason) {
        auto bytes = valid; put32(bytes, at, value); rejects(bytes, reason);
    };
    for (std::size_t length = 0; length < valid.size(); ++length)
        rejects(Bytes(valid.begin(), valid.begin() + length), "truncated");
    auto bytes = valid; bytes[0] = 'X'; rejects(bytes, "signature");
    change32(objects_header, 0xffffffffU, "truncated");
    change32(objects_header + 4, 0xffffffffU, "truncated");
    change32(objects_header, 0, "inconsistent empty section");
    change32(objects_header + 4, 0, "inconsistent empty section");
    change32(objects_header, header_size - 1, "overlaps header");
    bytes = valid; put32(bytes, 4 + 12 * 8, objects_at + 4); put32(bytes, 8 + 12 * 8, 4);
    rejects(bytes, "overlapping sections");
    bytes = valid;
    bytes.insert(bytes.begin() + header_size, 8, 0); // Later twenty-list dialect/padded header.
    for (std::size_t section = 0; section < 19; ++section) {
        const auto offset_at = 4 + section * 8;
        if (get32(bytes, offset_at + 4) != 0)
            put32(bytes, offset_at, get32(bytes, offset_at) + 8);
    }
    rejects(bytes, "header dialect or padding");
    bytes = valid; put32(bytes, objects_header, 0); put32(bytes, objects_header + 4, 0);
    rejects(bytes, "missing objects section");
    for (std::uint32_t length = 1; length < 4; ++length)
        change32(objects_header + 4, length, "truncated");
    change32(objects_at, 0, "object count");
    change32(objects_at, 0xffffffffU, "object count");
    change32(objects_at, static_cast<std::uint32_t>((valid.size() - objects_at - 4) / 4 + 1), "object count");
    change32(objects_at + 4, 15, "object offset"); // Offset lands in the three-entry table.
    change32(objects_at + 8, get32(valid, objects_at + 4), "object offset"); // Equal adjacent offsets.
    change32(objects_at + 8, get32(valid, objects_at + 4) - 1, "object offset"); // Descending offsets.
    change32(objects_at + 12, 0xffffffffU, "object offset");
    change32(objects_at + 8, get32(valid, objects_at + 8) - 1, "level info");
    change32(objects_at + 12, get32(valid, objects_at + 12) - 1, "entity record boundary");
    bytes = valid; bytes.pop_back(); put32(bytes, objects_header + 4, get32(valid, objects_header + 4) - 1);
    rejects(bytes, "entity record boundary");
    rejects(world({entity_record(true)}), "missing level info");
    rejects(world({info_record(), info_record()}), "duplicate level info");
    change32(modern_at, 99, "unsupported object type");
    for (const auto offset : {232U, 236U})
        change32(modern_at + offset, 4, "attachment index"); // Nonzero and strictly above object count.
    const auto nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for (const auto offset : {4U, 16U, 28U, 140U, 216U, 224U, 228U})
        change32(modern_at + offset, nan, "nonfinite");
    for (const auto offset : {4U, 16U, 28U, 96U, 124U, 132U})
        change32(old_at + offset, nan, "nonfinite");
    change32(info_at + 28, nan, "nonfinite");
    change32(modern_at + 4, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()), "nonfinite");
    for (const auto name : {"", ".", "../car.MDL", "..\\car.MDL", "sub/car.MDL", "sub\\car.MDL", "C:car.MDL"}) {
        bytes = valid; put_text(bytes, modern_at + 73, 33, name); rejects(bytes, "unsafe or empty");
        bytes = valid; put_text(bytes, old_at + 60, 13, name); rejects(bytes, "unsafe or empty");
    }
    bytes = valid; bytes[modern_at + 73] = '\n'; rejects(bytes, "control character");
    bytes = valid; bytes[info_at + 33] = 3; rejects(bytes, "lightmap size");
    change32(header_size, 1, "embedded textures");
    for (const auto section : {1U, 5U, 11U, 17U}) {
        bytes = valid;
        put32(bytes, 4 + section * 8, static_cast<std::uint32_t>(bytes.size()));
        put32(bytes, 8 + section * 8, 4);
        bytes.resize(bytes.size() + 4);
        rejects(bytes, "unsupported nonempty geometry");
    }
    bytes = valid; put32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 8, 783); bytes.resize(bytes.size() + 783);
    rejects(bytes, "palette boundary");
}
c4::assets::WorldScene original_world(const std::filesystem::path& path) {
    const auto bytes = c4::assets::read_resource(path);
    auto scene = c4::assets::parse_world(bytes);
    require(scene.source_empty_collision_root_known,
            "original parsed world lost the validated Source Newton world root fact");
    require(scene.object_count == 1318 && scene.entities.size() == 1317, "original object/entity counts mismatch");
    const auto old_count = std::count_if(scene.entities.begin(), scene.entities.end(),
                                       [](const auto& entity) { return entity.old_record; });
    require(old_count == 47 && scene.entities.size() - old_count == 1270, "original modern/old record counts mismatch");
    require(std::none_of(scene.entities.begin(), scene.entities.end(),
                         [](const auto& entity) { return entity.path != 0; }), "original nonzero path count mismatch");
    require(scene.models.empty(), "parser loaded model/block geometry");
    for (const auto section : {1U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 11U, 13U, 16U, 17U, 18U})
        require(get32(bytes, 8 + section * 8) == 0, "original world unexpectedly contains block/BSP geometry");
    require(scene.compiler_version == 7.01f, "original compiler version mismatch");
    const auto& car = scene.entities.front();
    require(car.object_index == 1 && !car.old_record && car.name == "C4_Arac" &&
            car.resource == "c4_arac_box.MDL" && car.action == "aracgovde", "original car identity/order mismatch");
    require(car.position == std::array<float, 3>{27685.333984375f, -32348.306640625f, -2.7320001125335693f},
            "original car disk spawn coordinates mismatch");
    require(car.angles[0] == 79.98553466796875f && car.scale == std::array<float, 3>{1.f, 1.f, 1.f} &&
            car.flags == 0, "original car pan/scale/flags mismatch");
    require(std::all_of(car.skills.begin(), car.skills.end(), [](float value) { return value == 0; }),
            "original car disk skills mismatch");
    return scene;
}
std::string resource_member(const std::filesystem::path& path) {
    const auto text = path.string();
    const auto separator = text.find("::");
    auto member = separator == std::string::npos ? path.filename().string() : text.substr(separator + 2);
    for (auto& character : member)
        if (character >= 'A' && character <= 'Z') character = char(character - 'A' + 'a');
    return member;
}
std::filesystem::path qualified_resource(const std::filesystem::path& owner,
                                         std::string_view archive, std::string_view member) {
    const auto text = owner.string();
    const auto container = std::filesystem::path(text.substr(0, text.find("::")));
    return (container.parent_path() / archive).string() + "::" + std::string(member);
}
void require_disk_placements(const c4::assets::WorldScene& scene, const c4::assets::WorldScene& disk) {
    require(scene.object_count == disk.object_count && scene.entities.size() == disk.entities.size(),
            "profile asset loading changed disk placement counts");
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const auto& entity = scene.entities[i];
        const auto& original = disk.entities[i];
        require(entity.object_index == original.object_index && entity.position == original.position &&
                entity.angles == original.angles && entity.scale == original.scale && entity.flags == original.flags,
                "profile asset loading changed disk positions, angles, scale or flags");
        require(entity.material == original.material,
                "profile asset loading replaced raw WMB editor material with post-action GPU material");
        require(!original.source_position_words && !entity.source_position_words,
                "profile asset loading manufactured runtime Q10 position words for disk placements");
        require(!original.native_prepared_inactive && !entity.native_prepared_inactive,
                "disk placement was mistaken for a preallocated unborn helper");
    }
}
void initial_material_baselines(const c4::assets::WorldScene& scene,
                                const c4::assets::WorldScene& disk) {
    using namespace c4::assets;
    require(scene.entities.size() == 1317, "initial material baseline did not cover all actual placements");
    std::array<unsigned, 2> missing{};
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const auto& entity = scene.entities[i];
        const auto& raw = disk.entities[i];
        require(!raw.source_initial_material_known && raw.source_initial_material_identity.empty() &&
                !raw.source_previous_material_known && raw.source_previous_material_identity.empty(),
                "parsed raw WMB falsely proved current or previous material");
        require(entity.source_initial_material_known,
                "actual loaded placement lacks its pre-action current material baseline");
        require(entity.source_previous_material_known && entity.source_previous_material_identity.empty(),
                "normal WMB construction/mode3 attachment lost its independent NULL previous material");
        const auto identity = entity.source_initial_material_identity;
        if (raw.material == "Mtl_ENGEL_ORTA" || raw.material == "Mtl_ShaderEditor") {
            ++missing[raw.material == "Mtl_ShaderEditor"];
            require(identity.empty(), "missing editor material lookup preserved a default or its raw label");
        } else {
            require(!identity.empty(), "actual attachment material was replaced by constructor NULL");
            require(identity.data() != entity.material.data() && identity.data() != raw.material.data() &&
                    identity.data() != scene.models.at(entity.model_index).material.data(),
                    "initial material identity borrows a mutable entity or GPU-binding string");
            if (!raw.material.empty())
                require(identity.size() == raw.material.size() &&
                        std::equal(identity.begin(), identity.end(), raw.material.begin(),
                            [](char left, char right) {
                                const auto fold = [](char value) {
                                    return value >= 'A' && value <= 'Z' ? char(value - 'A' + 'a') : value;
                                };
                                return fold(left) == fold(right);
                            }),
                        "initial editor lookup used post-action material instead of canonical authored registration");
        }
        if (entity.object_index == 56)
            require(identity == "Mtl_kapsul_ust2",
                    "water56 initial current material did not preserve its valid editor lookup");
        if (entity.object_index == 608) {
            require(raw.material.empty() && identity == "mat_model",
                    "water608 empty editor name lost its pre-action model attachment material");
            const auto recipe = source_fresh_material_identity(scene.models.at(entity.model_index),
                                                                scene.source_profile);
            require(recipe.known && recipe.identity == "mat_model",
                    "fresh actual MDL recipe used a post-action GPU binding or constructor NULL");
            for (const auto& [flags, expected] :
                 std::array<std::pair<std::uint32_t, std::string_view>, 4>{{
                     {0, "mat_model"}, {0x20000, "mat_unlit"},
                     {0x400000, "mat_metal"}, {0x420000, "mat_unlit"}}}) {
                const auto selected = source_resource_material_identity(
                    scene.models.at(entity.model_index), flags, scene.source_profile);
                require(selected.known && selected.identity == expected,
                        "actual MDL current-flag repair lost UNLIT/METAL/default selector priority");
                WorldModel unknown_origin;
                unknown_origin.resource = scene.models.at(entity.model_index).resource;
                const auto unknown = source_resource_material_identity(
                    unknown_origin, flags, scene.source_profile);
                require(!unknown.known && unknown.identity.empty(),
                        "current flags invented material provenance for an undecoded resource");
            }
            WorldModel unrelated;
            unrelated.model = scene.models.at(entity.model_index).model;
            unrelated.resource = "unrelated-validated-origin.MDL";
            const auto unrelated_recipe = source_fresh_material_identity(unrelated, scene.source_profile);
            require(!unrelated_recipe.known && unrelated_recipe.identity.empty(),
                    "validated decoded model falsely proved an unrelated filename registration set");
            for (const auto flags : {0U, 0x20000U, 0x400000U, 0x420000U}) {
                const auto unknown = source_resource_material_identity(unrelated, flags, scene.source_profile);
                require(!unknown.known && unknown.identity.empty(),
                        "current flags invented material provenance outside the authored filename cohort");
            }
        }
        if (entity.object_index == 471) {
            const auto& binding = scene.models.at(entity.model_index);
            require(raw.resource == "kopru_golge.tga" && raw.material.empty() &&
                    !binding.model && binding.sprite_gpu.has_value() && identity == "mat_sprite",
                    "actual sprite attachment did not retain its pre-action default material");
            const auto recipe = source_fresh_material_identity(binding, scene.source_profile);
            require(recipe.known && recipe.identity == "mat_sprite",
                    "fresh decoded source sprite recipe guessed constructor NULL or a GPU material label");
            for (const auto& [flags, expected] :
                 std::array<std::pair<std::uint32_t, std::string_view>, 4>{{
                     {0, "mat_sprite"}, {0x20000, "mat_unlit"},
                     {0x400000, "mat_sprite"}, {0x420000, "mat_unlit"}}}) {
                const auto selected = source_resource_material_identity(binding, flags, scene.source_profile);
                require(selected.known && selected.identity == expected,
                        "actual sprite current-flag repair lost UNLIT-before-sprite-before-METAL priority");
            }
        }
    }
    require(missing == std::array<unsigned, 2>{1, 1},
            "actual material baseline did not cover both missing editor registrations");

    // Owned placement copies only: do not reallocate the physics-borrowed loaded scene.
    WorldScene owned;
    owned.entities = scene.entities;
    owned.entities.reserve(owned.entities.capacity() + 1);
    for (std::size_t i = 0; i < owned.entities.size(); ++i) {
        auto& entity = owned.entities[i];
        entity.material.assign(256, 'x');
        require(entity.source_initial_material_known &&
                entity.source_initial_material_identity.data() == scene.entities[i].source_initial_material_identity.data() &&
                entity.source_initial_material_identity == scene.entities[i].source_initial_material_identity,
                "canonical material view changed after entity copy, reallocation or raw-name mutation");
    }
}
void profile_aliases(const std::filesystem::path& path) {
    // Exact WDL aliases and canonical archive members, not inferred suffix substitution.
    constexpr std::array<std::string_view, 3> atq_names{
        "C_skin_gri_img", "C_iconcam_model", "C_normal_img"};
    constexpr std::array<std::array<std::string_view, 3>, 3> atq_members{{
        {"C_skin_gri_256.dds", "doseme_camlar_b.MDL", "C_normal_256.dds"},
        {"C_skin_gri_512.dds", "doseme_camlar_b.MDL", "C_normal_512.dds"},
        {"C_skin_gri_1024.dds", "doseme_camlar_c.MDL", "C_normal_1024.dds"}}};
    constexpr std::array<std::string_view, 3> atq_archives{
        "kaynak_atq0.rc4", "kaynak_atq1.rc4", "kaynak_atq2.rc4"};
    constexpr std::array<std::string_view, 3> timing_members{
        "zamanlama_a.MDL", "zamanlama_b.MDL", "zamanlama_c.MDL"};
    constexpr std::array<std::string_view, 3> mtq_archives{
        "kaynak_mtq0.rc4", "kaynak_mtq1.rc4", "kaynak_mtq2.rc4"};
    constexpr std::array<std::string_view, 1> timing_name{"MTM_zamanlama"};
    for (std::uint8_t q = 0; q < 3; ++q) {
        auto profile = c4::assets::original_source_profile;
        profile.atq = q; // Leave every other axis Original.
        const auto resources = c4::assets::resolve_resources(path, atq_names, profile);
        require(resources.size() == atq_names.size(), "ATQ batch result count mismatch");
        for (std::size_t i = 0; i < resources.size(); ++i)
            require(resources[i] == qualified_resource(path, atq_archives[q], atq_members[q][i]),
                    "ATQ alias did not resolve exact selected archive/member");
        const auto body = c4::assets::load_image(resources[0]);
        require(body.width == (256U << q) && body.height == (256U << q),
                "ATQ body alias selected the wrong actual image dimensions");
        profile = c4::assets::original_source_profile;
        profile.mtq = q;
        const auto timing = c4::assets::resolve_resources(path, timing_name, profile);
        require(timing.size() == 1 && timing[0] == qualified_resource(path, mtq_archives[q], timing_members[q]),
                "MTQ timing model did not resolve exact selected archive/member");
    }
    constexpr std::array<std::string_view, 4> shadow_names{
        "SHI_anayol", "SHI_kule", "SHI_kule2", "SHI_parca2"};
    constexpr std::array<std::array<std::string_view, 4>, 4> shadow_members{{
        {"bos_32_32.bmp", "bos_32_32.bmp", "bos_32_32.bmp", "bos_32_32.bmp"},
        {"SH_anayol_0.dds", "SH_kule_0.dds", "SH_kule2_0.dds", "SH_parca2_0.dds"},
        {"SH_anayol_1.dds", "SH_kule_1.dds", "SH_kule2_1.dds", "SH_parca2_0.dds"},
        {"SH_anayol_2.dds", "SH_kule_2.dds", "SH_kule2_2.dds", "SH_parca2_0.dds"}}};
    constexpr std::array<std::string_view, 4> shadow_archives{
        "C4.wrs", "kaynak_stq1.rc4", "kaynak_stq2.rc4", "kaynak_stq3.rc4"};
    for (std::uint8_t q = 0; q < 4; ++q) {
        auto profile = c4::assets::original_source_profile;
        profile.stq = q;
        const auto resources = c4::assets::resolve_resources(path, shadow_names, profile);
        require(resources.size() == shadow_names.size(), "STQ batch result count mismatch");
        for (std::size_t i = 0; i < resources.size(); ++i)
            require(resources[i] == qualified_resource(path, shadow_archives[q], shadow_members[q][i]),
                    "STQ alias or repeated parca2 member escaped the selected archive");
    }
    constexpr std::array<std::string_view, 16> all_shadow_names{
        "SHI_y_kum_yan", "SHI_yol_kum", "SHI_parca2", "SHI_vadi_yan",
        "SHI_vadi", "SHI_cikis", "SHI_ex_yol_kald", "SHI_denekop_ek",
        "SHI_ex_yol", "SHI_anayol", "SHI_direkanayol", "SHI_beton",
        "SHI_kule", "SHI_kule2", "SHI_zgisealt", "SHI_gecis"};
    const auto blank_shadows = c4::assets::resolve_resources(path, all_shadow_names);
    require(blank_shadows.size() == all_shadow_names.size() &&
            std::all_of(blank_shadows.begin(), blank_shadows.end(), [&](const auto& resource) {
                return resource == qualified_resource(path, "C4.wrs", "bos_32_32.bmp");
            }), "Original STQ0 did not resolve every authored shadow alias to the blank bitmap");
    const auto low_body = c4::assets::resolve_resources(path, atq_names);
    require(low_body[0] == qualified_resource(path, "kaynak_atq0.rc4", "C_skin_gri_256.dds"),
            "default batch API silently resolved high ATQ assets");
    const auto high_body = c4::assets::resolve_resources(path, atq_names, c4::assets::high_source_profile);
    require(high_body[0] == qualified_resource(path, "kaynak_atq2.rc4", "C_skin_gri_1024.dds"),
            "explicit high batch API did not select high ATQ assets");
    c4::assets::Model external;
    external.groups.resize(1);
    external.groups[0].skins.resize(2);
    external.groups[0].skins[0].external_texture = "C_skin_gri_img";
    const auto low_skins = c4::assets::resolve_model_skins(path, external);
    const auto high_skins = c4::assets::resolve_model_skins(path, external, c4::assets::high_source_profile);
    require(low_skins.size() == 1 && low_skins[0].size() == 2 &&
            low_skins[0][0] == low_body[0] && low_skins[0][1].empty() &&
            high_skins.size() == 1 && high_skins[0].size() == 2 &&
            high_skins[0][0] == high_body[0] && high_skins[0][1].empty(),
            "profiled model skin batch resolution changed embedded/alias skin semantics");
    const std::array<std::string_view, 1> unselected{"C_skin_gri_1024.dds"};
    bool rejected = false;
    try { (void)c4::assets::resolve_resources(path, unselected); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()).find("missing original resource") != std::string_view::npos,
                "wrong diagnostic for resource in an unselected quality archive");
        rejected = true;
    }
    require(rejected, "Original catalog exposed a resource from an unselected ATQ archive");
}
void fish_particle_assets(const std::filesystem::path& path, const c4::assets::WorldScene& scene,
                          bool particles_enabled) {
    // c4_effects.wdl:1053-1108,1186-1241,1434-1489 return before morphskin when
    // particle_count == 0. init_particle starts at 1, clears for SHQ0 && CGM0,
    // then restores 2 for BLOOM1 || SHQ2 || MTQ2; this is not an all-FX-off switch.
    constexpr std::array<std::string_view, 9> actions{
        "fish_m_sword1a", "fish_m_sword1b", "fish_m_sword1c",
        "fish_m_sword2a", "fish_m_sword2b", "fish_m_sword2c",
        "fish_m_shark2a", "fish_m_shark2b", "fish_m_shark2c"};
    std::array<bool, actions.size()> seen{};
    std::map<std::filesystem::path, std::shared_ptr<const c4::assets::Model>> geometry;
    std::map<std::pair<std::filesystem::path, std::string>, std::uint32_t> bindings;
    for (const auto& entity : scene.entities) {
        const auto found = std::find(actions.begin(), actions.end(), entity.action);
        if (found == actions.end()) continue;
        const auto action_index = std::size_t(found - actions.begin());
        seen[action_index] = true;
        require(entity.model_index < scene.models.size(), "particle-gated fish has no loaded binding");
        const auto& binding = scene.models[entity.model_index];
        const bool sword = action_index < 6;
        require(binding.model && resource_member(binding.resource) ==
                (sword ? "bal_kil_loop.mdl" : "bal_kopek.mdl"),
                "particle-gated fish lost its actual original model");
        require(!binding.morphed_model && binding.morphed_resource.empty() &&
                binding.imported_resource.empty(),
                "particle-gated fish fabricated a source geometry replacement");
        if (particles_enabled) {
            const auto q = std::to_string(scene.source_profile.mtq);
            const auto member = std::string(sword ? "bal_kil_loop_txr" : "bal_kopek_txr") + q + ".dds";
            require(binding.texture_resource ==
                    qualified_resource(path, "kaynak_mtq" + q + ".rc4", member),
                    "enabled fish did not restore its actual MTQ skin alias");
        } else {
            require(binding.texture_resource.empty(),
                    "Original particle-zero fish executed morphskin before its source early return");
        }
        const auto [cached_binding, new_binding] =
            bindings.emplace(std::pair{binding.resource, binding.material}, entity.model_index);
        require(new_binding || cached_binding->second == entity.model_index,
                "identical fish source bindings did not reuse the world binding cache");
        const auto [cached_model, new_model] = geometry.emplace(binding.resource, binding.model);
        require(new_model || cached_model->second == binding.model,
                "fish source bindings did not share original collision/render geometry");
        if (particles_enabled || !new_model) continue;
        const auto original = c4::assets::load_model(binding.resource);
        require(!original.groups.empty() && binding.model->groups.size() == original.groups.size() &&
                binding.skin_resources.size() == original.groups.size(),
                "fish original model/skin group shape changed");
        require(binding.skin_resources ==
                c4::assets::resolve_model_skins(binding.resource, original, scene.source_profile),
                "fish skin paths no longer describe the actual original model skins");
        std::size_t skin_count = 0;
        for (std::size_t group = 0; group < original.groups.size(); ++group) {
            const auto& skins = binding.model->groups[group].skins;
            const auto& originals = original.groups[group].skins;
            require(skins.size() == originals.size() &&
                    binding.skin_resources[group].size() == originals.size(),
                    "fish original model/skin slot shape changed");
            for (std::size_t slot = 0; slot < skins.size(); ++slot) {
                ++skin_count;
                const auto& skin = skins[slot];
                const auto& source = originals[slot];
                require(skin.format == source.format && skin.flags == source.flags &&
                        skin.width == source.width && skin.height == source.height &&
                        skin.name == source.name && skin.external_texture == source.external_texture &&
                        skin.payload == source.payload && skin.material == source.material &&
                        skin.effect == source.effect,
                        "fish skin was synthesized or premorphed into the original cached model");
                const auto& resource = binding.skin_resources[group][slot];
                require(!source.external_texture.empty() || resource.empty(),
                        "embedded original fish skin became a fake external morphskin texture");
            }
        }
        require(skin_count != 0, "fish skin preservation regression checked no actual original skins");
    }
    require(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
            "actual world did not exercise every one of the nine particle-gated fish actions");
    // c4_effects.wdl:1319,1332,1345: shark1 has no particle_count gate.
    const auto shark1 = std::find_if(scene.entities.begin(), scene.entities.end(),
        [](const auto& entity) { return entity.action == "fish_m_shark1a"; });
    require(shark1 != scene.entities.end() && shark1->model_index < scene.models.size(),
            "ungated fish control is missing from the actual world");
    const auto& ungated = scene.models[shark1->model_index];
    const auto q = std::to_string(scene.source_profile.mtq);
    require(ungated.texture_resource ==
            qualified_resource(path, "kaynak_mtq" + q + ".rc4", "bal_kopek_txr" + q + ".dds") &&
            geometry.at(ungated.resource) == ungated.model,
            "particle-zero gate suppressed ungated fish morphskin or duplicated its original geometry");
}
void enabled_particle_assets(const std::filesystem::path& path) {
    // Four independent raw axes, not 1728 full asset loads. The public loader takes
    // an on-disk world; each actual world load is destroyed before the next one.
    constexpr std::array<c4::assets::SourceProfile, 4> profiles{{
        {0, 0, 0, 0, 0, 0, 1}, // BLOOM1 must defeat the otherwise-zero gate.
        {0, 0, 0, 0, 1, 0, 0}, // SHQ1 preserves the initial count of 1.
        {0, 0, 0, 1, 0, 0, 0}, // CGM1 independently preserves it.
        {0, 2, 0, 0, 0, 0, 0}}}; // MTQ2 restores it after the zero branch.
    for (const auto profile : profiles)
        fish_particle_assets(path, c4::assets::load_world(path, profile), true);
}
void world_sprite_assets(const c4::assets::WorldScene& scene) {
    using namespace c4::assets;
    std::set<std::filesystem::path> resources;
    for (const auto& binding : scene.models) {
        if (binding.model) {
            require(!binding.sprite_gpu, "model binding acquired an unrelated sprite GPU image");
            continue;
        }
        resources.insert(binding.resource);
        require(binding.sprite_gpu.has_value(), "world sprite retained only its CPU fallback");
        const auto& gpu = *binding.sprite_gpu;
        const auto& cpu = binding.sprite;
        validate_gpu_image(gpu);
        require(gpu.width != 0 && gpu.height != 0 && gpu.width == cpu.width &&
                gpu.height == cpu.height && gpu.face_count == 1 && cpu.face_count == 1,
                "world sprite GPU dimensions differ from retained CPU geometry image");
        require(!gpu.texels.empty() && gpu.texels.data() != cpu.rgba.data() &&
                gpu.levels.size() == 1 && gpu.levels[0].offset == 0 &&
                gpu.levels[0].byte_size == gpu.texels.size(),
                "actual BMP/TGA sprite lost independently owned base upload storage");
        const auto member = resource_member(binding.resource);
        const auto bytes = read_resource(binding.resource);
        const bool bmp = member.ends_with(".bmp");
        require(bmp || member.ends_with(".tga"), "actual world sprite format census changed");
        require(bytes.size() >= (bmp ? 30U : 18U), "actual sprite source header is truncated");
        const auto depth = bmp ? std::uint16_t(bytes[28] | (std::uint16_t(bytes[29]) << 8))
                               : std::uint16_t(bytes[16]);
        require(gpu.source_format == (bmp ? ImageSourceFormat::Bmp : ImageSourceFormat::Tga) &&
                gpu.source_bits_per_pixel == depth && gpu.source_format == cpu.source_format &&
                gpu.source_bits_per_pixel == cpu.source_bits_per_pixel &&
                gpu.source_alpha_known && gpu.source_alpha_known == cpu.source_alpha_known &&
                gpu.source_alpha_flag == (!bmp && depth == 32) &&
                gpu.source_alpha_flag == cpu.source_alpha_flag &&
                !gpu.implementation_precision_fallback,
                "world sprite upload lost actual resource-header format/depth/alpha provenance");
        // Decoder/packed-key details live in test_assets. This checks the world's
        // resource binding and paired CPU/GPU retention, not original GPU parity.
        const auto original = load_image(binding.resource);
        require(cpu.width == original.width && cpu.height == original.height &&
                cpu.rgba == original.rgba && cpu.mip_rgba == original.mip_rgba &&
                cpu.mip_levels.empty() && original.mip_levels.empty() &&
                cpu.source_format == original.source_format &&
                cpu.source_bits_per_pixel == original.source_bits_per_pixel &&
                cpu.source_alpha_known == original.source_alpha_known &&
                cpu.source_alpha_flag == original.source_alpha_flag,
                "world first-upload conversion quantized or color-keyed original CPU sprite pixels");
        const auto expected = gpu_image_from_image(original, original_world_sprite_upload);
        require(gpu.format == expected.format && gpu.texels == expected.texels &&
                gpu.color_key_applied == expected.color_key_applied &&
                gpu.color_key_applied == !cpu.source_alpha_flag,
                "world sprite did not retain its ordinary original first-upload OVERLAY representation");
    }
    std::size_t sprite_entities = 0;
    for (const auto& entity : scene.entities) {
        require(entity.model_index < scene.models.size(), "world sprite census encountered an invalid binding");
        if (scene.models[entity.model_index].model) continue;
        ++sprite_entities;
        require((entity.flags & 0x8000U) == 0 &&
                (((entity.flags | entity.source_flags_set) & ~entity.source_flags_clear) & 0x8000U) == 0,
                "actual world sprite unexpectedly needs unsupported original texture mode 16");
    }
    require(sprite_entities == 37 && resources.size() == 10,
            "actual world sprite retention checks did not cover all 37 placements and ten resources");
}
void original_assets(const std::filesystem::path& path, const c4::assets::WorldScene& disk) {
    const auto scene = c4::assets::load_world(path);
    require(scene.source_empty_collision_root_known,
            "Original asset loading lost the validated Source Newton world root fact");
    require(scene.source_profile == c4::assets::SourceProfile{0, 0, 0, 0, 0, 0, 0},
            "default world asset load silently selected high source assets");
    require(scene.object_count == 1318 && scene.entities.size() == 1317,
            "loaded Original source asset counts mismatch");
    require_disk_placements(scene, disk);
    initial_material_baselines(scene, disk);
    fish_particle_assets(path, scene, false);
    world_sprite_assets(scene);
    for (const auto& entity : scene.entities) {
        require(entity.model_index < scene.models.size(), "Original entity has invalid binding index");
        if (entity.object_index == 56 || entity.object_index == 608)
            require(scene.models[entity.model_index].material == disk.entities[entity.object_index - 1].material,
                    "Original disabled water initializer replaced authored water material");
    }
    constexpr std::array<std::string_view, 4> names{
        "mat_multi_anadolu", "Mtl_car3", "Mtl_robot_kaporta", "Mtl_gise_reklam"};
    const auto materials = c4::assets::resolve_material_resources(path, names);
    require(materials.size() == names.size() && materials[0].name == names[0] && materials[0].definition_known &&
            materials[0].material_skins_known_mask == 0xf &&
            materials[0].material_skin_cube_mask == 0 &&
            materials[0].effect == qualified_resource(path, "C4.wrs", "multi_texture4a.fx") &&
            materials[0].skins[0] == qualified_resource(path, "kaynak_mtq0.rc4", "multi_texture2a.dds") &&
            materials[0].skins[1] == qualified_resource(path, "kaynak_mtq0.rc4", "multi_texture3a.dds") &&
            materials[0].skins[2] == qualified_resource(path, "kaynak_mtq0.rc4", "multi_texture4a.dds") &&
            materials[0].skins[3].empty(),
            "Original terrain shader/slots were removed or used the high shadow variant");
    for (std::size_t i = 1; i < materials.size(); ++i) {
        require(materials[i].name == names[i] && materials[i].definition_known &&
                materials[i].material_skins_known_mask == 0xf &&
                materials[i].material_skin_cube_mask == 1 &&
                materials[i].effect == qualified_resource(path, "C4.wrs", "EnvCube_Mix.fx") &&
                materials[i].skins[0] == qualified_resource(path, "kaynak_kap.rc4",
                    i == 3 ? "skycube2_128+6.tga" : "skycube0_128+6.tga") &&
                materials[i].skins[1].empty(),
                "SHQ0 lost source capability-gated environment cube or retained a high normal slot");
    }
    constexpr std::array<std::string_view, 1> capsule_name{"Mtl_kapsul_ust2"}, cube_name{"bcube00_bmap"};
    const auto capsule = c4::assets::resolve_material_resources(path, capsule_name);
    const auto cube = c4::assets::resolve_resources(path, cube_name);
    require(capsule.size() == 1 && cube.size() == 1 && capsule[0].definition_known &&
            capsule[0].material_skins_known_mask == 0xf && capsule[0].material_skin_cube_mask == 1 &&
            capsule[0].effect.empty() && capsule[0].skins[0] == cube[0] &&
            capsule[0].skins[1].empty() && capsule[0].skins[2].empty() && capsule[0].skins[3].empty(),
            "Original no-effect capsule lost its authored cube bitmap recipe");
    profile_aliases(path);
}
void high_assets(const std::filesystem::path& path, const c4::assets::WorldScene& disk) {
    const auto scene = c4::assets::load_world(path, c4::assets::high_source_profile);
    require(scene.source_empty_collision_root_known,
            "high asset loading lost the validated Source Newton world root fact");
    require(scene.source_profile == c4::assets::high_source_profile, "explicit high source profile not retained");
    require(scene.object_count == 1318 && scene.entities.size() == 1317, "loaded high asset entity count mismatch");
    require(scene.entities.size() == disk.entities.size(), "asset loading changed entity count");
    require_disk_placements(scene, disk);
    initial_material_baselines(scene, disk);
    world_sprite_assets(scene);
    constexpr std::array<std::string_view, 14> material_names{
        "Mtl_tabela_8_9", "Mtl_33", "Mtl_kapsul1", "Mtl_adres",
        "Mtl_kapsul_ust2", "Mtl_afis_site", "Mtl_SH_anayol", "Mtl_silindir",
        "Mtl_ic_panel", "Mtl_ic_tavan", "Mtl_gise_reklam", "Mtl_kule_tut",
        "Mtl_ENGEL_ORTA", "Mtl_ShaderEditor"};
    constexpr std::array<std::string_view, 12> material_effects{
        "Normal_Optim.fx", "Normal_Optim.fx", "Normal_Optim.fx", "Normal_Optim.fx",
        "Normal_Mix.fx", "CubeNormal_Optim.fx", "Normal_T_Spc_Golge.fx", "Normal_TileOptim.fx",
        "Normal_Optim.fx", "Normal_Optim.fx", "Normal_Optim.fx", "Normal_TileOptim.fx"};
    constexpr std::array<unsigned, 14> populated_slots{0, 0, 0, 1, 1, 3, 3, 1, 1, 1, 1, 1, 0, 0};
    constexpr std::array<unsigned, 14> cube_slots{0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto effects = c4::assets::resolve_resources(
        path, material_effects, c4::assets::high_source_profile);
    const auto materials = c4::assets::resolve_material_resources(
        path, material_names, c4::assets::high_source_profile);
    require(materials.size() == material_names.size(), "high material batch result count mismatch");
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        const bool definition_known = i < material_effects.size();
        require(material.name == material_names[i] && material.definition_known == definition_known &&
                material.material_skins_known_mask == (definition_known ? 0xf : 0),
                "authored material slot provenance confused recovered NULL with an absent definition");
        require(material.material_skin_cube_mask == cube_slots[i],
                "selected MATERIAL bitmap cube recipe was inferred from its effect or retained event");
        if (definition_known)
            require(material.effect == effects[i],
                    "high normal material family lost its original effect");
        else require(material.effect.empty(), "absent material definition invented an effect");
        for (std::size_t slot = 0; slot < material.skins.size(); ++slot)
            require(material.skins[slot].empty() == ((populated_slots[i] & (1U << slot)) == 0),
                    "authored NULL slot invented an image or high material family lost its image");
    }
    // These retained envmap events do not override the selected high MATERIAL bitmap.
    constexpr std::array<std::string_view, 5> event_skin_names{
        "C_icpanel_nor_img", "C_ictavan_nor_img", "MTI_gise_reklam_nor",
        "MTI_halat_nor", "MTI_kule_tut_nor"};
    constexpr std::array<std::size_t, 5> event_material_indices{8, 9, 10, 7, 11};
    const auto event_skins = c4::assets::resolve_resources(
        path, event_skin_names, c4::assets::high_source_profile);
    for (std::size_t i = 0; i < event_skins.size(); ++i)
        require(materials[event_material_indices[i]].skins[0] == event_skins[i],
                "high authored normal bitmap was replaced by a retained envmap event cube");
    struct Appearance { std::string_view action; float alpha; };
    constexpr std::array<Appearance, 6> appearance{{
        {"arac_far_cam", 55}, {"arac_govde_cam", 70}, {"robot_farcam", 55},
        {"robot_camlar", 70}, {"c4_ghost_arac", 0}, {"c4_ghost_robo", 0}}};
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const auto& entity = scene.entities[i];
        require(entity.model_index < scene.models.size(), "loaded entity has invalid model index");
        require(entity.ambient == disk.entities[i].ambient && entity.albedo == disk.entities[i].albedo,
                "source appearance overwrote original disk ambient/albedo");
        for (const auto& [action, alpha] : appearance)
            if (entity.action == action)
                require(entity.alpha_percent == alpha, "source car/robot/ghost alpha mismatch");
        if (entity.action.size() == 9 && entity.action.starts_with("can_agac") &&
            entity.action.back() >= '1' && entity.action.back() <= '9')
            require(entity.source_ambient == -42.f, "tree source ambient literal was not preserved");
    }

    std::map<std::filesystem::path, const c4::assets::Model*> geometry;
    std::set<std::filesystem::path> imported;
    std::size_t shared_references = 0;
    const auto cache_reference = [&](const std::filesystem::path& resource,
                                     const std::shared_ptr<const c4::assets::Model>& model) {
        require(!resource.empty() && bool(model), "loaded model cache contains a missing resource/model");
        const auto [entry, inserted] = geometry.emplace(resource, model.get());
        if (!inserted) {
            require(entry->second == model.get(), "same-resource model geometry was not shared");
            ++shared_references;
        }
    };
    for (const auto& model : scene.models) {
        require(!model.resource.empty(), "loaded world binding has no original resource");
        const bool definition_known = !model.material.empty() &&
            model.material != "Mtl_ENGEL_ORTA" && model.material != "Mtl_ShaderEditor";
        require(model.material_skins_known_mask == (definition_known ? 0xf : 0),
                "loaded world material lost authored slot provenance");
        const auto selected = std::find(material_names.begin(), material_names.end(), model.material);
        if (selected != material_names.end())
            require(model.material_skin_cube_mask ==
                    materials[static_cast<std::size_t>(selected - material_names.begin())].material_skin_cube_mask,
                    "loaded world material lost selected bitmap cube provenance");
        if (model.model) cache_reference(model.resource, model.model);
        else require(!model.sprite.rgba.empty(), "loaded world binding has neither model nor sprite");
        if (!model.morphed_resource.empty()) cache_reference(model.morphed_resource, model.morphed_model);
        if (!model.imported_resource.empty()) imported.insert(model.imported_resource);
    }
    require(shared_references != 0, "original world did not exercise shared geometry cache");
    require(imported.size() == 15, "original unique imported mesh count mismatch");

    const auto road = std::find_if(scene.entities.begin(), scene.entities.end(),
                                   [](const auto& entity) { return entity.action == "SHM_anayol"; });
    require(road != scene.entities.end(), "original road action missing");
    const auto& binding = scene.models[road->model_index];
    const auto& mesh = binding.imported_mesh;
    require(resource_member(binding.imported_resource) == "sh_ay.rc4", "road imported resource mismatch");
    require(mesh.vertices.size() == 956 && mesh.indices.size() == 916 * 3,
            "road imported vertex/triangle counts mismatch");
    require(mesh.secondary_uv.size() == 956 && mesh.tertiary_uv.size() == 956,
            "road imported secondary/tertiary UV counts mismatch");
    require(std::all_of(mesh.indices.begin(), mesh.indices.end(),
                        [&](std::uint32_t index) { return index < mesh.vertices.size(); }),
            "road imported mesh index out of range");
    std::array<float, 3> minimum, maximum;
    minimum.fill(std::numeric_limits<float>::max());
    maximum.fill(std::numeric_limits<float>::lowest());
    for (const auto& vertex : mesh.vertices) {
        const std::array<float, 3> point{vertex.x, vertex.y, vertex.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            require(std::isfinite(point[axis]), "road imported vertex is nonfinite");
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    }
    require(minimum == std::array<float, 3>{-72436.8515625f, -33208.48828125f, -2443.8662109375f} &&
            maximum == std::array<float, 3>{72120.7265625f, -23188.427734375f, 1839.2298583984375f},
            "road imported mesh Acknex-normalized bounds mismatch");
    require(road->material == disk.entities[road->object_index - 1].material &&
            binding.material == "Mtl_SH_anayol",
            "road raw editor material or separate post-action GPU material binding mismatch");
    require(resource_member(binding.texture_resource) == "ana_yol_txr2.dds", "road source texture mismatch");
    require(binding.material_skins_known_mask == 0xf &&
            binding.material_skin_cube_mask == 0 &&
            resource_member(binding.material_skin_resources[0]) == "sh_anayol_2.dds" &&
            resource_member(binding.material_skin_resources[1]) == "ana_yol_nor2.dds" &&
            binding.material_skin_resources[2].empty() && binding.material_skin_resources[3].empty(),
            "road source material skin slots mismatch");
    require(resource_member(binding.effect_resource) == "normal_t_spc_golge.fx", "road source effect mismatch");
}
std::array<float, 3> apply(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
    require(matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 && matrix[15] == 1,
            "world transform is not affine column-major");
    return {
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14]
    };
}
void require_point(const std::array<float, 3>& actual, const std::array<float, 3>& expected,
                   const char* message) {
    for (std::size_t axis = 0; axis < 3; ++axis)
        require(std::isfinite(actual[axis]) && std::abs(actual[axis] - expected[axis]) < 0.00002f, message);
}
void transforms() {
    using c4::assets::world_entity_transform;
    c4::assets::WorldEntity entity;
    entity.scale = {1, 1, 1};
    const auto identity = world_entity_transform(entity);
    require(identity == std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
            "zero-angle unit-scale world transform is not identity");
    for (const auto basis : {std::array<float, 3>{1, 0, 0}, {0, 1, 0}, {0, 0, 1}})
        require_point(apply(identity, basis), basis, "identity world basis changed");
    entity.angles = {90, 0, 0};
    require_point(apply(world_entity_transform(entity), {1, 0, 0}), {0, 1, 0}, "pan does not rotate X toward +Y");
    entity.angles = {0, 90, 0};
    require_point(apply(world_entity_transform(entity), {1, 0, 0}), {0, 0, 1}, "tilt does not rotate X toward +Z");
    entity.angles = {0, 0, 90};
    require_point(apply(world_entity_transform(entity), {0, 1, 0}), {0, 0, 1}, "roll does not rotate Y toward +Z");
    // Vendor vec_rotate example: http://manual.conitec.net/vec_rotate.htm.
    entity.angles = {90, 45, 0};
    require_point(apply(world_entity_transform(entity), {10, 0, 0}), {0, 7.0710678f, 7.0710678f},
                  "combined pan/tilt disagrees with vendor vector example");
    entity.angles = {90, 0, 90};
    entity.scale = {2, 3, 4};
    entity.position = {10, -20, 30};
    require_point(apply(world_entity_transform(entity), {1, 2, 3}), {22, -18, 36},
                  "world transform did not scale, then rotate, then translate");
    entity.angles = {90, 90, 90};
    require_point(apply(world_entity_transform(entity), {1, 2, 3}), {22, -26, 32},
                  "world transform pan/tilt/roll composition order mismatch");

    const auto valid = entity;
    for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()}) {
        for (std::size_t field = 0; field < 3; ++field) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                auto invalid = valid;
                const std::array<std::array<float, 3>*, 3> fields{&invalid.position, &invalid.angles, &invalid.scale};
                (*fields[field])[axis] = value;
                bool rejected = false;
                try { (void)world_entity_transform(invalid); }
                catch (const std::runtime_error& error) {
                    require(std::string_view(error.what()).find("nonfinite entity transform") != std::string_view::npos,
                            "wrong invalid-transform diagnostic");
                    rejected = true;
                }
                require(rejected, "nonfinite world transform accepted");
            }
        }
    }
    // Finite scales cannot overflow a rotation matrix: each coefficient has
    // magnitude <= 1. Verify the boundary rather than invent an invalid case.
    entity.scale.fill(std::numeric_limits<float>::max());
    const auto large = world_entity_transform(entity);
    require(std::all_of(large.begin(), large.end(), [](float value) { return std::isfinite(value); }),
            "finite maximum-scale world transform overflowed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc <= 3 && (argc != 3 || std::string_view(argv[2]) == "--load-assets"),
                "usage: native_world_test [world.wmb or archive::member [--load-assets]]");
        acceptance();
        malformed();
        transforms();
        source_profiles();
        if (argc >= 2) {
            const auto disk = original_world(argv[1]);
            if (argc == 3) {
                high_assets(argv[1], disk);
                original_assets(argv[1], disk);
                enabled_particle_assets(argv[1]);
            }
        }
        std::cout << "world parser checks passed";
        if (argc >= 2) std::cout << " (including original world)";
        if (argc == 3) std::cout << " (including Original and explicit high source assets)";
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
