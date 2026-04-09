/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanFramebuffer.h"

#include "VulkanTexture.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace
{

    void CheckVkResult(VkResult result, const char *what)
    {
        if (result == VK_SUCCESS)
            return;

        throw std::runtime_error(std::string(what) + " failed with VkResult=" + std::to_string(static_cast<int>(result)));
    }

    std::uint32_t ClampAtLeastOne(std::uint32_t value)
    {
        return std::max<std::uint32_t>(1u, value);
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

    bool IsDepthAttachmentPoint(gfx::AttachmentPoint point)
    {
        switch (point)
        {
        case gfx::AttachmentPoint::Depth:
        case gfx::AttachmentPoint::Stencil:
        case gfx::AttachmentPoint::DepthStencil:
            return true;
        case gfx::AttachmentPoint::Color0:
        case gfx::AttachmentPoint::Color1:
        case gfx::AttachmentPoint::Color2:
        case gfx::AttachmentPoint::Color3:
        case gfx::AttachmentPoint::Color4:
        case gfx::AttachmentPoint::Color5:
        case gfx::AttachmentPoint::Color6:
        case gfx::AttachmentPoint::Color7:
            break;
        }

        return false;
    }

    VkSampleCountFlagBits TranslateSampleCount(std::uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 1u:
            return VK_SAMPLE_COUNT_1_BIT;
        case 2u:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4u:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8u:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16u:
            return VK_SAMPLE_COUNT_16_BIT;
        case 32u:
            return VK_SAMPLE_COUNT_32_BIT;
        case 64u:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    VkImageLayout ChooseAttachmentLayout(gfx::AttachmentPoint point, VkImageAspectFlags aspectMask)
    {
        if (IsDepthAttachmentPoint(point) || ((aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u))
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    std::uintptr_t ToNativeHandle(VkFramebuffer handle)
    {
        std::uintptr_t nativeHandle = 0;
        std::memcpy(&nativeHandle, &handle, std::min(sizeof(nativeHandle), sizeof(handle)));
        return nativeHandle;
    }

} // namespace

namespace gfx
{

    struct AttachmentBindingInfo
    {
        AttachmentPoint point = AttachmentPoint::Color0;
        std::uint32_t attachmentIndex = 0;
        VkImageAspectFlags aspectMask = 0;
    };

    VulkanFramebuffer::VulkanFramebuffer(VkDevice device_, const RenderTargetDesc &desc)
        : device(device_)
        , extent({ClampAtLeastOne(desc.extent.width), ClampAtLeastOne(desc.extent.height), ClampAtLeastOne(desc.extent.depth)})
        , sampleCount(ClampAtLeastOne(desc.sampleCount))
        , attachmentViews(desc.attachments.begin(), desc.attachments.end())
        , colorDrawTargets(desc.colorDrawOrder.begin(), desc.colorDrawOrder.end())
    {
        RecreateFramebuffer();
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        DestroyFramebufferObjects();
    }

    std::uintptr_t VulkanFramebuffer::GetNativeHandle() const noexcept
    {
        return ToNativeHandle(framebuffer);
    }

    void VulkanFramebuffer::SetAttachments(std::span<const AttachmentViewDesc> attachments)
    {
        attachmentViews.assign(attachments.begin(), attachments.end());
        RecreateFramebuffer();
    }

    void VulkanFramebuffer::SetColorDrawTargets(std::span<const AttachmentPoint> colorTargets)
    {
        colorDrawTargets.assign(colorTargets.begin(), colorTargets.end());
        RecreateFramebuffer();
    }

    FramebufferStatus VulkanFramebuffer::Validate() const noexcept
    {
        return status;
    }

    bool VulkanFramebuffer::Blit(const FramebufferBlitDesc &desc)
    {
        (void)desc;
        return false;
    }

    void VulkanFramebuffer::RecreateFramebuffer()
    {
        DestroyFramebufferObjects();

        if (device == VK_NULL_HANDLE)
        {
            status = FramebufferStatus::Unsupported;
            return;
        }

        if (attachmentViews.empty())
        {
            status = FramebufferStatus::MissingAttachment;
            return;
        }

        try
        {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkImageView> framebufferAttachments;
            std::vector<AttachmentBindingInfo> bindingInfos;

            attachmentDescriptions.reserve(attachmentViews.size());
            framebufferAttachments.reserve(attachmentViews.size());
            bindingInfos.reserve(attachmentViews.size());

            for (const AttachmentViewDesc &view : attachmentViews)
            {
                if (view.texture == nullptr)
                    continue;

                auto *vulkanTexture = dynamic_cast<VulkanTexture *>(view.texture);
                if (vulkanTexture == nullptr)
                {
                    status = FramebufferStatus::Unsupported;
                    return;
                }

                const VkImageView imageView = vulkanTexture->GetImageView();
                if (imageView == VK_NULL_HANDLE)
                    continue;

                const VkImageAspectFlags textureAspectMask = vulkanTexture->GetAspectMask();

                VkAttachmentDescription attachmentDescription{};
                attachmentDescription.format = vulkanTexture->GetVkFormat();
                attachmentDescription.samples = TranslateSampleCount(sampleCount);
                attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                if ((textureAspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u)
                {
                    attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                }
                else
                {
                    attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }

                attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
                attachmentDescription.finalLayout = ChooseAttachmentLayout(view.point, textureAspectMask);

                const std::uint32_t attachmentIndex = static_cast<std::uint32_t>(attachmentDescriptions.size());
                attachmentDescriptions.push_back(attachmentDescription);
                framebufferAttachments.push_back(imageView);
                bindingInfos.push_back({view.point, attachmentIndex, textureAspectMask});
            }

            if (attachmentDescriptions.empty())
            {
                status = FramebufferStatus::MissingAttachment;
                return;
            }

            std::vector<AttachmentPoint> effectiveColorTargets;
            if (!colorDrawTargets.empty())
            {
                effectiveColorTargets = colorDrawTargets;
            }
            else
            {
                for (const AttachmentBindingInfo &bindingInfo : bindingInfos)
                {
                    if (IsColorAttachmentPoint(bindingInfo.point))
                        effectiveColorTargets.push_back(bindingInfo.point);
                }
            }

            std::vector<VkAttachmentReference> colorAttachmentRefs;
            colorAttachmentRefs.reserve(effectiveColorTargets.size());

            for (const AttachmentPoint colorTarget : effectiveColorTargets)
            {
                const auto bindingIt = std::find_if(
                    bindingInfos.begin(),
                    bindingInfos.end(),
                    [colorTarget](const AttachmentBindingInfo &bindingInfo)
                    {
                        return (bindingInfo.point == colorTarget) && IsColorAttachmentPoint(bindingInfo.point);
                    });

                if (bindingIt == bindingInfos.end())
                    continue;

                VkAttachmentReference attachmentReference{};
                attachmentReference.attachment = bindingIt->attachmentIndex;
                attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                colorAttachmentRefs.push_back(attachmentReference);
            }

            VkAttachmentReference depthAttachmentRef{};
            bool hasDepthAttachment = false;

            const auto depthIt = std::find_if(
                bindingInfos.begin(),
                bindingInfos.end(),
                [](const AttachmentBindingInfo &bindingInfo)
                {
                    return IsDepthAttachmentPoint(bindingInfo.point) || ((bindingInfo.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u);
                });

            if (depthIt != bindingInfos.end())
            {
                depthAttachmentRef.attachment = depthIt->attachmentIndex;
                depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                hasDepthAttachment = true;
            }

            if (colorAttachmentRefs.empty() && !hasDepthAttachment)
            {
                status = FramebufferStatus::MissingAttachment;
                return;
            }

            VkSubpassDescription subpassDescription{};
            subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDescription.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentRefs.size());
            subpassDescription.pColorAttachments = colorAttachmentRefs.empty() ? nullptr : colorAttachmentRefs.data();
            subpassDescription.pDepthStencilAttachment = hasDepthAttachment ? &depthAttachmentRef : nullptr;

            VkRenderPassCreateInfo renderPassCreateInfo{};
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.attachmentCount = static_cast<std::uint32_t>(attachmentDescriptions.size());
            renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
            renderPassCreateInfo.subpassCount = 1;
            renderPassCreateInfo.pSubpasses = &subpassDescription;

            CheckVkResult(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass), "vkCreateRenderPass");

            VkFramebufferCreateInfo framebufferCreateInfo{};
            framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCreateInfo.renderPass = renderPass;
            framebufferCreateInfo.attachmentCount = static_cast<std::uint32_t>(framebufferAttachments.size());
            framebufferCreateInfo.pAttachments = framebufferAttachments.data();
            framebufferCreateInfo.width = extent.width;
            framebufferCreateInfo.height = extent.height;
            framebufferCreateInfo.layers = 1;

            CheckVkResult(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer), "vkCreateFramebuffer");

            status = FramebufferStatus::Complete;
        }
        catch (...)
        {
            DestroyFramebufferObjects();
            status = FramebufferStatus::Unsupported;
        }
    }

    void VulkanFramebuffer::DestroyFramebufferObjects() noexcept
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }

        if (renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
    }

} // namespace gfx
