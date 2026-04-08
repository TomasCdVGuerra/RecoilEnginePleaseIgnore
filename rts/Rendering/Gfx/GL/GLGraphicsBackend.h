/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IGraphicsBackend.h"

namespace gfx
{

    class GLGraphicsBackend final : public IGraphicsBackend
    {
    public:
        ~GLGraphicsBackend() override = default;

        [[nodiscard]] BackendType Type() const noexcept override;
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] bool SupportsPersistentMapping() const noexcept override;

        [[nodiscard]] std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo &ci) override;
        [[nodiscard]] std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &ci) override;

        void DrawLineBatches(
            IVertexBuffer &vertexBuffer,
            std::span<const LineBatchDesc> batches,
            const LineStippleState &stippleState) override;

        void BeginFrame() override;
        void EndFrame() override;
        void DeviceWaitIdle() override;
    };

} // namespace gfx
