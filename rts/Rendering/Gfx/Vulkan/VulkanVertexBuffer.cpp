/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanVertexBuffer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    void CheckVkResult(VkResult result, const char *what)
    {
        if (result == VK_SUCCESS)
            return;

        throw std::runtime_error(std::string(what) + " failed with VkResult=" + std::to_string(static_cast<int>(result)));
    }

    std::size_t GetAllocationRequestSize(std::size_t requestedSize)
    {
        return (requestedSize == 0) ? 1 : requestedSize;
    }

} // namespace

namespace gfx
{

    VulkanVertexBuffer::VulkanVertexBuffer(VkDevice device_, VkPhysicalDevice physicalDevice_, const BufferCreateInfo &ci)
        : device(device_), physicalDevice(physicalDevice_), sizeBytes(ci.sizeBytes), usage(ci.usage), memoryAccess(ci.memoryAccess), readable(ci.readable)
    {
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanVertexBuffer: VkDevice is null");

        if (physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanVertexBuffer: VkPhysicalDevice is null");

        CreateBufferAndMemory(sizeBytes);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        UnmapWrite();
        DestroyBufferAndMemory();
    }

    bool VulkanVertexBuffer::IsMappable() const noexcept
    {
        return (memory != VK_NULL_HANDLE);
    }

    void VulkanVertexBuffer::Resize(std::size_t newSizeBytes, bool preserveData)
    {
        if (newSizeBytes == sizeBytes)
            return;

        const std::size_t copySizeBytes = std::min(sizeBytes, newSizeBytes);
        std::vector<std::byte> preservedData;

        if (preserveData && (copySizeBytes > 0))
        {
            void *srcPtr = nullptr;
            CheckVkResult(
                vkMapMemory(device, memory, 0, ToVkDeviceSize(copySizeBytes), 0, &srcPtr),
                "vkMapMemory(copy-old-data)");

            preservedData.resize(copySizeBytes);
            std::memcpy(preservedData.data(), srcPtr, copySizeBytes);

            vkUnmapMemory(device, memory);
        }

        UnmapWrite();
        DestroyBufferAndMemory();

        sizeBytes = newSizeBytes;
        CreateBufferAndMemory(sizeBytes);

        if (!preservedData.empty())
            Update(std::span<const std::byte>(preservedData.data(), preservedData.size()), 0);
    }

    void VulkanVertexBuffer::Update(std::span<const std::byte> src, std::size_t dstOffsetBytes)
    {
        if (src.empty())
            return;

        if (dstOffsetBytes > sizeBytes)
            throw std::runtime_error("VulkanVertexBuffer::Update: destination offset exceeds buffer size");

        if (src.size() > (sizeBytes - dstOffsetBytes))
            throw std::runtime_error("VulkanVertexBuffer::Update: update range exceeds buffer size");

        if (mappedPtr != nullptr)
        {
            if (dstOffsetBytes < mappedOffsetBytes)
                throw std::runtime_error("VulkanVertexBuffer::Update: destination offset before mapped region");

            const std::size_t relativeOffsetBytes = dstOffsetBytes - mappedOffsetBytes;
            if (src.size() > (mappedSizeBytes - relativeOffsetBytes))
                throw std::runtime_error("VulkanVertexBuffer::Update: update range exceeds currently mapped region");

            std::memcpy(static_cast<std::byte *>(mappedPtr) + relativeOffsetBytes, src.data(), src.size());
            return;
        }

        void *dstPtr = nullptr;
        CheckVkResult(
            vkMapMemory(device, memory, ToVkDeviceSize(dstOffsetBytes), ToVkDeviceSize(src.size()), 0, &dstPtr),
            "vkMapMemory(update)");

        std::memcpy(dstPtr, src.data(), src.size());

        vkUnmapMemory(device, memory);
    }

    std::span<std::byte> VulkanVertexBuffer::MapWrite(std::size_t offsetBytes, std::size_t mapSizeBytes)
    {
        if (mapSizeBytes == 0)
            return {};

        if (mappedPtr != nullptr)
            throw std::runtime_error("VulkanVertexBuffer::MapWrite: buffer is already mapped");

        if (offsetBytes > sizeBytes)
            throw std::runtime_error("VulkanVertexBuffer::MapWrite: offset exceeds buffer size");

        if (mapSizeBytes > (sizeBytes - offsetBytes))
            throw std::runtime_error("VulkanVertexBuffer::MapWrite: map range exceeds buffer size");

        void *ptr = nullptr;
        CheckVkResult(
            vkMapMemory(device, memory, ToVkDeviceSize(offsetBytes), ToVkDeviceSize(mapSizeBytes), 0, &ptr),
            "vkMapMemory(map-write)");

        mappedPtr = ptr;
        mappedOffsetBytes = offsetBytes;
        mappedSizeBytes = mapSizeBytes;

        return {static_cast<std::byte *>(mappedPtr), mapSizeBytes};
    }

    void VulkanVertexBuffer::UnmapWrite()
    {
        if (mappedPtr == nullptr)
            return;

        vkUnmapMemory(device, memory);

        mappedPtr = nullptr;
        mappedOffsetBytes = 0;
        mappedSizeBytes = 0;
    }

    void VulkanVertexBuffer::CreateBufferAndMemory(std::size_t requestedSizeBytes)
    {
        const std::size_t allocationRequestBytes = GetAllocationRequestSize(requestedSizeBytes);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = ToVkDeviceSize(allocationRequestBytes);
        bufferInfo.usage = TranslateUsage(usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CheckVkResult(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &requirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        CheckVkResult(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory");
        CheckVkResult(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");

        allocatedBytes = ToSizeT(requirements.size);
    }

    void VulkanVertexBuffer::DestroyBufferAndMemory() noexcept
    {
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }

        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        allocatedBytes = 0;
    }

    std::uint32_t VulkanVertexBuffer::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const
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

        throw std::runtime_error("VulkanVertexBuffer: failed to find compatible Vulkan memory type");
    }

    VkBufferUsageFlags VulkanVertexBuffer::TranslateUsage(BufferUsage usage_) noexcept
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        if (usage_ != BufferUsage::Static)
            usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        return usageFlags;
    }

    VkDeviceSize VulkanVertexBuffer::ToVkDeviceSize(std::size_t value) noexcept
    {
        constexpr std::size_t maxValue = static_cast<std::size_t>(std::numeric_limits<VkDeviceSize>::max());
        if (value > maxValue)
            return std::numeric_limits<VkDeviceSize>::max();

        return static_cast<VkDeviceSize>(value);
    }

    std::size_t VulkanVertexBuffer::ToSizeT(VkDeviceSize value) noexcept
    {
        constexpr VkDeviceSize maxValue = static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max());
        if (value > maxValue)
            return std::numeric_limits<std::size_t>::max();

        return static_cast<std::size_t>(value);
    }

} // namespace gfx
