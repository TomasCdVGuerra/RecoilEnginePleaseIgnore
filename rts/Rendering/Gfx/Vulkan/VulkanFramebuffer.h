/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IFramebuffer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

namespace gfx
{

    class VulkanFramebuffer final : public IFramebuffer
    {
    public:
        VulkanFramebuffer(VkDevice device, const RenderTargetDesc &desc);
        ~VulkanFramebuffer() override;

        VulkanFramebuffer(const VulkanFramebuffer &) = delete;
        VulkanFramebuffer &operator=(const VulkanFramebuffer &) = delete;

        [[nodiscard]] Extent3D GetExtent() const noexcept override
        {
            return extent;
        }

        [[nodiscard]] std::uint32_t GetSampleCount() const noexcept override
        {
            return sampleCount;
        }

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override;

        void SetAttachments(std::span<const AttachmentViewDesc> attachments) override;
        void SetColorDrawTargets(std::span<const AttachmentPoint> colorTargets) override;
        [[nodiscard]] FramebufferStatus Validate() const noexcept override;
        bool Blit(const FramebufferBlitDesc &desc) override;

        [[nodiscard]] VkRenderPass GetRenderPass() const noexcept
        {
            return renderPass;
        }

        [[nodiscard]] VkFramebuffer GetFramebuffer() const noexcept
        {
            return framebuffer;
        }

    private:
        void RecreateFramebuffer();
        void DestroyFramebufferObjects() noexcept;

    private:
        VkDevice device = VK_NULL_HANDLE;

        Extent3D extent = {};
        std::uint32_t sampleCount = 1;

        std::vector<AttachmentViewDesc> attachmentViews;
        std::vector<AttachmentPoint> colorDrawTargets;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        FramebufferStatus status = FramebufferStatus::MissingAttachment;
    };

} // namespace gfx
