#include "Ghost.hpp"
#include "SourceTime.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace c4::game {
using physics::Var;
using physics::v;
namespace {
constexpr std::size_t max_bytes = 2 * 1024 * 1024;
constexpr std::size_t max_name = 128;
[[noreturn]] void invalid(const char* reason) { throw std::invalid_argument(reason); }
[[noreturn]] void io_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
struct Descriptor {
    int value;
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    explicit Descriptor(int fd) : value(fd) {}
};
struct Tokens {
    std::string_view text;
    std::size_t offset{};
    std::string_view next(std::size_t maximum) {
        const auto end = text.find('~', offset);
        if (end == std::string_view::npos || end == offset || end-offset > maximum)
            invalid("Invalid or truncated ghost token");
        const auto token = text.substr(offset, end-offset);
        for (const unsigned char byte : token)
            if (byte < 36 || byte > 125) invalid("Invalid ghost encoded character");
        offset = end+1;
        return token;
    }
};
int wrapped(int value) {
    // Original encoder subtracts 90 above 125, decoder adds 90 below 36.
    return 36 + ((value-36)%90+90)%90;
}
int number_base(int key, int name_sum) {
    // Retain source Q10 overflow in the name checksum product, once per file.
    const auto base=v(key)+v(32)+v(name_sum)*v(13)
        +physics::var_modulo(v(key)*v(101),v(57))
        +physics::var_modulo(v(name_sum)*v(255),v(371));
    return base.raw/1024;
}
Var decode_number(std::string_view token, int base, int sequence) {
    const int offset=base+(sequence*6)%197;
    char plain[33]{};
    for (std::size_t i=0; i<token.size(); ++i)
        plain[i] = static_cast<char>(wrapped(static_cast<unsigned char>(token[i])
            - offset - 7*(static_cast<int>(i)+1)));
    // str_for_num emits decimal numbers, not exponents, NaN, or trailing junk.
    std::size_t i = plain[0]=='-' ? 1 : 0;
    const auto first_digit = i;
    while (i<token.size() && plain[i]>='0' && plain[i]<='9') ++i;
    if (i==first_digit) invalid("Invalid ghost number");
    if (i<token.size() && plain[i]=='.') {
        const auto fractional = ++i;
        while (i<token.size() && plain[i]>='0' && plain[i]<='9') ++i;
        if (i==fractional) invalid("Invalid ghost decimal");
    }
    if (i!=token.size()) invalid("Invalid ghost numeric suffix");
    double value{};
    const auto result = std::from_chars(plain,plain+token.size(),value);
    if (result.ec!=std::errc{} || result.ptr!=plain+token.size() || !std::isfinite(value))
        invalid("Ghost number out of range");
    return source_str_to_num(plain);
}
void encode_token(std::string& output, std::string_view text, int base, int sequence) {
    const int offset=base+(sequence*6)%197;
    for (std::size_t i=0; i<text.size(); ++i)
        output.push_back(static_cast<char>(wrapped(static_cast<unsigned char>(text[i])
            + offset + 7*(static_cast<int>(i)+1))));
    output.push_back('~');
}
void encode_number(std::string& output, Var value, int base, int sequence) {
    const auto text=source_str_for_num(value);
    encode_token(output,text,base,sequence);
}
void validate_record(const GhostRecord& record, bool sentinel=false) {
    if ((!sentinel && record.time<v(0)) || record.action<0 || record.action>11
        || record.frame<v(0) || record.frame>v(100) || record.mode<0 || record.mode>1)
        invalid("Invalid ghost record fields");
}
void validate_track(const GhostTrack& track) {
    if (track.name.empty() || track.name.size()>max_name) invalid("Invalid ghost name length");
    for (const unsigned char character : track.name)
        if (character<36 || character>125) invalid("Ghost name is not original-codec representable");
    if (track.record_ticks<=v(0) || track.records.empty()
        || track.records.size()>ghost_capacity || track.records.front().time!=v(0))
        invalid("Invalid ghost record metadata");
    auto previous=v(-1);
    for (Var ticks : track.checkpoint_ticks) {
        if (ticks<v(0) || ticks>track.record_ticks || ticks<previous)
            invalid("Invalid ghost checkpoint times");
        previous=ticks;
    }
    previous=v(-1);
    for (const auto& record : track.records) {
        validate_record(record);
        if (record.time<=previous || record.time>track.record_ticks)
            invalid("Ghost timestamps are not strictly increasing");
        previous=record.time;
    }
}
int integer_field(Var value) {
    if (value<v(0) || value>v(11) || value.raw%1024!=0)
        invalid("Invalid ghost integer field");
    return value.raw/1024;
}
} // namespace

namespace {
GhostTrack parse_ghost_impl(std::string_view encoded, std::size_t capacity) {
    if (encoded.empty() || encoded.size()>max_bytes) invalid("Invalid ghost file size");
    Tokens tokens{encoded};
    for (int i=0; i<3; ++i) {
        if (tokens.next(23).size()<4) invalid("Invalid original ghost padding");
    }
    const auto key_token=tokens.next(1);
    const int key=static_cast<unsigned char>(key_token.front());
    const auto name_token=tokens.next(max_name);
    GhostTrack track;
    track.name.reserve(name_token.size());
    int name_sum=0;
    for (std::size_t i=0; i<name_token.size(); ++i) {
        const int character=wrapped(static_cast<unsigned char>(name_token[i])
            - key - 13*(static_cast<int>(i)+1) - 15 - (key*421)%248);
        track.name.push_back(static_cast<char>(character));
        name_sum+=character;
    }
    const int base=number_base(key,name_sum);
    int sequence=0;
    auto number=[&] { return decode_number(tokens.next(32),base,++sequence); };
    if (number()!=v(64871)) invalid("Invalid original local ghost signature");
    track.record_ticks=number();
    for (auto& ticks : track.checkpoint_ticks) ticks=number();
    track.records.reserve(std::min(ghost_capacity,encoded.size()/20));
    for (;;) {
        GhostRecord record;
        record.time=number();
        record.position={number(),number(),number()};
        record.angles={number(),number(),number()};
        record.action=integer_field(number());
        record.frame=number();
        record.mode=integer_field(number());
        if (record.time==ghost_sentinel) {
            validate_record(record,true);
            if (tokens.offset!=encoded.size()) invalid("Trailing ghost data after sentinel");
            break;
        }
        if (track.records.size()==capacity) invalid("Ghost loader capacity exceeded");
        track.records.push_back(record);
    }
    validate_track(track);
    return track;
}
} // namespace

GhostTrack parse_ghost(std::string_view encoded) {
    return parse_ghost_impl(encoded,ghost_import_capacity);
}

std::string serialize_ghost(const GhostTrack& track) {
    validate_track(track);
    // Random tokens/key are obfuscation, not security; stable valid padding keeps output reproducible.
    constexpr int key=71;
    std::string output="C4RC~GHOST~LOCAL~G~";
    output.reserve(256+(track.records.size()+1)*120);
    int name_sum=0;
    for (std::size_t i=0; i<track.name.size(); ++i) {
        const int character=static_cast<unsigned char>(track.name[i]);
        name_sum+=character;
        output.push_back(static_cast<char>(wrapped(character+key+13*(static_cast<int>(i)+1)+15+(key*421)%248)));
    }
    output.push_back('~');
    const int base=number_base(key,name_sum);
    int sequence=0;
    auto number=[&](Var value) { encode_number(output,value,base,++sequence); };
    number(v(64871));
    number(track.record_ticks);
    for (Var ticks : track.checkpoint_ticks) number(ticks);
    auto record=[&](const GhostRecord& value) {
        number(value.time);
        number(value.position.x); number(value.position.y); number(value.position.z);
        number(value.angles.x); number(value.angles.y); number(value.angles.z);
        number(v(value.action)); number(value.frame); number(v(value.mode));
    };
    for (const auto& value : track.records) record(value);
    auto sentinel=track.records.back();
    sentinel.time=ghost_sentinel;
    record(sentinel);
    return output;
}

namespace {
std::optional<GhostTrack> load_ghost_impl(const std::filesystem::path& path, std::size_t capacity) {
    Descriptor file(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if (file.value<0) {
        if (errno==ENOENT) return std::nullopt;
        io_error("Opening ghost record");
    }
    struct stat status{};
    if (::fstat(file.value,&status)!=0) io_error("Inspecting ghost record");
    if (!S_ISREG(status.st_mode) || status.st_size<=0 || status.st_size>static_cast<off_t>(max_bytes))
        invalid("Ghost record is not a bounded regular file");
    std::string encoded(static_cast<std::size_t>(status.st_size),'\0');
    std::size_t offset=0;
    while (offset<encoded.size()) {
        const auto count=::read(file.value,encoded.data()+offset,encoded.size()-offset);
        if (count<0) { if (errno==EINTR) continue; io_error("Reading ghost record"); }
        if (count==0) invalid("Ghost record changed or was truncated during read");
        offset+=static_cast<std::size_t>(count);
    }
    char extra;
    ssize_t count;
    do { count=::read(file.value,&extra,1); } while (count<0 && errno==EINTR);
    if (count<0) io_error("Reading ghost record end");
    if (count!=0) invalid("Ghost record changed during read");
    return parse_ghost_impl(encoded,capacity);
}
} // namespace

std::optional<GhostTrack> load_ghost(const std::filesystem::path& path) {
    return load_ghost_impl(path,ghost_import_capacity);
}

std::filesystem::path local_ghost_path() {
    if (const char* xdg=std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        const std::filesystem::path root(xdg);
        if (root.is_absolute()) return root/"citroen-c4-robot"/"bhlt.rc4";
    }
    const char* home=std::getenv("HOME");
    if (!home || !*home || !std::filesystem::path(home).is_absolute())
        throw GhostSaveError("No absolute XDG_DATA_HOME or HOME for local ghost persistence");
    return std::filesystem::path(home)/".local"/"share"/"citroen-c4-robot"/"bhlt.rc4";
}

namespace {
void write_ghost_atomic(const std::filesystem::path& path, std::string_view encoded) {
    const auto filename=path.filename().string();
    std::filesystem::create_directories(path.parent_path());
    Descriptor directory(::open(path.parent_path().c_str(),O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW));
    if (directory.value<0) io_error("Opening local ghost directory");
    static std::atomic<unsigned> serial{};
    std::string temporary;
    int fd=-1;
    for (int attempt=0; attempt<32; ++attempt) {
        temporary="."+filename+"."+std::to_string(::getpid())+"."+std::to_string(serial.fetch_add(1));
        fd=::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd>=0) break;
        if (errno!=EEXIST) io_error("Creating local ghost temporary file");
    }
    if (fd<0) io_error("Creating unique local ghost temporary file");
    Descriptor file(fd);
    try {
        std::size_t offset=0;
        while (offset<encoded.size()) {
            const auto count=::write(file.value,encoded.data()+offset,encoded.size()-offset);
            if (count<0) { if (errno==EINTR) continue; io_error("Writing local ghost record"); }
            if (count==0) { errno=EIO; io_error("Writing local ghost record"); }
            offset+=static_cast<std::size_t>(count);
        }
        if (::fsync(file.value)!=0) io_error("Syncing local ghost record");
        if (::close(file.value)!=0) { file.value=-1; io_error("Closing local ghost record"); }
        file.value=-1;
        if (::renameat(directory.value,temporary.c_str(),directory.value,filename.c_str())!=0)
            io_error("Committing local ghost record");
        if (::fsync(directory.value)!=0) io_error("Syncing local ghost directory");
    } catch (...) {
        ::unlinkat(directory.value,temporary.c_str(),0);
        throw;
    }
}
void save_ghost_track(const std::filesystem::path& path, const GhostTrack& track, std::size_t capacity) {
    try {
        if (track.records.size()>capacity) invalid("Ghost save capacity exceeded");
        write_ghost_atomic(path,serialize_ghost(track));
    } catch (const std::system_error& error) {
        throw GhostSaveError(error.what());
    } catch (const std::invalid_argument& error) {
        throw GhostSaveError(error.what());
    }
}
} // namespace

void save_local_ghost(const GhostTrack& track) {
    save_ghost_track(local_ghost_path(),track,ghost_import_capacity);
}

std::filesystem::path local_ghost_archive_path() {
    return local_ghost_path().parent_path()/"native-recording.rc4";
}

void save_ghost_archive(const GhostTrack& track) {
    save_ghost_track(local_ghost_archive_path(),track,ghost_capacity);
}

std::optional<GhostTrack> load_ghost_archive() {
    return load_ghost_impl(local_ghost_archive_path(),ghost_capacity);
}

std::optional<GhostSample> sample_ghost(const GhostTrack& track, Var ticks) {
    if (track.records.empty() || ticks<track.records.front().time
        || ticks>track.records.back().time) return std::nullopt;
    const auto next=std::upper_bound(track.records.begin(),track.records.end(),ticks,
        [](Var time,const GhostRecord& record) { return time<record.time; });
    const auto& before=*(next-1);
    GhostSample sample{before,before.action,before.frame,v(0),false};
    if (next==track.records.end()) return sample;
    const auto elapsed=ticks-before.time;
    const auto span=next->time-before.time;
    const auto fraction=elapsed/span;
    sample.record.time=ticks;
    // Preserve source multiply-before-divide order, not precomputed-fraction lerp.
    sample.record.position={before.position.x+elapsed*(next->position.x-before.position.x)/span,
        before.position.y+elapsed*(next->position.y-before.position.y)/span,
        before.position.z+elapsed*(next->position.z-before.position.z)/span};
    sample.record.angles={before.angles.x+elapsed*physics::var_ang(next->angles.x-before.angles.x)/span,
        before.angles.y+elapsed*physics::var_ang(next->angles.y-before.angles.y)/span,
        before.angles.z+elapsed*physics::var_ang(next->angles.z-before.angles.z)/span};
    sample.next_action=next->action;
    sample.next_frame=next->frame;
    sample.blend=fraction;
    sample.action_transition=before.action!=next->action;
    if (!sample.action_transition) {
        auto end=next->frame;
        if (before.frame>end) end+=v(100);
        sample.record.frame=before.frame+(end-before.frame)*fraction;
        if (sample.record.frame>v(100)) sample.record.frame=physics::var_modulo(sample.record.frame,v(100));
    }
    return sample;
}

Var ghost_alpha(Var distance, Var minimum_distance) {
    if (distance<minimum_distance) return v(0);
    if (distance>=v(3000)) return physics::var_min(v(100),v(50)+(distance-v(3000))/v(10));
    const auto alpha=(distance-minimum_distance)/v(8);
    return minimum_distance<v(180) ? physics::var_clamp(alpha,v(10),v(50)) : physics::var_min(alpha,v(50));
}
} // namespace c4::game
