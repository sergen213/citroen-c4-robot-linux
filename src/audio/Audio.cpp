#include "Audio.hpp"
#include "Resources.hpp"

#include <SDL3/SDL.h>
#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace c4::audio {
namespace {
constexpr std::uint64_t max_pcm_bytes = 256ULL * 1024 * 1024;
struct Memory {
    std::span<const std::uint8_t> bytes;
    sf_count_t position = 0;
};
sf_count_t length(void* data) { return static_cast<sf_count_t>(static_cast<Memory*>(data)->bytes.size()); }
sf_count_t seek(sf_count_t offset, int origin, void* data) {
    auto& memory = *static_cast<Memory*>(data);
    const sf_count_t end = length(data);
    sf_count_t base = 0;
    if (origin == SEEK_CUR) base = memory.position;
    else if (origin == SEEK_END) base = end;
    else if (origin != SEEK_SET) return -1;
    if (offset < -base || offset > end - base) return -1;
    memory.position = base + offset;
    return memory.position;
}
sf_count_t read(void* destination, sf_count_t count, void* data) {
    auto& memory = *static_cast<Memory*>(data);
    if (count <= 0) return 0;
    const auto size = std::min(count, length(data) - memory.position);
    if (size) std::memcpy(destination, memory.bytes.data() + memory.position, static_cast<std::size_t>(size));
    memory.position += size;
    return size;
}
sf_count_t tell(void* data) { return static_cast<Memory*>(data)->position; }
void range(float value, float low, float high, const char* name) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::runtime_error(std::string("audio ") + name + " is outside its supported range");
}
void valid_clip(const Clip& clip) {
    if (clip.sample_rate < 1000 || clip.sample_rate > 384000 ||
        (clip.channels != 1 && clip.channels != 2) || clip.samples.empty() ||
        clip.samples.size() % clip.channels || clip.samples.size() > max_pcm_bytes / sizeof(float))
        throw std::runtime_error("invalid mono/stereo audio clip");
}
}

void validate_clip(const Clip& clip) {
    valid_clip(clip);
    for (const float sample : clip.samples)
        if (!std::isfinite(sample)) throw std::runtime_error("non-finite audio sample");
}

std::shared_ptr<const Clip> load_clip(const std::filesystem::path& resource) {
    const auto bytes = assets::read_resource(resource);
    Memory memory{bytes};
    SF_VIRTUAL_IO io{length, seek, read, nullptr, tell};
    SF_INFO info{};
    std::unique_ptr<SNDFILE, decltype(&sf_close)> file(sf_open_virtual(&io, SFM_READ, &info, &memory), sf_close);
    if (!file) throw std::runtime_error(resource.string() + ": audio decode: " + sf_strerror(nullptr));
    const int format = info.format & SF_FORMAT_TYPEMASK;
    if ((format != SF_FORMAT_WAV && format != SF_FORMAT_WAVEX &&
         !(format == SF_FORMAT_OGG && (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_VORBIS)) ||
        (info.channels != 1 && info.channels != 2) || info.samplerate < 1000 || info.samplerate > 384000 ||
        info.frames <= 0 || static_cast<std::uint64_t>(info.frames) > max_pcm_bytes / sizeof(float) / info.channels)
        throw std::runtime_error(resource.string() + ": unsupported or oversized WAV/Ogg audio");
    auto clip = std::make_shared<Clip>();
    clip->sample_rate = info.samplerate;
    clip->channels = info.channels;
    clip->samples.resize(static_cast<std::size_t>(info.frames) * info.channels);
    const auto frames = sf_readf_float(file.get(), clip->samples.data(), info.frames);
    if (frames != info.frames || sf_error(file.get()) != SF_ERR_NO_ERROR)
        throw std::runtime_error(resource.string() + ": truncated or corrupt audio frames");
    validate_clip(*clip);
    return clip;
}

struct Audio::Impl {
    struct Voice {
        Handle handle = 0;
        std::shared_ptr<const Clip> clip;
        State state;
    };
    std::array<Voice, 32> voices;
    mutable std::mutex mutex;
    Handle next_handle = 1;
    int output_rate;
    Mode mode;
    SDL_AudioStream* stream = nullptr;
    bool initialized = false;
    std::string driver_name;
    std::string device_name;
    std::array<char, 256> failure{};
    std::uint64_t frames = 0;
    float master = 100;

    Impl(int rate, Mode selected) : output_rate(rate), mode(selected) {}
    ~Impl() {
        if (stream) SDL_DestroyAudioStream(stream);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    Voice* find(Handle handle) {
        if (!handle) return nullptr;
        for (auto& voice : voices) if (voice.handle == handle) return &voice;
        return nullptr;
    }
    void mix(std::span<float> stereo) {
        std::fill(stereo.begin(), stereo.end(), 0.f);
        const std::size_t count = stereo.size() / 2;
        for (auto& voice : voices) {
            auto& state = voice.state;
            if (!state.playing) continue;
            const auto& clip = *voice.clip;
            const auto source_frames = clip.frames();
            const double step = static_cast<double>(clip.sample_rate) / output_rate * state.frequency / 100.;
            const float gain = state.volume / 100.f;
            const float left = gain * (state.balance > 0 ? 1 - state.balance / 100.f : 1);
            const float right = gain * (state.balance < 0 ? 1 + state.balance / 100.f : 1);
            for (std::size_t frame = 0; frame < count; ++frame) {
                const auto index = static_cast<std::uint64_t>(state.frame);
                const auto next = index + 1 < source_frames ? index + 1 : (state.loop ? 0 : index);
                const float fraction = static_cast<float>(state.frame - index);
                for (int channel = 0; channel < 2; ++channel) {
                    const int source_channel = clip.channels == 1 ? 0 : channel;
                    const float a = clip.samples[index * clip.channels + source_channel];
                    const float b = clip.samples[next * clip.channels + source_channel];
                    stereo[frame * 2 + channel] += (a + (b - a) * fraction) * (channel ? right : left);
                }
                state.frame += step;
                if (state.frame >= source_frames) {
                    if (state.loop) state.frame = std::fmod(state.frame, static_cast<double>(source_frames));
                    else {
                        state.frame = static_cast<double>(source_frames);
                        state.playing = false;
                        break;
                    }
                }
            }
        }
        const float master_gain = master / 100.f;
        for (auto& sample : stereo) sample = std::clamp(sample * master_gain, -1.f, 1.f);
        frames += count;
    }
    static void SDLCALL callback(void* data, SDL_AudioStream* destination, int additional, int) {
        auto& self = *static_cast<Impl*>(data);
        std::array<float, 2048> buffer;
        std::lock_guard lock(self.mutex);
        while (additional > 0 && !self.failure[0]) {
            const auto needed_frames = (static_cast<std::size_t>(additional) + 2 * sizeof(float) - 1) / (2 * sizeof(float));
            const auto sample_count = std::min(buffer.size() / 2, needed_frames) * 2;
            self.mix(std::span<float>(buffer.data(), sample_count));
            const auto bytes = static_cast<int>(sample_count * sizeof(float));
            if (!SDL_PutAudioStreamData(destination, buffer.data(), bytes)) {
                SDL_strlcpy(self.failure.data(), SDL_GetError(), self.failure.size());
                for (auto& voice : self.voices) voice.state.playing = false;
                return;
            }
            additional -= bytes;
        }
    }
};

Audio::Audio(int output_rate, Mode mode) : impl_(std::make_unique<Impl>(output_rate, mode)) {
    if (output_rate < 1000 || output_rate > 384000) throw std::runtime_error("invalid audio output sample rate");
    if (mode == Mode::Offline) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw AudioDeviceError(std::string("audio initialization: ") + SDL_GetError());
    impl_->initialized = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, output_rate};
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, impl_.get());
    if (!impl_->stream) throw AudioDeviceError(std::string("audio output unavailable: ") + SDL_GetError());
    const auto* driver_name = SDL_GetCurrentAudioDriver();
    const auto* device_name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(impl_->stream));
    impl_->driver_name = driver_name ? driver_name : "unknown";
    impl_->device_name = device_name ? device_name : "unknown";
    if (!SDL_ResumeAudioStreamDevice(impl_->stream)) throw AudioDeviceError(std::string("audio output resume: ") + SDL_GetError());
}
Audio::~Audio() = default;
Mode Audio::mode() const { return impl_->mode; }
int Audio::output_rate() const { return impl_->output_rate; }

Handle Audio::play(std::shared_ptr<const Clip> clip, float volume, float balance, bool loop) {
    if (!clip) throw std::runtime_error("null audio clip");
    valid_clip(*clip);
    range(volume, 0, 100, "volume"); range(balance, -100, 100, "balance");
    std::lock_guard lock(impl_->mutex);
    if (impl_->failure[0]) throw AudioDeviceError(std::string("audio output: ") + impl_->failure.data());
    if (!impl_->next_handle) throw std::runtime_error("audio handle space exhausted");
    const Handle handle = impl_->next_handle++;
    for (auto& voice : impl_->voices) {
        if (voice.state.playing) continue;
        voice.handle = handle;
        voice.clip = std::move(clip);
        voice.state = {true, loop, 0, volume, 100, balance};
        return voice.handle;
    }
    return 0;
}
void Audio::tune(Handle handle, float volume, float frequency, float balance) {
    range(volume, 0, 100, "volume"); range(balance, -100, 100, "balance");
    if (frequency != 0) range(frequency, 10, 1000, "frequency");
    std::lock_guard lock(impl_->mutex);
    auto* voice = impl_->find(handle);
    if (!voice || !voice->state.playing) return;
    if (volume) voice->state.volume = volume;
    if (frequency) voice->state.frequency = frequency;
    if (balance) voice->state.balance = balance;
}
bool Audio::playing(Handle handle) const { return state(handle).playing; }
std::uint32_t Audio::active_voices() const {
    std::lock_guard lock(impl_->mutex);
    std::uint32_t count = 0;
    for (const auto& voice : impl_->voices)
        if (voice.state.playing) ++count;
    return count;
}
State Audio::state(Handle handle) const {
    std::lock_guard lock(impl_->mutex);
    const auto* voice = impl_->find(handle);
    return voice ? voice->state : State{};
}
void Audio::stop(Handle handle) {
    std::lock_guard lock(impl_->mutex);
    if (auto* voice = impl_->find(handle)) voice->state.playing = false;
}
void Audio::stop_all() {
    std::lock_guard lock(impl_->mutex);
    for (auto& voice : impl_->voices) voice.state.playing = false;
}
void Audio::set_master_volume(float volume) {
    range(volume, 0, 100, "master volume");
    std::lock_guard lock(impl_->mutex);
    impl_->master = volume;
}
float Audio::master_volume() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->master;
}
void Audio::render(std::span<float> stereo) {
    if (impl_->mode != Mode::Offline) throw std::runtime_error("manual audio render requires offline mode");
    if (stereo.size() % 2) throw std::runtime_error("audio output requires complete stereo frames");
    std::lock_guard lock(impl_->mutex);
    impl_->mix(stereo);
}
std::string Audio::driver() const { return impl_->driver_name; }
std::string Audio::device() const { return impl_->device_name; }
std::string Audio::error() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->failure.data();
}
std::uint64_t Audio::rendered_frames() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->frames;
}
} // namespace c4::audio
