#include "NativeNumeric.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace c4::physics {
namespace {
// Constants are decoded from the hash-scoped original DLL, not mathematical pi.
constexpr double angleUnitsPerDegree = 182.04444444444445; // 0x1018a420
constexpr double radiansPerAngleUnit = 0.00009587379913330079; // 0x1018a440
constexpr double degreesPerAngleUnit = 0.0054931640625; // 0x1018a418
constexpr double rotationRadiansPerDegree = 0.01745329238474369; // 0x1018aaf8
constexpr double degreesPerRadian = 57.29577951308232; // 0x10190ba8
// CRT startup sets nominal 53-bit x87 precision; double models unstored
// caller arithmetic. Runtime-thread FP state and CRT/libm bit parity remain
// qualified in work/reports/physics/native-numeric-math.json.

std::int32_t truncateRaw(double value) {
    const auto truncated = std::trunc(value);
    if (!std::isfinite(truncated) || truncated < std::numeric_limits<std::int32_t>::min() ||
        truncated > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Original numeric conversion exceeds supported signed range");
    return static_cast<std::int32_t>(truncated);
}

Var fromEngineFloat(float value) {
    return {truncateRaw(static_cast<double>(value) * 1024)};
}

std::int16_t angleUnits(float degrees) {
    const auto units = truncateRaw(static_cast<double>(degrees) * angleUnitsPerDegree);
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(units));
}

float scalarRadians(Var value) {
    return static_cast<float>(static_cast<double>(angleUnits(value.to_float())) * radiansPerAngleUnit);
}

struct FloatVec { float x, y, z; };
FloatVec engineFloat(ScriptVec value) {
    return {value.x.to_float(), value.y.to_float(), value.z.to_float()};
}

double squareSum(FloatVec value) {
    const double x = value.x, y = value.y, z = value.z;
    return (x*x + y*y) + z*z;
}

std::int32_t divideRaw(std::int64_t numerator, std::int32_t denominator) {
    if (denominator == 0)
        throw std::domain_error("Original modulo division by zero");
    const auto quotient = numerator / denominator;
    if (quotient < std::numeric_limits<std::int32_t>::min() ||
        quotient > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Original modulo division quotient overflow");
    return static_cast<std::int32_t>(quotient);
}

struct SinCos { float sin, cos; };
SinCos rotationTrig(float degrees) {
    if (degrees == 0) return {0, 1}; // The original skips its CRT calls for zero.
    const auto radians = static_cast<float>(static_cast<double>(degrees) * rotationRadiansPerDegree);
    return {static_cast<float>(std::sin(static_cast<double>(radians))),
            static_cast<float>(std::cos(static_cast<double>(radians)))};
}
} // namespace

Var var_sin(Var value) {
    return fromEngineFloat(static_cast<float>(std::sin(static_cast<double>(scalarRadians(value)))));
}

Var var_cos(Var value) {
    return fromEngineFloat(static_cast<float>(std::cos(static_cast<double>(scalarRadians(value)))));
}

Var var_sqrt(Var value) {
    // Observed original masked CW0x027f/ftolflag1: CRT NaN -> CVTTSD2SI indefinite.
    if (value.raw < 0) return {std::numeric_limits<std::int32_t>::min()};
    return fromEngineFloat(static_cast<float>(std::sqrt(static_cast<double>(value.to_float()))));
}

Var var_asin(Var value) {
    // 0x10004440 clamps raw input before any floating conversion.
    const auto raw = value.raw < -1024 ? -1024 : value.raw > 1024 ? 1024 : value.raw;
    const auto radians = static_cast<float>(std::asin(static_cast<double>(Var{raw}.to_float())));
    const auto degrees = static_cast<float>(static_cast<double>(radians) * 180.0 / 3.14159265);
    return fromEngineFloat(degrees);
}

Var var_ang(Var value) {
    auto degrees = static_cast<float>(static_cast<double>(angleUnits(value.to_float())) * degreesPerAngleUnit);
    if (degrees > 180) degrees = static_cast<float>(static_cast<double>(degrees) - 360.0);
    else if (degrees < -180) degrees = static_cast<float>(static_cast<double>(degrees) + 360.0);
    return fromEngineFloat(degrees);
}

Var var_frc(Var value) {
    return {value.raw % 1024}; // Sign-preserving masks at 0x10004790, including INT32_MIN.
}

Var var_modulo(Var value, Var modulus) {
    // '%' calls 0x1000c140: compensate negative operands using 1000000 Q10,
    // including its first IDIV fault boundary and wrapping IMUL/ADD stores.
    auto adjusted = value.raw;
    if (adjusted < 0) {
        const auto quotient = divideRaw(std::int64_t{0x3d090000} * 1024, modulus.raw);
        const auto count = (quotient >> 10) + 1;
        const auto offset = static_cast<std::uint32_t>(count) * static_cast<std::uint32_t>(modulus.raw);
        adjusted = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(adjusted) + offset);
    }
    const auto quotient = divideRaw(adjusted, modulus.raw);
    return {static_cast<std::int32_t>(static_cast<std::int64_t>(adjusted) -
                                    static_cast<std::int64_t>(quotient) * modulus.raw)};
}

Var var_vec_length(ScriptVec value) {
    // 0x10004b10 has no binary32 store between the sum and square root.
    return fromEngineFloat(static_cast<float>(std::sqrt(squareSum(engineFloat(value)))));
}

ScriptVec var_vec_to_angle(ScriptVec value, ScriptVec previous) {
    const auto input = engineFloat(value);
    const auto squaredLength = static_cast<float>(squareSum(input));
    const auto length = static_cast<float>(std::sqrt(static_cast<double>(squaredLength)));
    // Original leaves destination untouched at this threshold and never writes roll.
    if (length <= 0.001) return previous;
    float pan;
    if (input.x == 0) pan = input.y > 0 ? 90.0f : 270.0f;
    else if (input.y == 0) pan = input.x > 0 ? 0.0f : 180.0f;
    else {
        const auto radians = static_cast<float>(std::atan2(static_cast<double>(input.y), static_cast<double>(input.x)));
        const auto degrees = static_cast<float>(static_cast<double>(radians) * degreesPerRadian);
        pan = static_cast<float>(static_cast<double>(angleUnits(degrees)) * degreesPerAngleUnit);
    }
    float tilt = 0;
    if (input.z != 0 && length != 0) {
        const auto ratio = static_cast<float>(static_cast<double>(input.z) / length);
        // Unlike scalar asinv, 0x10041ee0 does not clamp its ratio.
        if (ratio < -1 || ratio > 1)
            throw std::domain_error("Original vector tilt outside inverse sine domain");
        const auto radians = static_cast<float>(std::asin(static_cast<double>(ratio)));
        tilt = static_cast<float>(static_cast<double>(radians) * degreesPerRadian);
    }
    return {fromEngineFloat(pan), fromEngineFloat(tilt), previous.z};
}

std::array<Vec3, 3> rotation_axes(Vec3 angles) {
    const FloatVec degrees{static_cast<float>(angles.x), static_cast<float>(angles.y), static_cast<float>(angles.z)};
    if (!std::isfinite(degrees.x) || !std::isfinite(degrees.y) || !std::isfinite(degrees.z))
        throw std::invalid_argument("Native rotation requires finite binary32 angles");
    const auto pan = rotationTrig(degrees.x), tilt = rotationTrig(degrees.y), roll = rotationTrig(degrees.z);
    const double sp = pan.sin, cp = pan.cos, st = tilt.sin, ct = tilt.cos, sr = roll.sin, cr = roll.cos;
    // 0x100421f0 stores the column-major Rz(pan) Ry(-tilt) Rx(roll)
    // matrix as binary32. Publish those exact coefficients, without Q10.
    return {{
        {static_cast<float>(ct*cp), static_cast<float>(ct*sp), static_cast<float>(st)},
        {static_cast<float>(-sr*st*cp - cr*sp), static_cast<float>(cr*cp - sr*st*sp), static_cast<float>(sr*ct)},
        {static_cast<float>(sr*sp - cr*st*cp), static_cast<float>(-sr*cp - cr*st*sp), static_cast<float>(cr*ct)}
    }};
}

Vec3 rotate_vector(Vec3 value, Vec3 angles) {
    const FloatVec input{static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
    if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.z))
        throw std::invalid_argument("Native rotation requires finite binary32 vectors");
    const auto axes = rotation_axes(angles);
    const double x = input.x, y = input.y, z = input.z;
    // 0x100425a0's ordered dot products have one binary32 output store.
    return {static_cast<float>((axes[1].x*y + x*axes[0].x) + axes[2].x*z),
            static_cast<float>((axes[0].y*x + axes[1].y*y) + axes[2].y*z),
            static_cast<float>((axes[0].z*x + axes[1].z*y) + axes[2].z*z)};
}

Vec3 model_vertex_position(Vec3 vertex, Vec3 position, Vec3 angles, Vec3 scale) {
    const FloatVec input{static_cast<float>(vertex.x), static_cast<float>(vertex.y), static_cast<float>(vertex.z)};
    const FloatVec translation{static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
    const FloatVec factors{static_cast<float>(scale.x), static_cast<float>(scale.y), static_cast<float>(scale.z)};
    if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.z) ||
        !std::isfinite(translation.x) || !std::isfinite(translation.y) || !std::isfinite(translation.z) ||
        !std::isfinite(factors.x) || !std::isfinite(factors.y) || !std::isfinite(factors.z))
        throw std::invalid_argument("Native model transform requires finite binary32 inputs");
    auto axes = rotation_axes(angles); // 0x10050800 uses the same 0x100421f0 builder.
    const std::array<double, 3> scales{factors.x, factors.y, factors.z};
    // 0x1004e240(mode2) composes R * diag(scale) through 0x10042a00.
    // Each resulting coefficient is stored as FLOAT before the point is read.
    for (std::size_t i = 0; i < axes.size(); ++i)
        axes[i] = {static_cast<float>(axes[i].x*scales[i]),
                   static_cast<float>(axes[i].y*scales[i]),
                   static_cast<float>(axes[i].z*scales[i])};
    const double x = input.x, y = input.y, z = input.z;
    // 0x1004bd80 adds stored translation before fstps at bdac/bdd2/bdf9.
    return {static_cast<float>(((x*axes[0].x + y*axes[1].x) + z*axes[2].x) + translation.x),
            static_cast<float>(((x*axes[0].y + y*axes[1].y) + z*axes[2].y) + translation.y),
            static_cast<float>(((x*axes[0].z + y*axes[1].z) + z*axes[2].z) + translation.z)};
}

bool source_matrix_inverse(const std::array<float, 16>& input, std::array<float, 16>& output) {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    // 0x611331: SSE body paired with the passively observed SSE2 normalization
    // installer. Preserve every lane operation; host RCPSS/MXCSR parity is
    // qualified in source-matrix-inverse.json, not inferred from x87 CW.
    // MOVLPS/MOVHPS pairs load these exact lanes before any arithmetic.
    __m128 r1 = _mm_setr_ps(input[0], input[1], input[4], input[5]);
    __m128 r2 = _mm_setr_ps(input[2], input[3], input[6], input[7]);
    __m128 r3 = _mm_setr_ps(input[8], input[9], input[12], input[13]);
    __m128 r4 = _mm_setr_ps(input[10], input[11], input[14], input[15]);
    __m128 r5, r6, r7;
    r5 = r2; // 611375
    r5 = _mm_shuffle_ps(r5, r4, 0x88); // 611378
    r4 = _mm_shuffle_ps(r4, r2, 0xdd); // 61137c
    r2 = r4; // 611380
    r2 = _mm_mul_ps(r2, r5); // 611383
    r2 = _mm_shuffle_ps(r2, r2, 0xb1); // 611386
    r6 = r2; // 61138a
    r6 = _mm_shuffle_ps(r6, r6, 0x4e); // 61138d
    r7 = r3; // 611391
    r3 = _mm_shuffle_ps(r3, r1, 0xdd); // 611394
    r1 = _mm_shuffle_ps(r1, r7, 0x88); // 611398
    r7 = r3; // 61139c
    r3 = _mm_mul_ps(r3, r6); // 61139f
    r6 = _mm_mul_ps(r6, r1); // 6113a2
    const __m128 td0 = r6; // 6113a5
    r6 = r7; // 6113ac
    r7 = _mm_mul_ps(r7, r2); // 6113af
    r2 = _mm_mul_ps(r2, r1); // 6113b2
    r3 = _mm_sub_ps(r3, r7); // 6113b5
    r7 = r6; // 6113b8
    r7 = _mm_mul_ps(r7, r5); // 6113bb
    r5 = _mm_shuffle_ps(r5, r5, 0x4e); // 6113be
    r7 = _mm_shuffle_ps(r7, r7, 0xb1); // 6113c2
    const __m128 tc0 = r2; // 6113c6
    r2 = r4; // 6113cd
    r2 = _mm_mul_ps(r2, r7); // 6113d0
    r2 = _mm_add_ps(r2, r3); // 6113d3
    r3 = r7; // 6113d6
    r7 = _mm_shuffle_ps(r7, r7, 0x4e); // 6113d9
    r3 = _mm_mul_ps(r3, r1); // 6113dd
    const __m128 tb0 = r3; // 6113e0
    r3 = r4; // 6113e7
    r3 = _mm_mul_ps(r3, r7); // 6113ea
    r7 = _mm_mul_ps(r7, r1); // 6113ed
    r2 = _mm_sub_ps(r2, r3); // 6113f0
    r3 = r6; // 6113f3
    r3 = _mm_shuffle_ps(r3, r3, 0x4e); // 6113f6
    r3 = _mm_mul_ps(r3, r4); // 6113fa
    r3 = _mm_shuffle_ps(r3, r3, 0xb1); // 6113fd
    const __m128 ta0 = r7; // 611401
    r7 = r5; // 611408
    r5 = _mm_mul_ps(r5, r3); // 61140b
    r5 = _mm_add_ps(r5, r2); // 61140e
    r2 = r3; // 611411
    r3 = _mm_shuffle_ps(r3, r3, 0x4e); // 611414
    r2 = _mm_mul_ps(r2, r1); // 611418
    const __m128 t90 = r4; // 61141b
    r4 = r7; // 611422
    r7 = _mm_mul_ps(r7, r3); // 611425
    r3 = _mm_mul_ps(r3, r1); // 611428
    r5 = _mm_sub_ps(r5, r7); // 61142b
    r3 = _mm_sub_ps(r3, r2); // 61142e
    r2 = r1; // 611431
    r1 = _mm_mul_ps(r1, r5); // 611434
    r3 = _mm_shuffle_ps(r3, r3, 0x4e); // 611437
    r7 = r1; // 61143b
    r1 = _mm_shuffle_ps(r1, r1, 0x4e); // 61143e
    const __m128 te0 = r5; // 611442
    r1 = _mm_add_ps(r1, r7); // 611449
    r5 = r1; // 61144c
    r1 = _mm_shuffle_ps(r1, r1, 0xb1); // 61144f
    r1 = _mm_add_ss(r1, r5); // 611453
    r5 = r6; // 61145e
    r5 = _mm_mul_ps(r5, r2); // 611461
    r5 = _mm_shuffle_ps(r5, r5, 0xb1); // 611464
    r7 = r5; // 611468
    r5 = _mm_shuffle_ps(r5, r5, 0x4e); // 61146b
    const __m128 t80 = r4; // 61146f
    r4 = t90; // 611473
    const __m128 t70 = r6; // 61147a
    r6 = r4; // 61147e
    r6 = _mm_mul_ps(r6, r7); // 611481
    r6 = _mm_add_ps(r6, r3); // 611484
    r3 = r4; // 611487
    r3 = _mm_mul_ps(r3, r5); // 61148a
    r3 = _mm_sub_ps(r3, r6); // 61148d
    r6 = r4; // 611490
    r6 = _mm_mul_ps(r6, r2); // 611493
    r6 = _mm_shuffle_ps(r6, r6, 0xb1); // 611496
    const __m128 t60 = r5; // 61149a
    r5 = t70; // 61149e
    const __m128 t50 = r7; // 6114a2
    r7 = r6; // 6114a6
    r7 = _mm_mul_ps(r7, r5); // 6114a9
    r7 = _mm_add_ps(r7, r3); // 6114ac
    r3 = r6; // 6114af
    r3 = _mm_shuffle_ps(r3, r3, 0x4e); // 6114b2
    const __m128 t40 = r4; // 6114b6
    r4 = r5; // 6114ba
    r5 = _mm_mul_ps(r5, r3); // 6114bd
    const __m128 t30 = r4; // 6114c0
    r4 = r6; // 6114c4
    r6 = r7; // 6114c7
    r6 = _mm_sub_ps(r6, r5); // 6114ca
    r5 = td0; // 6114cd
    r7 = tc0; // 6114d4
    r5 = _mm_sub_ps(r5, r7); // 6114db
    r5 = _mm_shuffle_ps(r5, r5, 0x4e); // 6114de
    r7 = t80; // 6114e2
    r4 = _mm_mul_ps(r4, r7); // 6114e6
    r3 = _mm_mul_ps(r3, r7); // 6114e9
    r5 = _mm_sub_ps(r5, r4); // 6114ec
    r2 = _mm_mul_ps(r2, r7); // 6114ef
    r3 = _mm_add_ps(r3, r5); // 6114f2
    r2 = _mm_shuffle_ps(r2, r2, 0xb1); // 6114f5
    r4 = r2; // 6114f9
    r4 = _mm_shuffle_ps(r4, r4, 0x4e); // 6114fc
    r5 = t40; // 611500
    const __m128 t20 = r6; // 611504
    r6 = r5; // 611508
    r5 = _mm_mul_ps(r5, r2); // 61150b
    r6 = _mm_mul_ps(r6, r4); // 61150e
    r5 = _mm_add_ps(r5, r3); // 611511
    r3 = r4; // 611514
    r4 = r5; // 611517
    r4 = _mm_sub_ps(r4, r6); // 61151a
    r5 = ta0; // 61151d
    r6 = tb0; // 611524
    r5 = _mm_sub_ps(r5, r6); // 61152b
    r5 = _mm_shuffle_ps(r5, r5, 0x4e); // 61152e
    r6 = t50; // 611532
    r6 = _mm_mul_ps(r6, r7); // 611536
    r6 = _mm_sub_ps(r6, r5); // 611539
    r5 = t60; // 61153c
    r7 = _mm_mul_ps(r7, r5); // 611540
    r6 = _mm_sub_ps(r6, r7); // 611543
    r5 = t30; // 611546
    r2 = _mm_mul_ps(r2, r5); // 61154a
    r5 = _mm_mul_ps(r5, r3); // 61154d
    r6 = _mm_sub_ps(r6, r2); // 611550
    r2 = r5; // 611553
    r2 = _mm_add_ps(r2, r6); // 611556
    r6 = t20; // 61155c
    if (_mm_comieq_ss(r1, _mm_setzero_ps())) return false; // 611559/611560
    __m128 r0 = r1; // 611570
    r1 = te0; // 611573
    r3 = r0; // 61157d
    r0 = _mm_rcp_ss(r0); // 611580: hardware estimate, not division
    r5 = r0; // 611584
    r0 = _mm_mul_ss(r0, r0); // 611587
    r3 = _mm_mul_ss(r3, r0); // 61158b
    r5 = _mm_add_ss(r5, r5); // 61158f
    r5 = _mm_sub_ss(r5, r3); // 611593
    r5 = _mm_shuffle_ps(r5, r5, 0x00); // 611597
    // All inputs were consumed before output; these stores also support alias.
    r1 = _mm_mul_ps(r1, r5); // 61159b
    _mm_storeu_ps(output.data(), r1); // 61159e/6115a1
    r4 = _mm_mul_ps(r4, r5); // 6115a5
    _mm_storeu_ps(output.data()+4, r4); // 6115a8/6115ac
    r6 = _mm_mul_ps(r6, r5); // 6115b0
    _mm_storeu_ps(output.data()+8, r6); // 6115b3/6115b7
    r5 = _mm_mul_ps(r5, r2); // 6115bb
    _mm_storeu_ps(output.data()+12, r5); // 6115be/6115c2
    return true;
#else
    // d3dx9_30.dll 0x441ccb..0x442071: scalar cofactor kernel, not an
    // orthonormal transpose. double models PC53 unstored x87 arithmetic;
    // PC64, SIMD dispatch and runtime rounding are NOT proven equivalent.
    // Keep every recovered fstps boundary and operation association.
    const double m0=input[0], m1=input[1], m2=input[2], m3=input[3];
    const double m4=input[4], m5=input[5], m6=input[6], m7=input[7];
    const double m8=input[8], m9=input[9], m10=input[10], m11=input[11];
    const double m12=input[12], m13=input[13], m14=input[14], m15=input[15];
    const double a0 = static_cast<float>(m5*m0 - m4*m1);
    const double a1 = static_cast<float>(m9*m0 - m8*m1);
    const double a2 = static_cast<float>(m13*m0 - m12*m1);
    const double a3 = static_cast<float>(m9*m4 - m8*m5);
    const double a4 = static_cast<float>(m13*m4 - m12*m5);
    const double a5 = static_cast<float>(m13*m8 - m12*m9);
    std::array<float, 16> cofactors;
    cofactors[15] = static_cast<float>((m2*a3 - m6*a1) + m10*a0);
    cofactors[14] = static_cast<float>((m6*a2 - m14*a0) - m2*a4);
    cofactors[13] = static_cast<float>((m2*a5 - m10*a2) + m14*a1);
    cofactors[12] = static_cast<float>((m10*a4 - m14*a3) - m6*a5);
    cofactors[11] = static_cast<float>((a1*m7 - a0*m11) - a3*m3);
    cofactors[10] = static_cast<float>((a4*m3 - a2*m7) + a0*m15);
    cofactors[9] = static_cast<float>((a2*m11 - a1*m15) - a5*m3);
    cofactors[8] = static_cast<float>((a5*m7 - a4*m11) + a3*m15);
    const double b0 = static_cast<float>(m2*m7 - m6*m3);
    const double b1 = static_cast<float>(m2*m11 - m10*m3);
    // 0x441e73 retains b2 on the x87 stack; b2..b5 have no FLOAT store.
    const double b2 = m2*m15 - m14*m3;
    const double b3 = m6*m11 - m10*m7;
    const double b4 = m6*m15 - m14*m7;
    const double b5 = m10*m15 - m14*m11;
    cofactors[3] = static_cast<float>((b1*m5 - b0*m9) - b3*m1);
    cofactors[2] = static_cast<float>((b4*m1 - b2*m5) + b0*m13);
    cofactors[1] = static_cast<float>((b2*m9 - b1*m13) - b5*m1);
    cofactors[0] = static_cast<float>((b5*m5 - b4*m9) + b3*m13);
    cofactors[7] = static_cast<float>((b3*m0 - b1*m4) + b0*m8);
    cofactors[6] = static_cast<float>((b2*m4 - b0*m12) - b4*m0);
    cofactors[5] = static_cast<float>((m0*b5 - b2*m8) + b1*m12);
    cofactors[4] = static_cast<float>((b4*m8 - b3*m12) - b5*m4);
    // Determinant stays unstored even when the optional pDeterminant is used.
    const double determinant = ((cofactors[3]*m12 + cofactors[2]*m8) +
                                cofactors[1]*m4) + cofactors[0]*m0;
    if (determinant == 0) return false;
    const float reciprocal = static_cast<float>(1.0/determinant);
    // Original _finite gate precedes all output stores.
    if (!std::isfinite(reciprocal)) return false;
    for (std::size_t i = 0; i < cofactors.size(); ++i)
        output[i] = static_cast<float>(static_cast<double>(cofactors[i])*reciprocal);
    return true;
#endif
}

ScriptVec var_vec_rotate(ScriptVec value, ScriptVec angles) {
    const auto input = engineFloat(value), degrees = engineFloat(angles);
    const auto result = rotate_vector({input.x, input.y, input.z}, {degrees.x, degrees.y, degrees.z});
    return {fromEngineFloat(static_cast<float>(result.x)), fromEngineFloat(static_cast<float>(result.y)),
            fromEngineFloat(static_cast<float>(result.z))};
}
} // namespace c4::physics
