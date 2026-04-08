/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLGraphicsBackend.h"

#include "GLTexture.h"
#include "GLVertexBuffer.h"

#include "Rendering/GL/myGL.h"
#include "System/Log/ILog.h"

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
                static_cast<GLushort>(stippleState.pattern)
            );
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
                ToGLSizei(batch.vertexCount, "DrawLineBatches::vertexCount")
            );
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
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
