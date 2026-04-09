/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gfx
{

    enum class LinePrimitive : std::uint8_t
    {
        LineStrip,
        Lines,
    };

    struct LineVertexPC
    {
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;

        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct LineBatchDesc
    {
        LinePrimitive primitive = LinePrimitive::LineStrip;
        std::uint32_t firstVertex = 0;
        std::uint32_t vertexCount = 0;
    };

    struct LineStippleState
    {
        bool enabled = false;
        std::uint32_t factor = 1;
        std::uint16_t pattern = 0xFFFF;
    };

    enum class IndexElementType : std::uint8_t
    {
        UInt16,
        UInt32,
    };

    struct TexturedVertexLayout
    {
        std::uint32_t strideBytes = 0;
        std::uint32_t positionOffsetBytes = 0;
        std::uint32_t texCoordOffsetBytes = 0;
        std::uint32_t colorOffsetBytes = 0;
    };

    struct TexturedIndexedBatchDesc
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct TexturedBatchState
    {
        bool depthTest = false;
        bool blend = true;
        bool premultipliedAlpha = false;
        bool useDefaultBlendFunc = true;
    };

    class IVertexBuffer;

    struct VertexArrayBufferBinding
    {
        std::uint32_t binding = 0;
        IVertexBuffer *vertexBuffer = nullptr;
    };

    struct IndexedDrawDesc
    {
        std::uint32_t indexCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
    };

    struct IndexedInstancedDrawDesc
    {
        std::uint32_t indexCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
        std::uint32_t instanceCount = 0;
        std::uint32_t firstInstance = 0;
    };

    struct IndexedIndirectDrawCommand
    {
        std::uint32_t indexCount = 0;
        std::uint32_t instanceCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
        std::uint32_t firstInstance = 0;
    };

    class IVertexArray;
    class ITexture;
    class IVertexBuffer;
    class IFramebuffer;
    class IRenderTarget;
    class IShader;
    class IShaderProgram;

    class IGraphicsBackend
    {
    public:
        virtual ~IGraphicsBackend() = default;

        [[nodiscard]] virtual BackendType Type() const noexcept = 0;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual bool SupportsPersistentMapping() const noexcept = 0;

        [[nodiscard]] virtual std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo &ci) = 0;
        [[nodiscard]] virtual std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &ci) = 0;
        [[nodiscard]] virtual std::unique_ptr<IFramebuffer> CreateFramebuffer(const RenderTargetDesc &desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<IShader> CreateShader(const ShaderCreateInfo &ci) = 0;
        [[nodiscard]] virtual std::unique_ptr<IShaderProgram> CreateShaderProgram(const ShaderProgramCreateInfo &ci) = 0;
        [[nodiscard]] virtual std::unique_ptr<IVertexArray> CreateVertexArray(
            const VertexLayoutDesc &layout,
            std::span<const VertexArrayBufferBinding> vertexBuffers,
            IVertexBuffer *indexBuffer = nullptr) = 0;

        virtual void BindFramebuffer(IRenderTarget *target) = 0;

        virtual void DrawLineBatches(
            IVertexBuffer &vertexBuffer,
            std::span<const LineBatchDesc> batches,
            const LineStippleState &stippleState) = 0;

        virtual void DrawTexturedIndexedBatches(
            IVertexBuffer &vertexBuffer,
            IVertexBuffer &indexBuffer,
            ITexture &texture,
            std::span<const TexturedIndexedBatchDesc> batches,
            const TexturedVertexLayout &vertexLayout,
            IndexElementType indexType,
            const TexturedBatchState &state) = 0;

        virtual void DrawIndexed(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedDrawDesc &draw,
            IndexElementType indexType) = 0;

        virtual void DrawIndexedInstanced(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedInstancedDrawDesc &draw,
            IndexElementType indexType) = 0;

        virtual void MultiDrawIndexedIndirect(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            std::span<const IndexedIndirectDrawCommand> commands,
            IndexElementType indexType) = 0;

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void DeviceWaitIdle() = 0;
    };

} // namespace gfx
