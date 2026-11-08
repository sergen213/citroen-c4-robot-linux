#include "InputScenario.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace c4::game {
namespace {
struct HeldKey {
    std::string_view name;
    bool InputSnapshot::* member;
};
constexpr HeldKey held_keys[] = {
    {"Up", &InputSnapshot::key_cuu}, {"Down", &InputSnapshot::key_cud},
    {"Left", &InputSnapshot::key_cul}, {"Right", &InputSnapshot::key_cur},
    {"D", &InputSnapshot::key_d}, {"S", &InputSnapshot::key_s},
    {"Space", &InputSnapshot::key_space}, {"C", &InputSnapshot::key_c},
    {"Esc", &InputSnapshot::key_esc}, {"F1", &InputSnapshot::key_f1},
    {"MouseRight", &InputSnapshot::mouse_right},
};
struct CommandKey {
    std::string_view name;
    UiCommand command;
};
constexpr CommandKey command_keys[] = {
    {"Play", UiCommand::play}, {"Resume", UiCommand::resume},
    {"Restart", UiCommand::restart}, {"Menu", UiCommand::menu},
    {"Help", UiCommand::help}, {"CloseHelp", UiCommand::close_help},
    {"Quit", UiCommand::quit}, {"SelectLocal", UiCommand::select_local},
    {"SelectWeb", UiCommand::select_web},
};
bool whitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}
[[noreturn]] void fail(std::size_t line, std::string_view reason) {
    throw std::runtime_error("input scenario line " + std::to_string(line) + ": " + std::string(reason));
}
} // namespace

InputScenario InputScenario::load(const std::filesystem::path& path) {
    // Seek before allocation so even a maliciously long physical line is bounded.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open input scenario: " + path.string());
    const auto end = file.tellg();
    if (end < 0 || end > static_cast<std::streamoff>(max_bytes))
        throw std::runtime_error("input scenario exceeds byte limit or is not seekable");
    std::string bytes(static_cast<std::size_t>(end), '\0');
    file.seekg(0);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || file.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("cannot read complete input scenario: " + path.string());
    if (file.bad()) throw std::runtime_error("input scenario I/O error: " + path.string());

    InputScenario result;
    std::size_t offset = 0, line_number = 0;
    while (offset < bytes.size()) {
        if (++line_number > max_lines) fail(line_number, "too many lines");
        const auto newline = bytes.find('\n', offset);
        const auto line_end = newline == std::string::npos ? bytes.size() : newline;
        const std::string_view line(bytes.data() + offset, line_end - offset);
        if (line.size() > max_line_bytes) fail(line_number, "line too long");
        offset = newline == std::string::npos ? bytes.size() : newline + 1;

        std::array<std::string_view, 3> fields{};
        std::size_t position = 0, count = 0;
        while (position < line.size()) {
            while (position < line.size() && whitespace(line[position])) ++position;
            if (position == line.size()) break;
            if (count == fields.size()) fail(line_number, "expected frame key value");
            const auto start = position;
            while (position < line.size() && !whitespace(line[position])) ++position;
            fields[count++] = line.substr(start, position - start);
        }
        if (count == 0) continue;
        if (count != fields.size()) fail(line_number, "expected frame key value");

        Event event;
        const auto frame_text = fields[0];
        for (const char c : frame_text)
            if (c < '0' || c > '9') fail(line_number, "frame must be unsigned decimal");
        const auto parsed_frame = std::from_chars(frame_text.data(), frame_text.data() + frame_text.size(), event.frame);
        if (parsed_frame.ec != std::errc{} || parsed_frame.ptr != frame_text.data() + frame_text.size() || event.frame > max_frame)
            fail(line_number, "frame exceeds limit");
        if (!result.events_.empty() && event.frame < result.events_.back().frame)
            fail(line_number, "frame regression");

        for (std::size_t i = 0; i < std::size(held_keys); ++i) {
            if (fields[1] == held_keys[i].name) {
                event.key = held_keys[i].member;
                event.key_mask = 1u << i;
                break;
            }
        }
        if (event.key) {
            if (fields[2] != "0" && fields[2] != "1") fail(line_number, "held input must be 0 or 1");
            event.value = fields[2] == "1" ? physics::v(1) : Var{};
        } else if (fields[1] == "mouse_force_x") {
            auto number = fields[2];
            if (number.front() == '+') {
                number.remove_prefix(1);
                if (number.empty() || number.front() == '-' || number.front() == '+') fail(line_number, "invalid mouse force");
            }
            double value{};
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !std::isfinite(value))
                fail(line_number, "mouse force must be finite");
            if (std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
                fail(line_number, "mouse force exceeds source Var range");
            try {
                event.value = physics::v(value);
            } catch (const std::out_of_range&) {
                fail(line_number, "mouse force exceeds source Var range");
            }
        } else {
            for (const auto& key : command_keys)
                if (fields[1] == key.name) { event.command = key.command; break; }
            if (event.command == UiCommand::none) fail(line_number, "unknown input name");
            if (fields[2] != "1") fail(line_number, "UI command must be 1");
        }
        result.events_.push_back(event);
    }
    if (result.events_.empty()) throw std::runtime_error("input scenario has no events");
    return result;
}

InputSnapshot InputScenario::frame(std::uint64_t index) {
    if (index > max_frame || (has_frame_ && index < frame_index_))
        throw std::invalid_argument("input scenario playback frame must be bounded and nondecreasing");
    if (has_frame_ && index == frame_index_) return input_;
    input_.ui_command = UiCommand::none;
    input_.mouse_force_x = Var{};
    input_.key_esc_pressed = false;
    input_.key_f1_pressed = false;
    std::uint32_t presses{};
    while (cursor_ < events_.size() && events_[cursor_].frame <= index) {
        const auto& event = events_[cursor_++];
        if (event.key) {
            const bool held = event.value.raw != 0;
            if (event.frame == index && held && !(physical_keys_ & event.key_mask)) {
                if (event.key != &InputSnapshot::mouse_right) presses |= event.key_mask;
                if (event.key == &InputSnapshot::key_esc) input_.key_esc_pressed = true;
                else if (event.key == &InputSnapshot::key_f1) input_.key_f1_pressed = true;
            }
            if (held) physical_keys_ |= event.key_mask;
            else physical_keys_ &= ~event.key_mask;
        } else if (event.frame == index) {
            if (event.command != UiCommand::none) input_.ui_command = event.command;
            else input_.mouse_force_x = event.value;
        }
    }
    const auto published = physical_keys_ | presses;
    for (std::size_t i = 0; i < std::size(held_keys); ++i)
        input_.*(held_keys[i].member) = (published & (1u << i)) != 0;
    has_frame_ = true;
    frame_index_ = index;
    return input_;
}
} // namespace c4::game
