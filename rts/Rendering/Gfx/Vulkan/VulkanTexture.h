/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/ITexture.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace gfx
{

    class VulkanTexture final : public ITexture
    {
    public:
        VulkanTexture(
            VkDevice device,
            VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool,
            VkQueue graphicsQueue,
            const TextureCreateInfo &ci);

        ~VulkanTexture() override;

        VulkanTexture(const VulkanTexture &) = delete;
        VulkanTexture &operator=(const VulkanTexture &) = delete;

        [[nodiscard]] TextureDimension Dimension() const noexcept override
        {
            return dimension;
        }

        [[nodiscard]] PixelFormat Format() const noexcept override
        {
            return format;
        }

        [[nodiscard]] Extent3D GetExtent() const noexcept override
        {
            return extent;
        }

        [[nodiscard]] std::uint32_t GetMipLevels() const noexcept override
        {
            return mipLevels;
        }

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override;

        void Upload(
            std::uint32_t mipLevel,
            std::uint32_t arrayLayer,
            std::span<const std::byte> pixels,
            std::size_t rowPitchBytes = 0,
            std::size_t slicePitchBytes = 0) override;

        void UploadSubRegion(
            std::uint32_t mipLevel,
            std::uint32_t arrayLayer,
            std::uint32_t xOffset,
            std::uint32_t yOffset,
            std::uint32_t width,
            std::uint32_t height,
            std::span<const std::byte> pixels,
            std::size_t rowPitchBytes = 0) override;

        void ApplySamplerState(const SamplerState &state) override;
        void GenerateMipmaps() override;

        [[nodiscard]] VkImage GetImage() const noexcept
        {
            return image;
        }

        [[nodiscard]] VkImageView GetImageView() const noexcept
        {
            return imageView;
        }

        [[nodiscard]] VkSampler GetSampler() const noexcept
        {
            return sampler;
        }

        [[nodiscard]] VkImageLayout GetCurrentLayout() const noexcept
        {
            return currentLayout;
        }

        [[nodiscard]] VkFormat GetVkFormat() const noexcept
        {
            return vkFormat;
        }

        [[nodiscard]] VkImageAspectFlags GetAspectMask() const noexcept
        {
            return aspectMask;
        }

    private:
        void CreateImageAndMemory(const TextureCreateInfo &ci);
        void CreateImageView();
        void CreateSampler(const SamplerState &state);

        void DestroySampler() noexcept;
        void DestroyImageView() noexcept;
        void DestroyImageAndMemory() noexcept;

        void UploadToImage(
            std::span<const std::byte> pixels,
            const VkBufferImageCopy &copyRegion,
            VkImageSubresourceRange transitionRange);

        void TransitionLayout(
            VkCommandBuffer commandBuffer,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            const VkImageSubresourceRange &range) const;

        [[nodiscard]] std::uint32_t FindMemoryType(
            std::uint32_t typeFilter,
            VkMemoryPropertyFlags properties) const;

        [[nodiscard]] VkImageLayout PreferredFinalLayout() const noexcept;

        static VkFormat TranslateFormat(PixelFormat format);
        static VkImageAspectFlags DetermineAspectMask(PixelFormat format);
        static std::uint32_t BytesPerPixel(PixelFormat format);

    private:
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;

        TextureDimension dimension = TextureDimension::Tex2D;
        PixelFormat format = PixelFormat::Unknown;
        Extent3D extent = {};
        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
        TextureUsage usage = TextureUsage::Sampled;

        VkFormat vkFormat = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags aspectMask = 0;
        std::uint32_t bytesPerPixel = 1;

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

} // namespace gfx
