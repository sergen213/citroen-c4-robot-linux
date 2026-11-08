#pragma once

#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace c4::physics {
// Original _var(float)/floatv(var) ABI, observed in the isolated 7.07.6 DLL.
// Script arithmetic is separately recovered from the original operator table/codegen.
// None of these primitives establish cooperative scheduler parity.
struct Var {
    std::int32_t raw{};
    static Var from_float(float value) {
        const double scaled=static_cast<double>(value)*1024;
        if(!std::isfinite(scaled)||scaled<std::numeric_limits<std::int32_t>::min()||
           scaled>std::numeric_limits<std::int32_t>::max())
            throw std::out_of_range("Original var conversion exceeds supported signed range");
        return {static_cast<std::int32_t>(scaled)}; // Truncation towards zero, not nearest.
    }
    float to_float() const { return static_cast<float>(raw)*(1.0f/1024); }
    static Var literal(double value) { return from_float(static_cast<float>(value)); }
    Var operator+() const { return *this; }
    Var operator-() const { return Var{} - *this; }
    explicit operator bool() const { return raw != 0; }
    auto operator<=>(const Var&) const = default;
    Var& operator+=(Var b) { return *this = *this + b; }
    Var& operator-=(Var b) { return *this = *this - b; }
    Var& operator*=(Var b) { return *this = *this * b; }
    Var& operator/=(Var b) { return *this = *this / b; }
    Var& operator++() { return *this += Var{1024}; }
    Var& operator--() { return *this -= Var{1024}; }
    Var operator++(int) { const auto old = *this; ++*this; return old; }
    Var operator--(int) { const auto old = *this; --*this; return old; }
    Var operator+(Var b) const {
        return {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw)+static_cast<std::uint32_t>(b.raw))};
    }
    Var operator-(Var b) const {
        return {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw)-static_cast<std::uint32_t>(b.raw))};
    }
    Var operator*(Var b) const {
        auto product=static_cast<std::int64_t>(raw)*b.raw;
        // Opcode8 biases only products whose high word is exactly -1.
        // For products below -2^32 the original compiler instead rounds down.
        if((static_cast<std::uint64_t>(product)>>32)==0xffffffffu)product+=1023;
        return {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(product>>10))};
    }
    Var operator/(Var b) const {
        if(b.raw==0)throw std::domain_error("Original var division by zero");
        const auto quotient=(static_cast<std::int64_t>(raw)*1024)/b.raw;
        if(quotient<std::numeric_limits<std::int32_t>::min()||quotient>std::numeric_limits<std::int32_t>::max())
            throw std::overflow_error("Original var division quotient overflow");
        return {static_cast<std::int32_t>(quotient)};
    }
    // vec_scale's observed 64-bit product, bit-9 rounding (ties toward +infinity)
    // and 32-bit wrapping store; not the unproven script multiplication opcode.
    Var scaled(Var factor) const {
        const auto product=static_cast<std::int64_t>(raw)*factor.raw;
        const auto rounded=(product>>10)+((static_cast<std::uint64_t>(product)&512)!=0);
        return {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(rounded))};
    }
};
} // namespace c4::physics
