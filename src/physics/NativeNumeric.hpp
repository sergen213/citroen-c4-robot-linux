#pragma once

#include "FixedPoint.hpp"
#include "Geometry.hpp"

#include <array>

namespace c4::physics {
// Source decimal constants pass through compiler float conversion. Raw source state
// stays in Var; number() publishes its exact Q10 value, not floatv's rounded float.
inline Var v(double value) { return Var::literal(value); }
inline double number(Var value) { return static_cast<double>(value.raw)/1024; }
// Body/VIEW publication already represents raw Q10. Re-import it without the
// FLOAT ABI narrowing used by literals and actual engine float-vector inputs.
inline Var property(double value) {
    const double raw = std::trunc(value*1024);
    if (!std::isfinite(raw) || raw < std::numeric_limits<std::int32_t>::min() ||
        raw > std::numeric_limits<std::int32_t>::max())
        throw std::out_of_range("Original Q10 property exceeds supported signed range");
    return {static_cast<std::int32_t>(raw)};
}

namespace literals {
inline Var operator""_v(long double value) { return Var::literal(static_cast<double>(value)); }
inline Var operator""_v(unsigned long long value) { return Var::literal(static_cast<double>(value)); }
} // namespace literals

inline Var var_abs(Var value) { return value.raw < 0 ? -value : value; }
inline Var var_sign(Var value) { return Var{value.raw < 0 ? -1024 : value.raw > 0 ? 1024 : 0}; }
inline Var var_min(Var a, Var b) { return a < b ? a : b; }
inline Var var_max(Var a, Var b) { return a > b ? a : b; }
inline Var var_clamp(Var value, Var low, Var high) { return value > high ? high : value < low ? low : value; }
inline Var var_integer(Var value) { return Var{value.raw/1024*1024}; }

struct ScriptVec {
    Var x{}, y{}, z{};
    ScriptVec operator+(ScriptVec b) const { return {x+b.x, y+b.y, z+b.z}; }
    ScriptVec operator-(ScriptVec b) const { return {x-b.x, y-b.y, z-b.z}; }
    ScriptVec& operator+=(ScriptVec b) { return *this = *this + b; }
    ScriptVec& operator-=(ScriptVec b) { return *this = *this - b; }
};

// Engine vector inputs use the recovered _var(float) ABI, deliberately unlike
// raw script state publication. vec_scale has its own rounding, not opcode8's.
inline ScriptVec source(Vec3 value) {
    return {Var::from_float(static_cast<float>(value.x)),
            Var::from_float(static_cast<float>(value.y)),
            Var::from_float(static_cast<float>(value.z))};
}
inline ScriptVec body_source(Vec3 value) {
    return {property(value.x), property(value.y), property(value.z)};
}
inline Vec3 native(ScriptVec value) { return {number(value.x), number(value.y), number(value.z)}; }
inline ScriptVec var_vec_scale(ScriptVec value, Var factor) {
    return {value.x.scaled(factor), value.y.scaled(factor), value.z.scaled(factor)};
}

// Implemented separately from recovered engine entrypoints and script opcodes.
Var var_sin(Var value);
Var var_cos(Var value);
Var var_sqrt(Var value);
Var var_asin(Var value);
Var var_ang(Var value);
Var var_frc(Var value);
Var var_modulo(Var value, Var modulus);
// Native SDK rotation shares one original float matrix, without Q10 publication.
std::array<Vec3, 3> rotation_axes(Vec3 degrees);
Vec3 rotate_vector(Vec3 value, Vec3 degrees);
// Boneless model mode2: stored FLOAT scale/rotation coefficients and translation,
// then one affine FLOAT output store; unlike vec_rotate, no rotation-only store.
Vec3 model_vertex_position(Vec3 vertex, Vec3 position, Vec3 degrees, Vec3 scale);
// Raw D3D _11.._44 FLOAT storage (no memory transpose). On x86 uses the
// recovered SSE inverse paired with the observed SSE2 table installer;
// other targets use the recovered scalar family. Runtime FP parity is qualified.
// Input/output may alias. Failure leaves output unchanged.
bool source_matrix_inverse(const std::array<float, 16>& input, std::array<float, 16>& output);
ScriptVec var_vec_rotate(ScriptVec value, ScriptVec angles);
ScriptVec var_vec_to_angle(ScriptVec value, ScriptVec previous = {});
Var var_vec_length(ScriptVec value);
inline Var var_vec_dist(ScriptVec a, ScriptVec b) { return var_vec_length(a-b); }
} // namespace c4::physics
