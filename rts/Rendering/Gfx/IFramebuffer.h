/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "IRenderTarget.h"

#include <span>

namespace gfx
{

    class IFramebuffer : public IRenderTarget
    {
    public:
        ~IFramebuffer() override = default;

        virtual void SetAttachments(std::span<const AttachmentViewDesc> attachments) = 0;
        virtual void SetColorDrawTargets(std::span<const AttachmentPoint> colorTargets) = 0;
        [[nodiscard]] virtual FramebufferStatus Validate() const noexcept = 0;
        virtual bool Blit(const FramebufferBlitDesc &desc) = 0;
    };

} // namespace gfx
