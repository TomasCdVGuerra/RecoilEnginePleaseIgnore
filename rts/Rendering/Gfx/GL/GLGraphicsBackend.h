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
        [[nodiscard]] std::unique_ptr<IFramebuffer> CreateFramebuffer(const RenderTargetDesc &desc) override;
        [[nodiscard]] std::unique_ptr<IVertexArray> CreateVertexArray(
            const VertexLayoutDesc &layout,
            std::span<const VertexArrayBufferBinding> vertexBuffers,
            IVertexBuffer *indexBuffer = nullptr) override;

        void BindFramebuffer(IRenderTarget *target) override;

        void DrawLineBatches(
            IVertexBuffer &vertexBuffer,
            std::span<const LineBatchDesc> batches,
            const LineStippleState &stippleState) override;

        void DrawTexturedIndexedBatches(
            IVertexBuffer &vertexBuffer,
            IVertexBuffer &indexBuffer,
            ITexture &texture,
            std::span<const TexturedIndexedBatchDesc> batches,
            const TexturedVertexLayout &vertexLayout,
            IndexElementType indexType,
            const TexturedBatchState &state) override;

        void DrawIndexed(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedDrawDesc &draw,
            IndexElementType indexType) override;

        void DrawIndexedInstanced(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedInstancedDrawDesc &draw,
            IndexElementType indexType) override;

        void MultiDrawIndexedIndirect(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            std::span<const IndexedIndirectDrawCommand> commands,
            IndexElementType indexType) override;

        void BeginFrame() override;
        void EndFrame() override;
        void DeviceWaitIdle() override;
    };

} // namespace gfx
