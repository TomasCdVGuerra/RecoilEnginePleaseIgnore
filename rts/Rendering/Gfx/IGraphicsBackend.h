/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <memory>
#include <string_view>

namespace gfx {

class ITexture;
class IVertexBuffer;

class IGraphicsBackend {
public:
	virtual ~IGraphicsBackend() = default;

	[[nodiscard]] virtual BackendType Type() const noexcept = 0;
	[[nodiscard]] virtual std::string_view Name() const noexcept = 0;
	[[nodiscard]] virtual bool SupportsPersistentMapping() const noexcept = 0;

	[[nodiscard]] virtual std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo& ci) = 0;
	[[nodiscard]] virtual std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo& ci) = 0;

	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void DeviceWaitIdle() = 0;
};

} // namespace gfx
