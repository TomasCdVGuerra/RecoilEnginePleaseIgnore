/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanShaderProgram.h"

#include "VulkanShader.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{

    thread_local gfx::VulkanShaderProgram *g_boundVulkanProgram = nullptr;

    std::uintptr_t ToNativeHandle(VkPipeline handle)
    {
        std::uintptr_t nativeHandle = 0;
        std::memcpy(&nativeHandle, &handle, std::min(sizeof(nativeHandle), sizeof(handle)));
        return nativeHandle;
    }

} // namespace

namespace gfx
{

    VulkanShaderProgram::VulkanShaderProgram(VkDevice device_, const ShaderProgramCreateInfo &ci)
        : device(device_), debugName(ci.debugName)
    {
        InitDescriptorScaffold();

        if (device == VK_NULL_HANDLE)
            AppendLog("VulkanShaderProgram: VkDevice is null");
    }

    VulkanShaderProgram::~VulkanShaderProgram()
    {
        if (g_boundVulkanProgram == this)
            g_boundVulkanProgram = nullptr;

        DestroyPipelineObjects();
    }

    void VulkanShaderProgram::AttachShader(IShader &shader)
    {
        auto *vulkanShader = dynamic_cast<VulkanShader *>(&shader);
        if (vulkanShader == nullptr)
        {
            AppendLog("VulkanShaderProgram::AttachShader received non-Vulkan shader object");
            valid = false;
            return;
        }

        if (!vulkanShader->IsValid() || (vulkanShader->GetShaderModule() == VK_NULL_HANDLE))
        {
            AppendLog("VulkanShaderProgram::AttachShader received invalid shader module");
            valid = false;
            return;
        }

        const ShaderStage stage = vulkanShader->Stage();
        const auto existingIt = std::find_if(
            attachedShaders.begin(),
            attachedShaders.end(),
            [stage](const AttachedShaderState &state)
            {
                return state.stage == stage;
            });

        if (existingIt != attachedShaders.end())
        {
            existingIt->module = vulkanShader->GetShaderModule();
            return;
        }

        attachedShaders.push_back({stage, vulkanShader->GetShaderModule()});
    }

    void VulkanShaderProgram::BindAttribLocation(std::string_view name, std::uint32_t index)
    {
        if (name.empty())
            return;

        attributeBindings[std::string(name)] = index;
    }

    void VulkanShaderProgram::BindOutputLocation(std::string_view name, std::uint32_t index)
    {
        if (name.empty())
            return;

        outputBindings[std::string(name)] = index;
    }

    bool VulkanShaderProgram::Link()
    {
        DestroyPipelineObjects();
        valid = false;

        if (device == VK_NULL_HANDLE)
        {
            AppendLog("VulkanShaderProgram::Link failed because VkDevice is null");
            return false;
        }

        if (attachedShaders.empty())
        {
            AppendLog("VulkanShaderProgram::Link requires at least one attached shader");
            return false;
        }

        if (!BuildDescriptorSetLayout())
        {
            ResetStateAfterLinkFailure();
            return false;
        }

        if (!BuildPipelineLayout())
        {
            ResetStateAfterLinkFailure();
            return false;
        }

        AppendLog("Vulkan pipeline creation scaffold active; VkPipeline creation deferred until render-pass and vertex-input integration.");

        valid = true;
        return true;
    }

    bool VulkanShaderProgram::Validate()
    {
        const bool descriptorsReady = (descriptorSetLayout != VK_NULL_HANDLE);
        const bool layoutReady = (pipelineLayout != VK_NULL_HANDLE);

        const bool isValid = valid && descriptorsReady && layoutReady;
        if (!isValid)
            AppendLog("VulkanShaderProgram::Validate failed (descriptor set layout and pipeline layout are required)");

        return isValid;
    }

    void VulkanShaderProgram::Bind()
    {
        if (!valid)
        {
            bound = false;

            if (g_boundVulkanProgram == this)
                g_boundVulkanProgram = nullptr;

            return;
        }

        bound = true;
        g_boundVulkanProgram = this;
    }

    void VulkanShaderProgram::Unbind()
    {
        bound = false;

        if (g_boundVulkanProgram == this)
            g_boundVulkanProgram = nullptr;
    }

    bool VulkanShaderProgram::SetParameter(std::string_view name, const UniformParameter &parameter)
    {
        if (name.empty())
            return false;

        uniformParametersByName[std::string(name)] = parameter;
        return true;
    }

    bool VulkanShaderProgram::SetParameter(int location, const UniformParameter &parameter)
    {
        if (location < 0)
            return false;

        uniformParametersByLocation[location] = parameter;
        return true;
    }

    void VulkanShaderProgram::BindTexture(std::uint32_t unit, ITexture *texture)
    {
        boundTextures[unit] = texture;
    }

    void VulkanShaderProgram::UnbindTexture(std::uint32_t unit)
    {
        boundTextures.erase(unit);
    }

    std::uintptr_t VulkanShaderProgram::GetNativeHandle() const noexcept
    {
        return ToNativeHandle(pipeline);
    }

    VulkanShaderProgram *VulkanShaderProgram::GetBoundProgram() noexcept
    {
        return g_boundVulkanProgram;
    }

    void VulkanShaderProgram::DestroyPipelineObjects() noexcept
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (descriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void VulkanShaderProgram::ResetStateAfterLinkFailure() noexcept
    {
        DestroyPipelineObjects();
        valid = false;
        bound = false;
    }

    void VulkanShaderProgram::AppendLog(std::string_view message)
    {
        if (message.empty())
            return;

        if (!log.empty())
            log += '\n';

        log.append(message.data(), message.size());
    }

    bool VulkanShaderProgram::BuildDescriptorSetLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(descriptorBindingScaffold.size());

        for (const DescriptorBindingScaffold &scaffold : descriptorBindingScaffold)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = scaffold.binding;
            binding.descriptorType = scaffold.descriptorType;
            binding.descriptorCount = scaffold.descriptorCount;
            binding.stageFlags = scaffold.stageFlags;
            binding.pImmutableSamplers = nullptr;
            bindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        createInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

        const VkResult result = vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &descriptorSetLayout);
        if (result != VK_SUCCESS)
        {
            AppendLog(std::string("vkCreateDescriptorSetLayout failed with VkResult=") + std::to_string(static_cast<int>(result)));
            descriptorSetLayout = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    bool VulkanShaderProgram::BuildPipelineLayout()
    {
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = (descriptorSetLayout != VK_NULL_HANDLE) ? 1u : 0u;
        createInfo.pSetLayouts = (descriptorSetLayout != VK_NULL_HANDLE) ? &descriptorSetLayout : nullptr;
        createInfo.pushConstantRangeCount = 0;
        createInfo.pPushConstantRanges = nullptr;

        const VkResult result = vkCreatePipelineLayout(device, &createInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS)
        {
            AppendLog(std::string("vkCreatePipelineLayout failed with VkResult=") + std::to_string(static_cast<int>(result)));
            pipelineLayout = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void VulkanShaderProgram::InitDescriptorScaffold()
    {
        descriptorBindingScaffold.clear();

        descriptorBindingScaffold.push_back({
            0u,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1u,
            VK_SHADER_STAGE_ALL_GRAPHICS,
        });

        descriptorBindingScaffold.push_back({
            1u,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            16u,
            VK_SHADER_STAGE_FRAGMENT_BIT,
        });
    }

} // namespace gfx
