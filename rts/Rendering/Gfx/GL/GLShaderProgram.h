/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IShaderProgram.h"
#include "Rendering/GL/myGL.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace gfx
{

    class GLShaderProgram final : public IShaderProgram
    {
    public:
        explicit GLShaderProgram(const ShaderProgramCreateInfo &ci);
        ~GLShaderProgram() override;

        GLShaderProgram(const GLShaderProgram &) = delete;
        GLShaderProgram &operator=(const GLShaderProgram &) = delete;

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

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override
        {
            return static_cast<std::uintptr_t>(programId);
        }

        [[nodiscard]] std::string_view GetLog() const noexcept override
        {
            return log;
        }

        [[nodiscard]] GLuint GetProgramId() const noexcept
        {
            return programId;
        }

    private:
        struct UniformCacheEntry
        {
            GLint location = -1;
            std::array<std::uint32_t, 16> rawWords = {};
            std::uint8_t rawWordCount = 0;
            bool transpose = false;
            bool initialized = false;
        };

        [[nodiscard]] GLint ResolveUniformLocation(std::string_view name, UniformCacheEntry &entry);
        [[nodiscard]] bool BuildRawWords(
            const UniformParameter &parameter,
            std::array<std::uint32_t, 16> &rawWords,
            std::uint8_t &rawWordCount,
            bool &transpose) const;

        [[nodiscard]] bool SubmitParameter(GLint location, UniformCacheEntry &entry, const UniformParameter &parameter);
        void ClearUniformCaches();

    private:
        GLuint programId = 0;
        bool valid = false;
        bool bound = false;

        std::string debugName;
        std::string log;

        std::unordered_map<ShaderStage, GLuint> attachedShaderIds;
        std::unordered_map<std::string, UniformCacheEntry> uniformByName;
        std::unordered_map<int, UniformCacheEntry> uniformByLocation;
        std::unordered_map<std::uint32_t, GLenum> textureUnitTargets;
    };

} // namespace gfx
