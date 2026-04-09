/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IFramebuffer.h"
#include "Rendering/GL/myGL.h"

#include <cstdint>
#include <span>

namespace gfx
{

    class GLFramebuffer final : public IFramebuffer
    {
    public:
        explicit GLFramebuffer(const RenderTargetDesc &desc);
        ~GLFramebuffer() override;

        GLFramebuffer(const GLFramebuffer &) = delete;
        GLFramebuffer &operator=(const GLFramebuffer &) = delete;

        GLFramebuffer(GLFramebuffer &&other) noexcept;
        GLFramebuffer &operator=(GLFramebuffer &&other) noexcept;

        [[nodiscard]] Extent3D GetExtent() const noexcept override;
        [[nodiscard]] std::uint32_t GetSampleCount() const noexcept override;
        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override;

        void SetAttachments(std::span<const AttachmentViewDesc> attachments) override;
        void SetColorDrawTargets(std::span<const AttachmentPoint> colorTargets) override;
        [[nodiscard]] FramebufferStatus Validate() const noexcept override;
        bool Blit(const FramebufferBlitDesc &desc) override;

    private:
        void MoveFrom(GLFramebuffer &&other) noexcept;
        void ClearAllAttachments() const;
        bool AttachView(const AttachmentViewDesc &view) const;

    private:
        GLuint framebufferId = 0;
        Extent3D extent = {};
        std::uint32_t sampleCount = 1;
    };

} // namespace gfx
