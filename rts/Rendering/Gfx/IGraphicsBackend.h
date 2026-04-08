/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "GfxTypes.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

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

    class ITexture;
    class IVertexBuffer;

    class IGraphicsBackend
    {
    public:
        virtual ~IGraphicsBackend() = default;

        [[nodiscard]] virtual BackendType Type() const noexcept = 0;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual bool SupportsPersistentMapping() const noexcept = 0;

        [[nodiscard]] virtual std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo &ci) = 0;
        [[nodiscard]] virtual std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &ci) = 0;

        virtual void DrawLineBatches(
            IVertexBuffer &vertexBuffer,
            std::span<const LineBatchDesc> batches,
            const LineStippleState &stippleState) = 0;

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void DeviceWaitIdle() = 0;
    };

} // namespace gfx
