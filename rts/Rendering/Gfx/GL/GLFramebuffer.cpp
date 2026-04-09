/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLFramebuffer.h"

#include "Rendering/Gfx/ITexture.h"
#include "System/Log/ILog.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{

    class ScopedFramebufferBinding
    {
    public:
        explicit ScopedFramebufferBinding(GLuint framebufferId)
        {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFramebufferId);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebufferId);
        }

        ~ScopedFramebufferBinding()
        {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(prevFramebufferId));
        }

    private:
        GLint prevFramebufferId = 0;
    };

    std::uint32_t ClampAtLeastOne(std::uint32_t value)
    {
        return std::max<std::uint32_t>(1u, value);
    }

    GLint ToGLInt(std::uint32_t value, const char *ctx)
    {
        constexpr std::uint32_t maxValue = static_cast<std::uint32_t>(std::numeric_limits<GLint>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLFramebuffer::%s] value (%u) exceeds GLint max (%u), clamping", ctx, value, maxValue);
            return std::numeric_limits<GLint>::max();
        }

        return static_cast<GLint>(value);
    }

    bool ToGLuint(std::uintptr_t nativeHandle, GLuint &outId)
    {
        constexpr std::uintptr_t maxValue = static_cast<std::uintptr_t>(std::numeric_limits<GLuint>::max());
        if (nativeHandle > maxValue)
            return false;

        outId = static_cast<GLuint>(nativeHandle);
        return true;
    }

    bool IsColorAttachmentPoint(gfx::AttachmentPoint point)
    {
        switch (point)
        {
        case gfx::AttachmentPoint::Color0:
        case gfx::AttachmentPoint::Color1:
        case gfx::AttachmentPoint::Color2:
        case gfx::AttachmentPoint::Color3:
        case gfx::AttachmentPoint::Color4:
        case gfx::AttachmentPoint::Color5:
        case gfx::AttachmentPoint::Color6:
        case gfx::AttachmentPoint::Color7:
            return true;
        case gfx::AttachmentPoint::Depth:
        case gfx::AttachmentPoint::Stencil:
        case gfx::AttachmentPoint::DepthStencil:
            break;
        }

        return false;
    }

    bool TryTranslateAttachmentPoint(gfx::AttachmentPoint point, GLenum &attachment)
    {
        switch (point)
        {
        case gfx::AttachmentPoint::Color0:
        case gfx::AttachmentPoint::Color1:
        case gfx::AttachmentPoint::Color2:
        case gfx::AttachmentPoint::Color3:
        case gfx::AttachmentPoint::Color4:
        case gfx::AttachmentPoint::Color5:
        case gfx::AttachmentPoint::Color6:
        case gfx::AttachmentPoint::Color7:
            attachment = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(point);
            return true;
        case gfx::AttachmentPoint::Depth:
            attachment = GL_DEPTH_ATTACHMENT;
            return true;
        case gfx::AttachmentPoint::Stencil:
            attachment = GL_STENCIL_ATTACHMENT;
            return true;
        case gfx::AttachmentPoint::DepthStencil:
            attachment = GL_DEPTH_STENCIL_ATTACHMENT;
            return true;
        }

        return false;
    }

    GLenum TranslateBlitFilter(gfx::FilterMode filter)
    {
        switch (filter)
        {
        case gfx::FilterMode::Nearest:
            return GL_NEAREST;
        case gfx::FilterMode::Linear:
        case gfx::FilterMode::NearestMipmapNearest:
        case gfx::FilterMode::LinearMipmapNearest:
        case gfx::FilterMode::NearestMipmapLinear:
        case gfx::FilterMode::LinearMipmapLinear:
            return GL_LINEAR;
        }

        return GL_NEAREST;
    }

    void ClearAttachment(GLenum attachment)
    {
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D, 0, 0);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, attachment, GL_RENDERBUFFER_EXT, 0);
    }

    bool IsValidRect(const std::array<int, 4> &rect)
    {
        return ((rect[2] - rect[0]) > 0) && ((rect[3] - rect[1]) > 0);
    }

} // namespace

namespace gfx
{

    GLFramebuffer::GLFramebuffer(const RenderTargetDesc &desc)
        : extent({ClampAtLeastOne(desc.extent.width),
                  ClampAtLeastOne(desc.extent.height),
                  ClampAtLeastOne(desc.extent.depth)}),
          sampleCount(ClampAtLeastOne(desc.sampleCount))
    {
        glGenFramebuffersEXT(1, &framebufferId);

        if (framebufferId == 0u)
        {
            LOG_L(L_WARNING, "[GLFramebuffer::GLFramebuffer] glGenFramebuffersEXT returned 0");
            return;
        }

        if (!desc.attachments.empty())
            SetAttachments(desc.attachments);

        if (!desc.colorDrawOrder.empty())
        {
            SetColorDrawTargets(desc.colorDrawOrder);
        }
        else
        {
            std::vector<AttachmentPoint> defaultColorTargets;
            defaultColorTargets.reserve(desc.attachments.size());

            for (const AttachmentViewDesc &view : desc.attachments)
            {
                if (!IsColorAttachmentPoint(view.point) || (view.texture == nullptr))
                    continue;

                defaultColorTargets.push_back(view.point);
            }

            SetColorDrawTargets(defaultColorTargets);
        }
    }

    GLFramebuffer::~GLFramebuffer()
    {
        if (framebufferId != 0u)
        {
            glDeleteFramebuffersEXT(1, &framebufferId);
            framebufferId = 0u;
        }
    }

    GLFramebuffer::GLFramebuffer(GLFramebuffer &&other) noexcept
    {
        MoveFrom(std::move(other));
    }

    GLFramebuffer &GLFramebuffer::operator=(GLFramebuffer &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (framebufferId != 0u)
            glDeleteFramebuffersEXT(1, &framebufferId);

        MoveFrom(std::move(other));
        return *this;
    }

    Extent3D GLFramebuffer::GetExtent() const noexcept
    {
        return extent;
    }

    std::uint32_t GLFramebuffer::GetSampleCount() const noexcept
    {
        return sampleCount;
    }

    std::uintptr_t GLFramebuffer::GetNativeHandle() const noexcept
    {
        return static_cast<std::uintptr_t>(framebufferId);
    }

    void GLFramebuffer::SetAttachments(std::span<const AttachmentViewDesc> attachments)
    {
        if (framebufferId == 0u)
            return;

        ScopedFramebufferBinding binding(framebufferId);
        ClearAllAttachments();

        for (const AttachmentViewDesc &view : attachments)
            AttachView(view);
    }

    void GLFramebuffer::SetColorDrawTargets(std::span<const AttachmentPoint> colorTargets)
    {
        if (framebufferId == 0u)
            return;

        ScopedFramebufferBinding binding(framebufferId);

        if (colorTargets.empty())
        {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            return;
        }

        std::vector<GLenum> drawBuffers;
        drawBuffers.reserve(colorTargets.size());

        for (const AttachmentPoint point : colorTargets)
        {
            if (!IsColorAttachmentPoint(point))
            {
                LOG_L(L_WARNING, "[GLFramebuffer::SetColorDrawTargets] non-color attachment point in draw target list");
                continue;
            }

            drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(point));
        }

        if (drawBuffers.empty())
        {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            return;
        }

        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        glReadBuffer(drawBuffers.front());
    }

    FramebufferStatus GLFramebuffer::Validate() const noexcept
    {
        if (framebufferId == 0u)
            return FramebufferStatus::Unsupported;

        ScopedFramebufferBinding binding(framebufferId);

        switch (glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT))
        {
        case GL_FRAMEBUFFER_COMPLETE_EXT:
            return FramebufferStatus::Complete;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
            return FramebufferStatus::MissingAttachment;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
            return FramebufferStatus::IncompleteAttachment;
        case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
            return FramebufferStatus::Unsupported;
        default:
            return FramebufferStatus::IncompleteAttachment;
        }
    }

    bool GLFramebuffer::Blit(const FramebufferBlitDesc &desc)
    {
        if (!IS_GL_FUNCTION_AVAILABLE(glBlitFramebufferEXT))
            return false;

        if (!IsValidRect(desc.srcRect) || !IsValidRect(desc.dstRect))
            return false;

        GLuint srcFramebufferId = 0u;
        GLuint dstFramebufferId = 0u;

        if ((desc.src != nullptr) && !ToGLuint(desc.src->GetNativeHandle(), srcFramebufferId))
        {
            LOG_L(L_WARNING, "[GLFramebuffer::Blit] src native handle exceeds GLuint range");
            return false;
        }

        if ((desc.dst != nullptr) && !ToGLuint(desc.dst->GetNativeHandle(), dstFramebufferId))
        {
            LOG_L(L_WARNING, "[GLFramebuffer::Blit] dst native handle exceeds GLuint range");
            return false;
        }

        GLint prevReadFramebuffer = 0;
        GLint prevDrawFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &prevReadFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &prevDrawFramebuffer);

        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, srcFramebufferId);
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, dstFramebufferId);

        glBlitFramebufferEXT(
            desc.srcRect[0],
            desc.srcRect[1],
            desc.srcRect[2],
            desc.srcRect[3],
            desc.dstRect[0],
            desc.dstRect[1],
            desc.dstRect[2],
            desc.dstRect[3],
            static_cast<GLbitfield>(desc.mask),
            TranslateBlitFilter(desc.filter));

        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, static_cast<GLuint>(prevReadFramebuffer));
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, static_cast<GLuint>(prevDrawFramebuffer));

        return true;
    }

    void GLFramebuffer::MoveFrom(GLFramebuffer &&other) noexcept
    {
        framebufferId = other.framebufferId;
        extent = other.extent;
        sampleCount = other.sampleCount;

        other.framebufferId = 0u;
        other.extent = {};
        other.sampleCount = 1u;
    }

    void GLFramebuffer::ClearAllAttachments() const
    {
        GLint maxColorAttachments = 0;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &maxColorAttachments);

        const GLuint colorAttachmentCount = static_cast<GLuint>(std::max(0, maxColorAttachments));

        for (GLuint i = 0; i < colorAttachmentCount; ++i)
            ClearAttachment(GL_COLOR_ATTACHMENT0 + i);

        ClearAttachment(GL_DEPTH_ATTACHMENT);
        ClearAttachment(GL_STENCIL_ATTACHMENT);
        ClearAttachment(GL_DEPTH_STENCIL_ATTACHMENT);
    }

    bool GLFramebuffer::AttachView(const AttachmentViewDesc &view) const
    {
        if (view.texture == nullptr)
            return true;

        GLenum attachment = GL_COLOR_ATTACHMENT0;
        if (!TryTranslateAttachmentPoint(view.point, attachment))
        {
            LOG_L(L_WARNING, "[GLFramebuffer::AttachView] Unsupported attachment point");
            return false;
        }

        GLuint textureId = 0u;
        if (!ToGLuint(view.texture->GetNativeHandle(), textureId))
        {
            LOG_L(L_WARNING, "[GLFramebuffer::AttachView] Texture native handle exceeds GLuint range");
            return false;
        }

        const GLint mipLevel = ToGLInt(view.mipLevel, "AttachView::mipLevel");
        const GLint layer = ToGLInt(view.baseLayer, "AttachView::baseLayer");

        if (view.layerCount > 1u && IS_GL_FUNCTION_AVAILABLE(glFramebufferTextureEXT)) {
            glFramebufferTextureEXT(GL_FRAMEBUFFER_EXT, attachment, textureId, mipLevel);
            return true;
        }

        switch (view.texture->Dimension())
        {
        case TextureDimension::Tex2D:
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D, textureId, mipLevel);
            return true;
        case TextureDimension::Tex2DArray:
            if (IS_GL_FUNCTION_AVAILABLE(glFramebufferTextureLayerEXT))
            {
                glFramebufferTextureLayerEXT(GL_FRAMEBUFFER_EXT, attachment, textureId, mipLevel, layer);
                return true;
            }

            LOG_L(L_WARNING, "[GLFramebuffer::AttachView] glFramebufferTextureLayerEXT unavailable for Tex2DArray attachment");
            return false;
        case TextureDimension::Tex3D:
            if (IS_GL_FUNCTION_AVAILABLE(glFramebufferTexture3DEXT))
            {
                glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_3D, textureId, mipLevel, layer);
                return true;
            }

            LOG_L(L_WARNING, "[GLFramebuffer::AttachView] 3D attachment path unavailable on this context");
            return false;
        case TextureDimension::Cube:
        {
            const GLint face = std::clamp(layer, 0, 5);
            const GLenum cubeFace = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment, cubeFace, textureId, mipLevel);
            return true;
        }
        }

        return false;
    }

} // namespace gfx
