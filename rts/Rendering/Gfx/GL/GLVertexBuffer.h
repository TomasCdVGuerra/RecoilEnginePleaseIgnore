/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IVertexBuffer.h"
#include "Rendering/GL/myGL.h"

#include <cstddef>
#include <span>

namespace gfx
{

    class GLVertexBuffer final : public IVertexBuffer
    {
    public:
        explicit GLVertexBuffer(const BufferCreateInfo &ci);
        ~GLVertexBuffer() override;

        GLVertexBuffer(const GLVertexBuffer &) = delete;
        GLVertexBuffer &operator=(const GLVertexBuffer &) = delete;

        GLVertexBuffer(GLVertexBuffer &&other) noexcept;
        GLVertexBuffer &operator=(GLVertexBuffer &&other) noexcept;

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

        [[nodiscard]] std::span<std::byte> MapWrite(std::size_t offsetBytes, std::size_t sizeBytes) override;
        void UnmapWrite() override;

        [[nodiscard]] GLuint GetBufferId() const noexcept
        {
            return bufferId;
        }

    private:
        void MoveFrom(GLVertexBuffer &&other) noexcept;
        GLenum TranslateUsage(BufferUsage value) const noexcept;
        GLbitfield GetMapWriteFlags() const noexcept;
        void ResetMapState() noexcept;

    private:
        GLuint bufferId = 0;

        std::size_t sizeBytes = 0;
        BufferUsage usage = BufferUsage::Dynamic;
        MemoryAccess memoryAccess = MemoryAccess::CpuToGpu;
        bool readable = false;

        bool mapped = false;
        std::size_t mappedOffsetBytes = 0;
        std::size_t mappedSizeBytes = 0;
    };

} // namespace gfx
