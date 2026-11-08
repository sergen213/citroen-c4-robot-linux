#pragma once
#include "VulkanDispatch.hpp"
#include <cstdint>

namespace c4::rendering {

// Owns only the scene attachments. The caller synchronizes the single in-flight
// frame before resize/destruction and records its geometry in this render pass.
class VulkanRasterTarget {
public:
    struct Context { VkPhysicalDevice physical{}; VkDevice device{}; };
    struct Image {
        VkImage handle{};
        VkDeviceMemory memory{};
        VkImageView view{};
        VkFormat format{};
        std::uint32_t width{},height{};
    };
    explicit VulkanRasterTarget(Context context) noexcept;
    ~VulkanRasterTarget() { clear(); }
    VulkanRasterTarget(const VulkanRasterTarget&)=delete;
    VulkanRasterTarget& operator=(const VulkanRasterTarget&)=delete;
    void resize(VkExtent2D,unsigned samples,VkFormat color_format=VK_FORMAT_R8G8B8A8_UNORM);
    void clear() noexcept;
    unsigned supported_sample_mask(VkFormat color_format=VK_FORMAT_R8G8B8A8_UNORM) const noexcept;
    bool hdr_supported() const noexcept;
    VkRenderPass render_pass() const noexcept { return pass_; }
    VkFramebuffer framebuffer() const noexcept { return framebuffer_; }
    const Image& color() const noexcept { return resolved_[0]; }
    const Image& position() const noexcept { return resolved_[1]; }
    const Image& normal() const noexcept { return resolved_[2]; }
    const Image& material_receiver() const noexcept { return resolved_[3]; }
    const Image& sun_direct() const noexcept { return resolved_[4]; }
    const Image& depth() const noexcept { return resolved_[5]; }
    VkFormat depth_format() const noexcept { return depth_format_; }
    bool sampled_depth_supported() const noexcept { return sampled_depth_; }
    VkSampleCountFlagBits sample_count() const noexcept { return samples_; }
    VkResolveModeFlagBits depth_resolve_mode() const noexcept { return depth_resolve_; }
private:
    Context context_;
    VkFormat depth_format_{VK_FORMAT_UNDEFINED};
    bool sampled_depth_{};
    Image resolved_[6]{},multisampled_[6]{};
    VkRenderPass pass_{};
    VkFramebuffer framebuffer_{};
    VkSampleCountFlagBits samples_{VK_SAMPLE_COUNT_1_BIT};
    VkResolveModeFlagBits depth_resolve_{VK_RESOLVE_MODE_NONE};
    std::uint32_t memory_type(std::uint32_t) const;
    Image image(VkExtent2D,VkFormat,VkSampleCountFlagBits,VkImageUsageFlags,VkImageAspectFlags);
    void destroy(Image&) noexcept;
};
} // namespace c4::rendering
