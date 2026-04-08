/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace gfx
{

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
        RGBA8_UNorm,
        BGRA8_UNorm,
        R8_UNorm,
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

    enum class TextureUsage : std::uint32_t
    {
        Sampled = 1u << 0,
        RenderTarget = 1u << 1,
        DepthStencil = 1u << 2,
        TransferSrc = 1u << 3,
        TransferDst = 1u << 4,
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
        std::string debugName;
    };

} // namespace gfx
