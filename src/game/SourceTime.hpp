#pragma once

#include "physics/FixedPoint.hpp"
#include <string>
#include <string_view>

namespace c4::game {
// acknex.dll 10005b20 / 10005af0: Q10 -> binary32 -> CRT %.3f,
// then CRT atof -> binary32 -> Q10 truncation. Integral vars use %d.
std::string source_str_for_num(physics::Var value);
physics::Var source_str_to_num(const char* text);
physics::Var source_time_roundtrip(physics::Var ticks);
// ghost_car.wdl timer/record panel: divide by 16, split integer minutes
// and seconds, clip the fraction's "0." and pad its tail to three digits.
void source_time_text(std::string& text, physics::Var ticks, std::string_view prefix);
} // namespace c4::game
