/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLTexture.h"

#include "System/Log/ILog.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{

    struct GLPixelFormatInfo
    {
        GLenum internalFormat;
        GLenum uploadFormat;
        GLenum uploadType;
        std::uint8_t bytesPerPixel;
    };

    GLPixelFormatInfo TranslatePixelFormat(gfx::PixelFormat format)
    {
        switch (format)
        {
        case gfx::PixelFormat::RGBA8_UNorm:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
        case gfx::PixelFormat::BGRA8_UNorm:
            return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4};
        case gfx::PixelFormat::R8_UNorm:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
        case gfx::PixelFormat::D24S8:
            return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4};
        case gfx::PixelFormat::D32_SFloat:
            return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4};
        case gfx::PixelFormat::Unknown:
            break;
        }

        LOG_L(L_WARNING, "[GLTexture::TranslatePixelFormat] Unknown format requested, defaulting to RGBA8");
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    }

    GLenum TranslateTextureTarget(gfx::TextureDimension dimension)
    {
        switch (dimension)
        {
        case gfx::TextureDimension::Tex2D:
            return GL_TEXTURE_2D;
        case gfx::TextureDimension::Tex2DArray:
            return GL_TEXTURE_2D_ARRAY;
        case gfx::TextureDimension::Tex3D:
            return GL_TEXTURE_3D;
        case gfx::TextureDimension::Cube:
            return GL_TEXTURE_CUBE_MAP;
        }

        return GL_TEXTURE_2D;
    }

    GLsizei ToGLSizei(std::uint32_t value)
    {
        constexpr std::uint32_t maxValue = static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLTexture::ToGLSizei] Value (%u) exceeds GLsizei max (%u), clamping", value, maxValue);
            return std::numeric_limits<GLsizei>::max();
        }

        return static_cast<GLsizei>(value);
    }

    GLint ToGLInt(std::size_t value)
    {
        constexpr std::size_t maxValue = static_cast<std::size_t>(std::numeric_limits<GLint>::max());
        if (value > maxValue)
        {
            LOG_L(L_WARNING, "[GLTexture::ToGLInt] Value (%zu) exceeds GLint max (%zu), clamping", value, maxValue);
            return std::numeric_limits<GLint>::max();
        }

        return static_cast<GLint>(value);
    }

    std::uint32_t ClampAtLeastOne(std::uint32_t value)
    {
        return std::max<std::uint32_t>(1u, value);
    }

    gfx::Extent3D CalcMipExtent(const gfx::Extent3D &baseExtent, std::uint32_t mipLevel)
    {
        return {
            std::max<std::uint32_t>(1u, baseExtent.width >> mipLevel),
            std::max<std::uint32_t>(1u, baseExtent.height >> mipLevel),
            std::max<std::uint32_t>(1u, baseExtent.depth >> mipLevel)};
    }

    void SetupTextureSampling(GLenum target, std::uint32_t mipLevels)
    {
        const GLint minFilter = (mipLevels > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;

        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

} // namespace

namespace gfx
{

    GLTexture::GLTexture(const TextureCreateInfo &ci)
        : dimension(ci.dimension), format(ci.format), extent({ClampAtLeastOne(ci.extent.width),
                                                              ClampAtLeastOne(ci.extent.height),
                                                              ClampAtLeastOne(ci.extent.depth)}),
          mipLevels(ClampAtLeastOne(ci.mipLevels)), arrayLayers(ClampAtLeastOne(ci.arrayLayers)), usage(ci.usage)
    {
        const GLPixelFormatInfo glFmt = TranslatePixelFormat(format);

        internalFormat = glFmt.internalFormat;
        uploadFormat = glFmt.uploadFormat;
        uploadType = glFmt.uploadType;
        bytesPerPixel = glFmt.bytesPerPixel;
        target = TranslateTextureTarget(dimension);

        if (dimension == TextureDimension::Cube && arrayLayers != 6u)
        {
            LOG_L(L_WARNING, "[GLTexture::GLTexture] Cube textures force arrayLayers to 6 (requested=%u)", arrayLayers);
            arrayLayers = 6u;
        }

        glGenTextures(1, &textureId);

        if (textureId == 0)
        {
            LOG_L(L_WARNING, "[GLTexture::GLTexture] glGenTextures returned 0");
            return;
        }

        glBindTexture(target, textureId);
        SetupTextureSampling(target, mipLevels);

        for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const Extent3D mipExtent = CalcMipExtent(extent, mip);

            switch (dimension)
            {
            case TextureDimension::Tex2D:
            {
                glTexImage2D(
                    GL_TEXTURE_2D,
                    static_cast<GLint>(mip),
                    static_cast<GLint>(internalFormat),
                    ToGLSizei(mipExtent.width),
                    ToGLSizei(mipExtent.height),
                    0,
                    uploadFormat,
                    uploadType,
                    nullptr);
            }
            break;

            case TextureDimension::Tex2DArray:
            {
                glTexImage3D(
                    GL_TEXTURE_2D_ARRAY,
                    static_cast<GLint>(mip),
                    static_cast<GLint>(internalFormat),
                    ToGLSizei(mipExtent.width),
                    ToGLSizei(mipExtent.height),
                    ToGLSizei(arrayLayers),
                    0,
                    uploadFormat,
                    uploadType,
                    nullptr);
            }
            break;

            case TextureDimension::Tex3D:
            {
                glTexImage3D(
                    GL_TEXTURE_3D,
                    static_cast<GLint>(mip),
                    static_cast<GLint>(internalFormat),
                    ToGLSizei(mipExtent.width),
                    ToGLSizei(mipExtent.height),
                    ToGLSizei(mipExtent.depth),
                    0,
                    uploadFormat,
                    uploadType,
                    nullptr);
            }
            break;

            case TextureDimension::Cube:
            {
                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    glTexImage2D(
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                        static_cast<GLint>(mip),
                        static_cast<GLint>(internalFormat),
                        ToGLSizei(mipExtent.width),
                        ToGLSizei(mipExtent.height),
                        0,
                        uploadFormat,
                        uploadType,
                        nullptr);
                }
            }
            break;
            }
        }

        glBindTexture(target, 0);
    }

    GLTexture::~GLTexture()
    {
        if (textureId != 0)
        {
            glDeleteTextures(1, &textureId);
            textureId = 0;
        }
    }

    GLTexture::GLTexture(GLTexture &&other) noexcept
    {
        MoveFrom(std::move(other));
    }

    GLTexture &GLTexture::operator=(GLTexture &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (textureId != 0)
            glDeleteTextures(1, &textureId);

        MoveFrom(std::move(other));
        return *this;
    }

    void GLTexture::Upload(
        std::uint32_t mipLevel,
        std::uint32_t arrayLayer,
        std::span<const std::byte> pixels,
        std::size_t rowPitchBytes,
        std::size_t slicePitchBytes)
    {
        if (textureId == 0 || pixels.empty())
            return;

        if (mipLevel >= mipLevels)
        {
            LOG_L(L_WARNING, "[GLTexture::Upload] Invalid mip level %u (max=%u)", mipLevel, (mipLevels - 1));
            return;
        }

        const Extent3D mipExtent = CalcMipExtent(extent, mipLevel);

        glBindTexture(target, textureId);

        GLint prevUnpackAlignment = 4;
        GLint prevUnpackRowLength = 0;
        GLint prevUnpackImageHeight = 0;
        bool unpackRowLengthChanged = false;
        bool unpackImageHeightChanged = false;

        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (rowPitchBytes != 0)
        {
            if ((rowPitchBytes % bytesPerPixel) == 0)
            {
                glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevUnpackRowLength);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, ToGLInt(rowPitchBytes / bytesPerPixel));
                unpackRowLengthChanged = true;
            }
            else
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] rowPitchBytes (%zu) is not aligned to bytesPerPixel (%u)", rowPitchBytes, bytesPerPixel);
            }
        }

        if (slicePitchBytes != 0 && (dimension == TextureDimension::Tex2DArray || dimension == TextureDimension::Tex3D))
        {
            const std::size_t effectiveRowPitch = (rowPitchBytes != 0)
                                                      ? rowPitchBytes
                                                      : (static_cast<std::size_t>(mipExtent.width) * bytesPerPixel);

            if (effectiveRowPitch != 0 && (slicePitchBytes % effectiveRowPitch) == 0)
            {
                glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &prevUnpackImageHeight);
                glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, ToGLInt(slicePitchBytes / effectiveRowPitch));
                unpackImageHeightChanged = true;
            }
            else
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] slicePitchBytes (%zu) is incompatible with row pitch (%zu)", slicePitchBytes, effectiveRowPitch);
            }
        }

        switch (dimension)
        {
        case TextureDimension::Tex2D:
        {
            if (arrayLayer != 0)
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] arrayLayer ignored for Tex2D (value=%u)", arrayLayer);
            }

            glTexSubImage2D(
                GL_TEXTURE_2D,
                static_cast<GLint>(mipLevel),
                0,
                0,
                ToGLSizei(mipExtent.width),
                ToGLSizei(mipExtent.height),
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;

        case TextureDimension::Tex2DArray:
        {
            if (arrayLayer >= arrayLayers)
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] Invalid arrayLayer %u for texture with %u layers", arrayLayer, arrayLayers);
                break;
            }

            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY,
                static_cast<GLint>(mipLevel),
                0,
                0,
                static_cast<GLint>(arrayLayer),
                ToGLSizei(mipExtent.width),
                ToGLSizei(mipExtent.height),
                1,
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;

        case TextureDimension::Tex3D:
        {
            if (arrayLayer != 0)
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] arrayLayer ignored for Tex3D (value=%u)", arrayLayer);
            }

            glTexSubImage3D(
                GL_TEXTURE_3D,
                static_cast<GLint>(mipLevel),
                0,
                0,
                0,
                ToGLSizei(mipExtent.width),
                ToGLSizei(mipExtent.height),
                ToGLSizei(mipExtent.depth),
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;

        case TextureDimension::Cube:
        {
            if (arrayLayer >= 6u)
            {
                LOG_L(L_WARNING, "[GLTexture::Upload] Invalid cube face index %u (expected 0..5)", arrayLayer);
                break;
            }

            glTexSubImage2D(
                (GL_TEXTURE_CUBE_MAP_POSITIVE_X + arrayLayer),
                static_cast<GLint>(mipLevel),
                0,
                0,
                ToGLSizei(mipExtent.width),
                ToGLSizei(mipExtent.height),
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;
        }

        if (unpackImageHeightChanged)
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, prevUnpackImageHeight);

        if (unpackRowLengthChanged)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRowLength);

        glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
        glBindTexture(target, 0);
    }

    void GLTexture::GenerateMipmaps()
    {
        if (textureId == 0)
            return;

        glBindTexture(target, textureId);
        glGenerateMipmap(target);
        glBindTexture(target, 0);
    }

    void GLTexture::UploadSubRegion(
        std::uint32_t mipLevel,
        std::uint32_t arrayLayer,
        std::uint32_t xOffset,
        std::uint32_t yOffset,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> pixels,
        std::size_t rowPitchBytes)
    {
        if (textureId == 0 || pixels.empty())
            return;

        if (mipLevel >= mipLevels)
        {
            LOG_L(L_WARNING, "[GLTexture::UploadSubRegion] Invalid mip level %u (max=%u)", mipLevel, (mipLevels - 1));
            return;
        }

        if ((width == 0u) || (height == 0u))
            return;

        const Extent3D mipExtent = CalcMipExtent(extent, mipLevel);
        if ((xOffset >= mipExtent.width) ||
            (yOffset >= mipExtent.height) ||
            (width > (mipExtent.width - xOffset)) ||
            (height > (mipExtent.height - yOffset)))
        {
            LOG_L(
                L_WARNING,
                "[GLTexture::UploadSubRegion] Invalid region (%u,%u %ux%u) for mip extent (%u,%u)",
                xOffset,
                yOffset,
                width,
                height,
                mipExtent.width,
                mipExtent.height);
            return;
        }

        glBindTexture(target, textureId);

        GLint prevUnpackAlignment = 4;
        GLint prevUnpackRowLength = 0;
        bool unpackRowLengthChanged = false;

        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (rowPitchBytes != 0)
        {
            if ((rowPitchBytes % bytesPerPixel) == 0)
            {
                glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevUnpackRowLength);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, ToGLInt(rowPitchBytes / bytesPerPixel));
                unpackRowLengthChanged = true;
            }
            else
            {
                LOG_L(L_WARNING, "[GLTexture::UploadSubRegion] rowPitchBytes (%zu) is not aligned to bytesPerPixel (%u)", rowPitchBytes, bytesPerPixel);
            }
        }

        switch (dimension)
        {
        case TextureDimension::Tex2D:
        {
            if (arrayLayer != 0)
            {
                LOG_L(L_WARNING, "[GLTexture::UploadSubRegion] arrayLayer ignored for Tex2D (value=%u)", arrayLayer);
            }

            glTexSubImage2D(
                GL_TEXTURE_2D,
                static_cast<GLint>(mipLevel),
                ToGLInt(static_cast<std::size_t>(xOffset)),
                ToGLInt(static_cast<std::size_t>(yOffset)),
                ToGLSizei(width),
                ToGLSizei(height),
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;

        case TextureDimension::Cube:
        {
            if (arrayLayer >= 6u)
            {
                LOG_L(L_WARNING, "[GLTexture::UploadSubRegion] Invalid cube face index %u (expected 0..5)", arrayLayer);
                break;
            }

            glTexSubImage2D(
                (GL_TEXTURE_CUBE_MAP_POSITIVE_X + arrayLayer),
                static_cast<GLint>(mipLevel),
                ToGLInt(static_cast<std::size_t>(xOffset)),
                ToGLInt(static_cast<std::size_t>(yOffset)),
                ToGLSizei(width),
                ToGLSizei(height),
                uploadFormat,
                uploadType,
                pixels.data());
        }
        break;

        case TextureDimension::Tex2DArray:
        case TextureDimension::Tex3D:
        {
            LOG_L(L_WARNING, "[GLTexture::UploadSubRegion] Sub-region upload via glTexSubImage2D is only supported for Tex2D and Cube textures");
        }
        break;
        }

        if (unpackRowLengthChanged)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRowLength);

        glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
        glBindTexture(target, 0);
    }

    void GLTexture::MoveFrom(GLTexture &&other) noexcept
    {
        textureId = other.textureId;

        dimension = other.dimension;
        format = other.format;
        extent = other.extent;

        mipLevels = other.mipLevels;
        arrayLayers = other.arrayLayers;
        usage = other.usage;

        target = other.target;
        internalFormat = other.internalFormat;
        uploadFormat = other.uploadFormat;
        uploadType = other.uploadType;
        bytesPerPixel = other.bytesPerPixel;

        other.textureId = 0;
    }

} // namespace gfx
