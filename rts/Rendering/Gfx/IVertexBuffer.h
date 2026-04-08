/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstddef>
#include <span>

namespace gfx {

class IVertexBuffer {
public:
	virtual ~IVertexBuffer() = default;

	[[nodiscard]] virtual std::size_t SizeBytes() const noexcept = 0;
	[[nodiscard]] virtual BufferUsage Usage() const noexcept = 0;
	[[nodiscard]] virtual bool IsMappable() const noexcept = 0;

	virtual void Resize(std::size_t newSizeBytes, bool preserveData) = 0;
	virtual void Update(std::span<const std::byte> src, std::size_t dstOffsetBytes = 0) = 0;

	[[nodiscard]] virtual std::span<std::byte> MapWrite(std::size_t offsetBytes, std::size_t sizeBytes) = 0;
	virtual void UnmapWrite() = 0;
};

} // namespace gfx
