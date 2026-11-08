#include "Resources.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace c4::assets {
namespace {

// Independently implemented from observed C4.wrs/kaynak_*.rc4 WRS3 bytes and
// format evidence: https://www.ctpax-x.org/index.php?goto=files&lang=en&show=139
// That source is Apache-2.0 except third-party/ripped assembler. No source or
// assembler is copied here, and no original executable is invoked.
constexpr std::uint64_t max_resource_size = 256ULL * 1024 * 1024;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& reason) {
    throw std::runtime_error("resource '" + path.string() + "': " + reason);
}

bool safe_name(std::string_view name) {
    if (name.empty() || name == "." || name.find("..") != std::string_view::npos) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](unsigned char c) {
        return c < 32 || c == 127 || c == '/' || c == '\\' || c == ':';
    });
}

std::ifstream open_file(const std::filesystem::path& path, std::uint64_t& size) {
    if (path.empty() || path.string().find('\0') != std::string::npos) {
        fail(path, "empty path or embedded NUL");
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail(path, "cannot open file");
    }
    const auto end = input.tellg();
    if (end < std::streampos(0)) {
        fail(path, "cannot determine file size");
    }
    size = static_cast<std::uint64_t>(static_cast<std::streamoff>(end));
    input.seekg(0);
    if (!input) {
        fail(path, "cannot seek to file start");
    }
    return input;
}

void read_exact(std::ifstream& input, void* data, std::size_t size,
                const std::filesystem::path& path, std::string_view what) {
    if (size != 0 && !input.read(static_cast<char*>(data), static_cast<std::streamsize>(size))) {
        fail(path, "truncated or unreadable " + std::string(what));
    }
}

std::uint32_t big_endian(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
         | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}

std::vector<Entry> scan(std::ifstream& input, std::uint64_t file_size,
                        const std::filesystem::path& path) {
    std::array<char, 4> magic{};
    read_exact(input, magic.data(), magic.size(), path, "WRS3 signature");
    if (magic != std::array<char, 4>{'W', 'R', 'S', '3'}) {
        fail(path, "unsupported archive signature (only WRS3 is supported)");
    }
    std::vector<Entry> entries;
    std::uint64_t position = magic.size();
    while (position < file_size) {
        if (file_size - position < 40) {
            fail(path, "truncated entry header at offset " + std::to_string(position));
        }
        std::array<std::uint8_t, 40> header{};
        read_exact(input, header.data(), header.size(), path, "entry header");
        position += header.size();
        // Originals end exactly at EOF. The published reader also stops on an
        // empty name; accept a complete zero terminal header, never a short tail.
        if (header[0] == 0) {
            if (position != file_size || std::any_of(header.begin(), header.end(),
                                                    [](std::uint8_t c) { return c != 0; })) {
                fail(path, "invalid terminal header at offset " + std::to_string(position - 40));
            }
            break;
        }
        std::string name;
        for (std::size_t i = 0; i < 32 && header[i] != 0; ++i) {
            name.push_back(static_cast<char>(header[i] ^ 0xB5));
        }
        if (!safe_name(name)) {
            fail(path, "unsafe entry name at offset " + std::to_string(position - 40));
        }
        const auto packed_size = big_endian(header.data() + 32);
        const auto size = big_endian(header.data() + 36);
        if (packed_size > max_resource_size || size > max_resource_size) {
            fail(path, "entry '" + name + "' exceeds the 256 MiB resource limit");
        }
        if ((packed_size == 0) != (size == 0)) {
            fail(path, "entry '" + name + "' has inconsistent empty sizes");
        }
        if (packed_size > file_size - position) {
            fail(path, "truncated packed data for entry '" + name + "'");
        }
        entries.push_back({std::move(name), packed_size, size, position});
        position += packed_size;
        input.seekg(static_cast<std::streamoff>(position));
        if (!input) {
            fail(path, "cannot seek past entry '" + entries.back().name + "'");
        }
    }
    // Views are created only after vector growth has finished, so names remain
    // stable and duplicate checking does not copy every entry name.
    std::unordered_set<std::string_view> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!names.insert(entry.name).second) {
            fail(path, "duplicate entry name '" + entry.name + "'");
        }
    }
    return entries;
}

std::vector<std::uint8_t> decode(const std::vector<std::uint8_t>& packed,
                               const Entry& entry, const std::filesystem::path& path) {
    std::vector<std::uint8_t> output(entry.size);
    std::array<std::uint8_t, 4096> dictionary;
    dictionary.fill(' ');
    std::size_t cursor = 4078;
    std::size_t source = 0;
    std::size_t target = 0;
    while (target < output.size()) {
        if (source == packed.size()) {
            fail(path, "truncated LZSS flags for entry '" + entry.name + "'");
        }
        const auto flags = packed[source++];
        for (unsigned bit = 0; bit < 8 && target < output.size(); ++bit) {
            if ((flags & (1U << bit)) != 0) {
                if (source == packed.size()) {
                    fail(path, "truncated LZSS literal for entry '" + entry.name + "'");
                }
                const auto value = packed[source++];
                output[target++] = value;
                dictionary[cursor] = value;
                cursor = (cursor + 1) & 4095;
            } else {
                if (packed.size() - source < 2) {
                    fail(path, "truncated LZSS reference for entry '" + entry.name + "'");
                }
                const auto first = packed[source++];
                const auto second = packed[source++];
                std::size_t offset = first | ((second & 0xF0U) << 4);
                const std::size_t length = (second & 0x0FU) + 3;
                if (length > output.size() - target) {
                    fail(path, "LZSS reference exceeds decoded size for entry '" + entry.name + "'");
                }
                for (std::size_t i = 0; i < length; ++i) {
                    const auto value = dictionary[offset];
                    offset = (offset + 1) & 4095;
                    output[target++] = value;
                    dictionary[cursor] = value;
                    cursor = (cursor + 1) & 4095;
                }
            }
        }
    }
    if (source != packed.size()) {
        fail(path, "trailing LZSS data for entry '" + entry.name + "'");
    }
    return output;
}

} // namespace

std::vector<Entry> list_resources(const std::filesystem::path& archive) {
    std::uint64_t size = 0;
    auto input = open_file(archive, size);
    return scan(input, size, archive);
}

std::vector<std::uint8_t> read_resource(const std::filesystem::path& path) {
    const auto text = path.string();
    const auto separator = text.find("::");
    if (separator == std::string::npos) {
        std::uint64_t size = 0;
        auto input = open_file(path, size);
        if (size > max_resource_size) {
            fail(path, "loose file exceeds the 256 MiB resource limit");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        read_exact(input, bytes.data(), bytes.size(), path, "loose file data");
        return bytes;
    }
    const auto member = std::string_view(text).substr(separator + 2);
    if (separator == 0 || !safe_name(member)) {
        fail(path, "expected archive::member with a safe flat member name");
    }
    const std::filesystem::path archive(text.substr(0, separator));
    std::uint64_t size = 0;
    auto input = open_file(archive, size);
    const auto entries = scan(input, size, archive);
    const auto entry = std::find_if(entries.begin(), entries.end(), [member](const Entry& item) {
        return item.name == member;
    });
    if (entry == entries.end()) {
        fail(path, "member not found");
    }
    input.seekg(static_cast<std::streamoff>(entry->offset));
    if (!input) {
        fail(path, "cannot seek to packed member data");
    }
    std::vector<std::uint8_t> packed(entry->packed_size);
    read_exact(input, packed.data(), packed.size(), path, "packed member data");
    return decode(packed, *entry, path);
}

} // namespace c4::assets
