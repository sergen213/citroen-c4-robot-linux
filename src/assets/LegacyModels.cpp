#include "LegacyModels.hpp"

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
    throw std::runtime_error(std::string("animation MDL4: ") + reason);
}
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
    if (!std::isfinite(value)) fail("non-finite scale or translation");
    return value;
}
struct Cursor {
    Bytes bytes;
    Bytes take(std::size_t size) {
        if (size > bytes.size()) fail("truncated record");
        const auto result = bytes.first(size);
        bytes = bytes.subspan(size);
        return result;
    }
    Bytes records(std::uint32_t count, std::size_t stride) {
        if (count > bytes.size() / stride) fail("record count exceeds file boundary");
        return take(static_cast<std::size_t>(count) * stride);
    }
};
std::string label(Bytes bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    return {reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(end - bytes.begin())};
}
// Format facts: Conitec's MDL3/4 documentation (84-byte header, signed UVs,
// indexed normals); corroborated by MDLFileData.h declarations, not importer code.
// http://www.conitec.net/beta/prog_mdlhmp.html
// Published 162-vector lookup, with its entries 0/76/161 transcription errors
// corrected against acknex.dll's SDK lookup at VA 0x101f2ca8 (SHA256 of table:
// db1cf609aca1c6ea814184177998a887768136e9f082406f11aeec61e08efe9c).
// No GPL normal-table source or original implementation is copied.
constexpr std::array<std::array<float, 3>, 162> normals{{
    {-0.525731f, 0.000000f, 0.850651f},
    {-0.442863f, 0.238856f, 0.864188f},
    {-0.295242f, 0.000000f, 0.955423f},
    {-0.309017f, 0.500000f, 0.809017f},
    {-0.162460f, 0.262866f, 0.951056f},
    {0.000000f, 0.000000f, 1.000000f},
    {0.000000f, 0.850651f, 0.525731f},
    {-0.147621f, 0.716567f, 0.681718f},
    {0.147621f, 0.716567f, 0.681718f},
    {0.000000f, 0.525731f, 0.850651f},
    {0.309017f, 0.500000f, 0.809017f},
    {0.525731f, 0.000000f, 0.850651f},
    {0.295242f, 0.000000f, 0.955423f},
    {0.442863f, 0.238856f, 0.864188f},
    {0.162460f, 0.262866f, 0.951056f},
    {-0.681718f, 0.147621f, 0.716567f},
    {-0.809017f, 0.309017f, 0.500000f},
    {-0.587785f, 0.425325f, 0.688191f},
    {-0.850651f, 0.525731f, 0.000000f},
    {-0.864188f, 0.442863f, 0.238856f},
    {-0.716567f, 0.681718f, 0.147621f},
    {-0.688191f, 0.587785f, 0.425325f},
    {-0.500000f, 0.809017f, 0.309017f},
    {-0.238856f, 0.864188f, 0.442863f},
    {-0.425325f, 0.688191f, 0.587785f},
    {-0.716567f, 0.681718f, -0.147621f},
    {-0.500000f, 0.809017f, -0.309017f},
    {-0.525731f, 0.850651f, 0.000000f},
    {0.000000f, 0.850651f, -0.525731f},
    {-0.238856f, 0.864188f, -0.442863f},
    {0.000000f, 0.955423f, -0.295242f},
    {-0.262866f, 0.951056f, -0.162460f},
    {0.000000f, 1.000000f, 0.000000f},
    {0.000000f, 0.955423f, 0.295242f},
    {-0.262866f, 0.951056f, 0.162460f},
    {0.238856f, 0.864188f, 0.442863f},
    {0.262866f, 0.951056f, 0.162460f},
    {0.500000f, 0.809017f, 0.309017f},
    {0.238856f, 0.864188f, -0.442863f},
    {0.262866f, 0.951056f, -0.162460f},
    {0.500000f, 0.809017f, -0.309017f},
    {0.850651f, 0.525731f, 0.000000f},
    {0.716567f, 0.681718f, 0.147621f},
    {0.716567f, 0.681718f, -0.147621f},
    {0.525731f, 0.850651f, 0.000000f},
    {0.425325f, 0.688191f, 0.587785f},
    {0.864188f, 0.442863f, 0.238856f},
    {0.688191f, 0.587785f, 0.425325f},
    {0.809017f, 0.309017f, 0.500000f},
    {0.681718f, 0.147621f, 0.716567f},
    {0.587785f, 0.425325f, 0.688191f},
    {0.955423f, 0.295242f, 0.000000f},
    {1.000000f, 0.000000f, 0.000000f},
    {0.951056f, 0.162460f, 0.262866f},
    {0.850651f, -0.525731f, 0.000000f},
    {0.955423f, -0.295242f, 0.000000f},
    {0.864188f, -0.442863f, 0.238856f},
    {0.951056f, -0.162460f, 0.262866f},
    {0.809017f, -0.309017f, 0.500000f},
    {0.681718f, -0.147621f, 0.716567f},
    {0.850651f, 0.000000f, 0.525731f},
    {0.864188f, 0.442863f, -0.238856f},
    {0.809017f, 0.309017f, -0.500000f},
    {0.951056f, 0.162460f, -0.262866f},
    {0.525731f, 0.000000f, -0.850651f},
    {0.681718f, 0.147621f, -0.716567f},
    {0.681718f, -0.147621f, -0.716567f},
    {0.850651f, 0.000000f, -0.525731f},
    {0.809017f, -0.309017f, -0.500000f},
    {0.864188f, -0.442863f, -0.238856f},
    {0.951056f, -0.162460f, -0.262866f},
    {0.147621f, 0.716567f, -0.681718f},
    {0.309017f, 0.500000f, -0.809017f},
    {0.425325f, 0.688191f, -0.587785f},
    {0.442863f, 0.238856f, -0.864188f},
    {0.587785f, 0.425325f, -0.688191f},
    {0.688191f, 0.587785f, -0.425325f},
    {-0.147621f, 0.716567f, -0.681718f},
    {-0.309017f, 0.500000f, -0.809017f},
    {0.000000f, 0.525731f, -0.850651f},
    {-0.525731f, 0.000000f, -0.850651f},
    {-0.442863f, 0.238856f, -0.864188f},
    {-0.295242f, 0.000000f, -0.955423f},
    {-0.162460f, 0.262866f, -0.951056f},
    {0.000000f, 0.000000f, -1.000000f},
    {0.295242f, 0.000000f, -0.955423f},
    {0.162460f, 0.262866f, -0.951056f},
    {-0.442863f, -0.238856f, -0.864188f},
    {-0.309017f, -0.500000f, -0.809017f},
    {-0.162460f, -0.262866f, -0.951056f},
    {0.000000f, -0.850651f, -0.525731f},
    {-0.147621f, -0.716567f, -0.681718f},
    {0.147621f, -0.716567f, -0.681718f},
    {0.000000f, -0.525731f, -0.850651f},
    {0.309017f, -0.500000f, -0.809017f},
    {0.442863f, -0.238856f, -0.864188f},
    {0.162460f, -0.262866f, -0.951056f},
    {0.238856f, -0.864188f, -0.442863f},
    {0.500000f, -0.809017f, -0.309017f},
    {0.425325f, -0.688191f, -0.587785f},
    {0.716567f, -0.681718f, -0.147621f},
    {0.688191f, -0.587785f, -0.425325f},
    {0.587785f, -0.425325f, -0.688191f},
    {0.000000f, -0.955423f, -0.295242f},
    {0.000000f, -1.000000f, 0.000000f},
    {0.262866f, -0.951056f, -0.162460f},
    {0.000000f, -0.850651f, 0.525731f},
    {0.000000f, -0.955423f, 0.295242f},
    {0.238856f, -0.864188f, 0.442863f},
    {0.262866f, -0.951056f, 0.162460f},
    {0.500000f, -0.809017f, 0.309017f},
    {0.716567f, -0.681718f, 0.147621f},
    {0.525731f, -0.850651f, 0.000000f},
    {-0.238856f, -0.864188f, -0.442863f},
    {-0.500000f, -0.809017f, -0.309017f},
    {-0.262866f, -0.951056f, -0.162460f},
    {-0.850651f, -0.525731f, 0.000000f},
    {-0.716567f, -0.681718f, -0.147621f},
    {-0.716567f, -0.681718f, 0.147621f},
    {-0.525731f, -0.850651f, 0.000000f},
    {-0.500000f, -0.809017f, 0.309017f},
    {-0.238856f, -0.864188f, 0.442863f},
    {-0.262866f, -0.951056f, 0.162460f},
    {-0.864188f, -0.442863f, 0.238856f},
    {-0.809017f, -0.309017f, 0.500000f},
    {-0.688191f, -0.587785f, 0.425325f},
    {-0.681718f, -0.147621f, 0.716567f},
    {-0.442863f, -0.238856f, 0.864188f},
    {-0.587785f, -0.425325f, 0.688191f},
    {-0.309017f, -0.500000f, 0.809017f},
    {-0.147621f, -0.716567f, 0.681718f},
    {-0.425325f, -0.688191f, 0.587785f},
    {-0.162460f, -0.262866f, 0.951056f},
    {0.442863f, -0.238856f, 0.864188f},
    {0.162460f, -0.262866f, 0.951056f},
    {0.309017f, -0.500000f, 0.809017f},
    {0.147621f, -0.716567f, 0.681718f},
    {0.000000f, -0.525731f, 0.850651f},
    {0.425325f, -0.688191f, 0.587785f},
    {0.587785f, -0.425325f, 0.688191f},
    {0.688191f, -0.587785f, 0.425325f},
    {-0.955423f, 0.295242f, 0.000000f},
    {-0.951056f, 0.162460f, 0.262866f},
    {-1.000000f, 0.000000f, 0.000000f},
    {-0.850651f, 0.000000f, 0.525731f},
    {-0.955423f, -0.295242f, 0.000000f},
    {-0.951056f, -0.162460f, 0.262866f},
    {-0.864188f, 0.442863f, -0.238856f},
    {-0.951056f, 0.162460f, -0.262866f},
    {-0.809017f, 0.309017f, -0.500000f},
    {-0.864188f, -0.442863f, -0.238856f},
    {-0.951056f, -0.162460f, -0.262866f},
    {-0.809017f, -0.309017f, -0.500000f},
    {-0.681718f, 0.147621f, -0.716567f},
    {-0.681718f, -0.147621f, -0.716567f},
    {-0.850651f, 0.000000f, -0.525731f},
    {-0.688191f, 0.587785f, -0.425325f},
    {-0.587785f, 0.425325f, -0.688191f},
    {-0.425325f, 0.688191f, -0.587785f},
    {-0.425325f, -0.688191f, -0.587785f},
    {-0.587785f, -0.425325f, -0.688191f},
    {-0.688191f, -0.587785f, -0.425325f},
}};
Vertex vertex(Bytes record, const std::array<float, 3>& scale,
              const std::array<float, 3>& translation) {
    const auto index = record[6];
    if (index >= normals.size()) fail("normal index is out of range");
    if (record[7] != 0) fail("unsupported packed vertex flags");
    std::array<float, 3> position;
    for (std::size_t axis = 0; axis < position.size(); ++axis) {
        position[axis] = scale[axis] * u16(record, axis * 2) + translation[axis];
        if (!std::isfinite(position[axis])) fail("decoded coordinate overflows");
    }
    const auto& normal = normals[index];
    return {position[0], position[1], position[2], 0, 0, normal[0], normal[1], normal[2]};
}
} // namespace

Model decode_legacy_model(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 256u * 1024u * 1024u) fail("model exceeds resource size limit");
    Cursor file{bytes};
    const auto header = file.take(84);
    if (std::memcmp(header.data(), "MDL4", 4) != 0) fail("unsupported legacy signature");
    if (u32(header, 76) != 0) fail("unsupported legacy model flags");
    std::array<float, 3> scale, translation;
    for (std::size_t axis = 0; axis < scale.size(); ++axis) {
        scale[axis] = number(header, 8 + axis * 4);
        translation[axis] = number(header, 20 + axis * 4);
    }
    const auto skin_count = u32(header, 48);
    const auto width = u32(header, 52);
    const auto height = u32(header, 56);
    const auto vertex_count = u32(header, 60);
    const auto triangle_count = u32(header, 64);
    const auto frame_count = u32(header, 68);
    const auto uv_count = u32(header, 72);
    // All original MDL4 members have one RGB565 skin and word-packed frames.
    // Other legacy dialects need their own original evidence, not guessed decoding.
    if (skin_count != 1) fail("unsupported legacy skin count");
    if (!width || !height || width > 16384 || height > 16384) fail("invalid skin dimensions");
    if (!vertex_count || vertex_count > 65536 || !triangle_count || triangle_count > 262144 ||
        !uv_count || uv_count > 65536 || !frame_count || frame_count > 65536 ||
        static_cast<std::uint64_t>(vertex_count) * frame_count > 1048576) {
        fail("invalid or allocation-capped legacy counts");
    }
    const auto skin_size = static_cast<std::uint64_t>(width) * height * 2;
    const auto minimum = 4 + skin_size + static_cast<std::uint64_t>(uv_count) * 4 +
        static_cast<std::uint64_t>(triangle_count) * 12 +
        static_cast<std::uint64_t>(frame_count) * (36 + static_cast<std::uint64_t>(vertex_count) * 8);
    if (minimum != file.bytes.size()) fail("legacy section sizes do not match file boundary");
    if (u32(file.take(4), 0) != 2) fail("unsupported legacy skin encoding");
    Model result;
    result.groups.emplace_back();
    auto& group = result.groups.back();
    Skin skin;
    skin.format = 2;
    skin.flags = 2;
    skin.width = width;
    skin.height = height;
    const auto pixels = file.take(static_cast<std::size_t>(skin_size));
    skin.payload.assign(pixels.begin(), pixels.end());
    group.skins.push_back(std::move(skin));
    const auto uvs = file.records(uv_count, 4);
    const auto triangles = file.records(triangle_count, 12);
    group.source_vertices.reserve(vertex_count);
    group.frames.reserve(frame_count);
    for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        if (u32(file.take(4), 0) != 2) fail("unsupported legacy frame encoding");
        (void)file.take(16); // Packed bounds: their normal/padding bytes are not vertex data.
        AnimationFrame frame;
        frame.name = label(file.take(16));
        const auto vertices = file.records(vertex_count, 8);
        if (frame_index != 0) frame.edits.reserve(vertex_count);
        for (std::uint32_t index = 0; index < vertex_count; ++index) {
            const auto value = vertex(vertices.subspan(static_cast<std::size_t>(index) * 8, 8),
                                      scale, translation);
            if (frame_index == 0) group.source_vertices.push_back(value);
            else frame.edits.push_back({index, value});
        }
        auto prefix = std::string_view(frame.name);
        while (!prefix.empty() && prefix.back() >= '0' && prefix.back() <= '9') prefix.remove_suffix(1);
        while (!prefix.empty() && prefix.back() == ' ') prefix.remove_suffix(1);
        if (group.clips.empty() || group.clips.back().name != prefix) {
            group.clips.push_back({std::string(prefix), frame_index, 1});
        } else {
            ++group.clips.back().frame_count;
        }
        group.frames.push_back(std::move(frame));
    }
    const auto corners = static_cast<std::size_t>(triangle_count) * 3;
    group.mesh.vertices.reserve(corners);
    // acknex 1004f280:68-108: ordinary named MDL resources keep RESOURCE bit
    // 0x80 clear, so TEXCOORD1 is each split vertex's own UV0 (local_c == 1).
    group.mesh.secondary_uv.reserve(corners);
    group.mesh.indices.reserve(corners);
    group.source_indices.reserve(corners);
    // MDL4 has no per-triangle material field; the sole original skin covers every face.
    group.triangle_materials.assign(triangle_count, 0);
    std::unordered_map<std::uint32_t, std::uint32_t> seams;
    seams.reserve(corners);
    for (std::size_t offset = 0; offset < triangles.size(); offset += 12) {
        const auto triangle = triangles.subspan(offset, 12);
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto source = u16(triangle, corner * 2);
            const auto uv = u16(triangle, 6 + corner * 2);
            if (source >= vertex_count || uv >= uv_count) fail("triangle vertex or UV index is out of range");
            const auto key = (static_cast<std::uint32_t>(source) << 16) | uv;
            const auto [entry, inserted] = seams.emplace(key, static_cast<std::uint32_t>(group.mesh.vertices.size()));
            if (inserted) {
                auto value = group.source_vertices[source];
                // UV records are signed pixel positions, including original wrapped/negative UVs.
                value.u = static_cast<float>(std::bit_cast<std::int16_t>(u16(uvs, uv * 4))) / width;
                value.v = static_cast<float>(std::bit_cast<std::int16_t>(u16(uvs, uv * 4 + 2))) / height;
                group.mesh.vertices.push_back(value);
                group.mesh.secondary_uv.push_back({value.u, value.v});
                group.source_indices.push_back(source);
            }
            group.mesh.indices.push_back(entry->second);
        }
    }
    result.source_no_linked_brightness_known = true;
    return result;
}
} // namespace c4::assets
