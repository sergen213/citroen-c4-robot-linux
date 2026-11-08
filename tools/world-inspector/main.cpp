#include "World.hpp"
#include "Resources.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <set>
#include <exception>

namespace {
bool parse_source_options(std::string_view text, c4::assets::SourceProfile& profile) {
    std::array<unsigned int, 7> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto comma = text.find(',');
        if ((i + 1 < values.size()) != (comma != std::string_view::npos)) return false;
        const auto token = text.substr(0, comma);
        if (token.empty() || token.front() < '0' || token.front() > '9') return false;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), values[i]);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size()
            || values[i] > 255) return false;
        if (comma != std::string_view::npos) text.remove_prefix(comma + 1);
    }
    const c4::assets::SourceProfile parsed{
        static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]),
        static_cast<std::uint8_t>(values[2]), static_cast<std::uint8_t>(values[3]),
        static_cast<std::uint8_t>(values[4]), static_cast<std::uint8_t>(values[5]),
        static_cast<std::uint8_t>(values[6])};
    if (!c4::assets::valid_source_profile(parsed)) return false;
    profile = parsed;
    return true;
}
}

int main(int argc, char** argv) {
    try {
        const auto usage = [] {
            std::cerr << "Usage: world_inspector ARCHIVE::bogazici.wmb [--records-only]\n"
                         "       [--source-high | --source-options ATQ,MTQ,STQ,CGM,SHQ,ANO,BLOOM]\n"
                         "Default: Original raw source options 0,0,0,0,0,0,0.\n"
                         "--source-high: prior high inspection asset profile 2,2,3,0,2,0,0 only;\n"
                         "not an observed Original launcher preset. Independent of native GPU/backend.\n"
                         "--source-options: seven unsigned decimal values, maxima 2,2,3,3,2,1,1.\n"
                         "--records-only: parse placements only; selected profile assets are not loaded.\n"
                         "Inspection only: does not execute entity actions or simulate the original race.\n";
            return 2;
        };
        if (argc < 2 || std::string_view(argv[1]).empty()
            || std::string_view(argv[1]).substr(0, 2) == "--") return usage();
        auto profile = c4::assets::original_source_profile;
        bool records_only = false, profile_selected = false;
        for (int i = 2; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--records-only") {
                if (records_only) return usage();
                records_only = true;
            } else if (argument == "--source-high") {
                if (profile_selected) return usage();
                profile = c4::assets::high_source_profile;
                profile_selected = true;
            } else if (argument == "--source-options") {
                if (profile_selected || i + 1 == argc
                    || !parse_source_options(argv[++i], profile)) return usage();
                profile_selected = true;
            } else {
                return usage();
            }
        }
        auto scene = records_only ? c4::assets::parse_world(c4::assets::read_resource(argv[1]))
                                  : c4::assets::load_world(argv[1], profile);
        if (records_only) scene.source_profile = profile;
        std::size_t modern = 0, old = 0, sprites = 0, imported = 0, vertices = 0, triangles = 0;
        for (const auto& entity : scene.entities) entity.old_record ? ++old : ++modern;
        std::set<const c4::assets::Model*> unique_geometry;
        for (const auto& model : scene.models) {
            if (!model.sprite.rgba.empty()) ++sprites;
            if (!model.imported_mesh.vertices.empty()) ++imported;
            if (model.model && unique_geometry.insert(model.model.get()).second)
                for (const auto& group : model.model->groups) {
                    vertices += group.mesh.vertices.size();
                    triangles += group.mesh.indices.size()/3;
                }
        }
        const auto& raw = scene.source_profile;
        std::cout << std::setprecision(10) << "world mode=" << (records_only ? "records-only" : "assets")
                  << " source_options=" << unsigned(raw.atq) << ',' << unsigned(raw.mtq)
                  << ',' << unsigned(raw.stq) << ',' << unsigned(raw.cgm) << ',' << unsigned(raw.shq)
                  << ',' << unsigned(raw.ano) << ',' << unsigned(raw.bloom)
                  << " objects=" << scene.object_count
                  << " entities=" << scene.entities.size() << " modern=" << modern << " old=" << old
                  << " compiler=" << scene.compiler_version << " unique_bindings=" << scene.models.size()
                  << " unique_original_geometry=" << unique_geometry.size()
                  << " original_vertices=" << vertices << " original_triangles=" << triangles
                  << " imported_models=" << imported << " sprite_models=" << sprites
                  << " unresolved_actions=" << scene.unresolved_actions.size()
                  << " unresolved_materials=" << scene.unresolved_materials.size() << '\n';
        for (const auto& entity : scene.entities) {
            std::cout << "entity=" << entity.object_index << " name=" << std::quoted(entity.name)
                      << " resource=" << std::quoted(entity.resource) << " action=" << std::quoted(entity.action)
                      << " position=" << entity.position[0] << ',' << entity.position[1] << ',' << entity.position[2]
                      << " angles=" << entity.angles[0] << ',' << entity.angles[1] << ',' << entity.angles[2]
                      << " scale=" << entity.scale[0] << ',' << entity.scale[1] << ',' << entity.scale[2]
                      << " flags=" << entity.flags << " source_set=" << entity.source_flags_set
                      << " source_clear=" << entity.source_flags_clear << " model=" << entity.model_index << '\n';
        }
        for (std::size_t i = 0; i < scene.models.size(); ++i) {
            const auto& model = scene.models[i];
            std::cout << "model=" << i << " resource=" << std::quoted(model.resource.string())
                      << " import=" << std::quoted(model.imported_resource.string())
                      << " texture=" << std::quoted(model.texture_resource.string())
                      << " material=" << std::quoted(model.material) << '\n';
            std::cout << "effect=" << std::quoted(model.effect_resource.string()) << " material_slots=";
            for (const auto& slot : model.material_skin_resources) std::cout << std::quoted(slot.string()) << ' ';
            std::cout << '\n';
        }
        for (const auto& action : scene.unresolved_actions)
            std::cout << "unexecuted_action=" << std::quoted(action) << '\n';
        for (const auto& material : scene.unresolved_materials)
            std::cout << "unresolved_material=" << std::quoted(material) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world error: " << error.what() << '\n';
        return 1;
    }
}
