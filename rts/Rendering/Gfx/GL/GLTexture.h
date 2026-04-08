/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/ITexture.h"
#include "Rendering/GL/myGL.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gfx
{

    class GLTexture final : public ITexture
    {
    public:
        explicit GLTexture(const TextureCreateInfo &ci);
        ~GLTexture() override;

        GLTexture(const GLTexture &) = delete;
        GLTexture &operator=(const GLTexture &) = delete;

        GLTexture(GLTexture &&other) noexcept;
        GLTexture &operator=(GLTexture &&other) noexcept;

        [[nodiscard]] TextureDimension Dimension() const noexcept override
        {
            return dimension;
        }

        [[nodiscard]] PixelFormat Format() const noexcept override
        {
            return format;
        }

        [[nodiscard]] Extent3D GetExtent() const noexcept override
        {
            return extent;
        }

        [[nodiscard]] std::uint32_t GetMipLevels() const noexcept override
        {
            return mipLevels;
        }

        [[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override
        {
            return static_cast<std::uintptr_t>(textureId);
        }

        void Upload(
            std::uint32_t mipLevel,
            std::uint32_t arrayLayer,
            std::span<const std::byte> pixels,
            std::size_t rowPitchBytes = 0,
            std::size_t slicePitchBytes = 0) override;

        void UploadSubRegion(
            std::uint32_t mipLevel,
            std::uint32_t arrayLayer,
            std::uint32_t xOffset,
            std::uint32_t yOffset,
            std::uint32_t width,
            std::uint32_t height,
            std::span<const std::byte> pixels,
            std::size_t rowPitchBytes = 0) override;

        void ApplySamplerState(const SamplerState &state) override;

        void GenerateMipmaps() override;

        [[nodiscard]] GLuint GetTextureId() const noexcept
        {
            return textureId;
        }

        [[nodiscard]] GLenum GetTarget() const noexcept
        {
            return target;
        }

    private:
        void MoveFrom(GLTexture &&other) noexcept;

    private:
        GLuint textureId = 0;

        TextureDimension dimension = TextureDimension::Tex2D;
        PixelFormat format = PixelFormat::Unknown;
        Extent3D extent = {};

        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
        TextureUsage usage = TextureUsage::Sampled;

        GLenum target = GL_TEXTURE_2D;
        GLenum internalFormat = GL_RGBA8;
        GLenum uploadFormat = GL_RGBA;
        GLenum uploadType = GL_UNSIGNED_BYTE;
        std::uint8_t bytesPerPixel = 4;
    };

} // namespace gfx
