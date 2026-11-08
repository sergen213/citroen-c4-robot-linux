#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace c4::rendering::pipeline_cache {
// Bound the entire on-disk file, not just the opaque driver payload.
inline constexpr std::size_t max_bytes=32u*1024u*1024u;
inline constexpr std::array<const char*,6> shader_names{
    "raster.vert.spv","raster.frag.spv","compose.frag.spv",
    "rt.comp.spv","temporal.comp.spv","fullscreen.vert.spv"};
enum class Status { ready, disabled, missing, loaded, saved, malformed, incompatible, too_large, io_error };
inline const char* status_name(Status status) noexcept {
    switch(status) {
    case Status::ready:return "ready"; case Status::disabled:return "disabled";
    case Status::missing:return "missing"; case Status::loaded:return "loaded";
    case Status::saved:return "saved"; case Status::malformed:return "malformed";
    case Status::incompatible:return "incompatible"; case Status::too_large:return "too_large";
    case Status::io_error:return "io_error";
    }
    return "unknown";
}
struct Result { Status status{Status::disabled}; std::error_code error; std::string_view reason{}; };
struct Prepared : Result {
    std::filesystem::path path;
    std::uint64_t key{};
    VkPhysicalDeviceProperties properties{};
};
struct Load : Result { std::vector<std::uint8_t> bytes; };
namespace detail {
inline constexpr std::uint32_t schema=1;
inline constexpr std::size_t envelope_size=40;
inline constexpr std::size_t native_header_size=32;
inline constexpr std::array<std::uint8_t,8> magic{'C','4','V','K','P','C','0','1'};
inline std::uint64_t hash(std::span<const std::uint8_t> bytes,std::uint64_t value=14695981039346656037ull) noexcept {
    for(auto byte:bytes) { value^=byte; value*=1099511628211ull; }
    return value;
}
inline void put(std::uint8_t* output,std::uint64_t value,unsigned count) noexcept {
    for(unsigned i=0;i<count;++i) { output[i]=static_cast<std::uint8_t>(value); value>>=8; }
}
inline std::uint64_t get(const std::uint8_t* input,unsigned count) noexcept {
    std::uint64_t value{}; for(unsigned i=0;i<count;++i)value|=std::uint64_t(input[i])<<(i*8); return value;
}
inline void mix(std::uint64_t& key,std::uint64_t value) noexcept {
    std::array<std::uint8_t,8> bytes{}; put(bytes.data(),value,8); key=hash(bytes,key);
}
inline Result io_error(int value=errno) { return {Status::io_error,{value,std::generic_category()}}; }
struct Descriptor {
    int value{-1};
    explicit Descriptor(int fd):value(fd){}
    ~Descriptor(){if(value>=0)::close(value);}
    Descriptor(const Descriptor&)=delete; Descriptor& operator=(const Descriptor&)=delete;
};
inline Result directory_status(int fd) {
    struct stat info{};
    if(::fstat(fd,&info)!=0)return io_error();
    // Do not chmod an existing path: require our cache directory to already be private.
    if(!S_ISDIR(info.st_mode)||info.st_uid!=::geteuid()||(info.st_mode&0077))return io_error(EACCES);
    return {Status::ready,{}};
}
inline Result read_exact(int fd,std::span<std::uint8_t> bytes) {
    while(!bytes.empty()) {
        const auto count=::read(fd,bytes.data(),bytes.size());
        if(count<0) { if(errno==EINTR)continue; return io_error(); }
        if(count==0)return {Status::malformed,{}};
        bytes=bytes.subspan(static_cast<std::size_t>(count));
    }
    return {Status::ready,{}};
}
inline Result write_exact(int fd,std::span<const std::uint8_t> bytes) {
    while(!bytes.empty()) {
        const auto count=::write(fd,bytes.data(),bytes.size());
        if(count<0) { if(errno==EINTR)continue; return io_error(); }
        if(count==0)return io_error(EIO);
        bytes=bytes.subspan(static_cast<std::size_t>(count));
    }
    return {Status::ready,{}};
}
inline Result validate(std::span<const std::uint8_t> bytes,const VkPhysicalDeviceProperties& properties) noexcept {
    if(bytes.size()<native_header_size)return {Status::malformed,{},"Vulkan pipeline cache data is shorter than the required 32-byte header"};
    // Version-one Vulkan cache fields are specified little-endian, independent of host layout.
    if(get(bytes.data(),4)!=native_header_size)return {Status::malformed,{},"Vulkan version-one pipeline cache headerSize is not 32"};
    if(get(bytes.data()+4,4)!=VK_PIPELINE_CACHE_HEADER_VERSION_ONE)return {Status::incompatible,{},"Vulkan pipeline cache header version is not supported"};
    if(get(bytes.data()+8,4)!=properties.vendorID||get(bytes.data()+12,4)!=properties.deviceID||
       std::memcmp(bytes.data()+16,properties.pipelineCacheUUID,VK_UUID_SIZE)!=0)return {Status::incompatible,{},"Vulkan pipeline cache device or UUID does not match"};
    return {Status::ready,{}};
}
}
// The caller loads all six actual SPVs before cache creation and retains their words
// for shader-module creation; this function hashes them without another file read/copy.
inline Prepared prepare(const VkPhysicalDeviceProperties& properties,
                        const std::array<std::span<const std::uint32_t>,6>& shaders) {
    Prepared output; output.properties=properties;
    output.key=detail::hash(detail::magic); detail::mix(output.key,detail::schema);
    detail::mix(output.key,properties.vendorID); detail::mix(output.key,properties.deviceID);
    detail::mix(output.key,properties.driverVersion);
    output.key=detail::hash({properties.pipelineCacheUUID,VK_UUID_SIZE},output.key);
    for(const auto words:shaders) {
        if(words.size()<5||words.front()!=0x07230203u) { output.status=Status::malformed; return output; }
        if(words.size_bytes()>max_bytes) { output.status=Status::too_large; return output; }
        const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(words.data()),words.size_bytes());
        detail::mix(output.key,words.size_bytes()); detail::mix(output.key,detail::hash(bytes));
    }
    std::filesystem::path root;
    if(const char* xdg=std::getenv("XDG_CACHE_HOME");xdg&&*xdg&&std::filesystem::path(xdg).is_absolute())root=xdg;
    else if(const char* home=std::getenv("HOME");home&&*home&&std::filesystem::path(home).is_absolute())root=std::filesystem::path(home)/".cache";
    else return output;
    std::string filename(16,'0'); constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i)filename[15-i]=digits[(output.key>>(i*4))&15];
    output.path=root/"citroen-c4-robot"/(filename+".vkcache"); output.status=Status::ready;
    return output;
}
inline Load load(const Prepared& prepared) {
    Load output; output.status=prepared.status; output.error=prepared.error;
    if(prepared.status!=Status::ready)return output;
    const auto directory_path=prepared.path.parent_path(); const auto filename=prepared.path.filename().string();
    detail::Descriptor directory(::open(directory_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(directory.value<0) { const auto result=errno==ENOENT?Result{Status::missing,{}}:detail::io_error(); output.status=result.status;output.error=result.error;return output; }
    auto result=detail::directory_status(directory.value);
    if(result.status!=Status::ready) { output.status=result.status;output.error=result.error;return output; }
    detail::Descriptor file(::openat(directory.value,filename.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));
    if(file.value<0) { result=errno==ENOENT?Result{Status::missing,{}}:detail::io_error();output.status=result.status;output.error=result.error;return output; }
    struct stat info{};
    if(::fstat(file.value,&info)!=0) { result=detail::io_error();output.status=result.status;output.error=result.error;return output; }
    if(!S_ISREG(info.st_mode)||info.st_size<static_cast<off_t>(detail::envelope_size)) { output.status=Status::malformed;return output; }
    if(info.st_size>static_cast<off_t>(max_bytes)) { output.status=Status::too_large;return output; }
    std::array<std::uint8_t,detail::envelope_size> header{};
    result=detail::read_exact(file.value,header);
    if(result.status!=Status::ready) { output.status=result.status;output.error=result.error;return output; }
    if(std::memcmp(header.data(),detail::magic.data(),detail::magic.size())!=0||detail::get(header.data()+12,4)!=header.size()) { output.status=Status::malformed;return output; }
    if(detail::get(header.data()+8,4)!=detail::schema||detail::get(header.data()+16,8)!=prepared.key) { output.status=Status::incompatible;return output; }
    const auto size=detail::get(header.data()+24,8);
    if(size!=static_cast<std::uint64_t>(info.st_size)-header.size()||size<detail::native_header_size) { output.status=Status::malformed;return output; }
    output.bytes.resize(static_cast<std::size_t>(size)); result=detail::read_exact(file.value,output.bytes);
    if(result.status==Status::ready) {
        std::uint8_t extra{}; ssize_t count;
        do { count=::read(file.value,&extra,1); } while(count<0&&errno==EINTR);
        if(count<0)result=detail::io_error(); else if(count!=0)result={Status::malformed,{}};
    }
    if(result.status==Status::ready)result=detail::validate(output.bytes,prepared.properties);
    if(result.status==Status::ready&&detail::hash(output.bytes)!=detail::get(header.data()+32,8))result.status=Status::malformed;
    output.status=result.status==Status::ready?Status::loaded:result.status; output.error=result.error; output.reason=result.reason;
    if(output.status!=Status::loaded)output.bytes.clear();
    return output;
}
inline Result save(const Prepared& prepared,std::span<const std::uint8_t> bytes) {
    if(prepared.status!=Status::ready)return {prepared.status,prepared.error,prepared.reason};
    if(bytes.size()>max_bytes-detail::envelope_size)return {Status::too_large,{},"Vulkan pipeline cache exceeds the on-disk size limit"};
    const auto valid=detail::validate(bytes,prepared.properties); if(valid.status!=Status::ready)return valid;
    const auto directory_path=prepared.path.parent_path(); const auto filename=prepared.path.filename().string();
    std::error_code error; std::filesystem::create_directories(directory_path.parent_path(),error);
    if(error)return {Status::io_error,error};
    if(::mkdir(directory_path.c_str(),0700)!=0&&errno!=EEXIST)return detail::io_error();
    detail::Descriptor directory(::open(directory_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(directory.value<0)return detail::io_error();
    auto result=detail::directory_status(directory.value); if(result.status!=Status::ready)return result;
    static std::atomic<std::uint64_t> serial{};
    std::string temporary; int fd=-1;
    for(unsigned attempt=0;attempt<64;++attempt) {
        temporary="."+filename+"."+std::to_string(::getpid())+"."+std::to_string(serial.fetch_add(1,std::memory_order_relaxed));
        fd=::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd>=0)break;
        if(errno!=EEXIST)return detail::io_error();
    }
    if(fd<0)return detail::io_error(EEXIST);
    detail::Descriptor file(fd);
    std::array<std::uint8_t,detail::envelope_size> header{};
    std::memcpy(header.data(),detail::magic.data(),detail::magic.size());
    detail::put(header.data()+8,detail::schema,4); detail::put(header.data()+12,header.size(),4);
    detail::put(header.data()+16,prepared.key,8); detail::put(header.data()+24,bytes.size(),8);
    detail::put(header.data()+32,detail::hash(bytes),8);
    result=detail::write_exact(file.value,header);
    if(result.status==Status::ready)result=detail::write_exact(file.value,bytes);
    if(result.status==Status::ready&&::fsync(file.value)!=0)result=detail::io_error();
    if(::close(file.value)!=0&&result.status==Status::ready)result=detail::io_error();
    file.value=-1;
    if(result.status==Status::ready&&::renameat(directory.value,temporary.c_str(),directory.value,filename.c_str())!=0)result=detail::io_error();
    if(result.status!=Status::ready) { ::unlinkat(directory.value,temporary.c_str(),0);return result; }
    if(::fsync(directory.value)!=0)return detail::io_error();
    return {Status::saved,{}};
}
}
