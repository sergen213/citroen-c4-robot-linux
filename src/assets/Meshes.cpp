#include "Assets.hpp"
#include "Animation.hpp"
#include "Resources.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <limits>
#include <utility>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#endif

namespace c4::assets {
namespace {
using Bytes = std::span<const std::uint8_t>;

[[noreturn]] void fail(const char* reason) {
    throw std::runtime_error(std::string("mesh: ") + reason);
}

using Vector3 = std::array<float, 3>;
bool finite(const Vector3& value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
float dot(const Vector3& a, const Vector3& b) {
    const float x = a[0] * b[0], y = a[1] * b[1], z = a[2] * b[2];
    const float xy = x + y;
    return xy + z;
}
Vector3 normalize_source(const Vector3& value) {
    if (!finite(value)) fail("non-finite tangent arithmetic");
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    // Captured D3DX SSE2 body: ordered MULSS/ADDSS, inclusive 2^-46,
    // RSQRTSS and (3-((r*norm2)*r))*(r*.5), with no near-unit shortcut.
    const __m128 x = _mm_set_ss(value[0]), y = _mm_set_ss(value[1]), z = _mm_set_ss(value[2]);
    const __m128 norm2 = _mm_add_ss(_mm_add_ss(_mm_mul_ss(x,x), _mm_mul_ss(y,y)), _mm_mul_ss(z,z));
    if (!(_mm_cvtss_f32(norm2) >= 0x1p-46f)) return {0,0,0};
    const __m128 r = _mm_rsqrt_ss(norm2);
    const __m128 correction = _mm_sub_ss(_mm_set_ss(3), _mm_mul_ss(_mm_mul_ss(r,norm2),r));
    const __m128 scale = _mm_mul_ss(correction,_mm_mul_ss(r,_mm_set_ss(.5f)));
    const Vector3 result{_mm_cvtss_f32(_mm_mul_ss(x,scale)),
                         _mm_cvtss_f32(_mm_mul_ss(y,scale)),
                         _mm_cvtss_f32(_mm_mul_ss(z,scale))};
    if (!finite(result)) fail("non-finite normalized tangent");
    return result;
#else
    fail("captured SSE2 tangent normalization is unavailable on this architecture");
#endif
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
    const float value = std::bit_cast<float>(u32(bytes, offset));
    if (!std::isfinite(value)) fail("non-finite coordinate, normal, or material value");
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


Mesh load_bmesh(Bytes bytes) {
    // Recovered original importer: Ghidra c4_3.dll 10001440/10001200.
    // D3DXCreateMeshFVF uses 0x200312: XYZ, NORMAL, TEX3; TEX2 is float4.
    // https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dfvf
    // These are D3D local mesh coordinates, not recovered game-world axes.
    Cursor file{bytes.subspan(5)};
    const auto vertex_count = u32(file.take(4), 0);
    if (!vertex_count || vertex_count > 65536) fail("invalid or capped bmesh vertex count");
    const auto vertices = file.records(vertex_count, 56);
    const auto triangle_count = u32(file.take(4), 0);
    if (!triangle_count || triangle_count > 262144) fail("invalid or capped bmesh triangle count");
    const auto triangles = file.records(triangle_count, 6);
    const auto attribute_count = u32(file.take(4), 0);
    if (attribute_count != triangle_count) fail("bmesh attribute count differs from triangle count");
    const auto attributes = file.records(attribute_count, 4);
    if (!file.bytes.empty()) fail("unexpected trailing bmesh data");
    for (std::size_t offset = 0; offset < attributes.size(); offset += 4) {
        if (u32(attributes, offset) != 0) fail("unsupported bmesh material attribute; only material zero is evidenced");
    }

    Mesh mesh;
    mesh.tangent_provenance = TangentProvenance::ImportedSource;
    mesh.vertices.reserve(vertex_count);
    mesh.secondary_uv.reserve(vertex_count);
    mesh.tertiary_uv.reserve(vertex_count);
    mesh.indices.reserve(static_cast<std::size_t>(triangle_count) * 3);
    for (std::size_t offset = 0; offset < vertices.size(); offset += 56) {
        const auto vertex = vertices.subspan(offset, 56);
        // Validate all fourteen floats, including normal and unused texture sets.
        std::array<float, 14> fields;
        for (std::size_t field = 0; field < fields.size(); ++field)
            fields[field] = number(vertex, field * 4);
        // uv0 can tile outside [0,1]; preserve it unchanged for the native sampler.
        mesh.vertices.push_back({fields[0], fields[1], fields[2], fields[6], fields[7],
                                 fields[3], fields[4], fields[5]});
        mesh.secondary_uv.push_back({fields[8], fields[9]});
        mesh.tertiary_uv.push_back({fields[10], fields[11], fields[12], fields[13]});
    }
    for (std::size_t offset = 0; offset < triangles.size(); offset += 2) {
        const auto index = u16(triangles, offset);
        if (index >= vertex_count) fail("bmesh triangle index is out of range");
        mesh.indices.push_back(index);
    }
    return mesh;
}
} // namespace

void generate_source_tangents(std::span<const Vertex> vertices,
                              std::span<const std::uint32_t> original_indices,
                              std::vector<std::array<float, 4>>& output) {
    if (original_indices.size() % 3) fail("tangent indices are not triangle triples");
    for (const auto& v : vertices)
        if (!finite({v.x,v.y,v.z}) || !finite({v.nx,v.ny,v.nz}) ||
            !std::isfinite(v.u) || !std::isfinite(v.v))
            fail("non-finite tangent input");
    for (const auto index : original_indices)
        if (index >= vertices.size()) fail("tangent index is out of range");
    output.resize(vertices.size()); // New native w is zero; original orphan w is unknown.
    for (auto& tangent : output) tangent[0] = tangent[1] = tangent[2] = 0;
    const auto corner = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const auto& A = vertices[a]; const auto& B = vertices[b]; const auto& C = vertices[c];
        const Vector3 n{A.nx,A.nz,A.ny};
        Vector3 e1{B.x-A.x,B.z-A.z,B.y-A.y}, e2{C.x-A.x,C.z-A.z,C.y-A.y};
        // Binary32 stores in the recovered producer; native_assets disables FMA.
        const float nd1 = dot(n,e1), nd2 = dot(n,e2);
        for (std::size_t i = 0; i < 3; ++i) {
            const float projection1 = n[i]*nd1, projection2 = n[i]*nd2;
            e1[i] = e1[i]-projection1; e2[i] = e2[i]-projection2;
        }
        if (!finite(e1) || !finite(e2)) fail("non-finite projected tangent edge");
        float du1 = B.u-A.u, du2 = C.u-A.u, dv1 = B.v-A.v, dv2 = C.v-A.v;
        const float lhs = du2*dv1, rhs = du1*dv2;
        if (!std::isfinite(lhs) || !std::isfinite(rhs)) fail("non-finite tangent UV arithmetic");
        if (lhs <= rhs) { du1 = -du1; du2 = -du2; }
        else { dv1 = -dv1; dv2 = -dv2; }
        Vector3 numerator;
        for (std::size_t i = 0; i < 3; ++i) {
            const float t1 = e1[i]*dv2, t2 = e2[i]*dv1;
            numerator[i] = t1-t2;
        }
        const auto t = normalize_source(numerator);
        auto& accumulated = output[a];
        for (std::size_t i = 0; i < 3; ++i) accumulated[i] = accumulated[i]+t[i];
        Vector3 binormal;
        for (std::size_t i = 0; i < 3; ++i) {
            const float b1 = e1[i]*du2, b2 = e2[i]*du1;
            binormal[i] = b1-b2;
        }
        Vector3 cross;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto j = (i+1)%3, k = (i+2)%3;
            const float first = t[j]*n[k], second = t[k]*n[j];
            cross[i] = first-second; // The source convention is T cross N.
        }
        const float h = dot(binormal,cross);
        if (!finite(binormal) || !finite(cross) || !std::isfinite(h))
            fail("non-finite tangent handedness");
        accumulated[3] = h < 0 ? -1.f : 1.f; // Last incident corner, including degenerates.
    };
    for (std::size_t face = 0; face < original_indices.size(); face += 3) {
        const auto a = original_indices[face], b = original_indices[face+1], c = original_indices[face+2];
        corner(a,b,c); corner(b,a,c); corner(c,a,b);
    }
    for (auto& tangent : output) {
        const auto t = normalize_source({tangent[0],tangent[1],tangent[2]});
        for (std::size_t i = 0; i < 3; ++i) tangent[i] = t[i];
        if (!std::isfinite(tangent[3])) fail("non-finite retained tangent w");
    }
}

Mesh load_mesh(const std::filesystem::path& path) {
    const auto storage = read_resource(path);
    if (storage.size() >= 5 && std::memcmp(storage.data(), "bmesh", 5) == 0)
        return load_bmesh(storage);
    auto model = decode_model(storage);
    if (model.groups.size() == 1) return std::move(model.groups.front().mesh);
    Mesh mesh;
    std::size_t vertex_count = 0, index_count = 0;
    const auto& first = model.groups.front().mesh;
    const bool secondary = !first.secondary_uv.empty(), tertiary = !first.tertiary_uv.empty();
    mesh.tangent_provenance = first.tangent_provenance;
    for (const auto& group : model.groups) {
        if ((!group.mesh.secondary_uv.empty()) != secondary ||
            (!group.mesh.tertiary_uv.empty()) != tertiary ||
            group.mesh.tangent_provenance != mesh.tangent_provenance)
            fail("combined MDL7 attribute layout or tangent provenance differs");
        if ((secondary && group.mesh.secondary_uv.size() != group.mesh.vertices.size()) ||
            (tertiary && group.mesh.tertiary_uv.size() != group.mesh.vertices.size()))
            fail("combined MDL7 attribute count differs from vertices");
        if (group.mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() - vertex_count)
            fail("combined MDL7 vertex count exceeds index range");
        vertex_count += group.mesh.vertices.size();
        if (group.mesh.indices.size() > mesh.indices.max_size() - index_count)
            fail("combined MDL7 index count exceeds allocation range");
        index_count += group.mesh.indices.size();
    }
    mesh.vertices.reserve(vertex_count);
    mesh.indices.reserve(index_count);
    if (secondary) mesh.secondary_uv.reserve(vertex_count);
    if (tertiary) mesh.tertiary_uv.reserve(vertex_count);
    for (const auto& group : model.groups) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), group.mesh.vertices.begin(), group.mesh.vertices.end());
        mesh.secondary_uv.insert(mesh.secondary_uv.end(), group.mesh.secondary_uv.begin(), group.mesh.secondary_uv.end());
        mesh.tertiary_uv.insert(mesh.tertiary_uv.end(), group.mesh.tertiary_uv.begin(), group.mesh.tertiary_uv.end());
        for (auto index : group.mesh.indices) mesh.indices.push_back(base + index);
    }
    return mesh;
}
} // namespace c4::assets
