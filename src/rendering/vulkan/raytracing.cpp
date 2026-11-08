#include "raytracing.hpp"
#include "game/ui/NativeUi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace c4::rendering {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
}
void write_json_string(std::ostream& out, std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') out << '\\' << static_cast<char>(ch);
        else if (ch < 32) out << "\\u00" << hex[ch >> 4] << hex[ch & 15];
        else out << static_cast<char>(ch);
    }
    out << '"';
}
struct Buffer {
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceAddress address{};
    void* mapped{};
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept { *this = std::move(other); }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            reset(); device = other.device;
            buffer = std::exchange(other.buffer, VK_NULL_HANDLE);
            memory = std::exchange(other.memory, VK_NULL_HANDLE);
            address = std::exchange(other.address, 0); mapped = std::exchange(other.mapped, nullptr);
        }
        return *this;
    }
    void reset() {
        if (mapped) vkUnmapMemory(device, memory);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        mapped = nullptr; buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE;
    }
    ~Buffer() { reset(); }
};
struct Image {
    VkDevice device{};
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept { *this = std::move(other); }
    Image& operator=(Image&& other) noexcept {
        if (this != &other) {
            reset(); device = other.device;
            image = std::exchange(other.image, VK_NULL_HANDLE);
            view = std::exchange(other.view, VK_NULL_HANDLE);
            memory = std::exchange(other.memory, VK_NULL_HANDLE);
        }
        return *this;
    }
    void reset() {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        view = VK_NULL_HANDLE; image = VK_NULL_HANDLE; memory = VK_NULL_HANDLE;
    }
    ~Image() { reset(); }
};
struct Acceleration {
    VkDevice device{};
    PFN_vkDestroyAccelerationStructureKHR destroy{};
    VkAccelerationStructureKHR handle{};
    VkDeviceAddress address{};
    Buffer storage;
    Acceleration() = default;
    Acceleration(const Acceleration&) = delete;
    Acceleration& operator=(const Acceleration&) = delete;
    Acceleration(Acceleration&& other) noexcept { *this = std::move(other); }
    Acceleration& operator=(Acceleration&& other) noexcept {
        if (this != &other) {
            if (handle) destroy(device, handle, nullptr);
            device = other.device; destroy = other.destroy;
            handle = std::exchange(other.handle, VK_NULL_HANDLE);
            address = std::exchange(other.address, 0); storage = std::move(other.storage);
        }
        return *this;
    }
    ~Acceleration() { if (handle) destroy(device, handle, nullptr); }
};
struct alignas(16) GpuMesh {
    std::uint64_t vertices{}, indices{};
    std::array<std::uint32_t, 4> data{};
    std::array<std::uint32_t, 4> attributes{};
    std::uint64_t tangents{}, padding{};
    std::array<std::uint32_t, 4> tangent_data{};
};
struct alignas(16) GpuInstance {
    std::array<float, 16> model{};
    std::array<float, 4> diffuse{};
    std::array<std::uint32_t, 4> data{};
    std::array<float, 4> alpha_policy{};
    std::array<float, 4> skill1{}, skill5{};
    std::array<std::uint32_t, 4> source_textures{};
    std::array<float, 4> material_ambient{};
};
struct alignas(16) GpuSourceVisuals {
    std::array<float, 4> sun_direction{}, view_position{};
};
struct RtPush {
    std::array<float, 4> camera;
    std::array<std::uint32_t, 4> config;
    std::array<float, 4> environment;
};
struct TemporalPush { std::array<float, 16> previous_vp; std::array<std::uint32_t, 4> config; };
static_assert(sizeof(GpuMesh) == 80 && sizeof(GpuInstance) == 176 && sizeof(GpuSourceVisuals) == 32);
static_assert(sizeof(RtPush) == 48 && sizeof(TemporalPush) == 80);
constexpr VkDeviceSize CounterBytes = 6 * sizeof(std::uint32_t);
}

struct RayTracing::Impl {
    RayTracingContext context;
    std::uint32_t requested{}, effects{}, frame_index{}, capacity{};
    std::string reason;
    std::vector<RayTracingMesh> meshes;
    std::vector<RayTracingTexture> textures;
    std::vector<Acceleration> blas;
    std::vector<std::uint64_t> pose_revisions;
    std::vector<GpuInstance> cached_instances;
    std::vector<std::uint8_t> eligible_instances;
    std::size_t cached_instance_count{};
    bool blas_built{}, tlas_built{}, pending_tlas_build{}, source_visuals_required{}, source_visuals_initialized{};
    bool source_coverage_ready{}, cache_tlas_dirty{true};
    std::optional<std::uint64_t> failed_source_draw_revision;
    bool cached_source_effects_enabled{true};
    GpuSourceVisuals cached_source_visuals;
    Acceleration tlas;
    Buffer mesh_data, instance_data, instance_build, scratch, counters, source_visuals_data;
    VkDeviceSize scratch_alignment{256};
    VkExtent2D extent{};
    RayTracingGBuffer inputs{};
    Image raw, fallback_environment;
    RayTracingEnvironment environment{};
    bool fallback_environment_initialized{}, environment_descriptors_ready{};
    std::array<Image, 2> history_color, history_position, history_normal, history_material, denoise_scratch;
    bool images_initialized{}, history_valid{}, history_written{}, submitted{}, active{}, temporal_descriptors_ready{};
    bool high_quality{}, input_hdr{};
    quality::Denoising denoising{quality::Denoising::Off};
    VkImageLayout raw_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    std::array<VkImageLayout, 2> history_color_layout{VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
    std::uint32_t history_index{}, pending_denoise_passes{};
    std::uint64_t completed_denoise_dispatches{};
    std::array<float, 16> previous_vp{};
    VkSampler sampler{};
    VkDescriptorSetLayout rt_layout{}, temporal_layout{};
    VkDescriptorPool descriptor_pool{};
    bool texture_update_after_bind{};
    std::uint32_t texture_descriptor_count{}, texture_sampler_limit{};
    VkDescriptorSet rt_set{};
    std::array<VkDescriptorSet, 2> temporal_sets{};
    std::array<std::array<VkDescriptorSet, 3>, 2> high_temporal_sets{};
    VkPipelineLayout rt_pipeline_layout{}, temporal_pipeline_layout{};
    VkPipeline rt_pipeline{}, temporal_pipeline{};
    std::uint64_t unique_triangles{}, instanced_triangles{}, instance_count{}, completed_frames{};
    std::uint64_t completed_blas_updates{}, completed_tlas_updates{}, pending_blas_updates{};
    std::array<std::uint64_t, 4> total_queries{};
    std::array<std::uint32_t, 4> last_queries{};
    std::array<std::uint64_t, 2> total_history{};
    std::array<std::uint32_t, 2> last_history{};
    double total_history_weight{};
    float last_configured_history_weight{};
    PFN_vkCreateAccelerationStructureKHR create_as{};
    PFN_vkDestroyAccelerationStructureKHR destroy_as{};
    PFN_vkGetAccelerationStructureBuildSizesKHR build_sizes{};
    PFN_vkGetAccelerationStructureDeviceAddressKHR as_address{};
    PFN_vkCmdBuildAccelerationStructuresKHR build_as{};

    explicit Impl(RayTracingContext c) : context(c) {}
    ~Impl() {
        // Owner waits its sole frame fence / device idle before destroying us.
        auto d = context.device;
        if (rt_pipeline) vkDestroyPipeline(d, rt_pipeline, nullptr);
        if (temporal_pipeline) vkDestroyPipeline(d, temporal_pipeline, nullptr);
        if (rt_pipeline_layout) vkDestroyPipelineLayout(d, rt_pipeline_layout, nullptr);
        if (temporal_pipeline_layout) vkDestroyPipelineLayout(d, temporal_pipeline_layout, nullptr);
        if (descriptor_pool) vkDestroyDescriptorPool(d, descriptor_pool, nullptr);
        if (rt_layout) vkDestroyDescriptorSetLayout(d, rt_layout, nullptr);
        if (temporal_layout) vkDestroyDescriptorSetLayout(d, temporal_layout, nullptr);
        if (sampler) vkDestroySampler(d, sampler, nullptr);
    }
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(context.physical_device, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("Vulkan RT: required memory type unavailable");
    }
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
        Buffer result; result.device = context.device;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::max<VkDeviceSize>(size, 16); info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(context.device, &info, nullptr, &result.buffer), "RT buffer creation");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context.device, result.buffer, &requirements);
        VkMemoryAllocateFlagsInfo address_flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        address_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, host ?
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) allocation.pNext = &address_flags;
        check(vkAllocateMemory(context.device, &allocation, nullptr, &result.memory), "RT buffer memory");
        check(vkBindBufferMemory(context.device, result.buffer, result.memory, 0), "RT buffer binding");
        if (host) check(vkMapMemory(context.device, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped), "RT mapped buffer");
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address.buffer = result.buffer;
            result.address = vkGetBufferDeviceAddress(context.device, &address);
        }
        return result;
    }
    Image image(VkFormat format, bool cube = false) {
        Image result; result.device = context.device;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
        info.extent = cube ? VkExtent3D{1, 1, 1} : VkExtent3D{extent.width, extent.height, 1};
        info.mipLevels = 1; info.arrayLayers = cube ? 6u : 1u; info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = cube ? VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT :
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(context.device, &info, nullptr, &result.image), "RT image creation");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(context.device, result.image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(context.device, &allocation, nullptr, &result.memory), "RT image memory");
        check(vkBindImageMemory(context.device, result.image, result.memory, 0), "RT image binding");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = result.image; view.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D; view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, cube ? 6u : 1u};
        check(vkCreateImageView(context.device, &view, nullptr, &result.view), "RT image view");
        return result;
    }
    Acceleration acceleration(VkAccelerationStructureTypeKHR type, VkDeviceSize size) {
        Acceleration result; result.device = context.device; result.destroy = destroy_as;
        result.storage = buffer(size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        info.buffer = result.storage.buffer; info.size = size; info.type = type;
        check(create_as(context.device, &info, nullptr, &result.handle), "RT acceleration structure creation");
        VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        address.accelerationStructure = result.handle; result.address = as_address(context.device, &address);
        return result;
    }
    VkDeviceAddress scratch_address() const {
        return (scratch.address + scratch_alignment - 1) & ~(scratch_alignment - 1);
    }
    VkAccelerationStructureGeometryKHR mesh_geometry(const RayTracingMesh& mesh) {
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        // Shared BLAS may serve both opaque and cutout instances; shaders confirm coverage.
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        auto& triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = mesh.vertex_address + mesh.position_offset;
        triangles.vertexStride = mesh.vertex_stride; triangles.maxVertex = mesh.vertex_count - 1;
        triangles.indexType = VK_INDEX_TYPE_UINT32; triangles.indexData.deviceAddress = mesh.index_address;
        return geometry;
    }
    void initialize(const RenderScene& scene, std::span<const RayTracingMesh> source_meshes,
                    std::span<const RayTracingTexture> source_textures, const std::filesystem::path& shaders,
                    std::uint32_t requested_effects) {
        requested = requested_effects & 7u;
        if (!requested && !context.initialize_for_capabilities) { reason = "Original raster: ray effects disabled"; return; }
        if (!(context.buffer_device_address && context.acceleration_structure && context.ray_query &&
              context.runtime_descriptor_array && context.sampled_image_nonuniform_indexing)) {
            reason = "Raster fallback: device lacks enabled BDA/accelerationStructure/rayQuery/runtimeDescriptorArray/nonuniform texture capabilities"; return;
        }
        if (source_meshes.size() != scene.meshes.size()) throw std::runtime_error("RT mesh cache differs from shared scene");
        if (source_textures.size() > std::numeric_limits<std::uint32_t>::max() - 117u) {
            reason = "Raster fallback: original texture cache exceeds descriptor count representation"; return;
        }
        meshes.assign(source_meshes.begin(), source_meshes.end()); textures.assign(source_textures.begin(), source_textures.end());
        texture_descriptor_count = static_cast<std::uint32_t>(std::max<std::size_t>(textures.size(), 1));
        const auto sampled_count = texture_descriptor_count + 7u;
        VkPhysicalDeviceAccelerationStructurePropertiesKHR as_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceDescriptorIndexingProperties indexing_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
        indexing_properties.pNext = &as_properties;
        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &indexing_properties;
        vkGetPhysicalDeviceProperties2(context.physical_device, &properties2);
        const auto& limits = properties2.properties.limits;
        texture_sampler_limit = std::min(limits.maxPerStageDescriptorSamplers, limits.maxDescriptorSetSamplers);
        // Temporal sets stay ordinary, even when the RT texture array requires update-after-bind.
        if (limits.maxPerStageDescriptorSamplers < 8 || limits.maxDescriptorSetSamplers < 8 ||
            limits.maxPerStageDescriptorSampledImages < 8 || limits.maxDescriptorSetSampledImages < 8 ||
            limits.maxPerStageDescriptorStorageImages < 4 || limits.maxDescriptorSetStorageImages < 4 ||
            limits.maxPerStageDescriptorStorageBuffers < 1 || limits.maxDescriptorSetStorageBuffers < 1 ||
            limits.maxPerStageResources < 13) {
            reason = "Raster fallback: ordinary temporal denoising descriptor limits insufficient"; return;
        }
        const bool ordinary_texture_limits = sampled_count <= texture_sampler_limit &&
            sampled_count <= limits.maxPerStageDescriptorSampledImages &&
            sampled_count <= limits.maxDescriptorSetSampledImages &&
            sampled_count + 6u <= limits.maxPerStageResources;
        if (ordinary_texture_limits) {
            if (limits.maxPerStageDescriptorStorageImages < 1 || limits.maxDescriptorSetStorageImages < 1 ||
                limits.maxPerStageDescriptorStorageBuffers < 3 || limits.maxDescriptorSetStorageBuffers < 3 ||
                limits.maxPerStageDescriptorUniformBuffers < 1 || limits.maxDescriptorSetUniformBuffers < 1 ||
                as_properties.maxPerStageDescriptorAccelerationStructures < 1 ||
                as_properties.maxDescriptorSetAccelerationStructures < 1) {
                reason = "Raster fallback: ordinary ray-query descriptor limits insufficient"; return;
            }
        } else {
            if (!context.sampled_image_update_after_bind) {
                reason = "Raster fallback: original texture cache exceeds ordinary ray-query limits and sampled-image update-after-bind was not enabled"; return;
            }
            texture_sampler_limit = std::min(indexing_properties.maxPerStageDescriptorUpdateAfterBindSamplers,
                indexing_properties.maxDescriptorSetUpdateAfterBindSamplers);
            if (sampled_count > texture_sampler_limit ||
                sampled_count > indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages ||
                sampled_count > indexing_properties.maxDescriptorSetUpdateAfterBindSampledImages ||
                sampled_count + 6u > indexing_properties.maxPerStageUpdateAfterBindResources ||
                indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageImages < 1 ||
                indexing_properties.maxDescriptorSetUpdateAfterBindStorageImages < 1 ||
                indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers < 3 ||
                indexing_properties.maxDescriptorSetUpdateAfterBindStorageBuffers < 3 ||
                indexing_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers < 1 ||
                indexing_properties.maxDescriptorSetUpdateAfterBindUniformBuffers < 1 ||
                as_properties.maxPerStageDescriptorUpdateAfterBindAccelerationStructures < 1 ||
                as_properties.maxDescriptorSetUpdateAfterBindAccelerationStructures < 1) {
                reason = "Raster fallback: original texture cache exceeds queried update-after-bind ray-query descriptor limits"; return;
            }
            // Shared pool reserves eight ordinary material-aware temporal sets.
            if (texture_descriptor_count + 117u > indexing_properties.maxUpdateAfterBindDescriptorsInAllPools) {
                reason = "Raster fallback: ray-query/temporal descriptor pool exceeds queried update-after-bind pool budget"; return;
            }
            texture_update_after_bind = true;
        }
        for (VkFormat format : {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT}) {
            VkFormatProperties support{}; vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &support);
            constexpr auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if ((support.optimalTilingFeatures & required) != required) {
                reason = "Raster fallback: ray history storage image format unsupported"; return;
            }
        }
#define C4_RT_PROC(member, name) member = name; if (!member) throw std::runtime_error("Missing enabled Vulkan procedure " #name)
        C4_RT_PROC(create_as, vkCreateAccelerationStructureKHR);
        C4_RT_PROC(destroy_as, vkDestroyAccelerationStructureKHR);
        C4_RT_PROC(build_sizes, vkGetAccelerationStructureBuildSizesKHR);
        C4_RT_PROC(as_address, vkGetAccelerationStructureDeviceAddressKHR);
        C4_RT_PROC(build_as, vkCmdBuildAccelerationStructuresKHR);
#undef C4_RT_PROC
        scratch_alignment = as_properties.minAccelerationStructureScratchOffsetAlignment;
        if (scene.instances.size() > std::min<std::uint64_t>(as_properties.maxInstanceCount, 0x1000000ull))
            throw std::runtime_error("RT original instance count exceeds hardware/custom-index limit");
        capacity = static_cast<std::uint32_t>(std::max<std::size_t>(scene.instances.size(), 1));
        blas.resize(meshes.size()); pose_revisions.resize(meshes.size());
        cached_instances.resize(capacity); eligible_instances.resize(capacity);
        for (std::size_t i = 0; i < meshes.size(); ++i) pose_revisions[i] = scene.meshes[i].pose_revision;
        VkDeviceSize scratch_size{};
        // Cached mesh geometry is built once; TLAS geometry changes only when dirty.
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            const auto& mesh = meshes[i];
            if (!mesh.index_count || !mesh.vertex_count) continue;
            if (!mesh.vertex_address || !mesh.index_address || mesh.index_count % 3 || mesh.vertex_stride % 4 ||
                mesh.position_offset % 4 || mesh.uv_offset % 4 || mesh.normal_offset % 4 ||
                mesh.position_offset + 12 > mesh.vertex_stride || mesh.uv_offset + 8 > mesh.vertex_stride ||
                mesh.normal_offset + 12 > mesh.vertex_stride)
                throw std::runtime_error("RT requires original indexed float XYZ/UV/normal mesh buffers");
            auto geometry = mesh_geometry(mesh);
            VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            build.geometryCount = 1; build.pGeometries = &geometry;
            std::uint32_t triangles = mesh.index_count / 3;
            if (triangles > as_properties.maxPrimitiveCount) throw std::runtime_error("RT original mesh exceeds hardware primitive limit");
            VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            build_sizes(context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &triangles, &sizes);
            blas[i] = acceleration(build.type, sizes.accelerationStructureSize);
            scratch_size = std::max({scratch_size, sizes.buildScratchSize, sizes.updateScratchSize}); unique_triangles += triangles;
        }
        instance_build = buffer(sizeof(VkAccelerationStructureInstanceKHR) * capacity,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true);
        instance_data = buffer(sizeof(GpuInstance) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        mesh_data = buffer(sizeof(GpuMesh) * std::max<std::size_t>(meshes.size(), 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        counters = buffer(CounterBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        std::memset(counters.mapped, 0, CounterBytes);
        source_visuals_data = buffer(sizeof(GpuSourceVisuals), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        std::memset(source_visuals_data.mapped, 0, sizeof(GpuSourceVisuals));
        auto* gpu_meshes = static_cast<GpuMesh*>(mesh_data.mapped);
        for (std::size_t i = 0; i < meshes.size(); ++i) gpu_meshes[i] = {meshes[i].vertex_address, meshes[i].index_address,
            {meshes[i].vertex_stride, meshes[i].position_offset, meshes[i].uv_offset, meshes[i].index_count},
            {meshes[i].normal_offset, meshes[i].vertex_count, 0, 0},
            meshes[i].tangent_address, 0, {meshes[i].tangent_stride, meshes[i].tangent_offset, 0, 0}};
        VkAccelerationStructureGeometryKHR geometry = instance_geometry();
        VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.geometryCount = 1; build.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        build_sizes(context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &capacity, &sizes);
        tlas = acceleration(build.type, sizes.accelerationStructureSize); scratch_size = std::max(scratch_size, sizes.buildScratchSize);
        scratch = buffer(scratch_size + scratch_alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_NEAREST; sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        try {
            check(vkCreateSampler(context.device, &sampler_info, nullptr, &sampler), "RT history sampler");
            fallback_environment = image(VK_FORMAT_R16G16B16A16_SFLOAT, true);
            create_descriptors();
            rt_pipeline = pipeline(shaders / "rt.comp.spv", rt_layout, sizeof(RtPush), rt_pipeline_layout);
            temporal_pipeline = pipeline(shaders / "temporal.comp.spv", temporal_layout, sizeof(TemporalPush), temporal_pipeline_layout);
        } catch (const std::runtime_error& error) {
            reason = std::string("Raster fallback: ray-query/denoising pipeline resources unavailable: ") + error.what();
            return;
        }
        active = true;
        update_environment({});
        if (context.initialize_for_capabilities && scene.instances.empty() && meshes.empty()) {
            build_empty_tlas();
            source_coverage_ready = true;
            reason = "Hardware ray-query pipelines prepared for capabilities; ray-effect dispatch disabled";
        } else {
            reason = "Raster fallback: waiting for actual source world draw coverage";
        }
    }
    VkAccelerationStructureGeometryKHR instance_geometry() {
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.data.deviceAddress = instance_build.address;
        return geometry;
    }
    static void as_barrier(VkCommandBuffer command) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    void build_cached_meshes(const FrameSnapshot& frame, VkCommandBuffer command) {
        if (frame.scene.meshes.size() != meshes.size()) throw std::runtime_error("RT geometry cache changed during playback");
        VkMemoryBarrier upload{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        upload.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        upload.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &upload, 0, nullptr, 0, nullptr);
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (!blas[i].handle) continue;
            if (frame.scene.meshes[i].vertices.size() != meshes[i].vertex_count ||
                frame.scene.meshes[i].indices.size() != meshes[i].index_count)
                throw std::runtime_error("RT pose update changed original topology");
            auto geometry = mesh_geometry(meshes[i]);
            VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; build.dstAccelerationStructure = blas[i].handle;
            build.geometryCount = 1; build.pGeometries = &geometry; build.scratchData.deviceAddress = scratch_address();
            VkAccelerationStructureBuildRangeInfoKHR range{}; range.primitiveCount = meshes[i].index_count / 3;
            const auto* ranges = &range; build_as(command, 1, &build, &ranges); as_barrier(command);
            pose_revisions[i] = frame.scene.meshes[i].pose_revision;
        }
        blas_built = true;
    }
    void build_empty_tlas() {
        VkCommandBuffer command{};
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = context.command_pool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(context.device, &allocation, &command), "RT upload command");
        VkFence fence{};
        try {
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(command, &begin), "RT upload begin");
            auto geometry = instance_geometry();
            VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; build.dstAccelerationStructure = tlas.handle;
            build.geometryCount = 1; build.pGeometries = &geometry; build.scratchData.deviceAddress = scratch_address();
            VkAccelerationStructureBuildRangeInfoKHR range{};
            const auto* ranges = &range; build_as(command, 1, &build, &ranges); as_barrier(command);
            check(vkEndCommandBuffer(command), "RT upload end");
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(vkCreateFence(context.device, &fence_info, nullptr, &fence), "RT upload fence");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            check(vkQueueSubmit(context.queue, 1, &submit, fence), "RT upload submit");
            check(vkWaitForFences(context.device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()), "RT upload wait");
        } catch (...) {
            if (fence) { vkQueueWaitIdle(context.queue); vkDestroyFence(context.device, fence, nullptr); }
            vkFreeCommandBuffers(context.device, context.command_pool, 1, &command); throw;
        }
        vkDestroyFence(context.device, fence, nullptr); vkFreeCommandBuffers(context.device, context.command_pool, 1, &command);
        tlas_built = true;
        instance_count = instanced_triangles = 0;
        ++completed_tlas_updates;
    }
    bool refit_meshes(const FrameSnapshot& frame, VkCommandBuffer command) {
        if (frame.scene.meshes.size() != meshes.size()) throw std::runtime_error("RT geometry cache changed during playback");
        bool upload_barrier_recorded{};
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (!blas[i].handle || pose_revisions[i] == frame.scene.meshes[i].pose_revision) continue;
            if (frame.scene.meshes[i].vertices.size() != meshes[i].vertex_count ||
                frame.scene.meshes[i].indices.size() != meshes[i].index_count)
                throw std::runtime_error("RT pose update changed original topology");
            if (!upload_barrier_recorded) {
                VkMemoryBarrier upload{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                upload.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
                upload.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &upload, 0, nullptr, 0, nullptr);
                upload_barrier_recorded = true;
            }
            auto geometry = mesh_geometry(meshes[i]);
            VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
            build.srcAccelerationStructure = build.dstAccelerationStructure = blas[i].handle;
            build.geometryCount = 1; build.pGeometries = &geometry; build.scratchData.deviceAddress = scratch_address();
            VkAccelerationStructureBuildRangeInfoKHR range{}; range.primitiveCount = meshes[i].index_count / 3;
            const auto* ranges = &range; build_as(command, 1, &build, &ranges); as_barrier(command);
            ++pending_blas_updates;
            pose_revisions[i] = frame.scene.meshes[i].pose_revision;
        }
        return upload_barrier_recorded;
    }
    VkPipeline pipeline(const std::filesystem::path& path, VkDescriptorSetLayout layout,
                        std::uint32_t push_size, VkPipelineLayout& pipeline_layout) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Missing installed Vulkan shader: " + path.string());
        const auto length = input.tellg();
        if (length <= 0 || static_cast<std::uint64_t>(length) % 4) throw std::runtime_error("Invalid SPIR-V shader: " + path.string());
        std::vector<std::uint32_t> words(static_cast<std::size_t>(length) / 4);
        input.seekg(0); input.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(length));
        if (!input) throw std::runtime_error("Unreadable SPIR-V shader: " + path.string());
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = words.size() * 4; module_info.pCode = words.data();
        VkShaderModule module{}; check(vkCreateShaderModule(context.device, &module_info, nullptr, &module), "RT shader module");
        VkPipeline result{};
        try {
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size};
            VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layout_info.setLayoutCount = 1; layout_info.pSetLayouts = &layout; layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &push;
            check(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout), "RT pipeline layout");
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = module; info.stage.pName = "main"; info.layout = pipeline_layout;
            check(vkCreateComputePipelines(context.device, context.pipeline_cache, 1, &info, nullptr, &result), "RT compute pipeline");
        } catch (...) { vkDestroyShaderModule(context.device, module, nullptr); throw; }
        vkDestroyShaderModule(context.device, module, nullptr); return result;
    }
    void create_descriptors() {
        std::array<VkDescriptorSetLayoutBinding, 14> bindings{};
        const std::array<VkDescriptorType, 14> types{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
        for (std::uint32_t i = 0; i < bindings.size(); ++i) bindings[i] = {i, types[i],
            i == 7 ? texture_descriptor_count : 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = static_cast<std::uint32_t>(bindings.size()); info.pBindings = bindings.data();
        std::array<VkDescriptorBindingFlags, 14> binding_flags{};
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        if (texture_update_after_bind) {
            binding_flags[7] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            flags_info.bindingCount = static_cast<std::uint32_t>(binding_flags.size());
            flags_info.pBindingFlags = binding_flags.data();
            info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            info.pNext = &flags_info;
        }
        check(vkCreateDescriptorSetLayout(context.device, &info, nullptr, &rt_layout), "RT descriptor layout");
        info.flags = 0; info.pNext = nullptr;
        info.bindingCount = 13;
        for (std::uint32_t i = 0; i < info.bindingCount; ++i) bindings[i] = {i,
            i < 6 || i == 9 || i == 10 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
            i == 12 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        check(vkCreateDescriptorSetLayout(context.device, &info, nullptr, &temporal_layout), "Temporal descriptor layout");
        const std::array<VkDescriptorPoolSize, 5> sizes{{
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture_descriptor_count + 71u},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 33}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        if (texture_update_after_bind) pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool.maxSets = 9; pool.poolSizeCount = sizes.size(); pool.pPoolSizes = sizes.data();
        check(vkCreateDescriptorPool(context.device, &pool, nullptr, &descriptor_pool), "RT descriptor pool");
        std::array<VkDescriptorSetLayout, 9> layouts{};
        layouts.fill(temporal_layout); layouts[0] = rt_layout;
        std::array<VkDescriptorSet, 9> sets{};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptor_pool; allocate.descriptorSetCount = layouts.size(); allocate.pSetLayouts = layouts.data();
        check(vkAllocateDescriptorSets(context.device, &allocate, sets.data()), "RT descriptor sets");
        rt_set = sets[0]; temporal_sets = {sets[1], sets[2]};
        for (unsigned i = 0; i < 2; ++i)
            for (unsigned pass = 0; pass < 3; ++pass)
                high_temporal_sets[i][pass] = sets[3 + i * 3 + pass];
        VkWriteDescriptorSetAccelerationStructureKHR as_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        as_write.accelerationStructureCount = 1; as_write.pAccelerationStructures = &tlas.handle;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.pNext = &as_write; write.dstSet = rt_set; write.dstBinding = 0; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        for (auto [binding, resource] : std::array<std::pair<std::uint32_t, Buffer*>, 3>{{{5, &instance_data}, {6, &mesh_data}, {8, &counters}}}) {
            VkDescriptorBufferInfo data{resource->buffer, 0, VK_WHOLE_SIZE};
            write.pNext = nullptr; write.dstBinding = binding; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &data;
            vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        }
        VkDescriptorBufferInfo source_visuals{source_visuals_data.buffer, 0, sizeof(GpuSourceVisuals)};
        write.pNext = nullptr; write.dstBinding = 9; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &source_visuals;
        vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        if (!textures.empty()) {
            std::vector<VkDescriptorImageInfo> data; data.reserve(textures.size());
            for (const auto& texture : textures) data.push_back({texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pNext = nullptr; write.dstBinding = 7; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pBufferInfo = nullptr; write.descriptorCount = static_cast<std::uint32_t>(data.size()); write.pImageInfo = data.data();
            vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        }
    }
    void bind_image(VkDescriptorSet set, std::uint32_t binding, VkImageView view, bool storage, VkImageLayout layout) {
        VkDescriptorImageInfo image_info{storage ? VK_NULL_HANDLE : sampler, view, layout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info; vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
    }
    void update_environment(RayTracingEnvironment updated) {
        if (!active) return;
        if (submitted) throw std::runtime_error("RT frame_completed must follow the frame fence before environment refresh");
        if (bool(updated.irradiance) != bool(updated.prefiltered) ||
            (updated.irradiance && (!updated.sampler || !std::isfinite(updated.max_lod) || updated.max_lod < 0)))
            throw std::runtime_error("RT prepared environment requires both cube views, sampler and finite nonnegative max LOD");
        if (!updated.irradiance) updated = {};
        const bool changed = environment.irradiance != updated.irradiance ||
            environment.prefiltered != updated.prefiltered || environment.sampler != updated.sampler ||
            environment.max_lod != updated.max_lod;
        if (!changed && environment_descriptors_ready) return;
        environment = updated;
        const bool prepared = environment.irradiance && environment.prefiltered;
        const std::array<VkDescriptorImageInfo, 2> images{{
            {prepared ? environment.sampler : sampler,
             prepared ? environment.irradiance : fallback_environment.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {prepared ? environment.sampler : sampler,
             prepared ? environment.prefiltered : fallback_environment.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = rt_set; writes[i].dstBinding = 12 + i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(context.device, writes.size(), writes.data(), 0, nullptr);
        environment_descriptors_ready = true;
        if (changed) history_valid = false;
    }
    void update_textures(std::span<const RayTracingTexture> updated) {
        if (!active) return;
        if (updated.size() != textures.size()) throw std::runtime_error("RT texture descriptor count changed during playback");
        if (submitted) throw std::runtime_error("RT frame_completed must follow the frame fence before texture refresh");
        bool changed{};
        for (std::size_t i = 0; i < textures.size(); ++i) {
            if (textures[i].view == updated[i].view && textures[i].sampler == updated[i].sampler) continue;
            VkDescriptorImageInfo image_info{updated[i].sampler, updated[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = rt_set; write.dstBinding = 7; write.dstArrayElement = static_cast<std::uint32_t>(i);
            write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
            textures[i] = updated[i]; changed = true;
        }
        if (changed) history_valid = false;
    }
    void resize(VkExtent2D new_extent, RayTracingGBuffer new_inputs) {
        if (!active) return;
        if (submitted) throw std::runtime_error("RT frame_completed must follow the frame fence before resize");
        if (!new_inputs.color || !new_inputs.world_position || !new_inputs.normal ||
            !new_inputs.material_receiver || !new_inputs.sun_direct)
            throw std::runtime_error("RT requires all five raster receiver inputs");
        if (!new_extent.width || !new_extent.height) throw std::runtime_error("RT zero-sized target");
        temporal_descriptors_ready = false;
        if (extent.width != new_extent.width || extent.height != new_extent.height || !raw.image) {
            extent = new_extent;
            try {
                raw = image(VK_FORMAT_R16G16B16A16_SFLOAT);
                for (unsigned i = 0; i < 2; ++i) {
                    history_color[i] = image(VK_FORMAT_R16G16B16A16_SFLOAT);
                    history_position[i] = image(VK_FORMAT_R32G32B32A32_SFLOAT);
                    history_normal[i] = image(VK_FORMAT_R16G16B16A16_SFLOAT);
                    history_material[i] = image(VK_FORMAT_R32G32B32A32_SFLOAT);
                    denoise_scratch[i] = image(VK_FORMAT_R16G16B16A16_SFLOAT);
                }
            } catch (const std::runtime_error& error) {
                active = false; invalidate_source_coverage();
                reason = std::string("Raster fallback: ray denoising images unavailable: ") + error.what();
                return;
            }
            images_initialized = history_written = false; history_index = 0;
            raw_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            history_color_layout.fill(VK_IMAGE_LAYOUT_UNDEFINED);
        }
        inputs = new_inputs; history_valid = false; denoising = quality::Denoising::Off;
        bind_image(rt_set, 1, inputs.color, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind_image(rt_set, 2, inputs.world_position, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind_image(rt_set, 3, inputs.normal, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind_image(rt_set, 4, raw.view, true, VK_IMAGE_LAYOUT_GENERAL);
        bind_image(rt_set, 10, inputs.material_receiver, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind_image(rt_set, 11, inputs.sun_direct, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (textures.empty()) bind_image(rt_set, 7, inputs.color, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (unsigned i = 0; i < 2; ++i) {
            const auto previous = i ^ 1u;
            for (unsigned pass = 0; pass < 4; ++pass) {
                const auto set = pass == 0 ? temporal_sets[i] : high_temporal_sets[i][pass - 1];
                const auto source = pass < 2 ? raw.view : denoise_scratch[pass - 2].view;
                const auto output = pass == 1 ? denoise_scratch[0].view :
                                    pass == 2 ? denoise_scratch[1].view : history_color[i].view;
                bind_image(set, 0, source, false, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 1, inputs.world_position, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                bind_image(set, 2, inputs.normal, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                bind_image(set, 3, history_color[previous].view, false, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 4, history_position[previous].view, false, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 5, history_normal[previous].view, false, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 6, output, true, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 7, history_position[i].view, true, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 8, history_normal[i].view, true, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 9, inputs.material_receiver, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                bind_image(set, 10, history_material[previous].view, false, VK_IMAGE_LAYOUT_GENERAL);
                bind_image(set, 11, history_material[i].view, true, VK_IMAGE_LAYOUT_GENERAL);
                VkDescriptorBufferInfo counter_info{counters.buffer, 0, CounterBytes};
                VkWriteDescriptorSet counter_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                counter_write.dstSet = set; counter_write.dstBinding = 12; counter_write.descriptorCount = 1;
                counter_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; counter_write.pBufferInfo = &counter_info;
                vkUpdateDescriptorSets(context.device, 1, &counter_write, 0, nullptr);
            }
        }
        temporal_descriptors_ready = inputs.color && inputs.world_position && inputs.normal &&
                                    inputs.material_receiver && inputs.sun_direct;
    }
    void completed() {
        if (!active || !submitted) return;
        std::memcpy(last_queries.data(), counters.mapped, 16);
        for (unsigned i = 0; i < 4; ++i) total_queries[i] += last_queries[i];
        std::memcpy(last_history.data(), static_cast<const std::byte*>(counters.mapped) + sizeof(last_queries), sizeof(last_history));
        for (unsigned i = 0; i < last_history.size(); ++i) total_history[i] += last_history[i];
        last_configured_history_weight = temporal_history_weight();
        total_history_weight += double(last_history[0]) * last_configured_history_weight;
        completed_blas_updates += pending_blas_updates; pending_blas_updates = 0;
        completed_denoise_dispatches += pending_denoise_passes; pending_denoise_passes = 0;
        completed_tlas_updates += pending_tlas_build; pending_tlas_build = false;
        std::memset(counters.mapped, 0, CounterBytes); ++completed_frames; submitted = false;
    }
    static void transition(VkCommandBuffer command, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                           VkAccessFlags source_access, VkAccessFlags target_access, VkPipelineStageFlags source_stage,
                           VkPipelineStageFlags target_stage, std::uint32_t layers = 1) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = source_access; barrier.dstAccessMask = target_access;
        barrier.oldLayout = old_layout; barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        vkCmdPipelineBarrier(command, source_stage, target_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    void invalidate_source_coverage() {
        source_coverage_ready = false; effects = 0; history_valid = false;
        // A rejected snapshot may have already updated earlier cache slots.
        cache_tlas_dirty = true;
        instance_count = instanced_triangles = 0;
    }
    bool cache_instances(const FrameSnapshot& frame, bool& tlas_dirty) {
        if (frame.scene.meshes.size() != meshes.size()) throw std::runtime_error("RT geometry cache changed during playback");
        const auto instances = frame.instances;
        const bool source_effects_enabled = frame.source_effects_enabled;
        auto* gpu_instances = static_cast<GpuInstance*>(instance_data.mapped);
        if (cached_source_effects_enabled != source_effects_enabled) {
            history_valid = false;
            cached_source_effects_enabled = source_effects_enabled;
        }
        source_visuals_required = false;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            const auto& instance = instances[i];
            if (instance.mesh_index >= meshes.size()) throw std::runtime_error("RT instance references absent cached mesh");
            if (instance.source_sprite && !instance.source_sprite_state_known &&
                !instance.native_prepared_inactive && !(instance.flags & (1u << 8))) {
                reason = "Raster fallback: source sprite ray coverage incomplete: ";
                reason += source_sprite_unavailable_reason(instance);
                invalidate_source_coverage();
                return false;
            }
            auto& cached = cached_instances[i];
            const bool added = i >= cached_instance_count;
            const bool model_changed = added ||
                std::memcmp(cached.model.data(), instance.model_to_world.data(), sizeof(cached.model)) != 0;
            const bool mesh_changed = added || cached.data[0] != instance.mesh_index;
            const auto& material = instance.material_state;
            const auto draw_state = original_material_draw_state(material, source_effects_enabled);
            if (draw_state.passes.empty()) {
                reason = "Raster fallback: source material draw has no effective pass for ray coverage";
                invalidate_source_coverage();
                return false;
            }
            const auto& raster = draw_state.passes.front().raster;
            const auto effective_raster = instance.original_alpha ?
                apply_original_model_alpha(raster, *instance.original_alpha) : raster;
            const bool cutout = effective_raster.alpha_comparison == AlphaComparison::GreaterEqual;
            const bool additive = effective_raster.blend && effective_raster.destination_blend == BlendFactor::One;
            const bool eligible = instance_visible(instance) && !additive && blas[instance.mesh_index].handle;
            float alpha_mode = cutout ? 1.0f : 0.0f;
            if (effective_raster.blend) {
                if (effective_raster.destination_blend == BlendFactor::InverseSourceAlpha) alpha_mode = 2.0f;
                else if (effective_raster.destination_blend == BlendFactor::SourceAlpha) alpha_mode = 3.0f;
            }
            const bool fixed_base_uv0 = draw_state.technique == MaterialTechnique::FixedFunction ||
                draw_state.technique == MaterialTechnique::EnvCubeMix ||
                draw_state.technique == MaterialTechnique::EnvCubeAlpha;
            const bool normal_optim = draw_state.technique == MaterialTechnique::NormalOptim;
            const auto normal_texture = normal_optim && material.material_skins[0] && *material.material_skins[0] < textures.size() ?
                *material.material_skins[0] : 0xffffffffu;
            // NOSHADER uses the known pre-effect AlphaOp, not the authored technique classification.
            const bool alpha_decision_known = instance.original_alpha.has_value();
            const bool diffuse_alpha_finite = alpha_decision_known && std::isfinite(instance.original_alpha->diffuse_alpha);
            const bool alpha_reference_finite = std::isfinite(effective_raster.alpha_reference);
            const bool alpha_operator_known = !source_effects_enabled || material.fixed_alpha_known;
            if (eligible && (!alpha_decision_known || !diffuse_alpha_finite || !alpha_reference_finite ||
                (alpha_mode != 0.0f && fixed_base_uv0 && !alpha_operator_known))) {
                std::ostringstream failure;
                failure << "Raster fallback: ray opacity requires actual source draw alpha decision, known alpha operator and finite diffuse alpha/reference"
                    << "; instance_index=" << i << "; source_entity=";
                if (instance.source_entity) failure << *instance.source_entity; else failure << "null";
                failure << "; source="; write_json_string(failure, instance.source);
                failure << "; material="; write_json_string(failure, instance.material);
                failure << "; alpha_decision_known=" << alpha_decision_known
                    << "; alpha_operator_known=" << alpha_operator_known
                    << "; diffuse_alpha_finite=" << diffuse_alpha_finite
                    << "; alpha_reference_finite=" << alpha_reference_finite;
                reason = failure.str();
                invalidate_source_coverage();
                return false;
            }
            if (eligible && alpha_mode != 0.0f && !fixed_base_uv0 && !normal_optim) {
                std::ostringstream failure;
                failure << "Raster fallback: source effect alpha equation or texture/UV binding not supported by ray coverage"
                    << "; instance_index=" << i << "; source_entity=";
                if (instance.source_entity) failure << *instance.source_entity; else failure << "null";
                failure << "; source="; write_json_string(failure, instance.source);
                failure << "; material="; write_json_string(failure, instance.material);
                failure << "; effect_technique=" << static_cast<std::uint32_t>(draw_state.technique)
                    << "; alpha_mode=" << alpha_mode
                    << "; alpha_decision_known=" << alpha_decision_known
                    << "; alpha_operator_known=" << alpha_operator_known
                    << "; diffuse_alpha_finite=" << diffuse_alpha_finite
                    << "; alpha_reference_finite=" << alpha_reference_finite
                    << "; ambient_alpha_known=" << material.ambient_alpha_known
                    << "; skills_known_mask=" << material.skills_known_mask;
                reason = failure.str();
                invalidate_source_coverage();
                return false;
            }
            if (eligible && alpha_mode != 0.0f && normal_optim) {
                if (!material.ambient_alpha_known || (material.skills_known_mask & 0x7fu) != 0x7fu) {
                    reason = "Raster fallback: source NormalOptim ray coverage requires known original ambient alpha and MATERIAL skill1 through skill7";
                    invalidate_source_coverage();
                    return false;
                }
                const auto& mesh = meshes[instance.mesh_index];
                // Provenance is live: cold meshes may acquire source tangents on activation.
                const auto& source_mesh = frame.scene.meshes[instance.mesh_index];
                const auto provenance = source_mesh.tangent_provenance;
                const bool source_tangents = provenance == assets::TangentProvenance::ImportedSource ||
                    provenance == assets::TangentProvenance::SourceAlgorithmNativePort;
                if (!source_tangents || source_mesh.tertiary_uv.size() != source_mesh.vertices.size() ||
                    !mesh.tangent_address || mesh.tangent_address % 4 || mesh.tangent_stride % 4 || mesh.tangent_offset % 4 ||
                    mesh.tangent_offset > mesh.tangent_stride || mesh.tangent_stride - mesh.tangent_offset < 12 ||
                    normal_texture == 0xffffffffu) {
                    reason = "Raster fallback: source NormalOptim ray coverage requires proven source tangent buffer and normal texture";
                    invalidate_source_coverage();
                    return false;
                }
                source_visuals_required = true;
            }
            if (eligible && (model_changed || !eligible_instances[i])) {
                const auto& m = instance.model_to_world;
                // Compute in double: tiny but invertible source scales remain valid.
                const double determinant =
                    double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6]) -
                    double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2]) +
                    double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
                if (!std::all_of(m.begin(), m.end(), [](float value) { return std::isfinite(value); }) ||
                    !std::isfinite(determinant) || determinant == 0.0) {
                    // Validate the whole snapshot before recording any AS commands.
                    reason = "Raster fallback: visible source instance transform is non-finite or singular";
                    invalidate_source_coverage();
                    return false;
                }
            }
            if (eligible != bool(eligible_instances[i])) {
                eligible_instances[i] = eligible;
                tlas_dirty = true;
            } else if (eligible && (model_changed || mesh_changed)) {
                tlas_dirty = true;
            }
            auto& gpu = gpu_instances[i];
            if (model_changed) gpu.model = cached.model = instance.model_to_world;
            if (added || std::memcmp(cached.diffuse.data(), instance.diffuse.data(), sizeof(cached.diffuse)) != 0)
                gpu.diffuse = cached.diffuse = instance.diffuse;
            const auto texture = material.entity_skins[0] && *material.entity_skins[0] < textures.size() ?
                *material.entity_skins[0] :
                instance.texture_index && *instance.texture_index < textures.size() ? *instance.texture_index : 0xffffffffu;
            const std::array<std::uint32_t, 4> data{instance.mesh_index, texture,
                instance.flags, static_cast<std::uint32_t>(instance.kind)};
            for (std::size_t field = 0; field < data.size(); ++field)
                if (added || cached.data[field] != data[field]) gpu.data[field] = cached.data[field] = data[field];
            // Payload changes leave non-opaque BLAS candidate handling and TLAS topology unchanged.
            const std::array<float, 4> alpha_policy{alpha_mode, effective_raster.alpha_reference,
                alpha_mode != 0.0f && normal_optim ? 1.0f : 0.0f,
                instance.original_alpha ? instance.original_alpha->diffuse_alpha : 0.0f};
            if (added && (alpha_mode == 0.0f || !normal_optim)) {
                gpu.skill1 = cached.skill1 = {};
                gpu.skill5 = cached.skill5 = {};
                gpu.material_ambient = cached.material_ambient = {};
            }
            for (std::size_t field = 0; field < alpha_policy.size(); ++field) {
                if (added || std::memcmp(&cached.alpha_policy[field], &alpha_policy[field], sizeof(float)) != 0)
                    gpu.alpha_policy[field] = cached.alpha_policy[field] = alpha_policy[field];
                if (alpha_mode != 0.0f && normal_optim) {
                    if (added || std::memcmp(&cached.skill1[field], &material.skills[field], sizeof(float)) != 0)
                        gpu.skill1[field] = cached.skill1[field] = material.skills[field];
                    if (added || std::memcmp(&cached.skill5[field], &material.skills[field + 4], sizeof(float)) != 0)
                        gpu.skill5[field] = cached.skill5[field] = material.skills[field + 4];
                    if (added || std::memcmp(&cached.material_ambient[field], &material.ambient[field], sizeof(float)) != 0)
                        gpu.material_ambient[field] = cached.material_ambient[field] = material.ambient[field];
                }
            }
            const std::array<std::uint32_t, 4> source_textures{normal_texture,
                instance.original_alpha && instance.original_alpha->modulate_diffuse ? 1u : 0u, 0,
                cutout ? 1u : 0u};
            for (std::size_t field = 0; field < source_textures.size(); ++field)
                if (added || cached.source_textures[field] != source_textures[field])
                    gpu.source_textures[field] = cached.source_textures[field] = source_textures[field];
        }
        for (std::size_t i = instances.size(); i < cached_instance_count; ++i) {
            if (eligible_instances[i]) {
                eligible_instances[i] = 0;
                tlas_dirty = true;
            }
        }
        cached_instance_count = instances.size();
        return true;
    }
    bool cache_source_visuals(const FrameSnapshot& frame) {
        if (!frame.visuals || !frame.visuals->view_inputs_known) {
            source_visuals_initialized = false;
            cached_source_visuals.sun_direction[3] = 0;
            static_cast<GpuSourceVisuals*>(source_visuals_data.mapped)->sun_direction[3] = 0;
            if (!source_visuals_required) return true;
            reason = "Raster fallback: source NormalOptim ray coverage requires known original view and sun inputs";
            invalidate_source_coverage();
            return false;
        }
        const auto& source = *frame.visuals;
        auto& gpu = *static_cast<GpuSourceVisuals*>(source_visuals_data.mapped);
        const bool sun_known = std::all_of(source.sun_direction.begin(), source.sun_direction.end(),
            [](float value) { return std::isfinite(value); }) &&
            double(source.sun_direction[0]) * source.sun_direction[0] +
            double(source.sun_direction[1]) * source.sun_direction[1] +
            double(source.sun_direction[2]) * source.sun_direction[2] > 1e-8;
        const std::array<float, 4> sun_direction{source.sun_direction[0], source.sun_direction[1], source.sun_direction[2],
                                               sun_known ? 1.0f : 0.0f};
        const std::array<float, 4> view_position{source.view_position[0], source.view_position[1], source.view_position[2], 1};
        for (std::size_t field = 0; field < 4; ++field) {
            if (!source_visuals_initialized || std::memcmp(&cached_source_visuals.sun_direction[field], &sun_direction[field], sizeof(float)) != 0)
                gpu.sun_direction[field] = cached_source_visuals.sun_direction[field] = sun_direction[field];
            if (!source_visuals_initialized || std::memcmp(&cached_source_visuals.view_position[field], &view_position[field], sizeof(float)) != 0)
                gpu.view_position[field] = cached_source_visuals.view_position[field] = view_position[field];
        }
        source_visuals_initialized = true;
        return true;
    }
    RayTracingOutput record(const FrameSnapshot& frame, VkCommandBuffer command, std::array<float, 3> camera,
                            quality::Denoising requested_denoising, bool high_quality_requested, bool input_hdr_requested) {
        if (!active) return {};
        if (submitted) throw std::runtime_error("RT frame_completed must follow the frame fence before reuse");
        if (frame.overlay && !frame.overlay->draw_world) {
            reason = "Raster fallback: source world draw disabled";
            invalidate_source_coverage();
            return {};
        }
        if (frame.instances.empty()) {
            if (context.initialize_for_capabilities && meshes.empty() && frame.scene.instances.empty()) return {};
            reason = "Raster fallback: waiting for actual source world draw coverage";
            invalidate_source_coverage();
            return {};
        }
        if (failed_source_draw_revision == frame.scene.source_draw_revision) return {};
        if (frame.instances.size() > capacity) throw std::runtime_error("RT frame instance count exceeds cached scene capacity");
        if (!cache_instances(frame, cache_tlas_dirty) || !cache_source_visuals(frame)) {
            failed_source_draw_revision = frame.scene.source_draw_revision;
            return {};
        }
        failed_source_draw_revision.reset();
        if (!source_coverage_ready) {
            source_coverage_ready = true;
            reason = requested ?
                "Native receiver-aware lighting approximation; prepared authored environment when available, not reconstructed original PBR" :
                "Hardware ray-query pipelines prepared for capabilities; ray-effect dispatch disabled";
        }
        effects = requested;
        if (!effects) return {};
        if (!raw.image) throw std::runtime_error("RT resize must precede recording");
        const auto actual_denoising = requested_denoising == quality::Denoising::High && high_quality_denoise_available() ?
            quality::Denoising::High : requested_denoising != quality::Denoising::Off && temporal_denoise_available() ?
            quality::Denoising::Standard : quality::Denoising::Off;
        bool tlas_dirty = cache_tlas_dirty || !tlas_built;
        if (!blas_built) {
            build_cached_meshes(frame, command);
            tlas_dirty = true;
        } else if (refit_meshes(frame, command)) {
            tlas_dirty = true;
        }
        if (tlas_dirty) {
            auto* build_instances = static_cast<VkAccelerationStructureInstanceKHR*>(instance_build.mapped);
            std::uint32_t count{}; instanced_triangles = 0;
            for (std::size_t i = 0; i < cached_instance_count; ++i) {
                if (!eligible_instances[i]) continue;
                const auto& instance = cached_instances[i];
                auto& output = build_instances[count++]; output = {};
                for (unsigned row = 0; row < 3; ++row)
                    for (unsigned column = 0; column < 4; ++column)
                        output.transform.matrix[row][column] = instance.model[column * 4 + row];
                output.instanceCustomIndex = static_cast<std::uint32_t>(i); output.mask = 0xff;
                output.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                output.accelerationStructureReference = blas[instance.data[0]].address;
                instanced_triangles += meshes[instance.data[0]].index_count / 3;
            }
            instance_count = count;
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
        if (tlas_dirty) {
            auto geometry = instance_geometry();
            VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; build.dstAccelerationStructure = tlas.handle;
            build.geometryCount = 1; build.pGeometries = &geometry; build.scratchData.deviceAddress = scratch_address();
            VkAccelerationStructureBuildRangeInfoKHR range{}; range.primitiveCount = static_cast<std::uint32_t>(instance_count);
            const auto* ranges = &range; build_as(command, 1, &build, &ranges); as_barrier(command);
            tlas_built = pending_tlas_build = true;
            cache_tlas_dirty = false;
        }
        if (!fallback_environment_initialized) {
            transition(command, fallback_environment.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 6);
            const VkClearColorValue black{{0, 0, 0, 0}};
            const VkImageSubresourceRange faces{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
            vkCmdClearColorImage(command, fallback_environment.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &faces);
            transition(command, fallback_environment.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 6);
            fallback_environment_initialized = true;
        }
        transition(command, raw.image, raw_layout, VK_IMAGE_LAYOUT_GENERAL,
            raw_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            raw_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
            raw_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        raw_layout = VK_IMAGE_LAYOUT_GENERAL;
        // Native optional quality/encoding flags share the shader; neither reconstructs source lighting.
        const auto ray_flags = (high_quality_requested ? 1u : 0u) | (input_hdr_requested ? 2u : 0u);
        RtPush push{{camera[0], camera[1], camera[2], static_cast<float>(ray_flags)},
                    {extent.width, extent.height, effects, frame_index},
                    {environment.max_lod, environment.irradiance && environment.prefiltered ? 1.0f : 0.0f, 0, 0}};
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, rt_pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, rt_pipeline_layout, 0, 1, &rt_set, 0, nullptr);
        vkCmdPushConstants(command, rt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(command, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
        VkImageView output = raw.view;
        pending_denoise_passes = 0;
        if (actual_denoising == quality::Denoising::Off) {
            transition(command, raw.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            raw_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            history_valid = false;
        } else {
            transition(command, raw.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            if (!images_initialized) {
                for (unsigned i = 0; i < 2; ++i)
                    for (auto* resource : {&history_color[i], &history_position[i], &history_normal[i],
                                           &history_material[i], &denoise_scratch[i]})
                        transition(command, resource->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                history_color_layout.fill(VK_IMAGE_LAYOUT_GENERAL);
                images_initialized = true;
            }
            for (unsigned i = 0; i < 2; ++i) {
                if (history_color_layout[i] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
                transition(command, history_color[i].image, history_color_layout[i], VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                history_color_layout[i] = VK_IMAGE_LAYOUT_GENERAL;
            }
            if (history_written) history_index ^= 1u;
            for (auto* resource : {&history_color[history_index], &history_position[history_index],
                                   &history_normal[history_index], &history_material[history_index]})
                transition(command, resource->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            const bool high_denoise = actual_denoising == quality::Denoising::High;
            if (high_denoise)
                for (auto& resource : denoise_scratch)
                    transition(command, resource.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            const auto history_flag = history_valid && input_hdr == input_hdr_requested &&
                high_quality == high_quality_requested && denoising == actual_denoising ? 1u : 0u;
            pending_denoise_passes = high_denoise ? 3u : 1u;
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_pipeline);
            for (unsigned pass = 0; pass < pending_denoise_passes; ++pass) {
                const auto flags = pass == 0 ? history_flag | (high_denoise ? 2u : 0u) :
                    2u | 4u | 8u | (pass == 2 ? 16u : 0u);
                TemporalPush temporal{previous_vp, {extent.width, extent.height, flags, 1u << pass}};
                const auto set = high_denoise ? high_temporal_sets[history_index][pass] : temporal_sets[history_index];
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_pipeline_layout, 0, 1, &set, 0, nullptr);
                vkCmdPushConstants(command, temporal_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(temporal), &temporal);
                vkCmdDispatch(command, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
                if (pass == 0)
                    for (auto* resource : {&history_position[history_index], &history_normal[history_index],
                                           &history_material[history_index]})
                        transition(command, resource->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                if (high_denoise && pass < 2)
                    transition(command, denoise_scratch[pass].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            }
            transition(command, history_color[history_index].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            history_color_layout[history_index] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            output = history_color[history_index].view;
            history_valid = history_written = true;
        }
        VkBufferMemoryBarrier readback{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        readback.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; readback.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        readback.srcQueueFamilyIndex = readback.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readback.buffer = counters.buffer; readback.offset = 0; readback.size = CounterBytes;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &readback, 0, nullptr);
        previous_vp = frame.camera.view_projection; ++frame_index; submitted = true;
        denoising = actual_denoising;
        high_quality = high_quality_requested;
        input_hdr = input_hdr_requested;
        return {output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, effects};
    }
    std::uint32_t ray_budget_per_valid_pixel() const noexcept {
        return ((effects & 1u) ? (high_quality ? 2u : 1u) : 0u) +
               ((effects & 2u) ? (high_quality ? 4u : 1u) : 0u) +
               ((effects & 4u) ? (high_quality ? 2u : 1u) : 0u);
    }
    bool temporal_denoise_available() const noexcept {
        return active && temporal_pipeline && temporal_descriptors_ready && raw.view &&
            history_color[0].view && history_color[1].view &&
            history_position[0].view && history_position[1].view && history_normal[0].view && history_normal[1].view &&
            history_material[0].view && history_material[1].view &&
            temporal_sets[0] && temporal_sets[1];
    }
    bool high_quality_denoise_available() const noexcept {
        if (!temporal_denoise_available() || !denoise_scratch[0].view || !denoise_scratch[1].view) return false;
        for (const auto& sets : high_temporal_sets) for (const auto set : sets) if (!set) return false;
        return true;
    }
    quality::Denoising denoising_mode() const noexcept {
        return active && effects ? denoising : quality::Denoising::Off;
    }
    std::uint32_t denoise_passes() const noexcept {
        const auto mode = denoising_mode();
        return mode == quality::Denoising::Off ? 0u : mode == quality::Denoising::High ? 3u : 1u;
    }
    std::uint32_t spatial_radius() const noexcept {
        const auto mode = denoising_mode();
        return mode == quality::Denoising::Off ? 0u : mode == quality::Denoising::High ? 2u : 1u;
    }
    float temporal_history_weight() const noexcept {
        const auto mode = denoising_mode();
        return mode == quality::Denoising::Off ? 0.0f : mode == quality::Denoising::High ? 0.9f : 0.8f;
    }
    std::string diagnostics() const {
        std::ostringstream out;
        out << "{\"supported\":" << (active ? "true" : "false") << ",\"requested_effects\":" << requested
            << ",\"enabled_effects\":" << effects << ",\"available_effects\":" << (active ? 7u : 0u)
            << ",\"source_coverage_ready\":" << (source_coverage_ready ? "true" : "false")
            << ",\"initialize_for_capabilities\":" << (context.initialize_for_capabilities ? "true" : "false")
            << ",\"pipeline_cache_borrowed\":" << (context.pipeline_cache ? "true" : "false")
            << ",\"texture_update_after_bind\":" << (rt_layout && texture_update_after_bind ? "true" : "false")
            << ",\"texture_descriptor_count\":" << texture_descriptor_count
            << ",\"texture_sampler_limit\":" << texture_sampler_limit
            << ",\"unique_triangles\":" << unique_triangles
            << ",\"instanced_triangles\":" << instanced_triangles << ",\"instances\":" << instance_count
            << ",\"completed_query_frames\":" << completed_frames << ",\"ray_query_totals\":{\"reflection\":" << total_queries[0]
            << ",\"shadow\":" << total_queries[1] << ",\"gi\":" << total_queries[2] << "},\"last_ray_queries\":{\"reflection\":"
            << last_queries[0] << ",\"shadow\":" << last_queries[1] << ",\"gi\":" << last_queries[2]
            << "},\"last_valid_pixels\":" << last_queries[3]
            << ",\"blas_updates\":" << completed_blas_updates << ",\"tlas_updates\":" << completed_tlas_updates
            << ",\"high_quality\":" << (active && effects && high_quality ? "true" : "false")
            << ",\"denoising_mode\":\"" << (denoising_mode() == quality::Denoising::Off ? "off" :
                denoising_mode() == quality::Denoising::High ? "high" : "standard") << "\""
            << ",\"denoise_passes\":" << denoise_passes()
            << ",\"completed_denoise_dispatches\":" << completed_denoise_dispatches
            << ",\"temporal_denoise_available\":" << (temporal_denoise_available() ? "true" : "false")
            << ",\"high_quality_denoise_available\":" << (high_quality_denoise_available() ? "true" : "false")
            << ",\"input_hdr\":" << (active && effects && input_hdr ? "true" : "false")
            << ",\"ray_budget_per_valid_pixel\":" << ray_budget_per_valid_pixel()
            << ",\"spatial_radius\":" << spatial_radius()
            << ",\"configured_temporal_history_weight\":" << temporal_history_weight()
            << ",\"last_completed_configured_history_weight\":" << last_configured_history_weight
            << ",\"history_pixels\":{\"accepted\":" << total_history[0] << ",\"rejected\":" << total_history[1]
            << "},\"last_history_pixels\":{\"accepted\":" << last_history[0] << ",\"rejected\":" << last_history[1] << '}'
            << ",\"observed_history_acceptance_fraction\":";
        const auto total_history_pixels = total_history[0] + total_history[1];
        const auto last_history_pixels = std::uint64_t(last_history[0]) + last_history[1];
        if (total_history_pixels) out << double(total_history[0]) / double(total_history_pixels); else out << "null";
        out << ",\"last_observed_history_acceptance_fraction\":";
        if (last_history_pixels) out << double(last_history[0]) / double(last_history_pixels); else out << "null";
        out << ",\"observed_effective_history_weight\":";
        if (total_history_pixels) out << total_history_weight / double(total_history_pixels); else out << "null";
        out << ",\"last_observed_effective_history_weight\":";
        if (last_history_pixels) out << double(last_history[0]) * last_configured_history_weight / double(last_history_pixels);
        else out << "null";
        out << ",\"history_counter_policy\":\"completed_valid_geometry_temporal_pixels_accepted_only_on_committed_finite_blend_64_lane_reduction_two_atomics_per_workgroup\""
            << ",\"max_rays_per_valid_pixel\":" << (active && effects ? (high_quality ? 8u : 3u) : 0u)
            << ",\"environment_prepared\":" << (environment.irradiance && environment.prefiltered ? "true" : "false")
            << ",\"environment_max_lod\":" << environment.max_lod
            << ",\"sun_direction_known\":" << (cached_source_visuals.sun_direction[3] > 0 ? "true" : "false")
            << ",\"lighting_provenance\":\"native_receiver_approximation_and_prepared_authored_environment_not_original_physical_metadata\","
               "\"original_roughness_known\":false,\"original_reflectivity_known\":false,"
               "\"receiver_parameters\":\"per_pixel_native_metallic_roughness_reflectivity_discrete_material_id\","
               "\"modern_sun_cone_radians\":0.015,\"sun_direction_policy\":\"known_source_shader_sun_xzy_else_native_fallback\","
               "\"modern_sun_fallback_direction\":[-0.4,-0.3,0.85],\"modern_hit_ambient_fraction\":0.3,\"modern_gi_gain\":0.15,"
               "\"environment_policy\":\"actual_prepared_authored_irradiance_and_roughness_prefilter_else_native_constant_approximation\","
               "\"modern_probe_fallback_radiance\":[0.18,0.24,0.32],"
               "\"shadow_scope\":\"selectedSUN_direct_residual_only_preserves_authored_ambient_cube_nonSUN_emissive\","
               "\"reflection_scope\":\"positive_receiver_reflectivity_energy_replacement_original_reflectivity_unknown\","
               "\"alpha_policy\":\"actual_source_draw_decision_fixed_texture_alpha_diffuse_modulation_source_GE_final_PS_alpha_at_actual_reference\","
               "\"blended_surface_policy\":\"additive_destination_One_excluded_destination_Zero_opaque_InvSrcAlpha_final_PS_alpha_ge_1_SourceAlpha_final_PS_alpha_le_0_natural_opaque_binary_ray_coverage_noncutout_translucency_rejected\","
               "\"effect_alpha_policy\":\"all_nonzero_opacity_modes_FixedFunction_EnvCubeMix_EnvCubeAlpha_entSkin1_UV0_known_source_SELECT_MODULATE_alpha_NormalOptim_exact_post_PS_alpha_requires_known_source_skills_ambient_alpha_tangents_normal_texture_view_and_sun_other_effect_alpha_equations_or_texture_UV_bindings_raster_fallback\","
               "\"ray_sampling_policy\":\"source_texture_LOD0_per_texture_sampler_no_source_derivative_mip_selection\","
               "\"ray_hit_shading_policy\":\"native_albedo_and_modern_hit_lighting_approximation_not_original_effect_lighting_parity\","
               "\"temporal_policy\":\"camera_reprojection_world_normal_depth_and_discrete_material_rejection_no_object_motion_vectors\","
               "\"status\":";
        write_json_string(out, reason); out << '}';
        return out.str();
    }
};

RayTracing::RayTracing(RayTracingContext context, const RenderScene& scene,
                       std::span<const RayTracingMesh> meshes, std::span<const RayTracingTexture> textures,
                       std::filesystem::path shaders, std::uint32_t effects)
    : impl_(std::make_unique<Impl>(context)) { impl_->initialize(scene, meshes, textures, shaders, effects); }
RayTracing::~RayTracing() = default;
void RayTracing::resize(VkExtent2D extent, RayTracingGBuffer inputs) { impl_->resize(extent, inputs); }
void RayTracing::update_textures(std::span<const RayTracingTexture> textures) { impl_->update_textures(textures); }
void RayTracing::update_environment(RayTracingEnvironment environment) { impl_->update_environment(environment); }
void RayTracing::invalidate_history() noexcept { impl_->history_valid = false; }
void RayTracing::frame_completed() { impl_->completed(); }
RayTracingOutput RayTracing::record(const FrameSnapshot& frame, VkCommandBuffer command, std::array<float, 3> camera,
                                   quality::Denoising denoising, bool high_ray_quality, bool input_hdr) {
    return impl_->record(frame, command, camera, denoising, high_ray_quality, input_hdr);
}
bool RayTracing::supported() const noexcept { return impl_->active; }
std::uint32_t RayTracing::enabled_effects() const noexcept { return impl_->effects; }
std::uint32_t RayTracing::available_effects() const noexcept { return supported() ? 7u : 0u; }
bool RayTracing::high_quality() const noexcept { return impl_->active && impl_->effects && impl_->high_quality; }
quality::Denoising RayTracing::denoising_mode() const noexcept { return impl_->denoising_mode(); }
std::uint32_t RayTracing::denoise_passes() const noexcept { return impl_->denoise_passes(); }
bool RayTracing::temporal_denoise_available() const noexcept { return impl_->temporal_denoise_available(); }
bool RayTracing::high_quality_denoise_available() const noexcept { return impl_->high_quality_denoise_available(); }
bool RayTracing::input_hdr() const noexcept { return impl_->active && impl_->effects && impl_->input_hdr; }
std::uint32_t RayTracing::ray_budget_per_valid_pixel() const noexcept { return impl_->ray_budget_per_valid_pixel(); }
std::uint32_t RayTracing::spatial_radius() const noexcept { return impl_->spatial_radius(); }
float RayTracing::temporal_history_weight() const noexcept { return impl_->temporal_history_weight(); }
std::string RayTracing::diagnostics_json() const { return impl_->diagnostics(); }
std::uint64_t RayTracing::completed_queries() const noexcept {
    return impl_->total_queries[0] + impl_->total_queries[1] + impl_->total_queries[2];
}
std::uint64_t RayTracing::triangles() const noexcept { return impl_->instanced_triangles; }
std::uint64_t RayTracing::instances() const noexcept { return impl_->instance_count; }
std::uint64_t RayTracing::blas_updates() const noexcept { return impl_->completed_blas_updates; }
std::uint64_t RayTracing::tlas_updates() const noexcept { return impl_->completed_tlas_updates; }
std::string_view RayTracing::fallback_reason() const noexcept {
    return (impl_->requested || impl_->context.initialize_for_capabilities) && (!impl_->active || !impl_->source_coverage_ready) ?
        std::string_view(impl_->reason) : std::string_view{};
}
} // namespace c4::rendering
