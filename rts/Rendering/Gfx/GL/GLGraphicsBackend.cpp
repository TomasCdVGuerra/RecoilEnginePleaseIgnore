/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLGraphicsBackend.h"

#include "GLFramebuffer.h"
#include "GLTexture.h"
#include "GLVertexArray.h"
#include "GLVertexBuffer.h"

#include "Rendering/GL/myGL.h"
#include "System/Log/ILog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

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

    GLenum TranslatePrimitiveTopology(gfx::PrimitiveTopology topology)
    {
        switch (topology)
        {
        case gfx::PrimitiveTopology::Triangles:
            return GL_TRIANGLES;
        case gfx::PrimitiveTopology::Lines:
            return GL_LINES;
        case gfx::PrimitiveTopology::LineStrip:
            return GL_LINE_STRIP;
        }

        return GL_TRIANGLES;
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

    GLuint ToGLuint(std::uintptr_t value, const char *ctx)
    {
        constexpr std::uintptr_t maxValue = static_cast<std::uintptr_t>(std::numeric_limits<GLuint>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::%s] native handle (%zu) exceeds GLuint max (%zu)", ctx, static_cast<std::size_t>(value), static_cast<std::size_t>(maxValue));
            return 0u;
        }

        return static_cast<GLuint>(value);
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

    struct GLVertexFormatInfo
    {
        GLint components = 0;
        GLenum type = GL_FLOAT;
        GLboolean normalized = GL_FALSE;
        bool integerAttribute = false;
    };

    bool TranslateVertexFormat(gfx::VertexFormat format, GLVertexFormatInfo &outInfo)
    {
        switch (format)
        {
        case gfx::VertexFormat::Float:
            outInfo = {1, GL_FLOAT, GL_FALSE, false};
            return true;
        case gfx::VertexFormat::Float2:
            outInfo = {2, GL_FLOAT, GL_FALSE, false};
            return true;
        case gfx::VertexFormat::Float3:
            outInfo = {3, GL_FLOAT, GL_FALSE, false};
            return true;
        case gfx::VertexFormat::Float4:
            outInfo = {4, GL_FLOAT, GL_FALSE, false};
            return true;
        case gfx::VertexFormat::UInt:
            outInfo = {1, GL_UNSIGNED_INT, GL_FALSE, true};
            return true;
        case gfx::VertexFormat::UInt2:
            outInfo = {2, GL_UNSIGNED_INT, GL_FALSE, true};
            return true;
        case gfx::VertexFormat::UInt3:
            outInfo = {3, GL_UNSIGNED_INT, GL_FALSE, true};
            return true;
        case gfx::VertexFormat::UInt4:
            outInfo = {4, GL_UNSIGNED_INT, GL_FALSE, true};
            return true;
        case gfx::VertexFormat::UByte4_UNorm:
            outInfo = {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
            return true;
        case gfx::VertexFormat::UByte4_UInt:
            outInfo = {4, GL_UNSIGNED_BYTE, GL_FALSE, true};
            return true;
        }

        return false;
    }

    const gfx::VertexBufferBindingDesc *FindBindingDesc(
        const gfx::VertexLayoutDesc &layout,
        std::uint32_t binding)
    {
        const auto it = std::find_if(
            layout.bindings.begin(),
            layout.bindings.end(),
            [binding](const gfx::VertexBufferBindingDesc &desc)
            {
                return desc.binding == binding;
            });

        if (it == layout.bindings.end())
            return nullptr;

        return &(*it);
    }

    const gfx::VertexArrayBufferBinding *FindVertexBufferBinding(
        std::span<const gfx::VertexArrayBufferBinding> vertexBuffers,
        std::uint32_t binding)
    {
        const auto it = std::find_if(
            vertexBuffers.begin(),
            vertexBuffers.end(),
            [binding](const gfx::VertexArrayBufferBinding &bufferBinding)
            {
                return bufferBinding.binding == binding;
            });

        if (it == vertexBuffers.end())
            return nullptr;

        return &(*it);
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

    std::unique_ptr<IFramebuffer> GLGraphicsBackend::CreateFramebuffer(const RenderTargetDesc &desc)
    {
        return std::make_unique<GLFramebuffer>(desc);
    }

    std::unique_ptr<IVertexArray> GLGraphicsBackend::CreateVertexArray(
        const VertexLayoutDesc &layout,
        std::span<const VertexArrayBufferBinding> vertexBuffers,
        IVertexBuffer *indexBuffer)
    {
        if (layout.attributes.empty())
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] layout.attributes must be non-empty");
            return nullptr;
        }

        GLuint vaoId = 0;
        glGenVertexArrays(1, &vaoId);

        if (vaoId == 0u)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] glGenVertexArrays returned 0");
            return nullptr;
        }

        glBindVertexArray(vaoId);

        for (const VertexAttributeDesc &attribute : layout.attributes)
        {
            const VertexBufferBindingDesc *bindingDesc = FindBindingDesc(layout, attribute.binding);
            if (bindingDesc == nullptr)
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] Missing binding descriptor for binding=%u", attribute.binding);
                continue;
            }

            if (bindingDesc->strideBytes == 0u)
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] binding=%u has zero strideBytes", attribute.binding);
                continue;
            }

            const VertexArrayBufferBinding *bufferBinding = FindVertexBufferBinding(vertexBuffers, attribute.binding);
            if ((bufferBinding == nullptr) || (bufferBinding->vertexBuffer == nullptr))
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] Missing vertex buffer for binding=%u", attribute.binding);
                continue;
            }

            auto *glVertexBuffer = dynamic_cast<GLVertexBuffer *>(bufferBinding->vertexBuffer);
            if (glVertexBuffer == nullptr)
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] Unsupported vertex buffer type for binding=%u", attribute.binding);
                continue;
            }

            const GLuint vertexBufferId = glVertexBuffer->GetBufferId();
            if (vertexBufferId == 0u)
                continue;

            GLVertexFormatInfo formatInfo;
            if (!TranslateVertexFormat(attribute.format, formatInfo))
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] Unsupported vertex format for location=%u", attribute.location);
                continue;
            }

            glBindBuffer(GL_ARRAY_BUFFER, vertexBufferId);

            const std::size_t totalOffsetBytes =
                static_cast<std::size_t>(bindingDesc->offsetBytes) +
                static_cast<std::size_t>(attribute.offsetBytes);

            const GLint strideBytes = ToGLInt(bindingDesc->strideBytes, "CreateVertexArray::strideBytes");
            const GLvoid *offsetPointer = ToGLPointer(totalOffsetBytes);

            if (formatInfo.integerAttribute)
            {
                glVertexAttribIPointer(
                    attribute.location,
                    formatInfo.components,
                    formatInfo.type,
                    strideBytes,
                    offsetPointer);
            }
            else
            {
                glVertexAttribPointer(
                    attribute.location,
                    formatInfo.components,
                    formatInfo.type,
                    formatInfo.normalized,
                    strideBytes,
                    offsetPointer);
            }

            glEnableVertexAttribArray(attribute.location);
            glVertexAttribDivisor(
                attribute.location,
                (bindingDesc->inputRate == VertexInputRate::PerInstance) ? 1u : 0u);
        }

        if (indexBuffer != nullptr)
        {
            auto *glIndexBuffer = dynamic_cast<GLVertexBuffer *>(indexBuffer);
            if (glIndexBuffer != nullptr)
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glIndexBuffer->GetBufferId());
            }
            else
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::CreateVertexArray] Unsupported index buffer type");
            }
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return std::make_unique<GLVertexArray>(vaoId);
    }

    void GLGraphicsBackend::BindFramebuffer(IRenderTarget *target)
    {
        GLuint framebufferId = 0u;

        if (target != nullptr)
            framebufferId = ToGLuint(target->GetNativeHandle(), "BindFramebuffer");

        glBindFramebuffer(GL_FRAMEBUFFER, framebufferId);
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

            if (state.useDefaultBlendFunc)
            {
                if (state.premultipliedAlpha)
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                else
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
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

    void GLGraphicsBackend::DrawIndexed(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        const IndexedDrawDesc &draw,
        IndexElementType indexType)
    {
        if (draw.indexCount == 0u)
            return;

        const std::size_t indexElementSizeBytes = IndexElementSizeBytes(indexType);
        if (indexElementSizeBytes == 0u)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawIndexed] Unsupported index element type");
            return;
        }

        const GLenum glIndexType = TranslateIndexElementType(indexType);
        const GLenum glPrimitive = TranslatePrimitiveTopology(topology);

        vertexArray.Bind();

        const std::size_t firstIndexByteOffset = static_cast<std::size_t>(draw.firstIndex) * indexElementSizeBytes;
        if (draw.baseVertex == 0)
        {
            glDrawElements(
                glPrimitive,
                ToGLSizei(draw.indexCount, "DrawIndexed::indexCount"),
                glIndexType,
                ToGLPointer(firstIndexByteOffset));
        }
        else
        {
            glDrawElementsBaseVertex(
                glPrimitive,
                ToGLSizei(draw.indexCount, "DrawIndexed::indexCount"),
                glIndexType,
                ToGLPointer(firstIndexByteOffset),
                draw.baseVertex);
        }

        vertexArray.Unbind();
    }

    void GLGraphicsBackend::DrawIndexedInstanced(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        const IndexedInstancedDrawDesc &draw,
        IndexElementType indexType)
    {
        if ((draw.indexCount == 0u) || (draw.instanceCount == 0u))
            return;

        const std::size_t indexElementSizeBytes = IndexElementSizeBytes(indexType);
        if (indexElementSizeBytes == 0u)
        {
            LOG_L(L_WARNING, "[GLGraphicsBackend::DrawIndexedInstanced] Unsupported index element type");
            return;
        }

        const GLenum glIndexType = TranslateIndexElementType(indexType);
        const GLenum glPrimitive = TranslatePrimitiveTopology(topology);

        vertexArray.Bind();

        const std::size_t firstIndexByteOffset = static_cast<std::size_t>(draw.firstIndex) * indexElementSizeBytes;
        const GLsizei indexCount = ToGLSizei(draw.indexCount, "DrawIndexedInstanced::indexCount");
        const GLsizei instanceCount = ToGLSizei(draw.instanceCount, "DrawIndexedInstanced::instanceCount");
        const GLvoid *indexPointer = ToGLPointer(firstIndexByteOffset);

        if ((draw.baseVertex == 0) && (draw.firstInstance == 0u))
        {
            glDrawElementsInstanced(
                glPrimitive,
                indexCount,
                glIndexType,
                indexPointer,
                instanceCount);
        }
        else if (draw.firstInstance == 0u)
        {
            glDrawElementsInstancedBaseVertex(
                glPrimitive,
                indexCount,
                glIndexType,
                indexPointer,
                instanceCount,
                draw.baseVertex);
        }
        else
        {
            glDrawElementsInstancedBaseVertexBaseInstance(
                glPrimitive,
                indexCount,
                glIndexType,
                indexPointer,
                instanceCount,
                draw.baseVertex,
                draw.firstInstance);
        }

        vertexArray.Unbind();
    }

    void GLGraphicsBackend::MultiDrawIndexedIndirect(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        std::span<const IndexedIndirectDrawCommand> commands,
        IndexElementType indexType)
    {
        if (commands.empty())
            return;

        const GLenum glIndexType = TranslateIndexElementType(indexType);
        const GLenum glPrimitive = TranslatePrimitiveTopology(topology);

        std::vector<SDrawElementsIndirectCommand> glCommands;
        glCommands.reserve(commands.size());

        for (const IndexedIndirectDrawCommand &command : commands)
        {
            if (command.baseVertex < 0)
            {
                LOG_L(L_WARNING, "[GLGraphicsBackend::MultiDrawIndexedIndirect] Negative baseVertex (%d) is not supported", command.baseVertex);
                continue;
            }

            glCommands.emplace_back(
                command.indexCount,
                command.instanceCount,
                command.firstIndex,
                static_cast<std::uint32_t>(command.baseVertex),
                command.firstInstance);
        }

        if (glCommands.empty())
            return;

        vertexArray.Bind();

        glMultiDrawElementsIndirect(
            glPrimitive,
            glIndexType,
            glCommands.data(),
            ToGLSizei(static_cast<std::uint32_t>(glCommands.size()), "MultiDrawIndexedIndirect::drawCount"),
            static_cast<GLsizei>(sizeof(SDrawElementsIndirectCommand)));

        vertexArray.Unbind();
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
