/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanShader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    std::uintptr_t ToNativeHandle(VkShaderModule handle)
    {
        std::uintptr_t nativeHandle = 0;
        std::memcpy(&nativeHandle, &handle, std::min(sizeof(nativeHandle), sizeof(handle)));
        return nativeHandle;
    }

} // namespace

namespace gfx
{

    VulkanShader::VulkanShader(VkDevice device_, const ShaderCreateInfo &ci)
        : device(device_), stage(ci.stage), sourceFormat(ci.sourceFormat)
    {
        if (device == VK_NULL_HANDLE)
        {
            log = "VulkanShader: VkDevice is null";
            return;
        }

        if (sourceFormat == ShaderSourceFormat::GLSL)
        {
            log = "GLSL to SPIR-V compilation not yet integrated";
            return;
        }

        valid = CreateFromSpirv(ci);
    }

    VulkanShader::~VulkanShader()
    {
        if (shaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            shaderModule = VK_NULL_HANDLE;
        }
    }

    std::uintptr_t VulkanShader::GetNativeHandle() const noexcept
    {
        return ToNativeHandle(shaderModule);
    }

    bool VulkanShader::CreateFromSpirv(const ShaderCreateInfo &ci)
    {
        if (ci.sourceBinary.empty())
        {
            log = "VulkanShader: SPIR-V source is empty";
            return false;
        }

        if ((ci.sourceBinary.size() % sizeof(std::uint32_t)) != 0u)
        {
            log = "VulkanShader: SPIR-V bytecode size is not a multiple of 4";
            return false;
        }

        std::vector<std::uint32_t> spirvWords(ci.sourceBinary.size() / sizeof(std::uint32_t), 0u);
        std::memcpy(spirvWords.data(), ci.sourceBinary.data(), ci.sourceBinary.size());

        VkShaderModuleCreateInfo moduleCreateInfo{};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = ci.sourceBinary.size();
        moduleCreateInfo.pCode = spirvWords.data();

        const VkResult result = vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule);
        if (result != VK_SUCCESS)
        {
            log = std::string("VulkanShader: vkCreateShaderModule failed with VkResult=") + std::to_string(static_cast<int>(result));
            shaderModule = VK_NULL_HANDLE;
            return false;
        }

        log.clear();
        return true;
    }

} // namespace gfx
