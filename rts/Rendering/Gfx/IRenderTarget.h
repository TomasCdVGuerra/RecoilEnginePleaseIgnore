/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstdint>

namespace gfx
{

    class IRenderTarget
    {
    public:
        virtual ~IRenderTarget() = default;

        [[nodiscard]] virtual Extent3D GetExtent() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetSampleCount() const noexcept = 0;
        [[nodiscard]] virtual std::uintptr_t GetNativeHandle() const noexcept = 0;
    };

} // namespace gfx
