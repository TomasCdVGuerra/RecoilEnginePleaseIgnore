/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gfx
{

    class ITexture;
    class IRenderTarget;

    enum class BackendType
    {
        OpenGL,
        Vulkan,
    };

    enum class BufferUsage
    {
        Static,
        Dynamic,
        Stream,
    };

    enum class MemoryAccess
    {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
    };

    enum class TextureDimension
    {
        Tex2D,
        Tex2DArray,
        Tex3D,
        Cube,
    };

    enum class PixelFormat
    {
        Unknown,
        R8_UNorm,
        RG8_UNorm,
        RGB8_UNorm,
        RGBA8_UNorm,
        BGRA8_UNorm,
        R16_UNorm,
        RG16_UNorm,
        RGB16_UNorm,
        RGBA16_UNorm,
        R32_SFloat,
        RG32_SFloat,
        RGB32_SFloat,
        RGBA32_SFloat,
        RGB10A2_UNorm,
        D24S8,
        D32_SFloat,
    };

    enum class FilterMode
    {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear,
    };

    enum class WrapMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge,
    };

    enum class VertexFormat : std::uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        UInt,
        UInt2,
        UInt3,
        UInt4,
        UByte4_UNorm,
        UByte4_UInt,
    };

    enum class VertexInputRate : std::uint8_t
    {
        PerVertex,
        PerInstance,
    };

    enum class PrimitiveTopology : std::uint8_t
    {
        Triangles,
        Lines,
        LineStrip,
    };

    enum class TextureUsage : std::uint32_t
    {
        Sampled = 1u << 0,
        RenderTarget = 1u << 1,
        DepthStencil = 1u << 2,
        TransferSrc = 1u << 3,
        TransferDst = 1u << 4,
    };

    enum class AttachmentPoint : std::uint8_t
    {
        Color0 = 0,
        Color1,
        Color2,
        Color3,
        Color4,
        Color5,
        Color6,
        Color7,
        Depth,
        Stencil,
        DepthStencil,
    };

    constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    struct Extent3D
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t depth = 1;
    };

    struct BufferCreateInfo
    {
        std::size_t sizeBytes = 0;
        BufferUsage usage = BufferUsage::Dynamic;
        MemoryAccess memoryAccess = MemoryAccess::CpuToGpu;
        bool readable = false;
        std::string debugName;
    };

    struct VertexAttributeDesc
    {
        std::uint32_t location = 0;
        std::uint32_t binding = 0;
        VertexFormat format = VertexFormat::Float3;
        std::uint32_t offsetBytes = 0;
    };

    struct VertexBufferBindingDesc
    {
        std::uint32_t binding = 0;
        std::uint32_t strideBytes = 0;
        std::uint32_t offsetBytes = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
    };

    struct VertexLayoutDesc
    {
        std::vector<VertexAttributeDesc> attributes;
        std::vector<VertexBufferBindingDesc> bindings;
    };

    struct SamplerState
    {
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
        WrapMode wrapS = WrapMode::ClampToEdge;
        WrapMode wrapT = WrapMode::ClampToEdge;
        WrapMode wrapR = WrapMode::ClampToEdge;
        float anisotropy = 0.0f;
        float lodBias = 0.0f;
    };

    struct TextureCreateInfo
    {
        TextureDimension dimension = TextureDimension::Tex2D;
        PixelFormat format = PixelFormat::RGBA8_UNorm;
        Extent3D extent;
        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
        TextureUsage usage = TextureUsage::Sampled;
        std::optional<SamplerState> samplerState;
        std::uintptr_t nativeHandle = 0;
        std::string debugName;
    };

    struct AttachmentViewDesc
    {
        AttachmentPoint point = AttachmentPoint::Color0;
        ITexture *texture = nullptr;
        std::uint32_t mipLevel = 0;
        std::uint32_t baseLayer = 0;
        std::uint32_t layerCount = 1;
    };

    struct RenderTargetDesc
    {
        Extent3D extent;
        std::uint32_t sampleCount = 1;
        std::vector<AttachmentViewDesc> attachments;
        std::vector<AttachmentPoint> colorDrawOrder;
        std::string debugName;
    };

    enum class FramebufferStatus : std::uint8_t
    {
        Complete,
        MissingAttachment,
        IncompleteAttachment,
        Unsupported,
    };

    struct FramebufferBlitDesc
    {
        IRenderTarget *src = nullptr;
        IRenderTarget *dst = nullptr;
        std::array<int, 4> srcRect = {0, 0, 0, 0};
        std::array<int, 4> dstRect = {0, 0, 0, 0};
        std::uint32_t mask = 0;
        FilterMode filter = FilterMode::Nearest;
    };

} // namespace gfx
