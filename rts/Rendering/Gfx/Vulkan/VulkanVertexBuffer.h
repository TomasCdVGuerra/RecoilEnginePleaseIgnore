/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IVertexBuffer.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace gfx
{

    class VulkanVertexBuffer final : public IVertexBuffer
    {
    public:
        VulkanVertexBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const BufferCreateInfo &ci);
        ~VulkanVertexBuffer() override;

        VulkanVertexBuffer(const VulkanVertexBuffer &) = delete;
        VulkanVertexBuffer &operator=(const VulkanVertexBuffer &) = delete;

        [[nodiscard]] std::size_t SizeBytes() const noexcept override
        {
            return sizeBytes;
        }

        [[nodiscard]] BufferUsage Usage() const noexcept override
        {
            return usage;
        }

        [[nodiscard]] bool IsMappable() const noexcept override;

        void Resize(std::size_t newSizeBytes, bool preserveData) override;
        void Update(std::span<const std::byte> src, std::size_t dstOffsetBytes = 0) override;

        [[nodiscard]] std::span<std::byte> MapWrite(std::size_t offsetBytes, std::size_t mapSizeBytes) override;
        void UnmapWrite() override;

        [[nodiscard]] VkBuffer GetBuffer() const noexcept
        {
            return buffer;
        }

    private:
        void CreateBufferAndMemory(std::size_t requestedSizeBytes);
        void DestroyBufferAndMemory() noexcept;

        [[nodiscard]] std::uint32_t FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        [[nodiscard]] static VkBufferUsageFlags TranslateUsage(BufferUsage usage) noexcept;

        [[nodiscard]] static VkDeviceSize ToVkDeviceSize(std::size_t value) noexcept;
        [[nodiscard]] static std::size_t ToSizeT(VkDeviceSize value) noexcept;

    private:
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;

        std::size_t sizeBytes = 0;
        std::size_t allocatedBytes = 0;
        BufferUsage usage = BufferUsage::Dynamic;
        MemoryAccess memoryAccess = MemoryAccess::CpuToGpu;
        bool readable = false;

        void *mappedPtr = nullptr;
        std::size_t mappedOffsetBytes = 0;
        std::size_t mappedSizeBytes = 0;
    };

} // namespace gfx
