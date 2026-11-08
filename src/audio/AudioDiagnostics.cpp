#include "AudioDiagnostics.hpp"
#include "Audio.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <thread>

namespace c4::audio {
void run_audio_diagnostic(const std::filesystem::path& resource, double seconds, bool loop, std::ostream& output) {
    if (!std::isfinite(seconds) || seconds < 0.1 || seconds > 60)
        throw std::runtime_error("audio diagnostic duration must be 0.1..60 seconds");
    const auto clip = load_clip(resource);
    double square_sum = 0;
    float peak = 0;
    for (const float sample : clip->samples) {
        square_sum += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::abs(sample));
    }
    output << std::setprecision(10) << "audio_resource=" << resource.string()
           << " sample_rate=" << clip->sample_rate << " channels=" << clip->channels
           << " frames=" << clip->frames() << " seconds=" << clip->seconds()
           << " pcm_peak=" << peak << " pcm_rms=" << std::sqrt(square_sum / clip->samples.size()) << '\n';

    // Same mixer as the device, with exact source-position rate transitions and end/loop checks.
    Audio offline(clip->sample_rate, Mode::Offline);
    const Handle probe = offline.play(clip, 100, 0, loop);
    std::array<float, 512> pcm{};
    offline.render(pcm);
    const auto first = offline.state(probe);
    offline.tune(probe, 0, 125, 0);
    offline.render(pcm);
    const auto tuned = offline.state(probe);
    offline.stop(probe);
    if (offline.playing(probe)) throw std::runtime_error("audio stop lifecycle failure");
    output << "offline_probe frame_before_tune=" << first.frame << " frame_after_tune=" << tuned.frame
           << " frequency=" << tuned.frequency << " loop=" << tuned.loop
           << " playing_before_stop=" << tuned.playing << " playing_after_stop=" << offline.playing(probe) << '\n';

    Audio audio;
    const Handle handle = audio.play(clip, 100, 0, loop);
    output << "audio_driver=" << audio.driver() << " device=" << audio.device()
           << " mixer_rate=48000 mixer_channels=2 loop=" << loop
           << " proof=" << (audio.driver() == "dummy" || audio.driver() == "disk" ? "non-audible-backend" : "device-stream-not-listener-proof")
           << '\n';
    const auto begin = std::chrono::steady_clock::now();
    bool tuned_up = false;
    bool tuned_back = false;
    for (;;) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (elapsed >= seconds) break;
        const auto error = audio.error();
        if (!error.empty()) throw std::runtime_error("audio stream failed: " + error);
        if (!tuned_up && elapsed >= seconds / 3) { audio.tune(handle, 0, 125, 0); tuned_up = true; }
        if (!tuned_back && elapsed >= seconds * 2 / 3) { audio.tune(handle, 0, 100, 0); tuned_back = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto error = audio.error();
    if (!error.empty()) throw std::runtime_error("audio stream failed: " + error);
    const auto state = audio.state(handle);
    const auto rendered = audio.rendered_frames();
    audio.stop(handle);
    if (!rendered || audio.playing(handle)) throw std::runtime_error("audio stream/stop lifecycle failure");
    output << "stream_rendered_frames=" << rendered << " source_frame=" << state.frame
           << " source_frequency=" << state.frequency << " playing_before_stop=" << state.playing
           << " playing_after_stop=" << audio.playing(handle) << '\n';
}
} // namespace c4::audio
