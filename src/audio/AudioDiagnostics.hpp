#pragma once

#include <filesystem>
#include <iosfwd>

namespace c4::audio {
// Finite actual resource decode + SDL playback + rate/stop lifecycle probe.
// This is an inspection control sequence, not a recovered game timeline.
void run_audio_diagnostic(const std::filesystem::path& resource, double seconds, bool loop, std::ostream& output);
} // namespace c4::audio
