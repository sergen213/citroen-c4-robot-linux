#include "MotorAudio.hpp"

#include <stdexcept>
#include <utility>

namespace c4::audio {
using namespace c4::physics;
using namespace c4::physics::literals;
namespace {
void validate_input(const MotorInput& input) {
    if ((input.arac_vites_no != 0_v && input.arac_vites_no != 1_v &&
         input.arac_vites_no != 2_v && input.arac_vites_no != 3_v &&
         input.arac_vites_no != 4_v && input.arac_vites_no != 5_v && input.arac_vites_no != 7_v) ||
        (input.arac_durum_tip != 0_v && input.arac_durum_tip != 1_v) || input.freeze_mode < 0_v ||
        input.c4_eski_havazaman < 0_v || input.c4_eski_havazaman >= 1400_v || input.time < 0_v)
        throw std::runtime_error("invalid recovered motor audio input");
}
bool suppressed(const MotorInput& input) {
    return input.ghost_reset_ok || input.freeze_mode > 0_v || input.gameover_panel_acik;
}
}

MotorAudio::MotorAudio(Audio& audio, MotorClips clips) : audio_(audio), clips_(std::move(clips)) {
    for (const auto* clip : {&clips_.flame, &clips_.acceleration, &clips_.idle, &clips_.skid,
                             &clips_.reverse, &clips_.sand, &clips_.asphalt}) {
        if (!*clip) throw std::runtime_error("missing original motor/road audio clip");
        validate_clip(**clip);
    }
}
MotorAudio::~MotorAudio() {
    stop_loops();
    for (const auto handle : flames_) audio_.stop(handle);
}

void MotorAudio::loop(Handle& handle, const std::shared_ptr<const Clip>& clip, Var volume) {
    if (!audio_.playing(handle)) handle = audio_.play(clip, static_cast<float>(number(volume)), 0, true);
}
void MotorAudio::skid(bool enabled) {
    if (enabled) {
        if (!audio_.playing(handles_.motor_pati)) {
            loop(handles_.motor_pati, clips_.skid, 30_v);
            // Source sets this even when the 32-voice allocator returns zero.
            state_.arac_pati_aktif = true;
        }
    } else if (audio_.playing(handles_.motor_pati)) {
        audio_.stop(handles_.motor_pati);
        state_.arac_pati_aktif = false;
    }
}
void MotorAudio::stop_loops() {
    for (const auto handle : {handles_.motor_geri, handles_.motor_max, handles_.motor_min,
                              handles_.motor_bosta, handles_.yol_kum, handles_.yol_asfalt})
        audio_.stop(handle);
    skid(false);
}
Handle MotorAudio::flame() {
    for (auto& handle : flames_) {
        if (audio_.playing(handle)) continue;
        handle = audio_.play(clips_.flame, static_cast<float>(number(100_v)), 0, false);
        handles_.flame = handle;
        return handle;
    }
    // All 32 slots contain live owned flames: the shared mixer cannot allocate another.
    handles_.flame = 0;
    return 0;
}
void MotorAudio::road(Handle& handle, const std::shared_ptr<const Clip>& clip,
                      Var& volume, bool fading, Var speed, Var time) {
    const Var frequency = 75_v + var_min(speed, 300_v) / 6_v;
    if (fading) {
        if (!audio_.playing(handle)) return;
        volume -= 6.25_v / 0.25_v * time;
        if (volume > 0_v)
            audio_.tune(handle, static_cast<float>(number(volume)), static_cast<float>(number(frequency)), 0);
        else audio_.stop(handle);
    } else {
        loop(handle, clip, 10_v);
        volume = var_min(speed, 100_v);
        // Keep snd_tune(0,...) unchanged, including the zero-speed audible-road bug.
        audio_.tune(handle, static_cast<float>(number(volume)), static_cast<float>(number(frequency)), 0);
    }
}

MotorEvents MotorAudio::update(const MotorInput& input) {
    validate_input(input);
    MotorEvents events;
    if (state_.waiting) {
        if (!suppressed(input)) state_.waiting = false;
        return events;
    }

    // Source ingame_panels.wdl:537-579. Both conditional smoothing branches are identical.
    if (state_.loc_eski_vites != input.arac_vites_no) {
        if (input.arac_vites_no != 0_v && state_.loc_eski_vites != 7_v && input.arac_vites_no != 7_v &&
            input.arac_vites_no - state_.loc_eski_vites > 0_v) {
            if (state_.loc_eski_vites != 0_v) events.flame = flame();
            events.gear_flame_animation = true;
        }
        if (state_.loc_eski_vites < input.arac_vites_no) state_.loc_beklehesap = 0_v;
        state_.loc_eski_vites = input.arac_vites_no;
    }
    state_.loc_motorhesap = var_sign(input.arac_vites_ivme - state_.loc_vites_frame);
    const Var root = var_sqrt(2_v * input.time);
    state_.loc_vites_frame += 0.6_v * (input.arac_vites_ivme - state_.loc_vites_frame) * 0.5_v * root;
    state_.loc_vites_frame = var_clamp(state_.loc_vites_frame, 0_v, 100_v);
    state_.loc_motor_frame += 0.8_v * (input.motor_ses_ivme - state_.loc_motor_frame) * 0.5_v * root;
    state_.loc_motor_frame = var_clamp(state_.loc_motor_frame, 100_v, 200_v);

    if (suppressed(input)) {
        // Lines 581-606 do not stop the untracked snd_play flame voice.
        stop_loops();
        state_.loc_kumses_volume = 0_v;
        state_.loc_asfalt_volume = 0_v;
        state_.waiting = true;
        return events;
    }

    if (input.arac_vites_no != 0_v || !input.ghost_yaris_start) {
        const Var speed = var_abs(input.arac_ivme);
        road(handles_.yol_kum, clips_.sand, state_.loc_kumses_volume,
             !input.toprak_yol_ok || input.c4_eski_havazaman >= 1_v, speed, input.time);
        road(handles_.yol_asfalt, clips_.asphalt, state_.loc_asfalt_volume,
             input.toprak_yol_ok || input.c4_eski_havazaman >= 1_v, speed, input.time);

        if (input.arac_vites_no != 1_v && input.arac_vites_no != 7_v && input.ghost_yaris_start) {
            // Lines 643-682: motor_min uses acceleration, never the idle clip.
            audio_.stop(handles_.motor_geri);
            audio_.stop(handles_.motor_bosta);
            skid(input.key_space);
            audio_.stop(handles_.motor_max);
            loop(handles_.motor_min, clips_.acceleration, 10_v);
            const Var pitch = state_.loc_motor_frame +
                var_max(state_.loc_motor_frame - 175_v, 0_v) * 3_v;
            audio_.tune(handles_.motor_min, static_cast<float>(number(50_v)), static_cast<float>(number(pitch)), 0);
            if (((!input.key_cuu && input.arac_ivme > 0_v) ||
                 (state_.loc_beklehesap < 100_v && state_.loc_motorhesap < 0_v)) && input.arac_ivme != 0_v)
                state_.loc_bagirtime = 10_v;
            else {
                state_.loc_bagirtime += 6.25_v / 0.5_v * input.time;
                state_.loc_bagirtime = var_min(state_.loc_bagirtime, 100_v);
            }
        } else if (input.arac_vites_no == 1_v || !input.ghost_yaris_start) {
            // Lines 685-715: prerace overrides gear, including neutral and reverse.
            audio_.stop(handles_.motor_geri);
            loop(handles_.motor_max, clips_.acceleration, 10_v);
            loop(handles_.motor_bosta, clips_.idle, 10_v);
            audio_.stop(handles_.motor_min);
            if (!input.key_cuu && input.arac_ivme > 0_v) {
                audio_.tune(handles_.motor_max, static_cast<float>(number(state_.loc_vites_frame / 10_v)),
                            static_cast<float>(number(state_.loc_motor_frame)), 0);
                state_.loc_bagirtime = 10_v;
            } else {
                audio_.tune(handles_.motor_max,
                    static_cast<float>(number(state_.loc_vites_frame * (state_.loc_bagirtime / 100_v))),
                    static_cast<float>(number(state_.loc_motor_frame)), 0);
                state_.loc_bagirtime += 6.25_v / 0.5_v * input.time;
                state_.loc_bagirtime = var_min(state_.loc_bagirtime, 100_v);
            }
            audio_.tune(handles_.motor_bosta, static_cast<float>(number(100_v - state_.loc_vites_frame)),
                        static_cast<float>(number(state_.loc_motor_frame)), 0);
            skid((state_.loc_vites_frame > 85_v || input.key_space) && input.ghost_yaris_start);
        } else {
            // Lines 718-735: reverse skid uses only the smoothed threshold.
            audio_.stop(handles_.motor_max);
            audio_.stop(handles_.motor_min);
            loop(handles_.motor_geri, clips_.reverse, 10_v);
            loop(handles_.motor_bosta, clips_.idle, 10_v);
            audio_.tune(handles_.motor_geri, static_cast<float>(number(state_.loc_vites_frame * 0.2_v)),
                        static_cast<float>(number(state_.loc_motor_frame * 1.3_v)), 0);
            audio_.tune(handles_.motor_bosta, static_cast<float>(number(100_v - state_.loc_vites_frame)),
                        static_cast<float>(number(state_.loc_motor_frame)), 0);
            skid(state_.loc_vites_frame > 85_v);
        }
    } else {
        // Lines 738-759: running-race neutral, with robot idle suppression.
        state_.loc_kumses_volume = 0_v;
        state_.loc_asfalt_volume = 0_v;
        for (const auto handle : {handles_.yol_asfalt, handles_.yol_kum, handles_.motor_geri,
                                  handles_.motor_max, handles_.motor_min})
            audio_.stop(handle);
        skid(false);
        if (input.arac_durum_tip == 0_v) {
            loop(handles_.motor_bosta, clips_.idle, 100_v);
            audio_.tune(handles_.motor_bosta, static_cast<float>(number(100_v)), static_cast<float>(number(100_v)), 0);
        } else audio_.stop(handles_.motor_bosta);
        state_.loc_bagirtime = 10_v;
    }
    state_.loc_beklehesap += 6.25_v / 0.5_v * input.time;
    state_.loc_beklehesap = var_min(state_.loc_beklehesap, 100_v);
    return events;
}
} // namespace c4::audio
