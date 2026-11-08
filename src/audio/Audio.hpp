#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace c4::audio {

class AudioDeviceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Clip {
    int sample_rate = 0;
    int channels = 0;
    std::vector<float> samples;
    std::uint64_t frames() const { return channels > 0 ? samples.size() / channels : 0; }
    double seconds() const { return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0; }
};

// Bounded original loose/WRS3 WAV or Ogg/Vorbis, decoded by libsndfile.
std::shared_ptr<const Clip> load_clip(const std::filesystem::path& resource);
// Validate decoded PCM once before retaining a caller-created clip; play only rechecks its shape.
void validate_clip(const Clip& clip);

using Handle = std::uint64_t;
enum class Mode { Device, Offline };
struct State {
    bool playing = false;
    bool loop = false;
    double frame = 0;
    float volume = 0;
    float frequency = 100;
    float balance = 0;
};

class Audio {
public:
    explicit Audio(int output_rate = 48000, Mode mode = Mode::Device);
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    Mode mode() const;
    int output_rate() const;

    // Acknex snd_play/snd_loop: independent positive handle per voice; zero on 32-voice exhaustion.
    Handle play(std::shared_ptr<const Clip> clip, float volume = 100, float balance = 0, bool loop = false);
    // Acknex snd_tune: each zero parameter leaves that property unchanged.
    void tune(Handle handle, float volume, float frequency, float balance);
    bool playing(Handle handle) const;
    // Counts playing voices, including muted voices.
    std::uint32_t active_voices() const;
    State state(Handle handle) const;
    void stop(Handle handle);
    void stop_all();
    // Global 0..100 output gain; zero mutes without pausing voices.
    void set_master_volume(float volume);
    float master_volume() const;

    // Offline mode only: deterministic interleaved stereo output, same mixer as the SDL device.
    void render(std::span<float> stereo);
    std::string driver() const;
    std::string device() const;
    std::string error() const;
    std::uint64_t rendered_frames() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4::audio
