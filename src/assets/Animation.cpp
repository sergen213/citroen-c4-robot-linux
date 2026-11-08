#include "Animation.hpp"
#include "LegacyModels.hpp"
#include "Resources.hpp"
#include "../physics/FixedPoint.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace c4::assets {
namespace {
using Bytes = std::span<const std::uint8_t>;

[[noreturn]] void fail(const char* reason) {
    throw std::runtime_error(std::string("animation: ") + reason);
}

// Record offsets, not importer code: Assimp MDLFileData.h, checked against
// work/reports/model-animation-inventory.json (all original archive MDLs).
// Coordinates, explicit float normals and normalized UVs stay native.
std::uint16_t u16(Bytes bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
}
std::uint32_t u32(Bytes bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}
float number(Bytes bytes, std::size_t offset) {
    const auto value = std::bit_cast<float>(u32(bytes, offset));
    if (!std::isfinite(value)) fail("non-finite coordinate, normal, UV or material value");
    return value;
}
struct Cursor {
    Bytes bytes;
    Bytes take(std::size_t size) {
        if (size > bytes.size()) fail("truncated record or invalid section size");
        const auto result = bytes.first(size);
        bytes = bytes.subspan(size);
        return result;
    }
    Bytes records(std::uint32_t count, std::size_t stride) {
        if (count > bytes.size() / stride) fail("record count exceeds section boundary");
        return take(static_cast<std::size_t>(count) * stride);
    }
};
std::string label(Bytes bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::size_t>(end - bytes.begin()));
}
Vertex vertex(Bytes bytes) {
    return {number(bytes, 0), number(bytes, 4), number(bytes, 8), 0, 0,
            number(bytes, 14), number(bytes, 18), number(bytes, 22)};
}
Skin skin(Cursor& group) {
    const auto header = group.take(28);
    Skin result;
    result.flags = header[0];
    result.format = result.flags & 7;
    result.width = u32(header, 4);
    result.height = u32(header, 8);
    result.name = label(header.subspan(12, 16));
    if (result.flags & 0xc0) fail("unsupported skin flags");
    if (result.width > 0x7fffffffU || result.height > 0x7fffffffU) fail("negative skin dimensions");
    const bool mipmapped = (result.flags & 8) != 0;
    if (result.format == 2 && mipmapped) fail("unsupported RGB565 mipmapped skin layout");
    if (result.format == 2 || result.format == 4 || result.format == 5) {
        if (!result.width || !result.height || result.width > 16384 || result.height > 16384) {
            fail("invalid or oversized embedded skin dimensions");
        }
        const std::uint64_t pixels = static_cast<std::uint64_t>(result.width) * result.height;
        const auto mip_pixels = mipmapped ? (pixels >> 2) + (pixels >> 4) + (pixels >> 6) : 0;
        const auto bytes_per_pixel = result.format == 2 ? 2u : result.format - 1u;
        const auto size = (pixels + mip_pixels) * bytes_per_pixel;
        if (size > group.bytes.size()) fail("truncated embedded skin");
        const auto payload = group.take(static_cast<std::size_t>(size));
        result.payload.assign(payload.begin(), payload.end());
    } else if (result.format == 6 && !mipmapped) {
        if (!result.width) fail("empty embedded DDS skin");
        const auto payload = group.take(result.width);
        result.payload.assign(payload.begin(), payload.end());
    } else if (result.format == 7 && !mipmapped) {
        const auto end = std::find(group.bytes.begin(), group.bytes.end(), 0);
        if (end == group.bytes.end() || end == group.bytes.begin()) fail("missing external skin filename");
        const auto payload = group.take(static_cast<std::size_t>(end - group.bytes.begin()) + 1);
        result.external_texture = label(payload);
    } else {
        fail("unsupported skin encoding or material reference");
    }
    if (result.flags & 16) {
        const auto material = group.take(68);
        result.material.reserve(17);
        for (std::size_t offset = 0; offset < material.size(); offset += 4) {
            result.material.push_back(number(material, offset));
        }
    }
    if (result.flags & 32) {
        const auto size = u32(group.take(4), 0);
        if (size > 0x7fffffffU) fail("negative ASCII material length");
        const auto effect = group.take(size);
        result.effect.assign(reinterpret_cast<const char*>(effect.data()), effect.size());
    }
    return result;
}
ModelGroup parse_group(Cursor& file) {
    const auto header = file.take(44);
    if (header[0] != 1) fail("unsupported non-triangle group");
    if (header[1] != 0 || header[2] != 0) fail("unsupported deformers or weighted vertices");
    const auto size = u32(header, 4);
    if (size < 44 || size > 0x7fffffffU) fail("invalid group size");
    Cursor group{file.take(size - 44)};
    const auto skin_count = u32(header, 24);
    const auto uv_count = u32(header, 28);
    const auto triangle_count = u32(header, 32);
    const auto vertex_count = u32(header, 36);
    const auto frame_count = u32(header, 40);
    if (skin_count > 4096 || !uv_count || uv_count > 65536 ||
        !triangle_count || triangle_count > 262144 || !vertex_count || vertex_count > 65536 ||
        frame_count > 65536) fail("invalid or allocation-capped group counts");
    const std::uint64_t minimum = static_cast<std::uint64_t>(skin_count) * 28 +
        static_cast<std::uint64_t>(uv_count) * 8 + static_cast<std::uint64_t>(triangle_count) * 16 +
        static_cast<std::uint64_t>(vertex_count) * 26 + static_cast<std::uint64_t>(frame_count) * 24;
    if (minimum > group.bytes.size()) fail("group counts exceed section boundary");
    ModelGroup result;
    result.name = label(header.subspan(8, 16));
    result.skins.reserve(skin_count);
    for (std::uint32_t i = 0; i < skin_count; ++i) result.skins.push_back(skin(group));
    const auto uvs = group.records(uv_count, 8);
    const auto triangles = group.records(triangle_count, 16);
    const auto vertices = group.records(vertex_count, 26);
    for (std::size_t offset = 0; offset < uvs.size(); offset += 4) (void)number(uvs, offset);
    result.source_vertices.reserve(vertex_count);
    for (std::size_t offset = 0; offset < vertices.size(); offset += 26) {
        const auto record = vertices.subspan(offset, 26);
        if (u16(record, 12) != 0xffff) fail("base vertex references a bone without a bone table");
        result.source_vertices.push_back(vertex(record));
    }
    for (std::uint32_t i = 0; i < frame_count; ++i) {
        const auto frame_header = group.take(24);
        const auto count = u32(frame_header, 16);
        if (count > vertex_count) fail("invalid frame replacement count");
        if (u32(frame_header, 20) != 0) fail("unsupported frame bone transforms");
        if (i == 0) {
            result.frames.reserve(frame_count + (count != 0));
            // acknex 10061070: 266..271 creates "_base"; 350..353 aliases an
            // empty first disk frame, while 535..542 appends an edited one.
            if (count != 0) {
                result.frames.push_back({"_base", {}});
                result.clips.push_back({"_base", 0, 1});
            }
        }
        const auto records = group.records(count, 26);
        AnimationFrame frame;
        frame.name = label(frame_header.first(16));
        frame.edits.reserve(count);
        for (std::size_t offset = 0; offset < records.size(); offset += 26) {
            const auto record = records.subspan(offset, 26);
            const auto index = u16(record, 12);
            if (index >= vertex_count) fail("frame replacement index is out of range");
            frame.edits.push_back({index, vertex(record)});
        }
        std::sort(frame.edits.begin(), frame.edits.end(),
                  [](const VertexEdit& a, const VertexEdit& b) { return a.index < b.index; });
        for (std::size_t j = 1; j < frame.edits.size(); ++j) {
            if (frame.edits[j - 1].index == frame.edits[j].index) fail("duplicate frame replacement index");
        }
        auto prefix = std::string_view(frame.name);
        while (!prefix.empty() && prefix.back() >= '0' && prefix.back() <= '9') prefix.remove_suffix(1);
        if (prefix.size() != frame.name.size()) {
            while (!prefix.empty() && prefix.back() == ' ') prefix.remove_suffix(1);
        }
        if (result.clips.empty() || result.clips.back().name != prefix) {
            result.clips.push_back({std::string(prefix), static_cast<std::uint32_t>(result.frames.size()), 1});
        } else {
            ++result.clips.back().frame_count;
        }
        result.frames.push_back(std::move(frame));
    }
    if (!group.bytes.empty()) fail("group size does not match its records");
    const auto corners = static_cast<std::size_t>(triangle_count) * 3;
    result.mesh.vertices.reserve(corners);
    // acknex 1004f280:68-108: ordinary named MDL resources keep RESOURCE bit
    // 0x80 clear, so TEXCOORD1 is each split vertex's own UV0 (local_c == 1).
    result.mesh.secondary_uv.reserve(corners);
    result.mesh.indices.reserve(corners);
    result.source_indices.reserve(corners);
    result.triangle_materials.reserve(triangle_count);
    std::unordered_map<std::uint32_t, std::uint32_t> seam_indices;
    seam_indices.reserve(corners);
    for (std::size_t offset = 0; offset < triangles.size(); offset += 16) {
        const auto triangle = triangles.subspan(offset, 16);
        const auto material = u32(triangle, 12);
        // Original skinless MDL7 geometry uses -1, not an invented skin zero.
        if (material != 0xffffffffU && material >= skin_count) {
            fail("triangle material index is out of range");
        }
        result.triangle_materials.push_back(material);
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto source = u16(triangle, corner * 2);
            const auto uv = u16(triangle, 6 + corner * 2);
            if (source >= vertex_count || uv >= uv_count) fail("triangle vertex or UV index is out of range");
            const auto key = (static_cast<std::uint32_t>(source) << 16) | uv;
            const auto [entry, inserted] = seam_indices.emplace(key, static_cast<std::uint32_t>(result.mesh.vertices.size()));
            if (inserted) {
                auto value = result.source_vertices[source];
                value.u = number(uvs, static_cast<std::size_t>(uv) * 8);
                value.v = number(uvs, static_cast<std::size_t>(uv) * 8 + 4);
                result.mesh.vertices.push_back(value);
                result.mesh.secondary_uv.push_back({value.u, value.v});
                result.source_indices.push_back(source);
            }
            result.mesh.indices.push_back(entry->second);
        }
    }
    return result;
}
bool frame_matches(std::string_view name, std::string_view requested) {
    requested = requested.substr(0, requested.find('\0'));
    requested = requested.substr(0, 16);
    if (name.size() < requested.size()) return false;
    const auto lower = [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i < requested.size(); ++i) {
        if (lower(static_cast<unsigned char>(name[i])) !=
            lower(static_cast<unsigned char>(requested[i]))) return false;
    }
    return true;
}
const Vertex& frame_vertex(const ModelGroup& group, std::uint32_t frame, std::uint32_t source) {
    const auto& edits = group.frames[frame].edits;
    const auto edit = std::lower_bound(edits.begin(), edits.end(), source,
        [](const VertexEdit& value, std::uint32_t index) { return value.index < index; });
    // A frame is an absolute sparse pose: omitted sources reset to base.
    return edit != edits.end() && edit->index == source ? edit->vertex : group.source_vertices[source];
}
void validate_pose(const ModelGroup& group, AnimationSample sample) {
    if (sample.first_frame >= group.frames.size() || sample.second_frame >= group.frames.size() ||
        !std::isfinite(sample.blend) || sample.blend < 0 || sample.blend > 1) {
        fail("invalid animation pose frame or blend");
    }
}
float position_component(float first, float next, float blend) {
    // EnginePhysics: vec_for_vertex 100501d0, 100502b5..100502fa keeps
    // next-first, FLOAT-blend multiplication and +first in x87 with no
    // intermediate FLOAT stores, then one final FLOAT store. This double
    // recipe implements nominal 53-bit incoming x87 precision, not recovered
    // steady game precision control: CreateDevice flags 0x54 omit FPU_PRESERVE,
    // and incoming control-word policy remains unresolved. Preserve separate
    // multiply/add stages; do not use float std::lerp.
    const double difference = static_cast<double>(next) - static_cast<double>(first);
    const double product = difference * static_cast<double>(blend);
    return static_cast<float>(product + static_cast<double>(first));
}
Vertex pose_vertex(const ModelGroup& group, AnimationSample sample, std::uint32_t source) {
    const auto& first = frame_vertex(group, sample.first_frame, source);
    auto value = first;
    if (sample.blend != 0) {
        const auto& second = frame_vertex(group, sample.second_frame, source);
        value.x = position_component(first.x, second.x, sample.blend);
        value.y = position_component(first.y, second.y, sample.blend);
        value.z = position_component(first.z, second.z, sample.blend);
        value.nx = std::lerp(first.nx, second.nx, sample.blend);
        value.ny = std::lerp(first.ny, second.ny, sample.blend);
        value.nz = std::lerp(first.nz, second.nz, sample.blend);
    }
    return value;
}
} // namespace

Model decode_model(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 256u * 1024u * 1024u) fail("model exceeds resource size limit");
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "MDL4", 4) == 0) {
        return decode_legacy_model(bytes);
    }
    Cursor file{bytes};
    const auto header = file.take(48);
    if (std::memcmp(header.data(), "MDL7", 4) != 0) fail("unsupported model signature");
    constexpr std::array<std::uint16_t, 10> sizes{36, 28, 16, 68, 8, 16, 26, 26, 68, 24};
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (u16(header, 28 + i * 2) != sizes[i]) fail("unsupported MDL7 record size layout");
    }
    if (u32(header, 8) != 0) fail("unsupported bone-driven model");
    const auto groups = u32(header, 12);
    const auto data_size = u32(header, 16);
    const auto entity_size = u32(header, 20);
    const auto editor_size = u32(header, 24);
    if (!groups || groups > 1024 || data_size < 48 || data_size > bytes.size() ||
        entity_size > 0x7fffffffU || editor_size > 0x7fffffffU ||
        static_cast<std::uint64_t>(data_size) + entity_size + editor_size != bytes.size()) {
        fail("invalid data or metadata section sizes");
    }
    Cursor data{file.take(data_size - 48)};
    if (groups > data.bytes.size() / 44) fail("group count exceeds data section");
    Model result;
    result.groups.reserve(groups);
    for (std::uint32_t i = 0; i < groups; ++i) result.groups.push_back(parse_group(data));
    if (!data.bytes.empty()) fail("unexpected bytes outside model groups");
    const auto entity = file.take(entity_size);
    const auto editor = file.take(editor_size);
    result.entity_data.assign(entity.begin(), entity.end());
    result.editor_data.assign(editor.begin(), editor.end());
    result.source_no_linked_brightness_known = true;
    return result;
}
Model load_model(const std::filesystem::path& path) {
    const auto bytes = read_resource(path);
    return decode_model(bytes);
}
AnimationSample animation_sample(const ModelGroup& group, std::string_view clip,
                                 float percentage, bool cycle) {
    if (!std::isfinite(percentage)) fail("non-finite animation percentage");
    const double fixed_input = std::trunc(static_cast<double>(percentage) * 1024.0);
    if (fixed_input < -2147483648.0 || fixed_input > 2147483647.0) {
        fail("animation percentage exceeds original fixed-point range");
    }
    if (group.frames.empty() || group.frames.size() > 65537) fail("invalid animation frame count");
    std::uint32_t first = 0;
    while (first < group.frames.size() && !frame_matches(group.frames[first].name, clip)) ++first;
    if (first == group.frames.size()) fail("animation scene prefix was not found");
    auto last = first;
    while (last + 1 < group.frames.size() && frame_matches(group.frames[last + 1].name, clip)) ++last;
    auto fixed = static_cast<std::int32_t>(fixed_input);
    if (fixed <= 0 || first == last) {
        return {first, std::min(first + 1, last), 0,
                static_cast<std::int32_t>((first + 1) * 1024), 0};
    }
    fixed = cycle ? fixed % 102400 : std::min(fixed, 102400);
    // Shipped acknex.dll 1003d880: implement the nominal 53-bit incoming x87
    // recipe with the observed FLOAT stores at each stage, integer multiply
    // + add before one FLOAT store, then 22.10 frame encoding via cvttsd2si
    // in 101556a0. This does not establish steady game precision control:
    // CreateDevice flags 0x54 omit FPU_PRESERVE; incoming policy is unresolved.
    // Do not replace with P*N/100.
    float coordinate = static_cast<float>(static_cast<double>(fixed) * (1.0 / 1024.0));
    coordinate = static_cast<float>(static_cast<double>(coordinate) * 0.01);
    const auto distance = last - first + (cycle ? 1u : 0u);
    coordinate = static_cast<float>(static_cast<double>(coordinate) * distance + first);
    const bool wrap = cycle && coordinate > static_cast<float>(last);
    coordinate = static_cast<float>(static_cast<double>(coordinate) + 1.0);
    const auto encoded = static_cast<std::uint32_t>(static_cast<double>(coordinate) * 1024.0);
    const auto source = encoded / 1024 - 1;
    if (source < first || source > last) fail("animation frame calculation exceeds scene boundary");
    return {source, wrap ? first : std::min(source + 1, last),
            static_cast<float>(encoded % 1024) / 1024.0f,
            static_cast<std::int32_t>(encoded),
            wrap ? static_cast<std::int32_t>((first + 1) * 1024) : 0};
}
AnimationSample animation_transition(const ModelGroup& group, AnimationSample previous,
                                     std::string_view target_clip, float target_percentage,
                                     float blend_percent) {
    if (previous.first_frame >= group.frames.size() || previous.second_frame >= group.frames.size() ||
        !std::isfinite(previous.blend) || previous.blend < 0 || previous.blend > 1 ||
        !std::isfinite(blend_percent) || blend_percent < 0 || blend_percent > 100) {
        fail("invalid animation transition frame or blend");
    }
    const auto target = animation_sample(group, target_clip, target_percentage, false);
    using physics::Var;
    // robot_fizik.wdl:854-890: round after Q10 fraction quantization,
    // then source line 885 multiplies the Q10 blend by literal .01 (raw 10).
    const auto source = Var::from_float(previous.blend).raw > 512 ?
        previous.second_frame : previous.first_frame;
    const auto blend = Var::from_float(blend_percent) * Var::literal(.01);
    return {source, target.first_frame, blend.to_float(),
            static_cast<std::int32_t>((source + 1) * 1024) + blend.raw,
            static_cast<std::int32_t>((target.first_frame + 1) * 1024)};
}
Vertex sample_source_vertex(const ModelGroup& group, AnimationSample sample, std::uint32_t index) {
    validate_pose(group, sample);
    if (index >= group.source_vertices.size()) fail("source vertex index is out of range");
    return pose_vertex(group, sample, index);
}
bool sample_pose(const ModelGroup& group, AnimationSample sample, std::span<Vertex> output) {
    static_assert(sizeof(Vertex) == 8 * sizeof(float), "Vertex comparison requires no padding");
    if (output.size() != group.mesh.vertices.size()) fail("output span does not match mesh vertex count");
    if (group.source_indices.size() != output.size()) fail("invalid mesh source index mapping");
    validate_pose(group, sample);
    bool changed = false;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto source = group.source_indices[i];
        if (source >= group.source_vertices.size()) fail("mesh source index is out of range");
        auto value = pose_vertex(group, sample, source);
        value.u = group.mesh.vertices[i].u;
        value.v = group.mesh.vertices[i].v;
        changed = changed || std::memcmp(&output[i], &value, sizeof(Vertex)) != 0;
        output[i] = value;
    }
    return changed;
}
void sample_animation(const ModelGroup& group, std::string_view clip, float percentage,
                      bool cycle, std::span<Vertex> output) {
    sample_pose(group, animation_sample(group, clip, percentage, cycle), output);
}
float advance_donus_percentage(float percentage, float normalized_ticks) {
    if (!std::isfinite(percentage) || !std::isfinite(normalized_ticks) || normalized_ticks < 0) {
        fail("invalid donus playback percentage or elapsed ticks");
    }
    using physics::Var;
    const auto current = Var::from_float(percentage);
    const auto ticks = Var::from_float(normalized_ticks);
    if (current.raw > Var::literal(100).raw) return current.to_float();
    // robot_fizik.wdl:4072-4075: sample first; evaluate every source
    // literal, division, multiplication and addition in original Q10.
    const auto divisor = current.raw < Var::literal(86.666).raw ?
        Var::literal(0.7) : Var::literal(0.925);
    return (current + (Var::literal(6.25) / divisor) * ticks).to_float();
}
} // namespace c4::assets
