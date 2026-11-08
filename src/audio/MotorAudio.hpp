#pragma once

#include "Audio.hpp"
#include "physics/NativeNumeric.hpp"

#include <array>

namespace c4::audio {

// Caller loads the original resources; no factory or missing-sample substitute.
struct MotorClips {
    std::shared_ptr<const Clip> flame;        // kaynak_snd.rc4: motor_ses_alev.ogg
    std::shared_ptr<const Clip> acceleration; // motor_bas_cek.ogg
    std::shared_ptr<const Clip> idle;         // motor_bosta.ogg
    std::shared_ptr<const Clip> skid;         // motor_pati.ogg
    std::shared_ptr<const Clip> reverse;      // motor_geri.ogg
    std::shared_ptr<const Clip> sand;         // kumyol_ses.ogg
    std::shared_ptr<const Clip> asphalt;      // asfaltyol_ses.ogg
};

struct MotorInput {
    physics::Var arac_vites_no{}; // Recovered assignments: 0,1,2,3,4,5,7 (reverse).
    physics::Var arac_vites_ivme{};
    physics::Var motor_ses_ivme{};
    physics::Var arac_ivme{};
    bool ghost_yaris_start = false;
    bool toprak_yol_ok = false;
    physics::Var c4_eski_havazaman{};
    bool key_cuu = false;
    bool key_space = false;
    physics::Var arac_durum_tip{}; // 0 car, 1 robot.
    bool ghost_reset_ok = false;
    physics::Var freeze_mode{}; // Any positive mode follows the source wait branch.
    bool gameover_panel_acik = false;
    physics::Var time{}; // Original normalized engine ticks, NOT elapsed seconds.
};

struct MotorHandles {
    Handle flame = 0; // Most recently started one-shot; older flames may overlap.
    Handle motor_min = 0;
    Handle motor_max = 0; // Distinct from motor_min even though both use acceleration.
    Handle motor_bosta = 0;
    Handle motor_pati = 0;
    Handle motor_geri = 0;
    Handle yol_kum = 0;
    Handle yol_asfalt = 0;
};

struct MotorState {
    physics::Var loc_eski_vites{};
    physics::Var loc_motorhesap{};
    physics::Var loc_vites_frame{};
    physics::Var loc_motor_frame{}; // Source starts at zero, clamps after its first update.
    physics::Var loc_beklehesap = physics::v(100);
    physics::Var loc_bagirtime{};
    physics::Var loc_kumses_volume{};
    physics::Var loc_asfalt_volume{};
    bool arac_pati_aktif = false;
    bool waiting = false;
};

struct MotorEvents {
    bool gear_flame_animation = false; // Source arac_animate_fx(1,0,0), not physics.
    Handle flame = 0; // This update's one-shot, or zero.
};

// ingame_panels.wdl:496-503,537-763: only motor/road sound behavior.
// Audio must outlive this controller. All seven clips are validated on construction.
// Source targets, speed, airtime and normalized time use canonical Acknex Q10 Var.
// Var construction validates numeric representability; this loop only enforces
// time >= 0 and airtime in [0,1400) (the source copy guard), not output clamps.
// Each call models one source loop frame. Suppression entry smooths before stopping
// loops; updates in its wait do nothing. The release call exits wait and skips the
// source else branch, so ordinary sound work resumes on the following update.
class MotorAudio {
public:
    MotorAudio(Audio& audio, MotorClips clips);
    ~MotorAudio();
    MotorAudio(const MotorAudio&) = delete;
    MotorAudio& operator=(const MotorAudio&) = delete;

    MotorEvents update(const MotorInput& input);
    const MotorHandles& handles() const { return handles_; }
    const MotorState& state() const { return state_; }

private:
    void loop(Handle& handle, const std::shared_ptr<const Clip>& clip, physics::Var volume);
    void skid(bool enabled);
    void stop_loops();
    Handle flame();
    void road(Handle& handle, const std::shared_ptr<const Clip>& clip,
              physics::Var& volume, bool fading, physics::Var speed, physics::Var time);

    Audio& audio_;
    MotorClips clips_;
    MotorHandles handles_;
    MotorState state_;
    std::array<Handle, 32> flames_{};
};

} // namespace c4::audio
