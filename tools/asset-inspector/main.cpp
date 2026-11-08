#include "Assets.hpp"
#include "Animation.hpp"
#include "SkinImages.hpp"
#include "Resources.hpp"
#include "RecoveredLogic.hpp"
#include "GpuImage.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <utility>

namespace {

double parse_number(const std::string& text) {
    std::istringstream input(text);
    double value{};
    if (!(input >> std::noskipws >> value) || !input.eof() || !std::isfinite(value))
        throw std::runtime_error("expected a finite numeric value");
    return value;
}

void checkpoint_trace(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open checkpoint trace");
    c4::game::RaceProgress progress;
    std::vector<c4::game::RaceEvent> events;
    events.reserve(10);
    bool finished = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        try {
            std::istringstream record(line);
            std::string eligible_text, side_text, elapsed_text, trailing;
            if (!(record >> eligible_text >> side_text >> elapsed_text) || record >> trailing ||
                (eligible_text != "0" && eligible_text != "1"))
                throw std::runtime_error("expected eligible(0|1) side_sum elapsed_ticks");
            int side{};
            const auto parsed = std::from_chars(side_text.data(), side_text.data() + side_text.size(), side);
            if (parsed.ec != std::errc{} || parsed.ptr != side_text.data() + side_text.size() ||
                side < -4 || side > 4)
                throw std::runtime_error("side_sum must be an integer in [-4,4]");
            const double elapsed = parse_number(elapsed_text);
            if (elapsed < 0) throw std::runtime_error("elapsed_ticks must be nonnegative");
            // The original caller stops observing after finish; still validate the entire file.
            if (finished) continue;
            const auto event = progress.observe(eligible_text == "1", side, elapsed);
            if (event.type != c4::game::RaceEventType::none) events.push_back(event);
            finished = event.type == c4::game::RaceEventType::finish;
        } catch (const std::exception& error) {
            throw std::runtime_error("checkpoint trace line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (!input.eof()) throw std::runtime_error("cannot read checkpoint trace");

    // Buffer only the ten possible events so malformed input never emits partial success.
    std::cout << std::setprecision(17);
    for (const auto& event : events)
        std::cout << "{\"event\":\""
                  << (event.type == c4::game::RaceEventType::finish ? "finish" : "checkpoint")
                  << "\",\"checkpoint\":" << event.checkpoint
                  << ",\"second_lap\":" << (event.second_lap ? "true" : "false")
                  << ",\"elapsed_ticks\":" << event.elapsed_ticks << "}\n";
    std::cout << "{\"event\":\"state\",\"next_gate\":" << progress.next_gate
              << ",\"previous_side\":" << progress.previous_side << ",\"split_ticks\":[";
    for (std::size_t i = 0; i < progress.split_ticks.size(); ++i) {
        if (i) std::cout << ',';
        if (progress.split_ticks[i]) std::cout << *progress.split_ticks[i];
        else std::cout << "null";
    }
    std::cout << "]}\n";
}

const char* gpu_format_name(c4::assets::GpuImageFormat format) {
    using c4::assets::GpuImageFormat;
    switch (format) {
    case GpuImageFormat::Rgba8Unorm: return "rgba8-unorm";
    case GpuImageFormat::Rgb565Unorm: return "rgb565-unorm";
    case GpuImageFormat::Argb1555Unorm: return "argb1555-unorm";
    case GpuImageFormat::Bc1Unorm: return "bc1-unorm";
    case GpuImageFormat::Bc2Unorm: return "bc2-unorm";
    case GpuImageFormat::Bc3Unorm: return "bc3-unorm";
    }
    throw std::runtime_error("unknown GPU image format");
}

std::uint32_t parse_unsigned(const std::string& text) {
    const bool hex=text.starts_with("0x") || text.starts_with("0X");
    const auto* first=text.data()+(hex ? 2 : 0);
    std::uint32_t value{};
    const auto parsed=std::from_chars(first,text.data()+text.size(),value,hex ? 16 : 10);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::runtime_error("expected an unsigned decimal or 0x hexadecimal integer");
    return value;
}

c4::assets::GpuImageUploadOptions runtime_options(int argc, char** argv, bool overlay) {
    c4::assets::GpuImageUploadOptions options{overlay};
    c4::assets::GpuImageRuntimeContext context;
    bool maximum=false, flags=false, mips=false;
    for (int arg=3; arg<argc; arg+=2) {
        if (arg+1==argc) throw std::runtime_error("runtime option requires a value");
        const std::string option=argv[arg];
        const auto value=parse_unsigned(argv[arg+1]);
        if (option=="--max-dimension") { context.maximum_dimension=value; maximum=true; }
        else if (option=="--bitmap-flags") { context.bitmap_flags=value; flags=true; }
        else if (option=="--requested-mips") { context.requested_mip_levels=value; mips=true; }
        else if (option=="--minimum-dimension") context.minimum_dimension=value;
        else if (option=="--automatic-transparency") {
            if (value>1) throw std::runtime_error("automatic transparency must be 0 or 1");
            context.automatic_transparency=value!=0;
        } else if (option=="--palette-is-global") {
            if (value>1) throw std::runtime_error("global palette proof must be 0 or 1");
            context.palette_is_global=value!=0;
        } else if (option=="--texture-depth" || option=="--alpha-depth") {
            if (value==0 || value>255) throw std::runtime_error("texture depth must fit a positive byte");
            if (option=="--texture-depth") options.texture_depth=static_cast<std::uint8_t>(value);
            else options.alpha_depth=static_cast<std::uint8_t>(value);
        } else throw std::runtime_error("unknown runtime option: "+option);
    }
    if (!maximum || !flags || !mips)
        throw std::runtime_error("runtime diagnostics require explicit --max-dimension, --bitmap-flags and --requested-mips");
    options.runtime=context;
    return options;
}

void gpu_image_report(const c4::assets::GpuImage& image) {
    std::cout << "gpu_image format=" << gpu_format_name(image.format)
              << " width=" << image.width << " height=" << image.height
              << " faces=" << image.face_count << " levels=" << image.levels.size()
              << " owned_bytes=" << image.texels.size() << '\n';
    std::cout << "source_format=" << static_cast<unsigned>(image.source_format)
              << " source_bpp=" << unsigned(image.source_bits_per_pixel)
              << " source_alpha_known=" << image.source_alpha_known
              << " source_alpha_flag=" << image.source_alpha_flag
              << " color_key_applied=" << image.color_key_applied
              << " implementation_precision_fallback=" << image.implementation_precision_fallback
              << '\n';
    if (image.runtime) {
        const auto& runtime=*image.runtime;
        std::cout << "runtime authored_width=" << runtime.source_width
                  << " authored_height=" << runtime.source_height
                  << " nominal_width=" << runtime.nominal_width
                  << " nominal_height=" << runtime.nominal_height
                  << " physical_width=" << image.width << " physical_height=" << image.height
                  << " written_width=" << runtime.written_width
                  << " written_height=" << runtime.written_height << '\n';
        std::cout << "authored_levels=" << runtime.authored_mip_levels
                  << " preserved_levels=" << runtime.preserved_mip_levels
                  << " generated_levels=" << runtime.generated_mip_levels
                  << " generated_suffix_source_lod=";
        if (runtime.generated_mip_levels)
            std::cout << (runtime.encoded_postfilter_attempted ? 0 : runtime.preserved_mip_levels-1);
        else std::cout << "none";
        std::cout << " resize_filter=" << runtime.resize_filter
                  << " mip_filter=" << runtime.mip_filter
                  << " mip_source_reuse="
                  << (runtime.mip_filter==0x80004 ? "fixed_initial_src_level" : "previous_generated_lod")
                  << " original_padding_known=" << runtime.original_padding_known
                  << " encoded_postfilter_attempted=" << runtime.encoded_postfilter_attempted
                  << " postfilter_output_known=" << runtime.postfilter_output_known
                  << " authored_dds_bytes=" << image.authored_dds.size() << '\n';
        std::cout << "runtime_provenance explicit_caller_context=1"
                     " original_outside_write_rectangle="
                  << (runtime.original_padding_known ? "known" : "unknown_native_zero_approximation")
                  << " original_filter_precision="
                  << (image.implementation_precision_fallback ? "unproved_model" : "exact_supported_domain")
                  << '\n';
    }
    if (image.levels.empty()) throw std::runtime_error("GPU image has no validated levels");
    for (std::size_t lod=0; lod<image.levels.size(); ++lod) {
        const auto& level=image.levels[lod];
        if (level.offset>image.texels.size() ||
            level.byte_size>image.texels.size()-level.offset)
            throw std::runtime_error("GPU level lies outside owned bytes");
        std::cout << "lod=" << lod << " width=" << level.width << " height=" << level.height
                  << " offset=" << level.offset << " byte_size=" << level.byte_size
                  << " row_pitch=" << level.row_pitch << " face_stride=" << level.face_stride;
        if (image.runtime) {
            std::cout << " end_offset=" << level.offset+level.byte_size;
            if (image.color_key_applied && lod<image.runtime->preserved_mip_levels)
                std::cout << " authored_key_width=" << (image.runtime->source_width>>lod)
                          << " authored_key_height=" << (image.runtime->source_height>>lod);
        }
        std::cout << " first_hex=";
        for (std::size_t byte=0; byte<std::min<std::size_t>(level.byte_size,16); ++byte)
            std::cout << std::hex << std::setfill('0') << std::setw(2)
                      << unsigned(image.texels.at(level.offset+byte));
        std::cout << std::dec << '\n';
    }
    if (image.face_count==6) {
        constexpr std::array<const char*,6> names{"+X","-X","+Y","-Y","+Z","-Z"};
        const auto& level=image.levels.front();
        for (std::size_t face=0; face<names.size(); ++face) {
            std::cout << "face=" << names[face] << " first_hex=";
            for (std::size_t byte=0; byte<std::min<std::size_t>(level.face_stride,16); ++byte)
                std::cout << std::hex << std::setfill('0') << std::setw(2)
                          << unsigned(image.texels.at(level.offset+face*level.face_stride+byte));
            std::cout << std::dec << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool runtime_mode=argc>1 && std::string(argv[1]).starts_with("gpu-runtime-");
        if (argc<3 || (!runtime_mode && argc!=3)) {
            std::cerr << "Usage: asset_inspector list|image|cubemap|skin|mesh|resource PATH\n"
                         "       skin decodes the model's first group/first embedded skin (no external/palette fallback).\n"
                         "       asset_inspector gpu-image|gpu-overlay|gpu-cubemap|gpu-skin PATH\n"
                         "       GPU modes report actual owned upload bytes/ranges without diagnostic RGBA decoding.\n"
                         "       gpu-overlay uses first-upload overlay role; defaults texture_depth=16 alpha_depth=32.\n"
                         "       gpu-skin selects the first embedded model skin, with no external/palette fallback.\n"
                         "       asset_inspector gpu-runtime-image|gpu-runtime-overlay|gpu-runtime-cubemap|gpu-runtime-skin PATH\n"
                         "         --max-dimension M --bitmap-flags F --requested-mips N\n"
                         "         [--minimum-dimension N] [--texture-depth N] [--alpha-depth N]\n"
                         "         [--automatic-transparency 0|1] (default1; 0 selects fixed API black key)\n"
                         "         [--palette-is-global 0|1] (default0; 1 requires caller-verified global palette identity)\n"
                         "       Runtime values are explicit caller state, not inferred from filenames/startup caps.\n"
                         "       Integers accept decimal or 0x hex; M must be positive. Minimum defaults to recovered 8.\n"
                         "       Runtime models report unknown original padding/precision; unsupported derivation domains reject.\n"
                         "       asset_inspector steering-percent VALUE\n"
                         "       asset_inspector checkpoint-trace PATH\n"
                         "Resource PATH can be archive::member; mesh reports static original base geometry.\n"
                         "Steering VALUE is finite signed percent [-100,100]; positive=left, negative=right.\n"
                         "Trace lines: eligible(0|1) side_sum(integer [-4,4]) elapsed_ticks(finite >=0).\n"
                         "Each trace line must contain exactly three fields; blank lines/comments are invalid.\n"
                         "Trace inputs are original world-vertex sign sums and normalized ticks (seconds=ticks/16).\n"
                         "Outputs JSON-lines accepted events and final state/splits; unseen splits are null.\n"
                         "After finish, remaining records are validated but not observed (original caller lifecycle).\n"
                         "Recovered diagnostics only: no world geometry, clock, or game simulation.\n";
            return 2;
        }
        const std::string mode = argv[1];
        const std::filesystem::path path = argv[2];
        const std::string gpu_mode=runtime_mode ? "gpu-"+mode.substr(12) : mode;
        if (mode == "list") {
            const auto entries = c4::assets::list_resources(path);
            std::cout << "WRS3 entries=" << entries.size() << '\n';
            for (const auto& e : entries)
                std::cout << e.name << " packed=" << e.packed_size << " raw=" << e.size
                          << " offset=" << e.offset << '\n';
        } else if (gpu_mode=="gpu-image" || gpu_mode=="gpu-overlay" ||
                   gpu_mode=="gpu-cubemap" || gpu_mode=="gpu-skin") {
            const auto options=runtime_mode ? runtime_options(argc,argv,gpu_mode=="gpu-overlay")
                                           : c4::assets::GpuImageUploadOptions{gpu_mode=="gpu-overlay"};
            if (options.runtime)
                std::cout << "runtime_context bitmap_flags=" << options.runtime->bitmap_flags
                          << " minimum_dimension=" << options.runtime->minimum_dimension
                          << " maximum_dimension=" << options.runtime->maximum_dimension
                          << " requested_mips=" << options.runtime->requested_mip_levels
                          << " automatic_transparency=" << options.runtime->automatic_transparency
                          << " palette_is_global=" << options.runtime->palette_is_global << '\n';
            const auto image=[&] {
                if (gpu_mode=="gpu-skin") {
                    auto model=c4::assets::load_model(path);
                    if (model.groups.empty() || model.groups.front().skins.empty())
                        throw std::runtime_error("model has no first skin");
                    return c4::assets::gpu_image_from_skin(std::move(model.groups.front().skins.front()),options);
                }
                auto image=c4::assets::load_gpu_image(path,options);
                if (gpu_mode=="gpu-cubemap")
                    return c4::assets::gpu_cubemap_from_image(std::move(image));
                return image;
            }();
            gpu_image_report(image);
        } else if (mode == "image" || mode == "cubemap" || mode == "skin") {
            const auto image = [&] {
                if (mode == "image") return c4::assets::load_image(path);
                if (mode == "cubemap") return c4::assets::load_cubemap(path);
                const auto model = c4::assets::load_model(path);
                if (model.groups.empty() || model.groups.front().skins.empty())
                    throw std::runtime_error("model has no first skin");
                return c4::assets::decode_skin_image(model.groups.front().skins.front());
            }();
            std::cout << "image width=" << image.width << " height=" << image.height
                      << " rgba_bytes=" << image.rgba.size() << " origin=top-left\n";
            if (image.rgba.size() < 4) throw std::runtime_error("empty image");
            std::cout << "top_left_rgba=" << unsigned(image.rgba[0]) << ','
                      << unsigned(image.rgba[1]) << ',' << unsigned(image.rgba[2]) << ','
                      << unsigned(image.rgba[3]) << '\n';
            std::cout << "faces=" << image.face_count << " source_format="
                      << static_cast<unsigned>(image.source_format) << " source_bpp="
                      << unsigned(image.source_bits_per_pixel) << '\n';
            std::cout << "mip_levels=" << 1 + image.mip_levels.size()
                      << " lower_rgba_bytes=" << image.mip_rgba.size() << '\n';
            for (std::size_t level = 0; level < image.mip_levels.size(); ++level) {
                const auto& mip = image.mip_levels[level];
                std::cout << "lod=" << level+1 << " width=" << mip.width
                          << " height=" << mip.height << " offset=" << mip.offset
                          << " top_left_rgba=" << unsigned(image.mip_rgba.at(mip.offset)) << ','
                          << unsigned(image.mip_rgba.at(mip.offset+1)) << ','
                          << unsigned(image.mip_rgba.at(mip.offset+2)) << ','
                          << unsigned(image.mip_rgba.at(mip.offset+3)) << '\n';
            }
            if (image.face_count == 6) {
                constexpr std::array<const char*, 6> names{"+X","-X","+Y","-Y","+Z","-Z"};
                const auto face_bytes = std::size_t(image.width) * image.height * 4;
                for (std::size_t face = 0; face < names.size(); ++face) {
                    const auto offset = face * face_bytes;
                    std::cout << "face=" << names[face] << " top_left_rgba="
                              << unsigned(image.rgba.at(offset)) << ','
                              << unsigned(image.rgba.at(offset+1)) << ','
                              << unsigned(image.rgba.at(offset+2)) << ','
                              << unsigned(image.rgba.at(offset+3)) << '\n';
                }
            }
        } else if (mode == "mesh") {
            const auto mesh = c4::assets::load_mesh(path);
            if (mesh.vertices.empty()) throw std::runtime_error("empty geometry");
            const auto& first = mesh.vertices.front();
            std::array<float, 3> lo{first.x, first.y, first.z}, hi = lo;
            for (const auto& v : mesh.vertices) {
                const std::array<float, 3> p{v.x, v.y, v.z};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    lo[axis] = std::min(lo[axis], p[axis]);
                    hi[axis] = std::max(hi[axis], p[axis]);
                }
            }
            std::cout << std::setprecision(9) << "mesh vertices=" << mesh.vertices.size()
                      << " indices=" << mesh.indices.size() << " triangles=" << mesh.indices.size()/3
                      << " pose=base-static\nmin=" << lo[0] << ',' << lo[1] << ',' << lo[2]
                      << "\nmax=" << hi[0] << ',' << hi[1] << ',' << hi[2] << '\n';
        } else if (mode == "resource") {
            const auto bytes = c4::assets::read_resource(path);
            std::cout << "resource bytes=" << bytes.size() << " prefix_hex=";
            for (std::size_t i = 0; i < std::min<std::size_t>(bytes.size(), 16); ++i)
                std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(bytes[i]);
            std::cout << '\n';
        } else if (mode == "steering-percent") {
            const double percent = parse_number(argv[2]);
            const double transition = c4::physics::number(c4::game::transition_percent(c4::physics::v(percent)));
            std::cout << std::setprecision(17) << "{\"signed_percent\":" << percent
                      << ",\"transition_percent\":" << transition << "}\n";
        } else if (mode == "checkpoint-trace") {
            checkpoint_trace(path);
        } else throw std::runtime_error("unknown diagnostic mode");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "asset error: " << e.what() << '\n';
        return 1;
    }
}
