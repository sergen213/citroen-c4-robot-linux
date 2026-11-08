#include "ShadowCascades.hpp"

#include "../../physics/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace c4::rendering::quality {
namespace {
using c4::physics::Vec3;
using c4::physics::cross;
using c4::physics::dot;

bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool unproject(const std::array<float, 16>& matrix, double x, double y,
               double z, Vec3& result) noexcept {
    const double w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
    if (!std::isfinite(w) || w == 0.0) return false;
    result = {(matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12]) / w,
              (matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13]) / w,
              (matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]) / w};
    return finite(result);
}

Vec3 unit(Vec3 value) noexcept {
    return value / std::hypot(value.x, value.y, value.z);
}

bool matrix_float(double value, float& output) noexcept {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(value);
    return true;
}
} // namespace

std::optional<ShadowCascades> build_shadow_cascades(
    const std::array<float, 16>& inverse_view_projection,
    const std::array<float, 3>& sun_direction,
    float near_depth, float far_depth,
    float camera_clip_near, float camera_clip_far,
    unsigned cascade_count, unsigned tile_resolution, float split_lambda,
    float caster_depth_padding, bool light_clip_zero_to_one) noexcept {
    if (cascade_count == 0 || cascade_count > 4 || tile_resolution < 4 ||
        !std::isfinite(near_depth) || !std::isfinite(far_depth) ||
        near_depth <= 0.0f || far_depth <= near_depth ||
        !std::isfinite(camera_clip_near) || !std::isfinite(camera_clip_far) ||
        camera_clip_near == camera_clip_far || !std::isfinite(split_lambda) ||
        split_lambda < 0.0f || split_lambda > 1.0f ||
        !std::isfinite(caster_depth_padding) || caster_depth_padding < 0.0f)
        return std::nullopt;
    for (float value : inverse_view_projection)
        if (!std::isfinite(value)) return std::nullopt;
    Vec3 sun{sun_direction[0], sun_direction[1], sun_direction[2]};
    if (!finite(sun) || dot(sun, sun) == 0.0) return std::nullopt;
    sun = unit(sun);
    const Vec3 reference = std::abs(sun.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Vec3 right = unit(cross(sun, reference));
    const Vec3 up = cross(right, sun);

    std::array<Vec3, 4> near_corners{}, far_corners{};
    for (unsigned i = 0; i < 4; ++i) {
        const double x = (i & 1u) ? 1.0 : -1.0;
        const double y = (i & 2u) ? 1.0 : -1.0;
        if (!unproject(inverse_view_projection, x, y, camera_clip_near, near_corners[i]) ||
            !unproject(inverse_view_projection, x, y, camera_clip_far, far_corners[i]))
            return std::nullopt;
    }
    // Reject collapsed projective cameras before fitting seemingly valid boxes.
    const Vec3 edge_x = near_corners[1] - near_corners[0];
    const Vec3 edge_y = near_corners[2] - near_corners[0];
    const Vec3 edge_z = far_corners[0] - near_corners[0];
    const double edge_scale = std::hypot(edge_x.x, edge_x.y, edge_x.z)
                            * std::hypot(edge_y.x, edge_y.y, edge_y.z)
                            * std::hypot(edge_z.x, edge_z.y, edge_z.z);
    if (!std::isfinite(edge_scale) || edge_scale == 0.0 ||
        std::abs(dot(cross(edge_x, edge_y), edge_z)) <= edge_scale * 1.0e-10)
        return std::nullopt;

    ShadowCascades result{};
    result.count = cascade_count;
    result.split_depths.fill(far_depth);
    for (auto& matrix : result.matrices)
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
    const double near_distance = near_depth;
    const double far_distance = far_depth;
    const double depth_range = far_distance - near_distance;
    double previous = near_distance;
    for (unsigned cascade = 0; cascade < cascade_count; ++cascade) {
        const double fraction = static_cast<double>(cascade + 1) / cascade_count;
        const double logarithmic = near_distance * std::pow(far_distance / near_distance, fraction);
        const double uniform = near_distance + depth_range * fraction;
        const double practical_split = cascade + 1 == cascade_count ? far_distance
            : (1.0 - static_cast<double>(split_lambda)) * uniform
              + static_cast<double>(split_lambda) * logarithmic;
        const float stored_split = static_cast<float>(practical_split);
        if (!std::isfinite(practical_split) || stored_split <= previous)
            return std::nullopt;
        result.split_depths[cascade] = stored_split;
        // Fit exactly the representable depths the GPU uses for selection.
        const double split = stored_split;

        std::array<Vec3, 8> corners{};
        Vec3 center{};
        for (unsigned i = 0; i < 4; ++i) {
            const Vec3 ray = far_corners[i] - near_corners[i];
            corners[i] = near_corners[i] + ray * ((previous - near_distance) / depth_range);
            corners[i + 4] = near_corners[i] + ray * ((split - near_distance) / depth_range);
            center = center + corners[i] + corners[i + 4];
        }
        center = center / 8.0;
        double radius_squared = 0.0;
        double minimum_z = std::numeric_limits<double>::infinity();
        double maximum_z = -minimum_z;
        for (Vec3 corner : corners) {
            const Vec3 offset = corner - center;
            radius_squared = std::max(radius_squared, dot(offset, offset));
            const double z = dot(sun, corner);
            minimum_z = std::min(minimum_z, z);
            maximum_z = std::max(maximum_z, z);
        }
        // Sphere fitting fixes scale during camera rotation. Two texel margins
        // accommodate center snapping and the PCF footprint at the tile edge.
        const double radius = std::sqrt(radius_squared)
                            * static_cast<double>(tile_resolution) / (tile_resolution - 2.0);
        const double texel = 2.0 * radius / tile_resolution;
        if (!std::isfinite(radius) || radius <= 0.0 || texel <= 0.0) return std::nullopt;
        const double center_x = std::round(dot(right, center) / texel) * texel;
        const double center_y = std::round(dot(up, center) / texel) * texel;
        minimum_z -= caster_depth_padding;
        maximum_z += caster_depth_padding;
        const double z_range = maximum_z - minimum_z;
        if (!std::isfinite(z_range) || z_range <= 0.0) return std::nullopt;
        const double z_scale = (light_clip_zero_to_one ? 1.0 : 2.0) / z_range;
        const double z_offset = -minimum_z * z_scale - (light_clip_zero_to_one ? 0.0 : 1.0);
        const std::array<double, 16> fitted{
            right.x / radius, up.x / radius, sun.x * z_scale, 0,
            right.y / radius, up.y / radius, sun.y * z_scale, 0,
            right.z / radius, up.z / radius, sun.z * z_scale, 0,
            -center_x / radius, -center_y / radius, z_offset, 1};
        auto& matrix = result.matrices[cascade];
        for (unsigned i = 0; i < fitted.size(); ++i)
            if (!matrix_float(fitted[i], matrix[i])) return std::nullopt;
        // Float conversion must not collapse any projection axis to zero.
        if ((matrix[0] == 0 && matrix[4] == 0 && matrix[8] == 0) ||
            (matrix[1] == 0 && matrix[5] == 0 && matrix[9] == 0) ||
            (matrix[2] == 0 && matrix[6] == 0 && matrix[10] == 0))
            return std::nullopt;
        // Half a texel constant offset and one texel of receiver slope. Convert
        // world units to sampled [0,1] depth, not the GL/Vulkan clip range.
        if (!matrix_float(0.5 * texel / z_range, result.depth_bias[cascade][0]) ||
            !matrix_float(texel / z_range, result.depth_bias[cascade][1]))
            return std::nullopt;
        previous = split;
    }
    return result;
}

} // namespace c4::rendering::quality
