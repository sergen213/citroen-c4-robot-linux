// Standalone, loader-free: c++ -std=c++20 -UNDEBUG -Isrc tests/test_vulkan_pipeline_cache.cpp -o /tmp/c4-pipeline-cache-test
#include "rendering/vulkan/VulkanPipelineCache.hpp"
#include <cassert>
#include <cstring>
#include <fstream>

using namespace c4::rendering::pipeline_cache;

int main() {
    char directory[]="/tmp/c4-pipeline-cache-XXXXXX";
    const char* root=::mkdtemp(directory); assert(root);
    assert(::setenv("XDG_CACHE_HOME",root,1)==0);
    VkPhysicalDeviceProperties properties{};
    properties.vendorID=42; properties.deviceID=7; properties.driverVersion=9;
    properties.pipelineCacheUUID[0]=1;
    std::array<std::uint32_t,5> shader{0x07230203u,0x00010000u,0,1,0};
    std::array<std::span<const std::uint32_t>,6> shaders; shaders.fill(shader);
    const auto prepared=prepare(properties,shaders);
    assert(prepared.status==Status::ready);
    assert(load(prepared).status==Status::missing);
    auto changed_properties=properties; ++changed_properties.driverVersion;
    assert(prepare(changed_properties,shaders).key!=prepared.key);
    for(unsigned index=0;index<shaders.size();++index) {
        auto changed=shaders; auto changed_shader=shader; ++changed_shader[2]; changed[index]=changed_shader;
        assert(prepare(properties,changed).key!=prepared.key);
    }
    VkPipelineCacheHeaderVersionOne native{sizeof(VkPipelineCacheHeaderVersionOne),VK_PIPELINE_CACHE_HEADER_VERSION_ONE,42,7,{1}};
    std::vector<std::uint8_t> payload(sizeof(native)+3,17);
    std::memcpy(payload.data(),&native,sizeof(native));
    // The API's reported byte count is authoritative, even if its header claims 32.
    const auto truncated=save(prepared,std::span(payload).first(20));
    assert(truncated.status==Status::malformed);
    auto bad_header=payload; bad_header[0]=20;
    const auto bad_size=save(prepared,bad_header);
    assert(bad_size.status==Status::malformed);
    assert(save(prepared,std::span(payload).first(32)).status==Status::saved);
    assert(load(prepared).bytes==std::vector<std::uint8_t>(payload.begin(),payload.begin()+32));
    assert(save(prepared,payload).status==Status::saved);
    assert(load(prepared).status==Status::loaded);
    assert(load(prepared).bytes==payload);
    auto wrong_key=prepared; ++wrong_key.key;
    assert(load(wrong_key).status==Status::incompatible);
    auto wrong_device=prepared; ++wrong_device.properties.deviceID;
    assert(load(wrong_device).status==Status::incompatible);
    assert(save(wrong_device,payload).status==Status::incompatible);
    {
        std::fstream file(prepared.path,std::ios::in|std::ios::out|std::ios::binary);
        file.seekp(-1,std::ios::end); file.put('\0'); assert(file);
    }
    assert(load(prepared).status==Status::malformed);
    assert(save(prepared,payload).status==Status::saved);
    std::filesystem::resize_file(prepared.path,1);
    assert(load(prepared).status==Status::malformed);
    std::filesystem::resize_file(prepared.path,max_bytes+1);
    assert(load(prepared).status==Status::too_large);
    std::filesystem::remove(prepared.path);
    std::filesystem::create_symlink("/dev/null",prepared.path);
    const auto symlink=load(prepared);
    assert(symlink.status==Status::io_error && symlink.error);
    std::filesystem::remove(prepared.path);
    assert(save(prepared,payload).status==Status::saved);
    struct stat permissions{}; assert(::stat(prepared.path.c_str(),&permissions)==0);
    assert((permissions.st_mode&0777)==0600);
    assert(::setenv("XDG_CACHE_HOME","relative-cache-is-invalid",1)==0);
    assert(::setenv("HOME",root,1)==0);
    assert(prepare(properties,shaders).path.parent_path()==std::filesystem::path(root)/".cache"/"citroen-c4-robot");
    std::filesystem::remove_all(root);
}
