#pragma once

#include <array>
#include <span>

namespace c4::physics {
struct Vec3 {
    double x{}, y{}, z{};
    constexpr Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    constexpr Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    constexpr Vec3 operator*(double s) const { return {x*s,y*s,z*s}; }
    constexpr Vec3 operator/(double s) const { return {x/s,y/s,z/s}; }
};
constexpr double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double length(Vec3 value);
Vec3 normalized(Vec3 value);
struct Bounds { Vec3 min{}, max{}; };
struct Triangle { std::array<Vec3,3> vertices{}; };
struct SweepContact {
    bool hit{}, initially_overlapping{};
    double fraction{1};
    Vec3 normal{}, point{};
};
// Triangle normals follow winding, or face against displacement for double-sided queries.
SweepContact ray_triangle(Vec3 origin, Vec3 displacement, const Triangle& triangle);
// Unit sphere sweep; oriented ellipsoid queries map actual triangles into this space.
SweepContact sweep_unit_sphere(Vec3 origin, Vec3 displacement, const Triangle& triangle);
} // namespace c4::physics
