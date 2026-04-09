/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IShaderProgram.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx
{

    class VulkanShader;

    class VulkanShaderProgram final : public IShaderProgram
    {
    public:
        VulkanShaderProgram(VkDevice device, const ShaderProgramCreateInfo &ci);
        ~VulkanShaderProgram() override;

        VulkanShaderProgram(const VulkanShaderProgram &) = delete;
        VulkanShaderProgram &operator=(const VulkanShaderProgram &) = delete;

        void AttachShader(IShader &shader) override;
        void BindAttribLocation(std::string_view name, std::uint32_t index) override;
        void BindOutputLocation(std::string_view name, std::uint32_t index) override;

        [[nodiscard]] bool Link() override;
        [[nodiscard]] bool Validate() override;
        [[nodiscard]] bool IsValid() const noexcept override
        {
            return valid;
        }

        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsBound() const noexcept override
        {
            return bound;
        }

        [[nodiscard]] bool SetParameter(std::string_view name, const UniformParameter &parameter) override;
        [[nodiscard]] bool SetParameter(int location, const UniformParameter &parameter) override;

        void BindTexture(std::uint32_t unit, ITexture *texture) override;
        void UnbindTexture(std::uint32_t unit) override;

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override;
        [[nodiscard]] std::string_view GetLog() const noexcept override
        {
            return log;
        }

        [[nodiscard]] VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept
        {
            return descriptorSetLayout;
        }

        [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept
        {
            return pipelineLayout;
        }

        [[nodiscard]] VkPipeline GetPipeline() const noexcept
        {
            return pipeline;
        }

    private:
        struct AttachedShaderState
        {
            ShaderStage stage = ShaderStage::Vertex;
            VkShaderModule module = VK_NULL_HANDLE;
        };

        struct DescriptorBindingScaffold
        {
            std::uint32_t binding = 0;
            VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            std::uint32_t descriptorCount = 1;
            VkShaderStageFlags stageFlags = 0;
        };

    private:
        void DestroyPipelineObjects() noexcept;
        void ResetStateAfterLinkFailure() noexcept;
        void AppendLog(std::string_view message);

        bool BuildDescriptorSetLayout();
        bool BuildPipelineLayout();
        void InitDescriptorScaffold();

    private:
        VkDevice device = VK_NULL_HANDLE;

        bool valid = false;
        bool bound = false;

        std::string debugName;
        std::string log;

        std::vector<AttachedShaderState> attachedShaders;
        std::vector<DescriptorBindingScaffold> descriptorBindingScaffold;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        std::unordered_map<std::string, std::uint32_t> attributeBindings;
        std::unordered_map<std::string, std::uint32_t> outputBindings;

        std::unordered_map<std::string, UniformParameter> uniformParametersByName;
        std::unordered_map<int, UniformParameter> uniformParametersByLocation;

        std::unordered_map<std::uint32_t, ITexture *> boundTextures;
    };

} // namespace gfx
