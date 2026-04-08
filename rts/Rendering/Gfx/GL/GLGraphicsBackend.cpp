/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLGraphicsBackend.h"

#include "GLTexture.h"
#include "GLVertexBuffer.h"

#include "Rendering/GL/myGL.h"
#include "System/Log/ILog.h"

#include <cstdint>
#include <limits>
#include <memory>

namespace
{

    GLenum TranslateLinePrimitive(gfx::LinePrimitive primitive)
    {
        switch (primitive)
        {
        case gfx::LinePrimitive::LineStrip:
            return GL_LINE_STRIP;
        case gfx::LinePrimitive::Lines:
            return GL_LINES;
        }

        return GL_LINES;
    }

    GLint ToGLInt(std::uint32_t value, const char *ctx)
    {
        constexpr std::uint32_t maxValue = static_cast<std::uint32_t>(std::numeric_limits<GLint>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::%s] value (%u) exceeds GLint max (%u), clamping", ctx, value, maxValue);
            return std::numeric_limits<GLint>::max();
        }

        return static_cast<GLint>(value);
    }

    GLsizei ToGLSizei(std::uint32_t value, const char *ctx)
    {
        constexpr std::uint32_t maxValue = static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::%s] value (%u) exceeds GLsizei max (%u), clamping", ctx, value, maxValue);
            return std::numeric_limits<GLsizei>::max();
        }

        return static_cast<GLsizei>(value);
    }

    GLenum TranslateIndexElementType(gfx::IndexElementType indexType)
    {
        switch (indexType)
        {
        case gfx::IndexElementType::UInt16:
            return GL_UNSIGNED_SHORT;
        case gfx::IndexElementType::UInt32:
            return GL_UNSIGNED_INT;
        }

        return GL_UNSIGNED_SHORT;
    }

    std::size_t IndexElementSizeBytes(gfx::IndexElementType indexType)
    {
        switch (indexType)
        {
        case gfx::IndexElementType::UInt16:
            return sizeof(std::uint16_t);
        case gfx::IndexElementType::UInt32:
            return sizeof(std::uint32_t);
        }

        return 0;
    }

    const GLvoid *ToGLPointer(std::size_t byteOffset)
    {
        return reinterpret_cast<const GLvoid *>(static_cast<std::uintptr_t>(byteOffset));
    }

} // namespace

namespace gfx
{

    BackendType GLGraphicsBackend::Type() const noexcept
    {
        return BackendType::OpenGL;
    }

    std::string_view GLGraphicsBackend::Name() const noexcept
    {
        return "OpenGL";
    }

    bool GLGraphicsBackend::SupportsPersistentMapping() const noexcept
    {
        return (GLAD_GL_ARB_buffer_storage != 0);
    }

    std::unique_ptr<IVertexBuffer> GLGraphicsBackend::CreateVertexBuffer(const BufferCreateInfo &ci)
    {
        return std::make_unique<GLVertexBuffer>(ci);
    }

    std::unique_ptr<ITexture> GLGraphicsBackend::CreateTexture(const TextureCreateInfo &ci)
    {
        return std::make_unique<GLTexture>(ci);
    }

    void GLGraphicsBackend::DrawLineBatches(
        IVertexBuffer &vertexBuffer,
        std::span<const LineBatchDesc> batches,
        const LineStippleState &stippleState)
    {
        if (batches.empty())
            return;

        auto *glVertexBuffer = dynamic_cast<GLVertexBuffer *>(&vertexBuffer);
        if (glVertexBuffer == nullptr)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawLineBatches] Unsupported vertex buffer type");
            return;
        }

        const GLuint bufferId = glVertexBuffer->GetBufferId();
        if (bufferId == 0)
            return;

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        glPushAttrib(GL_ENABLE_BIT);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);

        if (stippleState.enabled)
        {
            glEnable(GL_LINE_STIPPLE);
            glLineStipple(
                ToGLInt(stippleState.factor, "DrawLineBatches::factor"),
                static_cast<GLushort>(stippleState.pattern));
        }
        else
        {
            glDisable(GL_LINE_STIPPLE);
        }

        glBindBuffer(GL_ARRAY_BUFFER, bufferId);

        constexpr GLsizei stride = static_cast<GLsizei>(sizeof(LineVertexPC));
        glVertexPointer(3, GL_FLOAT, stride, reinterpret_cast<const GLvoid *>(0));
        glColorPointer(4, GL_FLOAT, stride, reinterpret_cast<const GLvoid *>(3u * sizeof(float)));

        for (const LineBatchDesc &batch : batches)
        {
            if (batch.vertexCount == 0)
                continue;

            glDrawArrays(
                TranslateLinePrimitive(batch.primitive),
                ToGLInt(batch.firstVertex, "DrawLineBatches::firstVertex"),
                ToGLSizei(batch.vertexCount, "DrawLineBatches::vertexCount"));
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    void GLGraphicsBackend::DrawTexturedIndexedBatches(
        IVertexBuffer &vertexBuffer,
        IVertexBuffer &indexBuffer,
        ITexture &texture,
        std::span<const TexturedIndexedBatchDesc> batches,
        const TexturedVertexLayout &vertexLayout,
        IndexElementType indexType,
        const TexturedBatchState &state)
    {
        if (batches.empty())
            return;

        if (vertexLayout.strideBytes == 0)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawTexturedIndexedBatches] vertexLayout.strideBytes must be non-zero");
            return;
        }

        auto *glVertexBuffer = dynamic_cast<GLVertexBuffer *>(&vertexBuffer);
        auto *glIndexBuffer = dynamic_cast<GLVertexBuffer *>(&indexBuffer);
        auto *glTexture = dynamic_cast<GLTexture *>(&texture);

        if ((glVertexBuffer == nullptr) || (glIndexBuffer == nullptr) || (glTexture == nullptr))
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawTexturedIndexedBatches] Unsupported resource implementation(s)");
            return;
        }

        const std::size_t indexElementSizeBytes = IndexElementSizeBytes(indexType);
        if (indexElementSizeBytes == 0)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawTexturedIndexedBatches] Unsupported index element type");
            return;
        }

        const GLuint vertexBufferId = glVertexBuffer->GetBufferId();
        const GLuint indexBufferId = glIndexBuffer->GetBufferId();
        const GLuint textureId = glTexture->GetTextureId();

        if ((vertexBufferId == 0u) || (indexBufferId == 0u) || (textureId == 0u))
            return;

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);

        if (state.depthTest)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);

        if (state.blend)
        {
            glEnable(GL_BLEND);

            if (state.premultipliedAlpha)
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            else
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }

        glEnable(GL_TEXTURE_2D);
        glBindTexture(glTexture->GetTarget(), textureId);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        const GLint stride = ToGLInt(vertexLayout.strideBytes, "DrawTexturedIndexedBatches::strideBytes");

        glBindBuffer(GL_ARRAY_BUFFER, vertexBufferId);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferId);

        glVertexPointer(3, GL_FLOAT, stride, ToGLPointer(vertexLayout.positionOffsetBytes));
        glTexCoordPointer(2, GL_FLOAT, stride, ToGLPointer(vertexLayout.texCoordOffsetBytes));
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, ToGLPointer(vertexLayout.colorOffsetBytes));

        const GLenum glIndexType = TranslateIndexElementType(indexType);

        for (const TexturedIndexedBatchDesc &batch : batches)
        {
            if (batch.indexCount == 0)
                continue;

            const std::size_t firstIndexByteOffset = static_cast<std::size_t>(batch.firstIndex) * indexElementSizeBytes;

            glDrawElements(
                GL_TRIANGLES,
                ToGLSizei(batch.indexCount, "DrawTexturedIndexedBatches::indexCount"),
                glIndexType,
                ToGLPointer(firstIndexByteOffset));
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        glBindTexture(glTexture->GetTarget(), 0);
        glPopAttrib();
    }

    void GLGraphicsBackend::BeginFrame()
    {
    }

    void GLGraphicsBackend::EndFrame()
    {
    }

    void GLGraphicsBackend::DeviceWaitIdle()
    {
        glFinish();
    }

} // namespace gfx
