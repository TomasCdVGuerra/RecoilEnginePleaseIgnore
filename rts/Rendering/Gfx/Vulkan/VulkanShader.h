/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IShader.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace gfx
{

    class VulkanShader final : public IShader
    {
    public:
        VulkanShader(VkDevice device, const ShaderCreateInfo &ci);
        ~VulkanShader() override;

        VulkanShader(const VulkanShader &) = delete;
        VulkanShader &operator=(const VulkanShader &) = delete;

        [[nodiscard]] ShaderStage Stage() const noexcept override
        {
            return stage;
        }

        [[nodiscard]] ShaderSourceFormat SourceFormat() const noexcept override
        {
            return sourceFormat;
        }

        [[nodiscard]] bool IsValid() const noexcept override
        {
            return valid;
        }

        [[nodiscard]] std::string_view GetLog() const noexcept override
        {
            return log;
        }

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override;

        [[nodiscard]] VkShaderModule GetShaderModule() const noexcept
        {
            return shaderModule;
        }

    private:
        bool CreateFromSpirv(const ShaderCreateInfo &ci);

    private:
        VkDevice device = VK_NULL_HANDLE;
        VkShaderModule shaderModule = VK_NULL_HANDLE;

        ShaderStage stage = ShaderStage::Vertex;
        ShaderSourceFormat sourceFormat = ShaderSourceFormat::GLSL;

        bool valid = false;
        std::string log;
    };

} // namespace gfx
