/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanTexture.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

    void CheckVkResult(VkResult result, const char *what)
    {
        if (result == VK_SUCCESS)
            return;

        throw std::runtime_error(std::string(what) + " failed with VkResult=" + std::to_string(static_cast<int>(result)));
    }

    std::uint32_t ClampAtLeastOne(std::uint32_t value)
    {
        return std::max<std::uint32_t>(1u, value);
    }

    bool HasTextureUsage(gfx::TextureUsage usage, gfx::TextureUsage flag)
    {
        return ((static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(flag)) != 0u);
    }

    gfx::Extent3D CalcMipExtent(const gfx::Extent3D &baseExtent, std::uint32_t mipLevel)
    {
        return {
            std::max<std::uint32_t>(1u, baseExtent.width >> mipLevel),
            std::max<std::uint32_t>(1u, baseExtent.height >> mipLevel),
            std::max<std::uint32_t>(1u, baseExtent.depth >> mipLevel),
        };
    }

    VkImageType TranslateImageType(gfx::TextureDimension dimension)
    {
        switch (dimension)
        {
        case gfx::TextureDimension::Tex2D:
        case gfx::TextureDimension::Tex2DArray:
        case gfx::TextureDimension::Cube:
            return VK_IMAGE_TYPE_2D;
        case gfx::TextureDimension::Tex3D:
            return VK_IMAGE_TYPE_3D;
        }

        return VK_IMAGE_TYPE_2D;
    }

    VkImageViewType TranslateViewType(gfx::TextureDimension dimension)
    {
        switch (dimension)
        {
        case gfx::TextureDimension::Tex2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case gfx::TextureDimension::Tex2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case gfx::TextureDimension::Tex3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case gfx::TextureDimension::Cube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        }

        return VK_IMAGE_VIEW_TYPE_2D;
    }

    VkSamplerAddressMode TranslateWrapMode(gfx::WrapMode mode)
    {
        switch (mode)
        {
        case gfx::WrapMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case gfx::WrapMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case gfx::WrapMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case gfx::WrapMode::ClampToBorder:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case gfx::WrapMode::MirrorClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        }

        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }

    void TranslateFilters(
        gfx::FilterMode minFilter,
        gfx::FilterMode magFilter,
        VkFilter &outMinFilter,
        VkFilter &outMagFilter,
        VkSamplerMipmapMode &outMipmapMode)
    {
        switch (minFilter)
        {
        case gfx::FilterMode::Nearest:
            outMinFilter = VK_FILTER_NEAREST;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case gfx::FilterMode::Linear:
            outMinFilter = VK_FILTER_LINEAR;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case gfx::FilterMode::NearestMipmapNearest:
            outMinFilter = VK_FILTER_NEAREST;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case gfx::FilterMode::LinearMipmapNearest:
            outMinFilter = VK_FILTER_LINEAR;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case gfx::FilterMode::NearestMipmapLinear:
            outMinFilter = VK_FILTER_NEAREST;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        case gfx::FilterMode::LinearMipmapLinear:
            outMinFilter = VK_FILTER_LINEAR;
            outMipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        }

        switch (magFilter)
        {
        case gfx::FilterMode::Nearest:
        case gfx::FilterMode::NearestMipmapNearest:
        case gfx::FilterMode::NearestMipmapLinear:
            outMagFilter = VK_FILTER_NEAREST;
            break;
        case gfx::FilterMode::Linear:
        case gfx::FilterMode::LinearMipmapNearest:
        case gfx::FilterMode::LinearMipmapLinear:
            outMagFilter = VK_FILTER_LINEAR;
            break;
        }
    }

    std::uintptr_t ToNativeHandle(VkImageView handle)
    {
        std::uintptr_t nativeHandle = 0;
        std::memcpy(&nativeHandle, &handle, std::min(sizeof(nativeHandle), sizeof(handle)));
        return nativeHandle;
    }

} // namespace

namespace gfx
{

    VulkanTexture::VulkanTexture(
        VkDevice device_,
        VkPhysicalDevice physicalDevice_,
        VkCommandPool commandPool_,
        VkQueue graphicsQueue_,
        const TextureCreateInfo &ci)
        : device(device_), physicalDevice(physicalDevice_), commandPool(commandPool_), graphicsQueue(graphicsQueue_), dimension(ci.dimension), format(ci.format), extent({ClampAtLeastOne(ci.extent.width), ClampAtLeastOne(ci.extent.height), ClampAtLeastOne(ci.extent.depth)}), mipLevels(ClampAtLeastOne(ci.mipLevels)), usage(ci.usage), vkFormat(TranslateFormat(ci.format)), aspectMask(DetermineAspectMask(ci.format)), bytesPerPixel(BytesPerPixel(ci.format))
    {
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanTexture: VkDevice is null");

        if (physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanTexture: VkPhysicalDevice is null");

        if (commandPool == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanTexture: VkCommandPool is null");

        if (graphicsQueue == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanTexture: VkQueue is null");

        switch (dimension)
        {
        case TextureDimension::Tex2D:
            arrayLayers = 1;
            break;
        case TextureDimension::Tex2DArray:
            arrayLayers = ClampAtLeastOne(ci.arrayLayers);
            break;
        case TextureDimension::Tex3D:
            arrayLayers = 1;
            break;
        case TextureDimension::Cube:
            arrayLayers = 6;
            break;
        }

        if (ci.nativeHandle != 0)
            throw std::runtime_error("VulkanTexture: wrapping native image handles is not implemented");

        CreateImageAndMemory(ci);
        CreateImageView();

        SamplerState samplerState = ci.samplerState.value_or(SamplerState{});
        if (!ci.samplerState.has_value() && (mipLevels > 1u))
            samplerState.minFilter = FilterMode::LinearMipmapLinear;

        CreateSampler(samplerState);
    }

    VulkanTexture::~VulkanTexture()
    {
        DestroySampler();
        DestroyImageView();
        DestroyImageAndMemory();
    }

    std::uintptr_t VulkanTexture::GetNativeHandle() const noexcept
    {
        return ToNativeHandle(imageView);
    }

    void VulkanTexture::Upload(
        std::uint32_t mipLevel,
        std::uint32_t arrayLayer,
        std::span<const std::byte> pixels,
        std::size_t rowPitchBytes,
        std::size_t slicePitchBytes)
    {
        if (pixels.empty())
            return;

        if (mipLevel >= mipLevels)
            throw std::runtime_error("VulkanTexture::Upload: mip level out of range");

        const Extent3D mipExtent = CalcMipExtent(extent, mipLevel);

        if (dimension != TextureDimension::Tex3D)
        {
            (void)slicePitchBytes;
            UploadSubRegion(
                mipLevel,
                arrayLayer,
                0,
                0,
                mipExtent.width,
                mipExtent.height,
                pixels,
                rowPitchBytes);
            return;
        }

        if (arrayLayer != 0u)
            throw std::runtime_error("VulkanTexture::Upload: Tex3D upload does not accept arrayLayer");

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;

        if (rowPitchBytes != 0u)
        {
            if ((rowPitchBytes % bytesPerPixel) != 0u)
                throw std::runtime_error("VulkanTexture::Upload: rowPitchBytes must align to bytesPerPixel");

            copyRegion.bufferRowLength = static_cast<std::uint32_t>(rowPitchBytes / bytesPerPixel);
        }

        if (slicePitchBytes != 0u)
        {
            if (rowPitchBytes == 0u)
                throw std::runtime_error("VulkanTexture::Upload: slicePitchBytes requires rowPitchBytes");

            if ((slicePitchBytes % rowPitchBytes) != 0u)
                throw std::runtime_error("VulkanTexture::Upload: slicePitchBytes must be divisible by rowPitchBytes");

            copyRegion.bufferImageHeight = static_cast<std::uint32_t>(slicePitchBytes / rowPitchBytes);
        }

        copyRegion.imageSubresource.aspectMask = aspectMask;
        copyRegion.imageSubresource.mipLevel = mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {mipExtent.width, mipExtent.height, mipExtent.depth};

        VkImageSubresourceRange range{};
        range.aspectMask = aspectMask;
        range.baseMipLevel = mipLevel;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        UploadToImage(pixels, copyRegion, range);
    }

    void VulkanTexture::UploadSubRegion(
        std::uint32_t mipLevel,
        std::uint32_t arrayLayer,
        std::uint32_t xOffset,
        std::uint32_t yOffset,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> pixels,
        std::size_t rowPitchBytes)
    {
        if (pixels.empty())
            return;

        if (dimension == TextureDimension::Tex3D)
            throw std::runtime_error("VulkanTexture::UploadSubRegion: Tex3D sub-region uploads are not implemented");

        if (mipLevel >= mipLevels)
            throw std::runtime_error("VulkanTexture::UploadSubRegion: mip level out of range");

        if (arrayLayer >= arrayLayers)
            throw std::runtime_error("VulkanTexture::UploadSubRegion: array layer out of range");

        const Extent3D mipExtent = CalcMipExtent(extent, mipLevel);

        if (width == 0u || height == 0u)
            return;

        if ((xOffset + width) > mipExtent.width || (yOffset + height) > mipExtent.height)
            throw std::runtime_error("VulkanTexture::UploadSubRegion: region exceeds mip extent");

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;

        if (rowPitchBytes != 0u)
        {
            if ((rowPitchBytes % bytesPerPixel) != 0u)
                throw std::runtime_error("VulkanTexture::UploadSubRegion: rowPitchBytes must align to bytesPerPixel");

            copyRegion.bufferRowLength = static_cast<std::uint32_t>(rowPitchBytes / bytesPerPixel);
        }

        copyRegion.imageSubresource.aspectMask = aspectMask;
        copyRegion.imageSubresource.mipLevel = mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = arrayLayer;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageOffset = {
            static_cast<std::int32_t>(xOffset),
            static_cast<std::int32_t>(yOffset),
            0,
        };
        copyRegion.imageExtent = {width, height, 1};

        VkImageSubresourceRange range{};
        range.aspectMask = aspectMask;
        range.baseMipLevel = mipLevel;
        range.levelCount = 1;
        range.baseArrayLayer = arrayLayer;
        range.layerCount = 1;

        UploadToImage(pixels, copyRegion, range);
    }

    void VulkanTexture::ApplySamplerState(const SamplerState &state)
    {
        CreateSampler(state);
    }

    void VulkanTexture::GenerateMipmaps()
    {
        // Full Vulkan mipmap generation will be added with command-buffer pipeline support.
    }

    void VulkanTexture::CreateImageAndMemory(const TextureCreateInfo &ci)
    {
        VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        if (HasTextureUsage(usage, TextureUsage::Sampled))
            imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

        if (HasTextureUsage(usage, TextureUsage::TransferSrc) || (mipLevels > 1u))
            imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        if (HasTextureUsage(usage, TextureUsage::DepthStencil) || ((aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u))
            imageUsageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        if (HasTextureUsage(usage, TextureUsage::RenderTarget) && ((aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0u))
            imageUsageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const VkExtent3D imageExtent = {
            extent.width,
            extent.height,
            (dimension == TextureDimension::Tex3D) ? extent.depth : 1u,
        };

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = TranslateImageType(dimension);
        imageInfo.format = vkFormat;
        imageInfo.extent = imageExtent;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = arrayLayers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = imageUsageFlags;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (dimension == TextureDimension::Cube)
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        CheckVkResult(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage");

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device, image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        CheckVkResult(vkAllocateMemory(device, &allocateInfo, nullptr, &imageMemory), "vkAllocateMemory(image)");
        CheckVkResult(vkBindImageMemory(device, image, imageMemory, 0), "vkBindImageMemory");

        currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        (void)ci;
    }

    void VulkanTexture::CreateImageView()
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = TranslateViewType(dimension);
        viewInfo.format = vkFormat;

        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = arrayLayers;

        CheckVkResult(vkCreateImageView(device, &viewInfo, nullptr, &imageView), "vkCreateImageView");
    }

    void VulkanTexture::CreateSampler(const SamplerState &state)
    {
        DestroySampler();

        VkFilter minFilter = VK_FILTER_LINEAR;
        VkFilter magFilter = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        TranslateFilters(state.minFilter, state.magFilter, minFilter, magFilter, mipmapMode);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = magFilter;
        samplerInfo.minFilter = minFilter;
        samplerInfo.mipmapMode = mipmapMode;
        samplerInfo.addressModeU = TranslateWrapMode(state.wrapS);
        samplerInfo.addressModeV = TranslateWrapMode(state.wrapT);
        samplerInfo.addressModeW = TranslateWrapMode(state.wrapR);
        samplerInfo.mipLodBias = state.lodBias;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>((mipLevels > 0u) ? (mipLevels - 1u) : 0u);
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        CheckVkResult(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "vkCreateSampler");
    }

    void VulkanTexture::DestroySampler() noexcept
    {
        if (sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }
    }

    void VulkanTexture::DestroyImageView() noexcept
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }
    }

    void VulkanTexture::DestroyImageAndMemory() noexcept
    {
        if (image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }

        if (imageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, imageMemory, nullptr);
            imageMemory = VK_NULL_HANDLE;
        }
    }

    void VulkanTexture::UploadToImage(
        std::span<const std::byte> pixels,
        const VkBufferImageCopy &copyRegion,
        VkImageSubresourceRange transitionRange)
    {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        try
        {
            VkBufferCreateInfo stagingBufferInfo{};
            stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingBufferInfo.size = static_cast<VkDeviceSize>(pixels.size());
            stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            CheckVkResult(vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer), "vkCreateBuffer(staging)");

            VkMemoryRequirements stagingMemReq{};
            vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingMemReq);

            VkMemoryAllocateInfo stagingAllocInfo{};
            stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            stagingAllocInfo.allocationSize = stagingMemReq.size;
            stagingAllocInfo.memoryTypeIndex = FindMemoryType(
                stagingMemReq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            CheckVkResult(vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingMemory), "vkAllocateMemory(staging)");
            CheckVkResult(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0), "vkBindBufferMemory(staging)");

            void *mappedData = nullptr;
            CheckVkResult(vkMapMemory(device, stagingMemory, 0, static_cast<VkDeviceSize>(pixels.size()), 0, &mappedData), "vkMapMemory(staging)");
            std::memcpy(mappedData, pixels.data(), pixels.size());
            vkUnmapMemory(device, stagingMemory);

            VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.commandPool = commandPool;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            CheckVkResult(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer), "vkAllocateCommandBuffers(upload)");

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            CheckVkResult(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(upload)");

            TransitionLayout(commandBuffer, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, transitionRange);
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            const VkImageLayout finalLayout = PreferredFinalLayout();
            TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, transitionRange);

            CheckVkResult(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(upload)");

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            CheckVkResult(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit(upload)");
            CheckVkResult(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle(upload)");

            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            currentLayout = finalLayout;
        }
        catch (...)
        {
            if (stagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(device, stagingBuffer, nullptr);

            if (stagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(device, stagingMemory, nullptr);

            throw;
        }

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
    }

    void VulkanTexture::TransitionLayout(
        VkCommandBuffer commandBuffer,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        const VkImageSubresourceRange &range) const
    {
        if (oldLayout == newLayout)
            return;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = range;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else
        {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            srcStage,
            dstStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
    }

    std::uint32_t VulkanTexture::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            const bool typeMatches = ((typeFilter & (1u << i)) != 0u);
            const bool propertiesMatch = ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties);

            if (typeMatches && propertiesMatch)
                return i;
        }

        throw std::runtime_error("VulkanTexture: failed to find suitable Vulkan memory type");
    }

    VkImageLayout VulkanTexture::PreferredFinalLayout() const noexcept
    {
        if (HasTextureUsage(usage, TextureUsage::DepthStencil) || ((aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u))
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        if (HasTextureUsage(usage, TextureUsage::RenderTarget) && ((aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0u))
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (HasTextureUsage(usage, TextureUsage::Sampled))
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        return VK_IMAGE_LAYOUT_GENERAL;
    }

    VkFormat VulkanTexture::TranslateFormat(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::R8_UNorm:
            return VK_FORMAT_R8_UNORM;
        case PixelFormat::RG8_UNorm:
            return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::RGB8_UNorm:
            return VK_FORMAT_R8G8B8_UNORM;
        case PixelFormat::RGBA8_UNorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::BGRA8_UNorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::R16_UNorm:
            return VK_FORMAT_R16_UNORM;
        case PixelFormat::RG16_UNorm:
            return VK_FORMAT_R16G16_UNORM;
        case PixelFormat::RGB16_UNorm:
            return VK_FORMAT_R16G16B16_UNORM;
        case PixelFormat::RGBA16_UNorm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case PixelFormat::R32_SFloat:
            return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::RG32_SFloat:
            return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::RGB32_SFloat:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case PixelFormat::RGBA32_SFloat:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::RGB10A2_UNorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case PixelFormat::D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::D32_SFloat:
            return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::Unknown:
            break;
        }

        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    VkImageAspectFlags VulkanTexture::DetermineAspectMask(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::D24S8:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case PixelFormat::D32_SFloat:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    std::uint32_t VulkanTexture::BytesPerPixel(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::R8_UNorm:
            return 1;
        case PixelFormat::RG8_UNorm:
            return 2;
        case PixelFormat::RGB8_UNorm:
            return 3;
        case PixelFormat::RGBA8_UNorm:
        case PixelFormat::BGRA8_UNorm:
        case PixelFormat::RGB10A2_UNorm:
        case PixelFormat::D24S8:
        case PixelFormat::D32_SFloat:
            return 4;
        case PixelFormat::R16_UNorm:
            return 2;
        case PixelFormat::RG16_UNorm:
            return 4;
        case PixelFormat::RGB16_UNorm:
            return 6;
        case PixelFormat::RGBA16_UNorm:
            return 8;
        case PixelFormat::R32_SFloat:
            return 4;
        case PixelFormat::RG32_SFloat:
            return 8;
        case PixelFormat::RGB32_SFloat:
            return 12;
        case PixelFormat::RGBA32_SFloat:
            return 16;
        case PixelFormat::Unknown:
            break;
        }

        return 4;
    }

} // namespace gfx
