/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstdint>
#include <string_view>

namespace gfx
{

    class IShader
    {
    public:
        virtual ~IShader() = default;

        [[nodiscard]] virtual ShaderStage Stage() const noexcept = 0;
        [[nodiscard]] virtual ShaderSourceFormat SourceFormat() const noexcept = 0;
        [[nodiscard]] virtual bool IsValid() const noexcept = 0;
        [[nodiscard]] virtual std::string_view GetLog() const noexcept = 0;
        [[nodiscard]] virtual std::uintptr_t GetNativeHandle() const noexcept = 0;
    };

} // namespace gfx
