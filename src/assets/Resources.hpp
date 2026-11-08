#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace c4::assets {

struct Entry {
    std::string name;
    std::uint32_t packed_size;
    std::uint32_t size;
    std::uint64_t offset; // Offset of the packed bytes, not the entry header.
};

// WRS3 only. Validates every entry header, name, and payload boundary.
// Throws std::runtime_error on I/O errors or malformed/unsupported resources.
std::vector<Entry> list_resources(const std::filesystem::path& archive);

// A loose file, or "archive::member" (exact, case-sensitive flat member name).
// Packed and decoded member sizes, and loose files, are limited to 256 MiB.
std::vector<std::uint8_t> read_resource(const std::filesystem::path& path);

} // namespace c4::assets
