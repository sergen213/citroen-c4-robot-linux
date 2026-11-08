#include "MotorAudio.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace c4::audio;
using namespace c4::physics;
using namespace c4::physics::literals;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.0001, message);
}
void exact(Var actual, Var expected, const std::string& message) {
    require(actual.raw == expected.raw, message);
}
template<class F> void rejects(F operation, const std::string& message) {
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    catch (const std::out_of_range&) { return; }
    throw std::runtime_error(message);
}
MotorClips check_clips() {
    // Generated check-only PCM; never a replacement for recovered game resources.
    auto clip = std::make_shared<Clip>();
    clip->sample_rate = 8000;
    clip->channels = 1;
    clip->samples.assign(16, 0.05F);
    return {clip, clip, clip, clip, clip, clip, clip};
}

void behavior() {
    const auto clips = check_clips();
    Audio audio(8000, Mode::Offline);
    const auto unrelated = audio.play(clips.idle, 5, 0, true);
    MotorHandles owned;
    std::array<Handle, 2> owned_flames{};
    {
        MotorAudio motor(audio, clips);
        MotorInput input;
        input.ghost_yaris_start = true;
        input.arac_vites_no = 1_v;
        input.arac_vites_ivme = 100_v;
        input.motor_ses_ivme = 200_v;
        input.arac_ivme = 60_v;
        input.key_cuu = true;
        input.time = 0.5_v;
        auto event = motor.update(input);
        require(event.gear_flame_animation && event.flame == 0, "neutral-to-first animates without flame audio");
        const auto first_frame = 0.6_v * 100_v * 0.5_v * var_sqrt(2_v * input.time);
        exact(motor.state().loc_vites_frame, first_frame, "source gear smoothing preserves every Q10 operation");
        exact(motor.state().loc_motor_frame, 100_v, "initial motor smoothing clamps from source zero to 100");
        exact(motor.state().loc_beklehesap, 6.25_v, "upshift resets then advances wait counter");
        exact(motor.state().loc_bagirtime, 6.25_v, "first tune uses pre-increment shout counter");
        const auto first_max = motor.handles().motor_max;
        const auto first_idle = motor.handles().motor_bosta;
        require(audio.playing(first_max) && audio.playing(first_idle), "first gear overlaps acceleration and idle loops");
        near(audio.state(first_max).volume, 10, "source zero tune volume leaves startup 10 unchanged");
        near(audio.state(first_idle).volume, number(100_v - first_frame), "first gear inverse idle volume");
        near(audio.state(motor.handles().yol_asfalt).frequency, 85, "road pitch uses absolute supplied acceleration");
        motor.update(input);
        const auto second_frame = first_frame + 0.6_v * (100_v - first_frame) * 0.5_v;
        const auto second_motor = 100_v + 0.8_v * (200_v - 100_v) * 0.5_v;
        exact(motor.state().loc_vites_frame, second_frame, "second gear-frame smoothing");
        exact(motor.state().loc_motor_frame, second_motor, "second motor smoothing");
        near(audio.state(first_max).volume, number(second_frame * (6.25_v / 100_v)), "first gear uses prior shout fraction");
        require(motor.handles().motor_max == first_max, "loop handle retained across updates");

        input.arac_vites_no = 2_v;
        event = motor.update(input);
        require(event.gear_flame_animation && audio.playing(event.flame), "forward upshift plays flame one-shot");
        require(!audio.state(event.flame).loop, "flame does not loop");
        const auto first_flame = event.flame;
        owned_flames[0] = first_flame;
        const auto forward_min = motor.handles().motor_min;
        require(forward_min != first_max && audio.playing(forward_min) && !audio.playing(first_max),
                "same acceleration clip keeps distinct source min/max handles");
        require(!audio.playing(first_idle), "forward gear stops idle");
        near(audio.state(forward_min).volume, 50, "forward volume source constant");
        const auto third_frame = second_frame + 0.6_v * (100_v - second_frame) * 0.5_v;
        const auto third_motor = second_motor + 0.8_v * (200_v - second_motor) * 0.5_v;
        near(audio.state(forward_min).frequency, number(third_motor), "forward motor pitch");
        input.arac_vites_no = 3_v;
        input.arac_vites_ivme = 0_v;
        input.motor_ses_ivme = 100_v;
        event = motor.update(input);
        require(event.flame != first_flame && audio.playing(first_flame) && audio.playing(event.flame),
                "successive upshift flame voices may overlap");
        owned_flames[1] = event.flame;
        exact(motor.state().loc_vites_frame, third_frame + 0.6_v * (0_v - third_frame) * 0.5_v,
              "falling gear smoothing is same as rising source branch");
        exact(motor.state().loc_motor_frame, third_motor + 0.8_v * (100_v - third_motor) * 0.5_v,
              "falling motor smoothing is same as rising source branch");
        exact(motor.state().loc_bagirtime, 10_v, "source falling shift counter branch");
        input.key_space = true;
        motor.update(input);
        require(audio.playing(motor.handles().motor_pati) && motor.state().arac_pati_aktif, "forward handbrake starts skid");
        near(audio.state(motor.handles().motor_pati).volume, 30, "skid volume source constant");
        input.arac_vites_no = 7_v;
        input.time = 0_v;
        event = motor.update(input);
        require(!event.gear_flame_animation && event.flame == 0, "reverse transition does not flame");
        require(audio.playing(motor.handles().motor_geri) && audio.playing(motor.handles().motor_bosta), "reverse overlaps reverse and idle");
        require(!audio.playing(forward_min) && !audio.playing(motor.handles().motor_pati), "reverse skid ignores handbrake below threshold");
        near(audio.state(motor.handles().motor_geri).frequency, number(motor.state().loc_motor_frame * 1.3_v), "reverse pitch scale");
        input.arac_vites_no = 2_v;
        event = motor.update(input);
        require(event.flame == 0 && !event.gear_flame_animation, "reverse-to-forward does not flame");
        input.arac_vites_no = 0_v;
        motor.update(input);
        require(audio.playing(motor.handles().motor_bosta) && !audio.playing(motor.handles().motor_min), "neutral only idles");
        require(!audio.playing(motor.handles().yol_asfalt) && !audio.playing(motor.handles().yol_kum), "neutral stops roads");
        near(audio.state(motor.handles().motor_bosta).volume, 100, "neutral idle volume");
        input.arac_durum_tip = 1_v;
        motor.update(input);
        require(!audio.playing(motor.handles().motor_bosta), "nonzero vehicle state silences neutral");
        input.ghost_yaris_start = false;
        motor.update(input);
        require(audio.playing(motor.handles().motor_max) && audio.playing(motor.handles().motor_bosta), "pre-race neutral takes first gear sound branch");

        // Source wait executes only after the entry frame's gear/smoothing work.
        input.freeze_mode = 1_v;
        input.time = 0.5_v;
        input.arac_vites_ivme = 100_v;
        motor.update(input);
        const auto paused = motor.state();
        require(paused.waiting && !audio.playing(motor.handles().motor_max), "freeze enters wait and stops motor loops");
        require(audio.playing(first_flame), "source suppression does not stop untracked flame one-shot");
        input.arac_vites_no = 4_v;
        input.time = 8_v;
        motor.update(input);
        exact(motor.state().loc_vites_frame, paused.loc_vites_frame, "wait must not smooth");
        require(motor.state().loc_eski_vites == paused.loc_eski_vites, "wait must not advance previous gear");
        input.freeze_mode = 0_v;
        motor.update(input);
        require(!motor.state().waiting && !audio.playing(motor.handles().motor_max), "wait exit skips source else branch until next iteration");
        input.time = 0_v;
        motor.update(input);
        require(audio.playing(motor.handles().motor_max), "iteration following wait resumes sound");
        input.ghost_reset_ok = true;
        motor.update(input);
        require(motor.state().waiting && !audio.playing(motor.handles().motor_max), "reset suppresses");
        input.ghost_reset_ok = false;
        motor.update(input);
        motor.update(input);
        input.gameover_panel_acik = true;
        motor.update(input);
        require(motor.state().waiting && !audio.playing(motor.handles().motor_max), "gameover suppresses");
        owned = motor.handles();
    }
    for (const auto handle : {owned.flame, owned.motor_min, owned.motor_max, owned.motor_bosta,
                              owned.motor_pati, owned.motor_geri, owned.yol_kum, owned.yol_asfalt})
        require(!audio.playing(handle), "destructor stops owned voices");
    for (const auto handle : owned_flames)
        require(!audio.playing(handle), "destructor stops older overlapping flame voices too");
    require(audio.playing(unrelated), "destructor must not stop unrelated voices");
}

void roads_and_boundaries() {
    const auto clips = check_clips();
    Audio audio(8000, Mode::Offline);
    MotorAudio motor(audio, clips);
    MotorInput input;
    input.arac_vites_no = 1_v;
    input.ghost_yaris_start = true;
    input.arac_ivme = -60_v;
    input.time = 0_v;
    motor.update(input);
    const auto asphalt = motor.handles().yol_asfalt;
    near(audio.state(asphalt).volume, 60, "road volume uses absolute supplied acceleration");
    input.toprak_yol_ok = true;
    input.time = 0.5_v;
    motor.update(input);
    const auto sand = motor.handles().yol_kum;
    near(audio.state(asphalt).volume, 47.5, "road fades at 25 normalized units per tick");
    near(audio.state(sand).volume, 60, "sand takes road volume immediately");
    require(audio.playing(asphalt) && audio.playing(sand), "surface transition overlaps independent road voices");
    input.c4_eski_havazaman = 1_v;
    input.time = 2_v;
    motor.update(input);
    require(!audio.playing(asphalt) && audio.playing(sand), "airtime fades both road surfaces independently");
    input.time = 0.5_v;
    motor.update(input);
    require(!audio.playing(sand), "fade reaching zero stops instead of zero-tuning");
    input.c4_eski_havazaman = 0_v;
    input.arac_ivme = 0_v;
    input.time = 0_v;
    motor.update(input);
    near(audio.state(motor.handles().yol_kum).volume, 10, "zero speed road tune preserves initial volume source bug");
    std::array<float, 128> rendered{};
    audio.render(rendered);
    require(audio.playing(motor.handles().yol_kum) && audio.state(motor.handles().yol_kum).loop, "offline road playback wraps actual check PCM");
    require(rendered.front() > 0, "offline native voices produce PCM");

    auto invalid = input;
    invalid.time = -1_v;
    rejects([&] { motor.update(invalid); }, "negative normalized time rejected");
    invalid = input;
    invalid.arac_vites_no = 6_v;
    rejects([&] { motor.update(invalid); }, "unsupported source gear rejected");
    // Non-finite/out-of-range values are rejected when crossing into Var, not
    // by inventing a narrower speed/target/time limit inside the source loop.
    rejects([&] { invalid.motor_ses_ivme = v(std::numeric_limits<double>::quiet_NaN()); },
            "non-finite externally supplied motor target rejected at Var boundary");
    rejects([&] { invalid.arac_ivme = v(std::numeric_limits<double>::infinity()); },
            "non-finite externally supplied speed rejected at Var boundary");
    rejects([&] { invalid.arac_vites_ivme = v(std::numeric_limits<double>::infinity()); },
            "non-finite externally supplied gear target rejected at Var boundary");
    rejects([&] { invalid.time = v(std::numeric_limits<double>::infinity()); },
            "non-finite normalized time rejected at Var boundary");
    rejects([&] { invalid.time = v(std::numeric_limits<double>::max()); },
            "unrepresentable normalized time rejected at Var boundary");
    invalid = input;
    invalid.c4_eski_havazaman = 1400_v;
    rejects([&] { motor.update(invalid); }, "strict recovered airtime copy guard enforced");
    invalid = input;
    invalid.arac_durum_tip = 2_v;
    rejects([&] { motor.update(invalid); }, "unknown vehicle mode rejected");
    invalid = input;
    invalid.freeze_mode = -1_v;
    rejects([&] { motor.update(invalid); }, "negative freeze mode rejected");
    auto missing = clips;
    missing.reverse.reset();
    rejects([&] { MotorAudio rejected(audio, missing); }, "missing actual clip is rejected without substitutes");
    auto bad_clip = std::make_shared<Clip>();
    missing = clips;
    missing.idle = bad_clip;
    rejects([&] { MotorAudio rejected(audio, missing); }, "invalid actual clip rejected before updates");
    bad_clip->sample_rate = 8000;
    bad_clip->channels = 1;
    bad_clip->samples.push_back(std::numeric_limits<float>::quiet_NaN());
    rejects([&] { MotorAudio rejected(audio, missing); }, "non-finite clip PCM rejected at construction");
}

void source_limits() {
    const auto clips = check_clips();
    Audio audio(8000, Mode::Offline);
    MotorAudio motor(audio, clips);
    MotorInput input;
    input.arac_vites_no = 1_v;
    input.ghost_yaris_start = true;
    input.arac_vites_ivme = 100_v;
    input.motor_ses_ivme = 200_v;
    input.arac_ivme = 1000_v; // No invented physics speed ceiling.
    input.time = 8_v; // Original smoothing overshoot clamps; no invented tick cap.
    motor.update(input);
    exact(motor.state().loc_vites_frame, 100_v, "source gear output clamps overshoot to 100");
    exact(motor.state().loc_motor_frame, 200_v, "source motor output clamps overshoot to 200");
    require(audio.playing(motor.handles().motor_pati), "first gear threshold skid without handbrake");
    near(audio.state(motor.handles().motor_bosta).volume, 10, "idle zero tune retains initial volume at full gear frame");
    near(audio.state(motor.handles().yol_asfalt).volume, 100, "road volume cap is not an input speed ceiling");
    near(audio.state(motor.handles().yol_asfalt).frequency, 125, "road pitch caps speed contribution at 300");
    input.arac_vites_no = 7_v;
    input.time = 0_v;
    motor.update(input);
    near(audio.state(motor.handles().motor_geri).volume, number(100_v * 0.2_v), "reverse source volume multiplier");
    near(audio.state(motor.handles().motor_geri).frequency, number(200_v * 1.3_v), "reverse source maximum pitch");
    require(audio.playing(motor.handles().motor_pati), "reverse threshold skid without handbrake");
    input.ghost_yaris_start = false;
    motor.update(input);
    require(!audio.playing(motor.handles().motor_geri) && audio.playing(motor.handles().motor_max),
            "prerace reverse uses first gear branch");
    require(!audio.playing(motor.handles().motor_pati), "prerace suppresses threshold skid");
    input.ghost_yaris_start = true;
    input.arac_vites_no = 2_v;
    motor.update(input);
    near(audio.state(motor.handles().motor_min).frequency, 275, "forward pitch adds three times frame excess over 175");
    exact(motor.state().loc_bagirtime, 10_v, "source throttle release branch sets shout counter");
    input.arac_vites_ivme = -200_v;
    input.motor_ses_ivme = -100_v;
    input.arac_ivme = 0_v;
    input.time = 8_v;
    motor.update(input);
    exact(motor.state().loc_vites_frame, 0_v, "raw gear target is not prematurely clamped before smoothing");
    exact(motor.state().loc_motor_frame, 100_v, "raw motor target is not prematurely clamped before smoothing");
    exact(motor.state().loc_bagirtime, 100_v, "zero speed excludes falling-shift shout reset");
    input.motor_ses_ivme = 50_v; // Explicit source neutral target, below local pitch clamp.
    input.time = 0.5_v;
    motor.update(input);
    exact(motor.state().loc_motor_frame, 100_v, "legitimate raw neutral target 50 accepted");
}

void q10_recurrence() {
    const auto clips = check_clips();
    Audio audio(8000, Mode::Offline);
    MotorAudio motor(audio, clips);
    MotorInput input;
    input.arac_vites_no = 1_v;
    input.ghost_yaris_start = true;
    input.key_cuu = true;
    input.arac_vites_ivme = 100_v;
    input.motor_ses_ivme = 150_v;
    input.time = 0.5_v;
    // ingame_panels.wdl:561,573: independently derived raw opcode-8 recurrence.
    // A float recurrence yields 30/51/65.7 and 100/120/132; a folded
    // Q10 .4 gain yields second motor 122850 instead of source 122875.
    constexpr std::array<std::int32_t, 3> gear_raw{30700, 52195, 67246};
    constexpr std::array<std::int32_t, 3> motor_raw{102400, 122875, 135161};
    constexpr std::array<std::int32_t, 3> gear_delta_raw{102400, 71700, 50205};
    constexpr std::array<std::int32_t, 3> gear_gain_raw{61400, 42991, 30103};
    constexpr std::array<std::int32_t, 3> gear_half_raw{30700, 21495, 15051};
    constexpr std::array<std::int32_t, 3> motor_delta_raw{153600, 51200, 30725};
    constexpr std::array<std::int32_t, 3> motor_gain_raw{122850, 40950, 24573};
    constexpr std::array<std::int32_t, 3> motor_half_raw{61425, 20475, 12286};
    require((0.6_v).raw == 614 && (0.8_v).raw == 819 && (0.5_v).raw == 512,
            "source literal conversion is float then Q10 truncation");
    const auto twice_time = 2_v * input.time;
    require(twice_time.raw == 1024, "source doubled normalized time Q10 operation");
    const auto root = var_sqrt(twice_time);
    require(root.raw == 1024, "source engine sqrt Q10 result");
    for (std::size_t frame = 0; frame < gear_raw.size(); ++frame) {
        const auto gear_delta = input.arac_vites_ivme - motor.state().loc_vites_frame;
        const auto gear_gain = 0.6_v * gear_delta;
        const auto gear_half = gear_gain * 0.5_v;
        require(gear_delta.raw == gear_delta_raw[frame], "source gear target subtraction");
        require(gear_gain.raw == gear_gain_raw[frame], "source gear first multiplication");
        require(gear_half.raw == gear_half_raw[frame], "source gear half multiplication");
        require((gear_half * root).raw == gear_half_raw[frame], "source gear root multiplication");
        const auto motor_delta = input.motor_ses_ivme - motor.state().loc_motor_frame;
        const auto motor_gain = 0.8_v * motor_delta;
        const auto motor_half = motor_gain * 0.5_v;
        require(motor_delta.raw == motor_delta_raw[frame], "source motor target subtraction");
        require(motor_gain.raw == motor_gain_raw[frame], "source motor first multiplication");
        require(motor_half.raw == motor_half_raw[frame], "source motor half multiplication");
        require((motor_half * root).raw == motor_half_raw[frame], "source motor root multiplication");
        motor.update(input);
        require(motor.state().loc_vites_frame.raw == gear_raw[frame], "exact source gear Q10 recurrence");
        require(motor.state().loc_motor_frame.raw == motor_raw[frame], "exact source motor Q10 recurrence");
    }
}
} // namespace

int main() {
    try {
        behavior();
        roads_and_boundaries();
        source_limits();
        q10_recurrence();
        std::cout << "Recovered motor/road audio behavior checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MotorAudioTest: " << error.what() << '\n';
        return 1;
    }
}
