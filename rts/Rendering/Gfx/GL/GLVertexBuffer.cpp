/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLVertexBuffer.h"

#include "System/Log/ILog.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{

    GLsizeiptr ToGLSize(std::size_t value)
    {
        constexpr std::size_t maxValue = static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::ToGLSize] Size (%zu) exceeds GLsizeiptr max (%zu), clamping", value, maxValue);
            return std::numeric_limits<GLsizeiptr>::max();
        }

        return static_cast<GLsizeiptr>(value);
    }

    GLintptr ToGLOffset(std::size_t value)
    {
        constexpr std::size_t maxValue = static_cast<std::size_t>(std::numeric_limits<GLintptr>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::ToGLOffset] Offset (%zu) exceeds GLintptr max (%zu), clamping", value, maxValue);
            return std::numeric_limits<GLintptr>::max();
        }

        return static_cast<GLintptr>(value);
    }

} // namespace

namespace gfx
{

    GLVertexBuffer::GLVertexBuffer(const BufferCreateInfo &ci)
        : sizeBytes(ci.sizeBytes), usage(ci.usage), memoryAccess(ci.memoryAccess), readable(ci.readable)
    {
        glGenBuffers(1, &bufferId);

        if (bufferId == 0)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::GLVertexBuffer] glGenBuffers returned 0");
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, bufferId);
        glBufferData(GL_ARRAY_BUFFER, ToGLSize(sizeBytes), nullptr, TranslateUsage(usage));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLVertexBuffer::~GLVertexBuffer()
    {
        UnmapWrite();

        if (bufferId != 0)
        {
            glDeleteBuffers(1, &bufferId);
            bufferId = 0;
        }
    }

    GLVertexBuffer::GLVertexBuffer(GLVertexBuffer &&other) noexcept
    {
        MoveFrom(std::move(other));
    }

    GLVertexBuffer &GLVertexBuffer::operator=(GLVertexBuffer &&other) noexcept
    {
        if (this == &other)
            return *this;

        UnmapWrite();

        if (bufferId != 0)
            glDeleteBuffers(1, &bufferId);

        MoveFrom(std::move(other));
        return *this;
    }

    bool GLVertexBuffer::IsMappable() const noexcept
    {
        return (memoryAccess != MemoryAccess::GpuOnly) || readable;
    }

    void GLVertexBuffer::Resize(std::size_t newSizeBytes, bool preserveData)
    {
        if (bufferId == 0)
            return;

        if (newSizeBytes == sizeBytes)
            return;

        if (mapped)
            UnmapWrite();

        const GLenum glUsage = TranslateUsage(usage);

        if (!preserveData || sizeBytes == 0 || newSizeBytes == 0 || !IS_GL_FUNCTION_AVAILABLE(glCopyBufferSubData))
        {
            if (preserveData && !IS_GL_FUNCTION_AVAILABLE(glCopyBufferSubData))
            {
                LOG_L(L_WARNING, "[GLVertexBuffer::Resize] glCopyBufferSubData unavailable, resizing without preserving data");
            }

            glBindBuffer(GL_ARRAY_BUFFER, bufferId);
            glBufferData(GL_ARRAY_BUFFER, ToGLSize(newSizeBytes), nullptr, glUsage);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            sizeBytes = newSizeBytes;
            return;
        }

        GLuint newBufferId = 0;
        glGenBuffers(1, &newBufferId);

        if (newBufferId == 0)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::Resize] glGenBuffers for resize failed, resizing without preserving data");

            glBindBuffer(GL_ARRAY_BUFFER, bufferId);
            glBufferData(GL_ARRAY_BUFFER, ToGLSize(newSizeBytes), nullptr, glUsage);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            sizeBytes = newSizeBytes;
            return;
        }

        glBindBuffer(GL_COPY_WRITE_BUFFER, newBufferId);
        glBufferData(GL_COPY_WRITE_BUFFER, ToGLSize(newSizeBytes), nullptr, glUsage);

        glBindBuffer(GL_COPY_READ_BUFFER, bufferId);

        const std::size_t copySizeBytes = std::min(sizeBytes, newSizeBytes);
        if (copySizeBytes > 0)
        {
            glCopyBufferSubData(
                GL_COPY_READ_BUFFER,
                GL_COPY_WRITE_BUFFER,
                ToGLOffset(0),
                ToGLOffset(0),
                ToGLSize(copySizeBytes));
        }

        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        glDeleteBuffers(1, &bufferId);
        bufferId = newBufferId;
        sizeBytes = newSizeBytes;
    }

    void GLVertexBuffer::Update(std::span<const std::byte> src, std::size_t dstOffsetBytes)
    {
        if (bufferId == 0 || src.empty())
            return;

        if (dstOffsetBytes > sizeBytes)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::Update] Offset (%zu) is outside buffer size (%zu)", dstOffsetBytes, sizeBytes);
            return;
        }

        const std::size_t updateSizeBytes = src.size();
        if (updateSizeBytes > (sizeBytes - dstOffsetBytes))
        {
            LOG_L(
                L_WARNING,
                "[GLVertexBuffer::Update] Update range (%zu..%zu) exceeds buffer size (%zu)",
                dstOffsetBytes,
                (dstOffsetBytes + updateSizeBytes),
                sizeBytes);
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, bufferId);
        glBufferSubData(GL_ARRAY_BUFFER, ToGLOffset(dstOffsetBytes), ToGLSize(updateSizeBytes), src.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    std::span<std::byte> GLVertexBuffer::MapWrite(std::size_t offsetBytes, std::size_t mapSizeBytes)
    {
        if (bufferId == 0 || mapSizeBytes == 0)
            return {};

        if (!IsMappable())
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::MapWrite] Attempted to map a non-mappable buffer");
            return {};
        }

        if (mapped)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::MapWrite] Buffer is already mapped");
            return {};
        }

        if (offsetBytes > sizeBytes || mapSizeBytes > (sizeBytes - offsetBytes))
        {
            LOG_L(
                L_WARNING,
                "[GLVertexBuffer::MapWrite] Map range (%zu..%zu) exceeds buffer size (%zu)",
                offsetBytes,
                (offsetBytes + mapSizeBytes),
                sizeBytes);
            return {};
        }

        glBindBuffer(GL_ARRAY_BUFFER, bufferId);

        void *mappedPtr = glMapBufferRange(
            GL_ARRAY_BUFFER,
            ToGLOffset(offsetBytes),
            ToGLSize(mapSizeBytes),
            GetMapWriteFlags());

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (mappedPtr == nullptr)
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::MapWrite] glMapBufferRange returned null");
            return {};
        }

        mapped = true;
        mappedOffsetBytes = offsetBytes;
        mappedSizeBytes = mapSizeBytes;

        return {static_cast<std::byte *>(mappedPtr), mapSizeBytes};
    }

    void GLVertexBuffer::UnmapWrite()
    {
        if (!mapped || bufferId == 0)
        {
            ResetMapState();
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, bufferId);

        if (!glUnmapBuffer(GL_ARRAY_BUFFER))
        {
            LOG_L(L_WARNING, "[GLVertexBuffer::UnmapWrite] glUnmapBuffer reported data corruption");
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        ResetMapState();
    }

    void GLVertexBuffer::MoveFrom(GLVertexBuffer &&other) noexcept
    {
        bufferId = other.bufferId;
        sizeBytes = other.sizeBytes;
        usage = other.usage;
        memoryAccess = other.memoryAccess;
        readable = other.readable;
        mapped = other.mapped;
        mappedOffsetBytes = other.mappedOffsetBytes;
        mappedSizeBytes = other.mappedSizeBytes;

        other.bufferId = 0;
        other.sizeBytes = 0;
        other.mapped = false;
        other.mappedOffsetBytes = 0;
        other.mappedSizeBytes = 0;
    }

    GLenum GLVertexBuffer::TranslateUsage(BufferUsage value) const noexcept
    {
        switch (value)
        {
        case BufferUsage::Static:
            return GL_STATIC_DRAW;
        case BufferUsage::Dynamic:
            return GL_DYNAMIC_DRAW;
        case BufferUsage::Stream:
            return GL_STREAM_DRAW;
        }

        return GL_DYNAMIC_DRAW;
    }

    GLbitfield GLVertexBuffer::GetMapWriteFlags() const noexcept
    {
        GLbitfield flags = GL_MAP_WRITE_BIT;

        if (usage != BufferUsage::Static)
            flags |= GL_MAP_INVALIDATE_RANGE_BIT;

        if (memoryAccess == MemoryAccess::CpuToGpu)
            flags |= GL_MAP_UNSYNCHRONIZED_BIT;

        return flags;
    }

    void GLVertexBuffer::ResetMapState() noexcept
    {
        mapped = false;
        mappedOffsetBytes = 0;
        mappedSizeBytes = 0;
    }

} // namespace gfx
