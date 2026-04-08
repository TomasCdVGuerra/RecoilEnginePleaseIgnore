/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstddef>
#include <span>

namespace gfx {

class ITexture {
public:
	virtual ~ITexture() = default;

	[[nodiscard]] virtual TextureDimension Dimension() const noexcept = 0;
	[[nodiscard]] virtual PixelFormat Format() const noexcept = 0;
	[[nodiscard]] virtual Extent3D GetExtent() const noexcept = 0;
	[[nodiscard]] virtual std::uint32_t GetMipLevels() const noexcept = 0;

	virtual void Upload(
		std::uint32_t mipLevel,
		std::uint32_t arrayLayer,
		std::span<const std::byte> pixels,
		std::size_t rowPitchBytes = 0,
		std::size_t slicePitchBytes = 0
	) = 0;

	virtual void GenerateMipmaps() = 0;
};

} // namespace gfx
