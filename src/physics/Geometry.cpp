#include "Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace c4::physics {
namespace {
bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate(Vec3 origin, Vec3 displacement, const Triangle& triangle) {
    if (!finite(origin) || !finite(displacement))
        throw std::invalid_argument("nonfinite geometry query");
    for (const Vec3 vertex : triangle.vertices)
        if (!finite(vertex))
            throw std::invalid_argument("nonfinite triangle vertex");
}

Vec3 triangle_normal(const Triangle& triangle) {
    // Scaling each edge avoids overflowing the cross product without discarding thin faces.
    return normalized(cross(normalized(triangle.vertices[1] - triangle.vertices[0]),
                            normalized(triangle.vertices[2] - triangle.vertices[0])));
}

bool inside_triangle(Vec3 point, const Triangle& triangle, Vec3 normal) {
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 edge = triangle.vertices[(i + 1) % 3] - triangle.vertices[i];
        if (dot(cross(edge, point - triangle.vertices[i]), normal) < 0)
            return false;
    }
    return true;
}

Vec3 closest_point(Vec3 point, const Triangle& triangle) {
    const Vec3 a = triangle.vertices[0];
    const Vec3 b = triangle.vertices[1];
    const Vec3 c = triangle.vertices[2];
    const Vec3 ab = b - a, ac = c - a, ap = point - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3 bp = point - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1*d4 - d3*d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = point - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5*d2 - d1*d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3*d6 - d5*d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double inverse = 1 / (va + vb + vc);
    return a + ab * (vb * inverse) + ac * (vc * inverse);
}

// Solve a*t^2 + 2*b*t + c = 0 without cancellation in the smaller root.
template<class Accept>
void sphere_roots(Vec3 offset, Vec3 velocity, Accept&& accept) {
    const double a = dot(velocity, velocity);
    if (a == 0) return;
    const double b = dot(offset, velocity), c = dot(offset, offset) - 1;
    const double discriminant = std::fma(-a, c, b*b);
    if (discriminant < 0) return;
    const double q = -b - std::copysign(std::sqrt(discriminant), b);
    if (q == 0) {
        accept(-b / a);
        return;
    }
    const double first = q / a, second = c / q;
    accept(std::min(first, second));
    if (first != second) accept(std::max(first, second));
}
} // namespace

double length(Vec3 value) { return std::hypot(value.x, value.y, value.z); }

Vec3 normalized(Vec3 value) {
    if (!finite(value)) throw std::invalid_argument("nonfinite geometry vector");
    const double scale = std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
    if (scale == 0) return {};
    const Vec3 scaled = value / scale;
    return scaled / length(scaled);
}

SweepContact ray_triangle(Vec3 origin, Vec3 displacement, const Triangle& triangle) {
    validate(origin, displacement, triangle);
    Vec3 normal = triangle_normal(triangle);
    const double velocity = dot(normal, displacement);
    if (velocity == 0) return {};
    const double fraction = dot(normal, triangle.vertices[0] - origin) / velocity;
    if (fraction < 0 || fraction > 1) return {};
    const Vec3 point = origin + displacement * fraction;
    if (!inside_triangle(point, triangle, normal)) return {};
    if (velocity > 0) normal = normal * -1;
    return {true, false, fraction, normal, point};
}

SweepContact sweep_unit_sphere(Vec3 origin, Vec3 displacement, const Triangle& triangle) {
    validate(origin, displacement, triangle);
    const Vec3 face_normal = triangle_normal(triangle);
    if (dot(face_normal, face_normal) == 0) return {};
    const Vec3 initial_point = closest_point(origin, triangle);
    const Vec3 initial_offset = origin - initial_point;
    const double initial_distance = length(initial_offset);
    if (initial_distance < 1) {
        Vec3 normal = normalized(initial_offset);
        if (initial_distance == 0)
            normal = face_normal * (dot(face_normal, displacement) > 0 ? -1 : 1);
        return {true, true, 0, normal, initial_point};
    }
    if (initial_distance == 1 && dot(initial_offset, displacement) >= 0) return {};

    SweepContact result;
    const auto consider = [&](double fraction, Vec3 point) {
        if (!std::isfinite(fraction) || fraction < 0 || fraction > result.fraction) return;
        const Vec3 normal = normalized(origin + displacement * fraction - point);
        if (dot(normal, displacement) > 0) return; // Exit roots cannot be first contact.
        result = {true, false, fraction, normal, point};
    };

    const double distance = dot(origin - triangle.vertices[0], face_normal);
    const double velocity = dot(displacement, face_normal);
    if (velocity != 0) {
        for (const double side : {-1.0, 1.0}) {
            const double fraction = (side - distance) / velocity;
            if (fraction < 0 || fraction > result.fraction) continue;
            const Vec3 point = origin + displacement * fraction - face_normal * side;
            if (inside_triangle(point, triangle, face_normal)) consider(fraction, point);
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 start = triangle.vertices[i];
        const Vec3 edge = triangle.vertices[(i + 1) % 3] - start;
        const double edge_squared = dot(edge, edge);
        const Vec3 offset = origin - start;
        const double edge_origin = dot(offset, edge) / edge_squared;
        const double edge_velocity = dot(displacement, edge) / edge_squared;
        sphere_roots(offset - edge * edge_origin, displacement - edge * edge_velocity,
            [&](double fraction) {
                if (fraction < 0 || fraction > result.fraction) return;
                const double along = edge_origin + edge_velocity * fraction;
                if (along >= 0 && along <= 1) consider(fraction, start + edge * along);
            });
        sphere_roots(offset, displacement,
            [&](double fraction) { consider(fraction, start); });
    }
    return result;
}
} // namespace c4::physics
