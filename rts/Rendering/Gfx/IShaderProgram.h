/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstdint>
#include <string_view>

namespace gfx
{

    class IShader;
    class ITexture;

    class IShaderProgram
    {
    public:
        virtual ~IShaderProgram() = default;

        virtual void AttachShader(IShader &shader) = 0;
        virtual void BindAttribLocation(std::string_view name, std::uint32_t index) = 0;
        virtual void BindOutputLocation(std::string_view name, std::uint32_t index) = 0;

        [[nodiscard]] virtual bool Link() = 0;
        [[nodiscard]] virtual bool Validate() = 0;
        [[nodiscard]] virtual bool IsValid() const noexcept = 0;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;
        [[nodiscard]] virtual bool IsBound() const noexcept = 0;

        [[nodiscard]] virtual bool SetParameter(std::string_view name, const UniformParameter &parameter) = 0;
        [[nodiscard]] virtual bool SetParameter(int location, const UniformParameter &parameter) = 0;

        virtual void BindTexture(std::uint32_t unit, ITexture *texture) = 0;
        virtual void UnbindTexture(std::uint32_t unit) = 0;

        [[nodiscard]] virtual std::uintptr_t GetNativeHandle() const noexcept = 0;
        [[nodiscard]] virtual std::string_view GetLog() const noexcept = 0;
    };

} // namespace gfx
