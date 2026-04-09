/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IShader.h"
#include "Rendering/GL/myGL.h"

#include <cstdint>
#include <string>

namespace gfx
{

    class GLShader final : public IShader
    {
    public:
        explicit GLShader(const ShaderCreateInfo &ci);
        ~GLShader() override;

        GLShader(const GLShader &) = delete;
        GLShader &operator=(const GLShader &) = delete;

        GLShader(GLShader &&other) noexcept;
        GLShader &operator=(GLShader &&other) noexcept;

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

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override
        {
            return static_cast<std::uintptr_t>(shaderId);
        }

        [[nodiscard]] GLuint GetShaderId() const noexcept
        {
            return shaderId;
        }

    private:
        GLenum TranslateStage(ShaderStage value) const noexcept;
        void CompileFromGLSL(const ShaderCreateInfo &ci);
        void MoveFrom(GLShader &&other) noexcept;

    private:
        GLuint shaderId = 0;
        ShaderStage stage = ShaderStage::Vertex;
        ShaderSourceFormat sourceFormat = ShaderSourceFormat::GLSL;
        bool valid = false;
        std::string log;
    };

} // namespace gfx
