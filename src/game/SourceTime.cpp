#include "SourceTime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>

namespace c4::game {
std::string source_str_for_num(physics::Var value) {
    char text[32];
    if((value.raw&1023)==0) std::snprintf(text,sizeof text,"%d",value.raw/1024);
    else {
        // Original 10170277 carries whenever the next decimal digit is >= '5'.
        // Its sign is emitted separately: exact decimal ties round away from zero.
        const double number=static_cast<double>(value.to_float());
        const auto millis=static_cast<std::int64_t>(std::floor(std::abs(number)*1000+.5));
        std::snprintf(text,sizeof text,"%s%lld.%03lld",number<0?"-":"",
                      static_cast<long long>(millis/1000),static_cast<long long>(millis%1000));
    }
    return text;
}
physics::Var source_str_to_num(const char* text) {
    return physics::Var::from_float(static_cast<float>(std::atof(text)));
}
physics::Var source_time_roundtrip(physics::Var ticks) {
    const auto text=source_str_for_num(ticks);
    return source_str_to_num(text.c_str());
}
void source_time_text(std::string& text,physics::Var ticks,std::string_view prefix) {
    using physics::Var;
    auto seconds=ticks/Var::literal(16);
    const Var minutes{((seconds/Var::literal(60)).raw/1024)*1024};
    seconds=seconds-minutes*Var::literal(60);
    const Var whole{(seconds.raw/1024)*1024};
    seconds=seconds-whole;
    auto fraction=source_str_for_num(seconds);
    fraction.erase(0,2);
    if(fraction.size()<3)fraction.append(3-fraction.size(),'0');
    text.assign(prefix);
    if(minutes.raw<10*1024)text+='0';
    text+=source_str_for_num(minutes);
    text+=whole.raw<10*1024?":0":":";
    text+=source_str_for_num(whole);
    text+='.';
    text+=fraction;
}
} // namespace c4::game
