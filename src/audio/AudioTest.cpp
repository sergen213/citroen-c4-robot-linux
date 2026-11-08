#include "Audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& diagnostic) {
    if (!condition) throw std::runtime_error(diagnostic);
}

void near(double actual, double expected, const std::string& diagnostic) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.000001,
            diagnostic + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}

template<class F> void rejects(F operation, const std::string& diagnostic) {
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error(diagnostic + ": expected std::runtime_error");
}

struct Directory {
    std::filesystem::path path;
    Directory() {
        auto pattern = (std::filesystem::temp_directory_path() / "c4-audio-test-XXXXXX").string();
        const auto created = ::mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("audio fixture mkdtemp failed");
        path = created;
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void little(std::vector<std::uint8_t>& bytes, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void text(std::vector<std::uint8_t>& bytes, const std::string& value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> wav() {
    // Generated unit-test input only; these samples are not production game audio.
    std::vector<std::uint8_t> bytes;
    text(bytes, "RIFF");
    little(bytes, 68, 4); // 36-byte header plus 16 mono 16-bit frames.
    text(bytes, "WAVEfmt ");
    little(bytes, 16, 4);
    little(bytes, 1, 2); // PCM.
    little(bytes, 1, 2); // Mono.
    little(bytes, 8000, 4);
    little(bytes, 16000, 4);
    little(bytes, 2, 2);
    little(bytes, 16, 2);
    text(bytes, "data");
    little(bytes, 32, 4);
    for (unsigned frame = 1; frame <= 16; ++frame) little(bytes, frame * 1024, 2);
    return bytes;
}

std::vector<std::uint8_t> wrs(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint8_t> packed;
    for (std::size_t start = 0; start < bytes.size(); start += 8) {
        const auto count = std::min<std::size_t>(8, bytes.size() - start);
        packed.push_back(static_cast<std::uint8_t>((1U << count) - 1));
        packed.insert(packed.end(), bytes.begin() + start, bytes.begin() + start + count);
    }
    std::vector<std::uint8_t> result{'W', 'R', 'S', '3'};
    std::array<std::uint8_t, 32> name{};
    const std::string member = "fixture.wav";
    for (std::size_t i = 0; i < member.size(); ++i) name[i] = static_cast<std::uint8_t>(member[i]) ^ 0xB5;
    result.insert(result.end(), name.begin(), name.end());
    for (const auto size : {packed.size(), bytes.size()})
        for (int shift = 24; shift >= 0; shift -= 8)
            result.push_back(static_cast<std::uint8_t>(size >> shift));
    result.insert(result.end(), packed.begin(), packed.end());
    return result;
}

void save(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(stream.good(), "cannot write audio fixture " + path.string());
}

std::vector<float> render(c4::audio::Audio& audio, std::size_t frames) {
    std::vector<float> samples(frames * 2, 123.0F);
    audio.render(samples);
    return samples;
}

void silence(const std::vector<float>& samples, const std::string& diagnostic) {
    for (const auto sample : samples) near(sample, 0, diagnostic);
}

void playback(const std::shared_ptr<const c4::audio::Clip>& clip) {
    using namespace c4::audio;
    Audio audio(16000, Mode::Offline);
    require(audio.active_voices() == 0, "fresh mixer must have no active voices");
    const auto once = audio.play(clip, 100, -100);
    require(once > 0 && audio.playing(once), "one-shot must start with a positive live handle");
    const auto initial = render(audio, 31);
    near(initial.front(), clip->samples.front(), "first left PCM sample");
    for (std::size_t i = 1; i < initial.size(); i += 2) near(initial[i], 0, "left pan must mute right");
    require(audio.playing(once), "one-shot ended before exact duration");
    near(audio.state(once).frame, 15.5, "sample-rate ratio must advance fractional source frames");
    render(audio, 1);
    require(!audio.playing(once), "one-shot must end at exactly 32 output frames");
    require(audio.active_voices() == 0, "ended one-shot must not count as active");
    silence(render(audio, 2), "ended one-shot must be silent");

    const auto loop = audio.play(clip, 50, 100, true);
    require(loop > 0 && loop != once, "ended slot must receive a new positive handle");
    require(audio.active_voices() == 1, "recycled ended slot must count its new voice");
    require(audio.state(loop).loop, "loop state must be recorded");
    const auto wrapped = render(audio, 38);
    require(audio.playing(loop), "loop must remain live past wrap");
    near(audio.state(loop).frame, 3, "loop position must wrap in source frames");
    for (std::size_t i = 0; i < wrapped.size(); i += 2) near(wrapped[i], 0, "right pan must mute left");
    near(wrapped[1], clip->samples[0] * 0.5, "right pan volume scaling");
    near(wrapped[65], clip->samples[0] * 0.5, "loop must replay first PCM frame at wrap");
    audio.tune(once, 1, 1000, -100);
    audio.stop(once);
    require(audio.playing(loop), "stale ended handle must not stop recycled slot");
    require(audio.active_voices() == 1, "stale ended handle must preserve active voice count");
    near(audio.state(loop).volume, 50, "stale ended handle must not tune recycled slot");
    audio.stop_all();
    require(!audio.playing(loop), "stop_all must stop loop");
    require(audio.active_voices() == 0, "stop_all must clear active voice count");
    silence(render(audio, 1), "stop_all must silence mixer");
    require(audio.rendered_frames() == 73, "rendered output frame counter");
}

void tuning(const std::shared_ptr<const c4::audio::Clip>& clip) {
    using namespace c4::audio;
    Audio audio(16000, Mode::Offline);
    const auto handle = audio.play(clip, 25, -100, true);
    render(audio, 4);
    near(audio.state(handle).frame, 2, "initial normal-speed cursor");
    audio.tune(handle, 50, 200, 100);
    const auto changed = audio.state(handle);
    near(changed.frame, 2, "frequency tune must preserve cursor");
    near(changed.volume, 50, "volume tune");
    near(changed.frequency, 200, "frequency tune");
    near(changed.balance, 100, "balance tune");
    const auto faster = render(audio, 3);
    near(faster[0], 0, "tuned right pan must mute left");
    near(faster[1], clip->samples[2] * 0.5, "frequency tune must resume existing PCM position");
    near(audio.state(handle).frame, 5, "200 percent advances one source frame per output frame");
    audio.tune(handle, 0, 50, 0);
    near(audio.state(handle).frame, 5, "slower tune must preserve cursor");
    render(audio, 4);
    near(audio.state(handle).frame, 6, "50 percent advances quarter source frames");
    audio.tune(handle, 0, 0, 0);
    const auto unchanged = audio.state(handle);
    near(unchanged.frame, 6, "zero tune must preserve cursor");
    near(unchanged.volume, 50, "zero volume must retain prior value");
    near(unchanged.frequency, 50, "zero frequency must retain prior value");
    near(unchanged.balance, 100, "zero balance must retain prior value");
    const auto slower = render(audio, 4);
    near(slower[0], 0, "zero balance must retain output pan");
    near(slower[1], clip->samples[6] * 0.5, "zero volume must retain output gain");
    near(audio.state(handle).frame, 7, "zero frequency must retain output advance");

    near(audio.master_volume(), 100, "default master volume");
    audio.set_master_volume(0);
    silence(render(audio, 4), "zero master volume must silence output");
    require(audio.playing(handle), "master mute must preserve live voice");
    require(audio.active_voices() == 1, "master mute must still count active voices");
    near(audio.state(handle).frame, 8, "master mute must advance source cursor");
    near(audio.state(handle).volume, 50, "master mute must preserve voice gain");
    audio.set_master_volume(100);
    const auto restored = render(audio, 4);
    near(restored[1], clip->samples[8] * 0.5, "restored master must resume same handle PCM");
    require(audio.playing(handle), "master restoration must preserve live voice");
    for (const float volume : {-1.0F, 101.0F, std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()})
        rejects([&] { audio.set_master_volume(volume); }, "invalid master volume");
    near(audio.master_volume(), 100, "invalid master volume must preserve gain");

    for (const float frequency : {-1.0F, 9.0F, 1001.0F, std::numeric_limits<float>::infinity()})
        rejects([&] { audio.tune(handle, 0, frequency, 0); }, "invalid nonzero tune frequency");
    rejects([&] { audio.tune(handle, std::numeric_limits<float>::quiet_NaN(), 0, 0); }, "NaN tune volume");
    rejects([&] { audio.play(clip, std::numeric_limits<float>::quiet_NaN()); }, "NaN play volume");
    for (const float volume : {-1.0F, 101.0F})
        rejects([&] { audio.play(clip, volume); }, "out-of-range play volume");
    for (const float balance : {-101.0F, 101.0F})
        rejects([&] { audio.play(clip, 100, balance); }, "out-of-range play balance");
    audio.tune(handle, 0, 10, 0);
    near(audio.state(handle).frequency, 10, "minimum tune frequency accepted");
    audio.tune(handle, 0, 1000, 0);
    near(audio.state(handle).frequency, 1000, "maximum tune frequency accepted");
}

void handles(const std::shared_ptr<const c4::audio::Clip>& clip) {
    using namespace c4::audio;
    Audio audio(16000, Mode::Offline);
    const auto left = audio.play(clip, 25, -100, true);
    const auto right = audio.play(clip, 50, 100, true);
    require(left > 0 && right > 0 && left != right, "concurrent handles must be independent and positive");
    require(audio.active_voices() == 2, "independent handles must count as two active voices");
    const auto both = render(audio, 1);
    near(both[0], clip->samples[0] * 0.25, "independent left voice gain");
    near(both[1], clip->samples[0] * 0.5, "independent right voice gain");
    audio.stop(left);
    audio.stop(left);
    require(!audio.playing(left) && audio.playing(right), "stopping one handle must preserve another");
    require(audio.active_voices() == 1, "repeated stop must remove only one active voice");
    const auto remaining = render(audio, 1);
    near(remaining[0], 0, "stopped voice must not mix");
    require(remaining[1] > 0, "independent voice must keep mixing");
    const auto recycled = audio.play(clip, 75, -100, true);
    require(recycled > 0 && recycled != left && recycled != right, "stopped slot must get a fresh handle");
    audio.stop(left);
    audio.tune(left, 1, 1000, 100);
    audio.stop(0);
    audio.stop(std::numeric_limits<Handle>::max());
    audio.tune(0, 1, 1000, 100);
    require(!audio.playing(0) && !audio.playing(left), "invalid and stale handles must not become live");
    require(audio.playing(recycled), "stale stop must not kill replacement");
    require(audio.active_voices() == 2, "recycled slot and stale handles must preserve two active voices");
    near(audio.state(recycled).volume, 75, "stale tune must not change replacement volume");
    near(audio.state(recycled).frequency, 100, "stale tune must not change replacement frequency");
    near(audio.state(recycled).balance, -100, "stale tune must not change replacement balance");
    audio.stop_all();

    std::array<Handle, 32> voices{};
    for (std::size_t i = 0; i < voices.size(); ++i) {
        voices[i] = audio.play(clip, 1, 0, true);
        require(voices[i] > 0 && audio.playing(voices[i]), "all 32 voice slots must be available");
        require(std::find(voices.begin(), voices.begin() + i, voices[i]) == voices.begin() + i,
                "voice handles must be unique");
    }
    require(audio.play(clip) == 0, "33rd concurrent voice must report exhaustion");
    for (const auto voice : voices) require(audio.playing(voice), "exhaustion must not evict existing voice");
    require(audio.active_voices() == 32, "exhaustion must preserve all 32 active voices");
    const auto old = voices[7];
    audio.stop(old);
    require(audio.active_voices() == 31, "stopping a full mixer voice must reduce active count");
    const auto replacement = audio.play(clip, 25, 100, true);
    require(replacement > 0 && std::find(voices.begin(), voices.end(), replacement) == voices.end(),
            "exhausted mixer must recycle stopped slot with fresh handle");
    audio.stop(old);
    audio.tune(old, 1, 1000, -100);
    require(audio.playing(replacement), "stale exhausted-slot handle must not stop replacement");
    near(audio.state(replacement).volume, 25, "stale exhausted-slot handle must not tune replacement");
    require(audio.play(clip) == 0, "recycled slot must restore exact capacity");
    require(audio.active_voices() == 32, "replacement must restore full active voice count");
    audio.stop_all();
    for (const auto voice : voices) require(!audio.playing(voice), "stop_all must invalidate every voice");
    require(!audio.playing(replacement), "stop_all must stop replacement");
    require(audio.active_voices() == 0, "stop_all must remove every active voice");
    silence(render(audio, 1), "all stopped voices must be silent");
}

} // namespace

int main() {
    try {
        Directory directory;
        const auto bytes = wav();
        const auto loose = directory.path / "fixture.wav";
        const auto archive = directory.path / "fixture.wrs";
        save(loose, bytes);
        save(archive, wrs(bytes));
        const auto clip = c4::audio::load_clip(loose);
        const auto archived = c4::audio::load_clip(archive.string() + "::fixture.wav");
        require(clip && archived, "decode must return clips");
        require(clip->sample_rate == 8000 && clip->channels == 1 && clip->frames() == 16,
                "WAV decode must preserve declared rate, channels and frame count");
        near(clip->seconds(), 16.0 / 8000, "WAV decoded duration");
        require(archived->sample_rate == clip->sample_rate && archived->channels == clip->channels &&
                archived->frames() == clip->frames() && archived->samples == clip->samples,
                "loose and literal-only WRS3 decode must be identical");
        for (std::size_t i = 0; i < clip->samples.size(); ++i)
            near(clip->samples[i], static_cast<double>(i + 1) / 32, "16-bit PCM normalized sample");

        const auto bad = directory.path / "malformed.wav";
        save(bad, {'n', 'o', 't', 'a', 'u', 'd', 'i', 'o'});
        rejects([&] { c4::audio::load_clip(bad); }, "malformed audio decode");
        save(bad, {});
        rejects([&] { c4::audio::load_clip(bad); }, "empty audio decode");
        save(bad, std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 12));
        rejects([&] { c4::audio::load_clip(bad); }, "truncated WAV header decode");
        save(archive, wrs({'n', 'o', 't', 'a', 'u', 'd', 'i', 'o'}));
        rejects([&] { c4::audio::load_clip(archive.string() + "::fixture.wav"); }, "malformed archived audio decode");

        playback(clip);
        tuning(clip);
        handles(clip);
        std::cout << "audio behavior checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "audio behavior check failed: " << error.what() << '\n';
        return 1;
    }
}
