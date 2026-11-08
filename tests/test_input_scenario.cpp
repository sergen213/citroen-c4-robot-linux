#include "game/InputScenario.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using c4::game::InputScenario;
using c4::game::InputSnapshot;
using c4::game::UiCommand;

constexpr std::array<std::pair<std::string_view, bool InputSnapshot::*>, 11> keys{{
    {"Up", &InputSnapshot::key_cuu}, {"Down", &InputSnapshot::key_cud},
    {"Left", &InputSnapshot::key_cul}, {"Right", &InputSnapshot::key_cur},
    {"D", &InputSnapshot::key_d}, {"S", &InputSnapshot::key_s},
    {"Space", &InputSnapshot::key_space}, {"C", &InputSnapshot::key_c},
    {"Esc", &InputSnapshot::key_esc}, {"F1", &InputSnapshot::key_f1},
    {"MouseRight", &InputSnapshot::mouse_right}
}};
constexpr std::array<std::pair<std::string_view, UiCommand>, 9> commands{{
    {"Play", UiCommand::play}, {"Resume", UiCommand::resume},
    {"Restart", UiCommand::restart}, {"Menu", UiCommand::menu},
    {"Help", UiCommand::help}, {"CloseHelp", UiCommand::close_help},
    {"Quit", UiCommand::quit}, {"SelectLocal", UiCommand::select_local},
    {"SelectWeb", UiCommand::select_web}
}};

template<class Exception, class Function>
void throws(Function&& function) {
    bool caught = false;
    try { function(); } catch (const Exception&) { caught = true; }
    assert(caught);
}

struct TemporaryScenario {
    std::filesystem::path directory;
    TemporaryScenario() {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("c4-input-scenario-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot create temporary scenario directory");
    }
    ~TemporaryScenario() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    std::filesystem::path save(std::string_view text) const {
        auto path = directory / "scenario.txt";
        std::ofstream file;
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.open(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        return path;
    }
    InputScenario load(std::string_view text) const { return InputScenario::load(save(text)); }
    void reject(std::string_view text) const {
        throws<std::runtime_error>([&] { (void)load(text); });
    }
};

void identical(const InputSnapshot& a, const InputSnapshot& b) {
    for (const auto& key : keys) assert(a.*key.second == b.*key.second);
    assert(a.key_esc_pressed == b.key_esc_pressed && a.key_f1_pressed == b.key_f1_pressed);
    assert(c4::physics::number(a.mouse_force_x) == c4::physics::number(b.mouse_force_x));
    assert(a.ui_command == b.ui_command);
}

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
struct LocaleGuard {
    std::locale previous{std::locale()};
    LocaleGuard() { std::locale::global(std::locale(previous, new CommaDecimal)); }
    ~LocaleGuard() { std::locale::global(previous); }
};
} // namespace

int main() {
    static_assert(InputScenario::max_frame == 10'000'000);
    static_assert(InputScenario::max_lines == 100'000);
    static_assert(InputScenario::max_bytes == 4 * 1024 * 1024);
    static_assert(InputScenario::max_line_bytes == 256);
    static_assert(noexcept(std::declval<const InputScenario&>().last_frame()));
    static_assert(noexcept(std::declval<const InputScenario&>().finished(0)));
    TemporaryScenario file;

    auto scenario = file.load(
        "\r\n\t  \r\n1 Up 1\r\n1 Down 1\r\n1 MouseRight 1\r\n"
        "2 Up 0\n2 Up 1\n2 mouse_force_x 2.5\n2 mouse_force_x -4.25\n"
        "2 Help 1\n2 Play 1\n4 Up 0\n4 Down 0\n4 MouseRight 0\n"
        "4 mouse_force_x 9\n4 Help 1\n5 Up 1\n8 Quit 1");
    identical(scenario.frame(0), InputSnapshot{});
    auto state = scenario.frame(1);
    assert(state.key_cuu && state.key_cud && state.mouse_right);
    assert(c4::physics::number(state.mouse_force_x) == 0 && state.ui_command == UiCommand::none);
    state = scenario.frame(2);
    assert(state.key_cuu && state.key_cud && state.mouse_right);
    assert(c4::physics::number(state.mouse_force_x) == -4.25 && state.ui_command == UiCommand::play);
    identical(state, scenario.frame(2));
    state = scenario.frame(3);
    assert(state.key_cuu && state.key_cud && state.mouse_right);
    assert(c4::physics::number(state.mouse_force_x) == 0 && state.ui_command == UiCommand::none);
    state = scenario.frame(6); // Held changes survive skipped frames; their pulses do not.
    assert(state.key_cuu && !state.key_cud && !state.mouse_right);
    assert(c4::physics::number(state.mouse_force_x) == 0 && state.ui_command == UiCommand::none);
    assert(scenario.last_frame() == 8 && !scenario.finished(7) && !scenario.finished(8));
    state = scenario.frame(8);
    assert(state.key_cuu && state.ui_command == UiCommand::quit);
    identical(state, scenario.frame(8));
    assert(scenario.finished(9));
    state = scenario.frame(9);
    assert(state.key_cuu && c4::physics::number(state.mouse_force_x) == 0 && state.ui_command == UiCommand::none);
    throws<std::invalid_argument>([&] { (void)scenario.frame(8); });
    identical(state, scenario.frame(9));
    throws<std::invalid_argument>([&] { (void)scenario.frame(InputScenario::max_frame + 1); });
    throws<std::invalid_argument>([&] { (void)scenario.frame(std::numeric_limits<std::uint64_t>::max()); });

    std::string recording;
    for (const auto& key : keys) {
        recording += "2 " + std::string(key.first) + " 0\n2 " + std::string(key.first) + " 1\n";
    }
    for (const auto& key : keys) recording += "4 " + std::string(key.first) + " 0\n";
    auto all_keys = file.load(recording);
    identical(all_keys.frame(0), InputSnapshot{});
    state = all_keys.frame(2);
    for (const auto& key : keys) assert(state.*key.second);
    assert(state.key_esc_pressed && state.key_f1_pressed);
    identical(state, all_keys.frame(2));
    state.key_esc_pressed = state.key_f1_pressed = false;
    identical(state, all_keys.frame(3));
    identical(all_keys.frame(4), InputSnapshot{});

    recording =
        "0 Esc 1\n0 F1 1\n1 Esc 1\n1 F1 1\n"
        "2 Esc 0\n2 Esc 1\n2 F1 0\n2 F1 1\n3 Esc 0\n3 F1 0\n"
        "4 Esc 1\n4 Esc 0\n4 F1 1\n4 F1 0\n5 Esc 1\n5 F1 1\n";
    auto edges = file.load(recording);
    state = edges.frame(0);
    assert(state.key_esc && state.key_f1 && state.key_esc_pressed && state.key_f1_pressed);
    identical(state, edges.frame(0));
    state = edges.frame(1);
    assert(state.key_esc && state.key_f1 && !state.key_esc_pressed && !state.key_f1_pressed);
    state = edges.frame(2);
    assert(state.key_esc && state.key_f1 && state.key_esc_pressed && state.key_f1_pressed);
    identical(state, edges.frame(2));
    state = edges.frame(3);
    assert(!state.key_esc && !state.key_f1 && !state.key_esc_pressed && !state.key_f1_pressed);
    state = edges.frame(4);
    assert(state.key_esc && state.key_f1 && state.key_esc_pressed && state.key_f1_pressed);
    identical(state, edges.frame(4));
    state = edges.frame(5);
    assert(state.key_esc && state.key_f1 && state.key_esc_pressed && state.key_f1_pressed);
    identical(state, edges.frame(5));
    state = edges.frame(6);
    assert(state.key_esc && state.key_f1 && !state.key_esc_pressed && !state.key_f1_pressed);
    identical(state, edges.frame(6));
    auto skipped_edges = file.load(recording);
    state = skipped_edges.frame(1);
    assert(state.key_esc && state.key_f1 && !state.key_esc_pressed && !state.key_f1_pressed);

    recording.clear();
    for (const auto& key : keys)
        recording += "0 " + std::string(key.first) + " 1\n0 " + std::string(key.first) + " 0\n";
    auto quick_taps = file.load(recording);
    state = quick_taps.frame(0);
    for (const auto& key : keys)
        assert(state.*key.second == (key.first != "MouseRight"));
    assert(state.key_esc_pressed && state.key_f1_pressed);
    identical(state, quick_taps.frame(0));
    identical(quick_taps.frame(1), InputSnapshot{});
    auto skipped_quick_taps = file.load(recording);
    identical(skipped_quick_taps.frame(1), InputSnapshot{});

    recording.clear();
    for (std::size_t i = 0; i < commands.size(); ++i)
        recording += std::to_string(i * 2) + " " + std::string(commands[i].first) + " 1\n";
    auto all_commands = file.load(recording);
    for (std::size_t i = 0; i < commands.size(); ++i) {
        state = all_commands.frame(i * 2);
        assert(state.ui_command == commands[i].second);
        identical(state, all_commands.frame(i * 2));
        assert(all_commands.frame(i * 2 + 1).ui_command == UiCommand::none);
    }
    auto first_jump = file.load("0 Up 1\n0 Play 1\n0 mouse_force_x 6\n2 D 1\n");
    state = first_jump.frame(2);
    assert(state.key_cuu && state.key_d && c4::physics::number(state.mouse_force_x) == 0 && state.ui_command == UiCommand::none);

    {
        LocaleGuard locale;
        auto numbers = file.load(
            "000 Up 1\n0 mouse_force_x +1.25\n1 mouse_force_x -2.5e1\n"
            "2 mouse_force_x -0\n3 mouse_force_x 0.0015\n"
            "4 mouse_force_x 0.00097656249\n5 mouse_force_x -2097152\n"
            "6 mouse_force_x 2097151.875\n");
        assert(c4::physics::number(numbers.frame(0).mouse_force_x) == 1.25);
        assert(c4::physics::number(numbers.frame(1).mouse_force_x) == -25);
        assert(c4::physics::number(numbers.frame(2).mouse_force_x) == 0);
        assert(c4::physics::number(numbers.frame(3).mouse_force_x) == 1.0 / 1024);
        assert(c4::physics::number(numbers.frame(4).mouse_force_x) == 1.0 / 1024);
        assert(c4::physics::number(numbers.frame(5).mouse_force_x) == -2097152);
        assert(c4::physics::number(numbers.frame(6).mouse_force_x) == 2097151.875);
        file.reject("0 mouse_force_x 1,25\n");
    }

    for (std::string_view bad : {
        "", "\n\t \r\n", "# comment\n", "0 Up\n", "0 Up 1 extra\n",
        "0 Up 1 # comment\n", "0Up 1\n", "0 unknown 1\n", "0 up 1\n",
        "0 key_cuu 1\n", "0 Escape 1\n", "0 MouseForceX 1\n", "0 play 1\n",
        "0 key_esc_pressed 1\n", "0 key_f1_pressed 1\n",
        "-1 Up 1\n", "+1 Up 1\n", "1.0 Up 1\n", "1e1 Up 1\n", "0x1 Up 1\n",
        "10000001 Up 1\n", "18446744073709551616 Up 1\n",
        "999999999999999999999999999999999999999999999999 Up 1\n",
        "2 Up 1\n1 Up 0\n", "0 mouse_force_x nan\n", "0 mouse_force_x NaN\n",
        "0 mouse_force_x inf\n", "0 mouse_force_x -inf\n", "0 mouse_force_x +infinity\n",
        "0 mouse_force_x 1e309\n", "0 mouse_force_x 2.5junk\n", "0 mouse_force_x --1\n",
        "0 mouse_force_x 1.7976931348623157e308\n",
        "0 mouse_force_x +2097152\n", "0 mouse_force_x -2097152.25\n"
    }) file.reject(bad);
    file.reject(std::string("0 Up 1\0", 7));
    for (const auto& key : keys)
        for (std::string_view bad : {"2", "-1", "true", "01", "1.0", "+1"})
            file.reject("0 " + std::string(key.first) + " " + std::string(bad) + "\n");
    for (const auto& command : commands)
        for (std::string_view bad : {"0", "2", "true", "01", "+1"})
            file.reject("0 " + std::string(command.first) + " " + std::string(bad) + "\n");
    throws<std::runtime_error>([&] { (void)InputScenario::load(file.directory / "missing.txt"); });
    throws<std::runtime_error>([&] { (void)InputScenario::load(file.directory); });

    auto frame_limit = file.load("10000000 Up 1\n");
    assert(!frame_limit.finished(InputScenario::max_frame));
    assert(frame_limit.frame(InputScenario::max_frame).key_cuu);
    assert(frame_limit.finished(InputScenario::max_frame + 1));

    recording = std::string(InputScenario::max_line_bytes - 6, ' ') + "0 Up 1\n";
    assert(file.load(recording).frame(0).key_cuu);
    recording.insert(recording.begin(), ' ');
    file.reject(recording);

    recording = "0 Up 1\n" + std::string(InputScenario::max_lines - 1, '\n');
    assert(file.load(recording).frame(0).key_cuu);
    recording += '\n';
    file.reject(recording);

    // 16,384 lines of 256 bytes each: the byte cap, independently of other caps.
    const std::string padded_blank(InputScenario::max_line_bytes - 1, ' ');
    recording = std::string(InputScenario::max_line_bytes - 7, ' ') + "0 Up 1\n";
    while (recording.size() < InputScenario::max_bytes) recording += padded_blank + '\n';
    assert(recording.size() == InputScenario::max_bytes);
    assert(file.load(recording).frame(0).key_cuu);
    recording += ' ';
    file.reject(recording);
}
